// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "mail.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>

namespace {

using Headers = QHash<QByteArray, QByteArray>;

QString decodeBytes(const QByteArray &data, const QByteArray &charset)
{
    QStringDecoder dec(charset.isEmpty() ? "utf-8" : charset.constData());
    if (!dec.isValid())
        dec = QStringDecoder(QStringDecoder::Utf8);
    return dec.decode(data);
}

QByteArray decodeQp(const QByteArray &in, bool header)
{
    QByteArray out;
    out.reserve(in.size());
    for (qsizetype i = 0; i < in.size(); i++) {
        char c = in[i];
        if (header && c == '_') {
            out += ' ';
        } else if (c == '=' && i + 1 < in.size() && in[i + 1] == '\n') {
            i++;
        } else if (c == '=' && i + 2 < in.size() && in[i + 1] == '\r' && in[i + 2] == '\n') {
            i += 2;
        } else if (c == '=' && i + 2 < in.size()) {
            bool ok;
            int v = in.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                out += char(v);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

QString decodeHeader(const QByteArray &raw)
{
    static const QRegularExpression gap(R"((\?=)\s+(=\?))");
    static const QRegularExpression word(R"(=\?([^?]+)\?([bBqQ])\?([^?]*)\?=)");
    QString s = QString::fromUtf8(raw);
    s.replace(gap, "\\1\\2");
    QString out;
    qsizetype last = 0;
    for (auto it = word.globalMatch(s); it.hasNext();) {
        auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        QByteArray text = m.captured(3).toLatin1();
        QByteArray bytes = m.captured(2).toUpper() == "B" ? QByteArray::fromBase64(text)
                                                          : decodeQp(text, true);
        out += decodeBytes(bytes, m.captured(1).toLatin1());
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out.simplified();
}

QByteArray param(const QByteArray &value, const QByteArray &name)
{
    QRegularExpression re(QString(R"x(%1\s*=\s*"?([^";]+)"?)x").arg(QString::fromLatin1(name)),
                          QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(QString::fromLatin1(value));
    return m.hasMatch() ? m.captured(1).trimmed().toLatin1() : QByteArray();
}

void parseHeaders(const QByteArray &block, Headers &h)
{
    QByteArray key;
    for (const QByteArray &line : block.split('\n')) {
        if ((line.startsWith(' ') || line.startsWith('\t')) && !key.isEmpty()) {
            h[key] += ' ' + line.trimmed();
            continue;
        }
        qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        key = line.left(colon).trimmed().toLower();
        if (!h.contains(key))
            h[key] = line.mid(colon + 1).trimmed();
        else
            key.clear();
    }
}

QString entityText(const Headers &h, const QByteArray &body)
{
    QByteArray type = h.value("content-type", "text/plain");
    if (type.toLower().startsWith("multipart/")) {
        QByteArray boundary = "--" + param(type, "boundary");
        qsizetype pos = body.indexOf(boundary);
        while (pos >= 0) {
            qsizetype start = body.indexOf('\n', pos);
            qsizetype next = body.indexOf(boundary, start);
            if (start < 0 || next < 0)
                break;
            QByteArray part = body.mid(start + 1, next - start - 1);
            qsizetype split = part.indexOf("\n\n");
            Headers ph;
            parseHeaders(part.left(split), ph);
            if (ph.value("content-type", "text/plain").toLower().startsWith("text/plain"))
                return entityText(ph, split < 0 ? QByteArray() : part.mid(split + 2));
            pos = next;
        }
        return {};
    }
    QByteArray enc = h.value("content-transfer-encoding").toLower();
    QByteArray data = body;
    if (enc == "quoted-printable")
        data = decodeQp(body, false);
    else if (enc == "base64")
        data = QByteArray::fromBase64(body.split('\n').join());
    return decodeBytes(data, param(type, "charset"));
}

Message parseMessage(QByteArray raw)
{
    raw.replace("\r\n", "\n");
    qsizetype split = raw.indexOf("\n\n");
    Headers h;
    parseHeaders(raw.left(split), h);
    QByteArray body = split < 0 ? QByteArray() : raw.mid(split + 2);

    static const QRegularExpression idRe("<([^>]+)>");
    auto ids = [](const QByteArray &v) {
        QStringList out;
        for (auto it = idRe.globalMatch(QString::fromLatin1(v)); it.hasNext();)
            out << it.next().captured(1);
        return out;
    };

    Message m;
    m.msgid = ids(h.value("message-id")).value(0);
    m.inReplyTo = ids(h.value("in-reply-to")).value(0);
    m.references = ids(h.value("references"));
    m.from = decodeHeader(h.value("from"));
    m.to = decodeHeader(h.value("to"));
    m.cc = decodeHeader(h.value("cc"));
    m.subject = decodeHeader(h.value("subject"));
    m.date = QString::fromLatin1(h.value("date"));
    static const QRegularExpression comment(R"(\s*\(.*\)\s*$)");
    m.dateTime = QDateTime::fromString(QString(m.date).remove(comment), Qt::RFC2822Date);
    m.body = entityText(h, body);
    return m;
}

}

QString Message::preview() const
{
    QString out = "From: " + from + "\nDate: " + date + "\nSubject: " + subject + "\nTo: " + to;
    if (!cc.isEmpty())
        out += "\nCc: " + cc;
    out += "\nMessage-ID: <" + msgid + ">\n\n" + body;
    return out;
}

QList<Message> parseMbox(const QByteArray &data)
{
    static const QRegularExpression escaped("^>(>*From )");
    QList<Message> out;
    QByteArray cur;
    auto flush = [&] {
        if (!cur.trimmed().isEmpty())
            out << parseMessage(cur);
        cur.clear();
    };
    for (const QByteArray &line : data.split('\n')) {
        if (line.startsWith("From ")) {
            flush();
            continue;
        }
        if (line.startsWith(">") && QString::fromLatin1(line).contains(escaped))
            cur += line.mid(1);
        else
            cur += line;
        cur += '\n';
    }
    flush();
    return out;
}

QStringList splitAddresses(const QString &list)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    int angle = 0;
    for (QChar c : list) {
        if (c == '"')
            quoted = !quoted;
        else if (c == '<')
            angle++;
        else if (c == '>')
            angle--;
        if (c == ',' && !quoted && angle <= 0) {
            if (!cur.trimmed().isEmpty())
                out << cur.trimmed();
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.trimmed().isEmpty())
        out << cur.trimmed();
    return out;
}

QString addressEmail(const QString &addr)
{
    qsizetype l = addr.indexOf('<'), r = addr.lastIndexOf('>');
    QString e = l >= 0 && r > l ? addr.mid(l + 1, r - l - 1) : addr;
    return e.trimmed().toLower();
}

QString addressName(const QString &addr)
{
    qsizetype l = addr.indexOf('<');
    QString n = l > 0 ? addr.left(l).trimmed() : addr;
    n.remove('"');
    return n.isEmpty() ? addr : n;
}
