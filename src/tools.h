// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include <QObject>
#include <QStringList>
#include <functional>

using ProcDone = std::function<void(int code, const QString &out, const QString &err)>;

void runProcess(QObject *ctx, const QString &prog, const QStringList &args, const QString &cwd, ProcDone done);
QString runSync(const QString &prog, const QStringList &args, const QString &cwd);
bool isGitRepo(const QString &dir);

struct Tree {
    QString section;
    QString url;
    QString branch;
};

QList<Tree> findTrees(const QString &maintainers, const QStringList &files);
