// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "series.h"

#include <QRegularExpression>
#include <algorithm>
#include <functional>

QString DiffLine::key() const
{
    QString k = type + text;
    while (!k.isEmpty() && k.back().isSpace())
        k.chop(1);
    return k;
}

QString stripQuote(const QString &line)
{
    qsizetype i = 0;
    while (i < line.size() && line[i] == '>') {
        i++;
        if (i < line.size() && line[i] == ' ')
            i++;
    }
    QString s = line.mid(i);
    while (!s.isEmpty() && s.back().isSpace())
        s.chop(1);
    return s;
}

QString cleanSubject(const QString &subject)
{
    static const QRegularExpression re(R"(^\s*((re|fwd?)\s*:\s*)+)", QRegularExpression::CaseInsensitiveOption);
    return QString(subject).remove(re);
}

QList<FileDiff> parseDiff(const QString &text)
{
    static const QRegularExpression git(R"(^diff --git a/(\S+) b/(\S+))");
    static const QRegularExpression hunkRe(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)");
    QList<FileDiff> files;
    int oldLeft = 0, newLeft = 0, oldNo = 0, newNo = 0;
    for (const QString &line : text.split('\n')) {
        if (oldLeft > 0 || newLeft > 0) {
            if (line.startsWith('\\'))
                continue;
            DiffLine dl{line.isEmpty() ? QChar(' ') : line[0], 0, 0, line.mid(1)};
            if (dl.type == '+') {
                dl.newNo = newNo++;
                newLeft--;
            } else if (dl.type == '-') {
                dl.oldNo = oldNo++;
                oldLeft--;
            } else {
                dl.type = ' ';
                dl.oldNo = oldNo++;
                dl.newNo = newNo++;
                oldLeft--;
                newLeft--;
            }
            files.back().hunks.back().lines << dl;
            continue;
        }
        if (auto m = git.match(line); m.hasMatch()) {
            files << FileDiff{line, m.captured(1), m.captured(2), {}};
        } else if (files.isEmpty()) {
            continue;
        } else if (line.startsWith("--- ") && files.back().hunks.isEmpty()) {
            files.back().oldPath = line.mid(4) == "/dev/null" ? "/dev/null" : line.mid(6);
        } else if (line.startsWith("+++ ") && files.back().hunks.isEmpty()) {
            files.back().newPath = line.mid(4) == "/dev/null" ? "/dev/null" : line.mid(6);
        } else if (auto h = hunkRe.match(line); h.hasMatch()) {
            Hunk hk{line, h.captured(1).toInt(), h.captured(3).toInt(), {}};
            oldLeft = h.captured(2).isEmpty() ? 1 : h.captured(2).toInt();
            newLeft = h.captured(4).isEmpty() ? 1 : h.captured(4).toInt();
            oldNo = hk.oldStart;
            newNo = hk.newStart;
            files.back().hunks << hk;
        }
    }
    return files;
}

namespace {

struct LineIndex {
    QList<LineRef> order;
    QHash<QString, QList<int>> byKey;

    explicit LineIndex(const Patch &p)
    {
        for (int f = 0; f < p.files.size(); f++)
            for (int h = 0; h < p.files[f].hunks.size(); h++)
                for (int l = 0; l < p.files[f].hunks[h].lines.size(); l++) {
                    byKey[p.files[f].hunks[h].lines[l].key()] << order.size();
                    order << LineRef{f, h, l};
                }
    }

    int find(const QString &key, int from) const
    {
        const QList<int> hits = byKey.value(key);
        auto it = std::lower_bound(hits.begin(), hits.end(), from);
        return it == hits.end() ? -1 : *it;
    }
};

void extractComments(Patch &p, const LineIndex &idx, int msg, const QString &body)
{
    struct Block {
        QStringList quote;
        QStringList text;
    };
    static const QRegularExpression snip(R"(^\s*(\[\s*(\.\.\.|…|snip)\s*\]|<\s*snip\s*>|\.\.\.|…)\s*$)",
                                         QRegularExpression::CaseInsensitiveOption);
    QList<Block> blocks{{}};
    for (const QString &line : body.split('\n')) {
        if (line == "-- ")
            break;
        if (line.contains(snip))
            continue;
        if (line.startsWith('>')) {
            if (!blocks.back().text.isEmpty())
                blocks << Block{};
            blocks.back().quote << line;
        } else {
            blocks.back().text << line;
        }
    }

    static const QRegularExpression wrote(R"(wrote:\s*$)");
    int cursor = 0;
    for (Block &b : blocks) {
        while (!b.text.isEmpty() && (b.text.back().trimmed().isEmpty() || b.text.back().contains(wrote)))
            b.text.removeLast();
        while (!b.text.isEmpty() && b.text.front().trimmed().isEmpty())
            b.text.removeFirst();
        if (b.text.isEmpty())
            continue;

        static const QRegularExpression blanks(R"(\n\s*\n(\s*\n)+)");
        Comment c{msg, {}, {}, b.quote, b.text.join('\n').replace(blanks, "\n\n")};
        int pos = -1;
        for (const QString &q : b.quote) {
            QString s = stripQuote(q);
            bool weak = s.trimmed().size() < 4;
            int from = pos >= 0 ? pos + 1 : cursor;
            int hit = -1;
            for (const QString &key : {s, " " + s}) {
                hit = idx.find(key, from);
                if (hit < 0 && pos < 0)
                    hit = idx.find(key, 0);
                if (hit >= 0)
                    break;
            }
            if (hit < 0 || (weak && pos < 0))
                continue;
            pos = hit;
            c.refs << idx.order[hit];
        }
        if (pos >= 0) {
            c.anchor = idx.order[pos];
            cursor = pos + 1;
        }
        p.comments << c;
    }
}

QStringList trailers(const QString &body)
{
    static const QRegularExpression re(R"(^\s*((Reviewed|Acked|Tested|Reported|Suggested)-by:.*)$)",
                                       QRegularExpression::MultilineOption);
    QStringList out;
    for (auto it = re.globalMatch(body); it.hasNext();)
        out << it.next().captured(1).trimmed();
    return out;
}

}

