// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "tools.h"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>

void runProcess(QObject *ctx, const QString &prog, const QStringList &args, const QString &cwd, ProcDone done)
{
    auto *p = new QProcess(ctx);
    p->setWorkingDirectory(cwd);
    QObject::connect(p, &QProcess::finished, ctx, [p, done](int code, QProcess::ExitStatus st) {
        done(st == QProcess::NormalExit ? code : -1, QString::fromUtf8(p->readAllStandardOutput()),
             QString::fromUtf8(p->readAllStandardError()));
        p->deleteLater();
    });
    QObject::connect(p, &QProcess::errorOccurred, ctx, [p, prog, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        done(-1, {}, prog + ": " + p->errorString());
        p->deleteLater();
    });
    p->start(prog, args);
}

QString runSync(const QString &prog, const QStringList &args, const QString &cwd)
{
    QProcess p;
    p.setWorkingDirectory(cwd);
    p.start(prog, args);
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool isGitRepo(const QString &dir)
{
    return runSync("git", {"rev-parse", "--is-inside-work-tree"}, dir) == "true";
}

namespace {

int matchLength(const QString &pat, const QString &file)
{
    if (pat.startsWith("N:"))
        return QRegularExpression(pat.mid(2)).match(file).hasMatch() ? pat.size() - 2 : 0;
    if (pat.contains('*') || pat.contains('?')) {
        auto re = QRegularExpression::fromWildcard(pat, Qt::CaseSensitive);
        if (!pat.endsWith('/'))
            return re.match(file).hasMatch() ? pat.size() : 0;
        for (qsizetype i = file.indexOf('/'); i >= 0; i = file.indexOf('/', i + 1))
            if (re.match(file.left(i + 1)).hasMatch())
                return pat.size();
        return 0;
    }
    QString dir = pat.endsWith('/') ? pat : pat + '/';
    return file == pat || file.startsWith(dir) ? pat.size() : 0;
}

}

QList<Tree> findTrees(const QString &maintainers, const QStringList &files)
{
    QFile f(maintainers);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QList<std::pair<int, Tree>> found;
    QString section;
    QStringList pats, trees;
    auto finish = [&] {
        int score = 0;
        for (const QString &file : files)
            for (const QString &p : pats)
                score = std::max(score, matchLength(p, file));
        if (score > 0 && !trees.isEmpty()) {
            QStringList t = trees.first().split(' ', Qt::SkipEmptyParts);
            found << std::pair{score, Tree{section, t.value(1), t.value(2)}};
        }
        pats.clear();
        trees.clear();
    };
    for (const QString &raw : QString::fromUtf8(f.readAll()).split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty()) {
            finish();
            section.clear();
        } else if (line.size() > 2 && line[1] == ':') {
            QString v = line.mid(2).trimmed();
            if (line[0] == 'F')
                pats << v;
            else if (line[0] == 'N')
                pats << "N:" + v;
            else if (line[0] == 'T' && v.startsWith("git "))
                trees << v;
        } else {
            section = line;
        }
    }
    finish();
    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QList<Tree> out;
    for (const auto &[score, t] : found)
        out << t;
    return out;
}
