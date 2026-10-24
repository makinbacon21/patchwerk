// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "project.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

bool Project::save(QString *err) const
{
    QJsonArray drafts;
    for (const Draft &d : this->drafts)
        drafts << QJsonObject{{"target", d.target}, {"file", d.anchor.file}, {"hunk", d.anchor.hunk},
                              {"line", d.anchor.line}, {"quote", QJsonArray::fromStringList(d.quote)},
                              {"text", d.text}, {"llm", d.llm}};
    QJsonObject replies;
    for (auto it = this->replies.begin(); it != this->replies.end(); ++it)
        replies[it.key()] = QJsonObject{{"to", it->to}, {"cc", it->cc}, {"body", it->body}};
    QJsonObject root{{"repo", repo}, {"msgid", msgid}, {"mbox", mbox}, {"drafts", drafts}, {"replies", replies}};

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson()) < 0) {
        *err = f.errorString();
        return false;
    }
    return true;
}

bool Project::load(const QString &file, QString *err)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *err = f.errorString();
        return false;
    }
    QJsonParseError pe;
    QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError) {
        *err = pe.errorString();
        return false;
    }
    *this = {};
    path = file;
    repo = root["repo"].toString();
    msgid = root["msgid"].toString();
    mbox = root["mbox"].toString();
    for (const QJsonValue &v : root["drafts"].toArray()) {
        QJsonObject o = v.toObject();
        Draft d{o["target"].toString(),
                {o["file"].toInt(-1), o["hunk"].toInt(-1), o["line"].toInt(-1)},
                {},
                o["text"].toString(),
                o["llm"].toBool()};
        for (const QJsonValue &q : o["quote"].toArray())
            d.quote << q.toString();
        drafts << d;
    }
    QJsonObject r = root["replies"].toObject();
    for (auto it = r.begin(); it != r.end(); ++it) {
        QJsonObject o = it->toObject();
        replies[it.key()] = {o["to"].toString(), o["cc"].toString(), o["body"].toString()};
    }
    return true;
}

Reply &Project::reply(const Series &s, const QString &target, const QString &me)
{
    if (!replies.contains(target) && s.byId.contains(target)) {
        const Message &m = s.msgs[s.byId.value(target)];
        QStringList cc;
        for (const QString &a : splitAddresses(m.to) + splitAddresses(m.cc))
            if (addressEmail(a) != addressEmail(me) && addressEmail(a) != addressEmail(m.from))
                cc << a;
        cc.removeDuplicates();
        replies[target] = {m.from, cc.join(", "), {}};
    }
    return replies[target];
}

bool Project::hasContent(const QString &target) const
{
    return !replies.value(target).body.trimmed().isEmpty() ||
           std::any_of(drafts.begin(), drafts.end(), [&](const Draft &d) { return d.target == target; });
}

QString patchLabel(const Series &s, int msg)
{
    const Message &m = s.msgs[msg];
    int pi = s.patchForMessage(msg);
    if (pi >= 0 && s.patches[pi].msg == msg)
        return m.subject;
    return addressName(m.from) + ": " + m.subject;
}

QString composeReply(const Series &s, const Project &p, const QString &target, const QString &from)
{
    int mi = s.byId.value(target, -1);
    if (mi < 0)
        return {};
    const Message &m = s.msgs[mi];
    const Reply r = p.replies.value(target);
    int pi = s.patchForMessage(mi);
    const Patch *patch = pi >= 0 && s.patches[pi].msg == mi ? &s.patches[pi] : nullptr;

    QList<Draft> drafts;
    for (const Draft &d : p.drafts)
        if (d.target == target)
            drafts << d;
    std::stable_sort(drafts.begin(), drafts.end(), [](const Draft &a, const Draft &b) { return a.anchor < b.anchor; });

    QStringList refs = m.references;
    if (refs.isEmpty() && !m.inReplyTo.isEmpty())
        refs << m.inReplyTo;
    refs << m.msgid;
    for (QString &id : refs)
        id = "<" + id + ">";

    QString out = "From: " + from + "\nTo: " + r.to + "\n";
    if (!r.cc.trimmed().isEmpty())
        out += "Cc: " + r.cc + "\n";
    out += "Subject: Re: " + cleanSubject(m.subject) + "\nIn-Reply-To: <" + m.msgid + ">\nReferences: " +
           refs.join(' ') + "\nMIME-Version: 1.0\nContent-Type: text/plain; charset=UTF-8\n"
           "Content-Transfer-Encoding: 8bit\n\n";

    QString body;
    QStringList general;
    LineRef last;
    for (const Draft &d : drafts) {
        if (!d.anchor.valid() && d.quote.isEmpty()) {
            general << d.text;
            continue;
        }
        if (body.isEmpty())
            body = "On " + m.date + ", " + m.from + " wrote:\n";
        else
            body += "\n[...]\n\n";
        if (!d.quote.isEmpty()) {
            for (const QString &q : d.quote)
                body += (q.startsWith('>') || q.isEmpty() ? ">" : "> ") + q + "\n";
        } else if (patch) {
            const FileDiff &f = patch->files[d.anchor.file];
            const Hunk &h = f.hunks[d.anchor.hunk];
            int start = std::max(0, d.anchor.line - 4);
            if (last.file != d.anchor.file)
                body += "> " + f.gitLine + "\n";
            if (last.file != d.anchor.file || last.hunk != d.anchor.hunk)
                body += "> " + h.header + "\n";
            else
                start = std::max(start, last.line + 1);
            for (int i = start; i <= d.anchor.line; i++)
                body += QString("> ") + h.lines[i].type + h.lines[i].text + "\n";
            last = d.anchor;
        }
        body += "\n" + d.text.trimmed() + "\n";
    }
    for (const QString &g : general)
        body += (body.isEmpty() ? "" : "\n") + g.trimmed() + "\n";
    if (!r.body.trimmed().isEmpty())
        body += (body.isEmpty() ? "" : "\n") + r.body.trimmed() + "\n";
    return out + body;
}
