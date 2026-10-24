// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include <QFrame>
#include <functional>

class Snackbar : public QObject {
    Q_OBJECT
public:
    explicit Snackbar(QWidget *host);
    void show(const QString &text, bool error = false, const QString &action = {},
              std::function<void()> onAction = {});

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void layout();

    QWidget *m_host;
    QList<QFrame *> m_items;
};
