// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include "series.h"

#include <QMap>

struct Draft {
    QString target;
    LineRef anchor;
    QStringList quote;
    QString text;
    bool llm = false;
};

struct Reply {
    QString to;
    QString cc;
    QString body;
};

struct Project {
    QString path;
    QString repo;
    QString msgid;
    QString mbox;
    QList<Draft> drafts;
    QMap<QString, Reply> replies;

    bool save(QString *err) const;
    bool load(const QString &file, QString *err);
    Reply &reply(const Series &s, const QString &target, const QString &me);
    bool hasContent(const QString &target) const;
};

QString composeReply(const Series &s, const Project &p, const QString &target, const QString &from);
QString patchLabel(const Series &s, int msg);
