// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include "project.h"

#include <QTextEdit>

class QPlainTextEdit;
class QPushButton;
class QToolButton;

class DiffView : public QTextEdit {
    Q_OBJECT
public:
    DiffView(const Series *s, Project *p, int patch, int file, const QString &path, QWidget *parent = nullptr);

    int patch() const { return m_patch; }
    int file() const { return m_file; }
    QString path() const { return m_path; }
    bool wholeFile() const { return m_whole; }
    void setWholeFile(bool whole);
    void rebuild();
    void highlightQuote(const QString &line);
    void highlightRefs(const QList<LineRef> &refs);
    void scrollTo(LineRef ref);

signals:
    void draftsChanged();
    void messageSelected(int msg);

protected:
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    struct Row {
        enum Kind { Text, Line, Comment, Draft, Editor } kind = Text;
        LineRef ref;
        int index = -1;
        QString key;
    };
    struct Span {
        QString text;
        bool bold = false;
        QString href;
    };
    struct Editing {
        enum Mode { None, New, Reply, Edit } mode = None;
        LineRef ref;
        int index = -1;
        QString text;
    };
    struct Shown {
        DiffLine line;
        LineRef ref;
    };

    QList<Shown> fileLines(int file, bool whole, QString *note) const;
    void addLine(const Shown &s);
    void addComments(LineRef ref);
    void addBlock(const QColor &bg, int indent, const QList<Span> &parts, const Row &row);
    void startEdit(Editing::Mode mode, LineRef ref, int index);
    bool commitEdit();
    void endEdit(bool save);
    void placeEditor();
    int navTarget(bool next) const;
    void updateNav();
    void styleNav();
    const Row *rowAt(const QPoint &pos) const;
    void highlightRows(const QList<int> &rows, bool scroll);

    const Series *m_series;
    Project *m_project;
    int m_patch;
    int m_file;
    QString m_path;
    bool m_whole = false;
    QList<Row> m_rows;
    int m_hover = -1;
    Editing m_edit;
    QWidget *m_editor;
    QPlainTextEdit *m_editText;
    QPushButton *m_delete;
    QToolButton *m_prev;
    QToolButton *m_next;
};
