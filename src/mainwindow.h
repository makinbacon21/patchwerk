// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include "llm.h"
#include "project.h"
#include "tools.h"

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QTimer>

class DiffView;
class MailView;
class QCheckBox;
class QPlainTextEdit;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class Sidebar;
class Snackbar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void openInput(const QString &input);
    void openProjectFile(const QString &file);
    void restoreSession(bool layoutOnly);

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void buildUi();
    void detectRepo();
    void setRepo(const QString &repo);
    bool requireRepo();
    bool chooseRepo();
    void updateTreeButton();
    void updatePanelIcons();
    void withMaintainers(std::function<void(const QString &)> done);
    void pickTree(const QList<Tree> &trees);
    void cloneTree(const Tree &tree);
    void fetchTree(const Tree &tree);
    void loadMbox();
    void refreshAll();
    void changed();
    void rebuildViews();
    void rebuildConversation();
    void updateOutgoing();
    void showMessage(int msg);
    DiffView *currentView() const;
    DiffView *openPatchTab(int patch, int file);
    void openPathTab(const QString &path);
    int currentPatch() const;

    void newSeries();
    void openProject();
    bool saveProject();
    void saveProjectAs();
    void fetchLatest(bool manual);
    void applySeries();
    void llmRebase();
    void pullTree();
    void review();
    void addMaintainers(bool prompt);
    void sendReplies();
    void sendNext(QStringList targets, int sent);
    void restartTimer();
    void saveSession();

    Series m_series;
    Project m_project;
    Llm m_llm;
    QNetworkAccessManager m_nam;
    QString m_me;
    bool m_dirty = false;
    int m_notified = 0;
    QTimer m_refresh;
    QTimer m_sessionTimer;

    Snackbar *m_snack;
    Sidebar *m_side;
    QTabWidget *m_tabs;
    QTreeWidget *m_convo;
    QCheckBox *m_whole;
    QTabWidget *m_right;
    MailView *m_mail;
    QPlainTextEdit *m_outgoing;
    QToolButton *m_treeButton;
    QAction *m_leftToggle;
    QAction *m_rightToggle;
};
