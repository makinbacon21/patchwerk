// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include "project.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTreeView;
class QTreeWidget;

class Sidebar : public QWidget {
    Q_OBJECT
public:
    Sidebar(const Series *s, Project *p, QWidget *parent = nullptr);

    void setIdentity(const QString &me) { m_me = me; }
    void refresh();
    void setCurrentMessage(int msg);
    QString target() const { return m_target; }
    void addCc(const QStringList &addrs);

signals:
    void openPatch(int patch, int file);
    void openPath(const QString &path);
    void messageSelected(int msg);
    void replyEdited();
    void draftActivated(int draft);
    void addMaintainers();
    void sendRequested();

private:
    void refreshTree();
    void refreshDrafter();
    void selectTarget(const QString &msgid);
    void insertTrailer(const QString &tag);

    const Series *m_series;
    Project *m_project;
    QString m_me;
    QString m_target;
    int m_msg = -1;

    QCheckBox *m_all;
    QStackedWidget *m_stack;
    QTreeWidget *m_tree;
    QTreeView *m_fsView;
    QFileSystemModel *m_fs;
    QLabel *m_meta;
    QComboBox *m_targets;
    QLineEdit *m_to;
    QLineEdit *m_cc;
    QPlainTextEdit *m_body;
    QListWidget *m_drafts;
};
