// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include "mail.h"

#include <QHash>

struct LineRef {
    int file = -1;
    int hunk = -1;
    int line = -1;

    bool valid() const { return file >= 0; }
    auto operator<=>(const LineRef &) const = default;
};

struct DiffLine {
    QChar type;
    int oldNo = 0;
    int newNo = 0;
    QString text;

    QString key() const;
};

struct Hunk {
    QString header;
    int oldStart = 0;
    int newStart = 0;
    QList<DiffLine> lines;
};

struct FileDiff {
    QString gitLine;
    QString oldPath;
    QString newPath;
    QList<Hunk> hunks;

    QString path() const { return newPath == "/dev/null" ? oldPath : newPath; }
};

struct Comment {
    int msg = -1;
    LineRef anchor;
    QList<LineRef> refs;
    QStringList quote;
    QString text;
};

struct Patch {
    int msg = -1;
    int number = 0;
    int total = 0;
    QString commitMsg;
    QList<FileDiff> files;
    QList<Comment> comments;
    QStringList trailers;

    const DiffLine &at(LineRef r) const { return files[r.file].hunks[r.hunk].lines[r.line]; }
};

struct Series {
    QList<Message> msgs;
    QList<int> parent;
    QHash<QString, int> byId;
    int cover = -1;
    int version = 1;
    QList<Patch> patches;

    static Series build(QList<Message> msgs);
    int patchForMessage(int msg) const;
    QList<int> threadOrder() const;
    int depth(int msg) const;
    QString title() const;
};

QList<FileDiff> parseDiff(const QString &text);
QString stripQuote(const QString &line);
QString cleanSubject(const QString &subject);
