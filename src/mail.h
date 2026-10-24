// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

struct Message {
    QString msgid;
    QString inReplyTo;
    QStringList references;
    QString from;
    QString to;
    QString cc;
    QString subject;
    QString date;
    QDateTime dateTime;
    QString body;

    QString preview() const;
};

QList<Message> parseMbox(const QByteArray &data);
QStringList splitAddresses(const QString &list);
QString addressEmail(const QString &addr);
QString addressName(const QString &addr);
