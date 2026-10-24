// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "mailview.h"

#include <QFontDatabase>
#include <QMouseEvent>
#include <QTextBlock>

MailView::MailView(QWidget *parent) : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    viewport()->setMouseTracking(true);
}

void MailView::mouseMoveEvent(QMouseEvent *e)
{
    QPlainTextEdit::mouseMoveEvent(e);
    QTextBlock b = cursorForPosition(e->pos()).block();
    if (b.blockNumber() == m_last)
        return;
    m_last = b.blockNumber();
    emit lineHovered(b.text());
}
