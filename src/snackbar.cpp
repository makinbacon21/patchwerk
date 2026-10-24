// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "snackbar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

Snackbar::Snackbar(QWidget *host) : QObject(host), m_host(host)
{
    host->installEventFilter(this);
}

void Snackbar::show(const QString &text, bool error, const QString &action, std::function<void()> onAction)
{
    auto *f = new QFrame(m_host);
    f->setAutoFillBackground(true);
    f->setFrameShape(QFrame::StyledPanel);
    f->setStyleSheet(QString("QFrame { background: %1; color: white; border-radius: 6px; }"
                             "QPushButton { color: white; border: 1px solid white; border-radius: 4px; padding: 2px 8px; }")
                         .arg(error ? "#b3261e" : "#323232"));
    auto *row = new QHBoxLayout(f);
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setMaximumWidth(420);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(label);

    auto close = [this, f] {
        m_items.removeOne(f);
        f->deleteLater();
        layout();
    };
    if (!action.isEmpty()) {
        auto *b = new QPushButton(action);
        connect(b, &QPushButton::clicked, f, [close, onAction] {
            close();
            onAction();
        });
        row->addWidget(b);
    }
    auto *x = new QPushButton("✕");
    x->setFlat(true);
    connect(x, &QPushButton::clicked, f, close);
    row->addWidget(x);

    QTimer::singleShot(action.isEmpty() ? (error ? 12000 : 6000) : 30000, f, close);
    m_items << f;
    f->adjustSize();
    f->show();
    f->raise();
    layout();
}

bool Snackbar::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::Resize)
        layout();
    return QObject::eventFilter(obj, ev);
}

void Snackbar::layout()
{
    int y = m_host->height() - 16;
    if (auto *mw = qobject_cast<QMainWindow *>(m_host); mw && mw->statusBar()->isVisible())
        y -= mw->statusBar()->height();
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
        QFrame *f = *it;
        y -= f->height();
        f->move(m_host->width() - f->width() - 16, y);
        y -= 8;
    }
}
