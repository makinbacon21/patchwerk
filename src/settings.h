// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include <QVariant>

class QWidget;

QVariant setting(const QString &key);
void applyTheme();
bool showSettings(QWidget *parent);