Series Series::build(QList<Message> msgs)
{
    Series s;
    std::stable_sort(msgs.begin(), msgs.end(),
                     [](const Message &a, const Message &b) { return a.dateTime < b.dateTime; });
    s.msgs = msgs;
    for (int i = 0; i < s.msgs.size(); i++)
        s.byId.insert(s.msgs[i].msgid, i);
    for (const Message &m : s.msgs) {
        QString p = m.inReplyTo.isEmpty() ? m.references.value(m.references.size() - 1) : m.inReplyTo;
        s.parent << (p == m.msgid ? -1 : s.byId.value(p, -1));
    }

    static const QRegularExpression tag(R"(^\s*\[([^\]]*PATCH[^\]]*)\])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression num(R"((\d+)/(\d+))");
    static const QRegularExpression ver(R"(\bv(\d+)\b)");
    struct Found {
        int msg, n, total, version;
    };
    QList<Found> found;
    for (int i = 0; i < s.msgs.size(); i++) {
        auto t = tag.match(s.msgs[i].subject);
        if (!t.hasMatch())
            continue;
        auto n = num.match(t.captured(1));
        auto v = ver.match(t.captured(1));
        Found f{i, n.hasMatch() ? n.captured(1).toInt() : 1, n.hasMatch() ? n.captured(2).toInt() : 1,
                v.hasMatch() ? v.captured(1).toInt() : 1};
        if (f.n == 0 || s.msgs[i].body.contains("\ndiff --git "))
            found << f;
    }
    for (const Found &f : found)
        s.version = std::max(s.version, f.version);
    for (const Found &f : found) {
        if (f.version != s.version)
            continue;
        if (f.n == 0) {
            s.cover = f.msg;
            continue;
        }
        const QString &body = s.msgs[f.msg].body;
        Patch p;
        p.msg = f.msg;
        p.number = f.n;
        p.total = f.total;
        qsizetype sep = body.indexOf("\n---\n");
        p.commitMsg = (sep < 0 ? body.left(body.indexOf("\ndiff --git ")) : body.left(sep)).trimmed();
        p.files = parseDiff(body);
        s.patches << p;
    }
    std::sort(s.patches.begin(), s.patches.end(), [](const Patch &a, const Patch &b) { return a.number < b.number; });

    QStringList coverTrailers;
    for (int i = 0; i < s.msgs.size(); i++) {
        int pi = s.patchForMessage(i);
        bool underCover = false;
        for (int m = i; m >= 0 && pi < 0; m = s.parent[m])
            underCover |= m == s.cover && m != i;
        if (underCover)
            coverTrailers << trailers(s.msgs[i].body);
        if (pi < 0 || s.patches[pi].msg == i)
            continue;
        Patch &p = s.patches[pi];
        p.trailers << trailers(s.msgs[i].body);
        extractComments(p, LineIndex(p), i, s.msgs[i].body);
    }
    for (Patch &p : s.patches) {
        p.trailers = coverTrailers + p.trailers;
        p.trailers.removeDuplicates();
    }
    return s;
}

int Series::patchForMessage(int msg) const
{
    for (int m = msg; m >= 0; m = parent[m])
        for (int i = 0; i < patches.size(); i++)
            if (patches[i].msg == m)
                return i;
    return -1;
}

int Series::depth(int msg) const
{
    int d = 0;
    for (int m = parent[msg]; m >= 0; m = parent[m])
        d++;
    return d;
}

QList<int> Series::threadOrder() const
{
    QList<int> out;
    std::function<void(int)> walk = [&](int p) {
        for (int i = 0; i < msgs.size(); i++)
            if (parent[i] == p) {
                out << i;
                walk(i);
            }
    };
    walk(-1);
    return out;
}

QString Series::title() const
{
    int m = cover >= 0 ? cover : patches.isEmpty() ? -1 : patches[0].msg;
    return m < 0 ? QString() : msgs[m].subject;
}
