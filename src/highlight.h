// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include <QStringList>
#include <QTextLayout>

using LineFormats = QList<QList<QTextLayout::FormatRange>>;

LineFormats highlightLines(const QString &path, const QStringList &lines, bool dark);
