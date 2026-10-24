// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include <QPlainTextEdit>

class MailView : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit MailView(QWidget *parent = nullptr);

signals:
    void lineHovered(const QString &line);

protected:
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    int m_last = -1;
};
