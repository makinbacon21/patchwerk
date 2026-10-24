// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "mainwindow.h"
#include "diffview.h"
#include "mailview.h"
#include "settings.h"
#include "sidebar.h"
#include "snackbar.h"
#include "tools.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPainter>
#include <QStatusBar>
#include <QToolButton>
#include <QProcess>
#include <QScrollBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <memory>

namespace {

const char *MaintainersUrl = "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/MAINTAINERS";

QString fileDiffText(const FileDiff &f)
{
    QString out = f.gitLine + "\n--- " + (f.oldPath == "/dev/null" ? f.oldPath : "a/" + f.oldPath) + "\n+++ " +
                  (f.newPath == "/dev/null" ? f.newPath : "b/" + f.newPath) + "\n";
    for (const Hunk &h : f.hunks) {
        out += h.header + "\n";
        for (const DiffLine &l : h.lines)
            out += l.type + l.text + "\n";
    }
    return out;
}

QString sessionDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    return dir;
}

QString errorText(const QString &out, const QString &err)
{
    QString s = (err.trimmed().isEmpty() ? out : err).trimmed();
    return s.size() > 600 ? "…" + s.right(600) : s;
}

}

MainWindow::MainWindow()
{
    m_snack = new Snackbar(this);
    buildUi();
    connect(&m_refresh, &QTimer::timeout, this, [this] { fetchLatest(false); });
    restartTimer();
    detectRepo();
    refreshAll();
}

void MainWindow::buildUi()
{
    setWindowTitle("Patchwerk");
    resize(1600, 950);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_convo = new QTreeWidget;
    m_convo->setHeaderLabels({"Subject", "From", "Date"});
    m_convo->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tabs->addTab(m_convo, "Conversation");
    m_tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    m_whole = new QCheckBox("Whole file");
    m_tabs->setCornerWidget(m_whole);
    setCentralWidget(m_tabs);

    m_side = new Sidebar(&m_series, &m_project);
    auto *left = new QDockWidget("Navigator", this);
    left->setObjectName("left");
    left->setWidget(m_side);
    addDockWidget(Qt::LeftDockWidgetArea, left);

    m_right = new QTabWidget;
    m_right->setDocumentMode(true);
    m_mail = new MailView;
    m_outgoing = new QPlainTextEdit;
    m_outgoing->setReadOnly(true);
    m_outgoing->setFont(m_mail->font());
    m_outgoing->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_right->addTab(m_mail, "Message");
    m_right->addTab(m_outgoing, "Outgoing");
    auto *right = new QDockWidget("Email", this);
    right->setObjectName("right");
    right->setWidget(m_right);
    addDockWidget(Qt::RightDockWidgetArea, right);
    resizeDocks({left, right}, {380, 520}, Qt::Horizontal);

    auto *tb = addToolBar("Main");
    tb->setObjectName("main");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto act = [&](const QString &icon, const QString &text, auto fn, QKeySequence key = {}) {
        QAction *a = tb->addAction(QIcon::fromTheme(icon), text, this, fn);
        a->setShortcut(key);
        return a;
    };
    act("document-new", "Open Series", [this] { newSeries(); }, QKeySequence::New);
    act("document-open", "Open Project", [this] { openProject(); }, QKeySequence::Open);
    act("document-save", "Save", [this] { saveProject(); }, QKeySequence::Save);
    act("document-save-as", "Save As", [this] { saveProjectAs(); }, QKeySequence::SaveAs);
    tb->addSeparator();
    act("view-refresh", "Fetch Latest", [this] { fetchLatest(true); }, QKeySequence::Refresh);
    act("go-down", "Apply Series", [this] { applySeries(); });
    act("vcs-merge", "LLM Rebase", [this] { llmRebase(); });
    act("vcs-pull", "Pull Tree", [this] { pullTree(); });
    tb->addSeparator();
    act("tools-check-spelling", "Review", [this] { review(); });
    act("mail-send", "Send", [this] { sendReplies(); });
    tb->addSeparator();
    act("configure", "Settings", [this] {
        if (showSettings(this))
            restartTimer();
    });

    m_leftToggle = left->toggleViewAction();
    m_rightToggle = right->toggleViewAction();
    m_leftToggle->setShortcut(QKeySequence("Ctrl+B"));
    m_rightToggle->setShortcut(QKeySequence("Ctrl+Alt+B"));
    m_leftToggle->setToolTip("Toggle navigator (Ctrl+B)");
    m_rightToggle->setToolTip("Toggle email (Ctrl+Alt+B)");
    auto toggle = [](QAction *a) {
        auto *b = new QToolButton;
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize({16, 16});
        return b;
    };
    m_treeButton = new QToolButton;
    m_treeButton->setAutoRaise(true);
    m_treeButton->setToolTip("Choose kernel tree");
    connect(m_treeButton, &QToolButton::clicked, this, &MainWindow::chooseRepo);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(toggle(m_leftToggle));
    statusBar()->addWidget(m_treeButton);
    statusBar()->addPermanentWidget(toggle(m_rightToggle));
    updatePanelIcons();
    updateTreeButton();

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) {
        if (i > 0)
            delete m_tabs->widget(i);
    });
    m_sessionTimer.setSingleShot(true);
    m_sessionTimer.setInterval(1000);
    connect(&m_sessionTimer, &QTimer::timeout, this, &MainWindow::saveSession);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        m_sessionTimer.start();
        DiffView *v = currentView();
        const QSignalBlocker b(m_whole);
        m_whole->setEnabled(v && v->file() >= 0);
        m_whole->setChecked(v && v->wholeFile());
        if (v && v->patch() >= 0) {
            m_side->setCurrentMessage(m_series.patches[v->patch()].msg);
            showMessage(m_series.patches[v->patch()].msg);
        }
    });
    connect(m_whole, &QCheckBox::toggled, this, [this](bool on) {
        m_sessionTimer.start();
        if (DiffView *v = currentView())
            v->setWholeFile(on);
    });
    connect(m_convo, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
        int msg = it->data(0, Qt::UserRole).toInt();
        showMessage(msg);
        m_side->setCurrentMessage(msg);
    });
    connect(m_convo, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        int pi = m_series.patchForMessage(it->data(0, Qt::UserRole).toInt());
        if (pi >= 0)
            openPatchTab(pi, -1);
    });
    connect(m_mail, &MailView::lineHovered, this, [this](const QString &line) {
        if (DiffView *v = currentView())
            v->highlightQuote(line);
    });

    connect(m_side, &Sidebar::openPatch, this, [this](int p, int f) { openPatchTab(p, f); });
    connect(m_side, &Sidebar::openPath, this, &MainWindow::openPathTab);
    connect(m_side, &Sidebar::messageSelected, this, &MainWindow::showMessage);
    connect(m_side, &Sidebar::replyEdited, this, [this] {
        m_dirty = true;
        m_sessionTimer.start();
        updateOutgoing();
    });
    connect(m_side, &Sidebar::draftActivated, this, [this](int i) {
        const Draft &d = m_project.drafts[i];
        int pi = m_series.patchForMessage(m_series.byId.value(d.target, -1));
        if (pi >= 0)
            openPatchTab(pi, -1)->scrollTo(d.anchor);
    });
    connect(m_side, &Sidebar::addMaintainers, this, [this] { addMaintainers(true); });
    connect(m_side, &Sidebar::sendRequested, this, &MainWindow::sendReplies);
}

void MainWindow::restartTimer()
{
    int min = setting("refresh/minutes").toInt();
    if (min > 0)
        m_refresh.start(min * 60000);
    else
        m_refresh.stop();
}

void MainWindow::detectRepo()
{
    QString repo = m_project.repo;
    if (!repo.isEmpty() && !isGitRepo(repo))
        repo.clear();
    if (repo.isEmpty()) {
        QString cwd = runSync("git", {"rev-parse", "--show-toplevel"}, QDir::currentPath());
        if (!cwd.isEmpty() && QFileInfo::exists(cwd + "/MAINTAINERS"))
            repo = cwd;
    }
    setRepo(repo);
}

void MainWindow::setRepo(const QString &repo)
{
    m_project.repo = repo;
    QString dir = repo.isEmpty() ? QDir::homePath() : repo;
    m_me = runSync("git", {"config", "user.name"}, dir) + " <" + runSync("git", {"config", "user.email"}, dir) + ">";
    m_side->setIdentity(m_me);
    updateTreeButton();
    m_sessionTimer.start();
}

bool MainWindow::requireRepo()
{
    if (!m_project.repo.isEmpty())
        return true;
    m_snack->show("This needs a kernel tree. Choose one, or clone one with Pull Tree.", true, "Choose Tree",
                  [this] { chooseRepo(); });
    return false;
}

bool MainWindow::chooseRepo()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select kernel tree", m_project.repo);
    if (dir.isEmpty())
        return false;
    QString top = runSync("git", {"rev-parse", "--show-toplevel"}, dir);
    if (top.isEmpty()) {
        m_snack->show(dir + " is not a git tree.", true);
        return false;
    }
    setRepo(top);
    m_dirty = true;
    refreshAll();
    return true;
}

void MainWindow::updateTreeButton()
{
    QString repo = m_project.repo;
    if (repo.isEmpty()) {
        m_treeButton->setText("No kernel tree");
        return;
    }
    QString branch = runSync("git", {"rev-parse", "--abbrev-ref", "HEAD"}, repo);
    QString home = QDir::homePath();
    m_treeButton->setText((repo.startsWith(home) ? "~" + repo.mid(home.size()) : repo) + "  ⎇ " + branch);
}

void MainWindow::newSeries()
{
    QString input = QInputDialog::getText(this, "Open Series", "Message-ID, lore URL or mbox file:");
    if (!input.trimmed().isEmpty())
        openInput(input.trimmed());
}

void MainWindow::openInput(const QString &input)
{
    if (m_dirty && QMessageBox::question(this, "Patchwerk", "Discard unsaved drafts?") != QMessageBox::Yes)
        return;
    QString repo = m_project.repo;
    m_project = {};
    m_project.repo = repo;
    detectRepo();

    QString name = QFileInfo(input).exists() ? QFileInfo(input).completeBaseName() : input;
    name.replace(QRegularExpression("[^A-Za-z0-9._@-]+"), "_");
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + name.right(120);
    QDir().mkpath(dir);
    m_project.path = dir + "/project.json";
    m_project.mbox = dir + "/thread.mbox";

    if (QFileInfo(input).exists()) {
        QFile::remove(m_project.mbox);
        QFile::copy(input, m_project.mbox);
        loadMbox();
        return;
    }
    m_snack->show("Fetching thread with b4…");
    runProcess(this, "b4", {"mbox", "-C", "-o", dir, "-n", "thread.mbox", input}, m_project.repo,
               [this](int code, const QString &out, const QString &err) {
                   if (code != 0) {
                       m_snack->show("b4 mbox failed: " + errorText(out, err), true);
                       return;
                   }
                   loadMbox();
               });
}

void MainWindow::loadMbox()
{
    QFile f(m_project.mbox);
    if (!f.open(QIODevice::ReadOnly)) {
        m_snack->show("Cannot read " + m_project.mbox + ": " + f.errorString(), true);
        return;
    }
    m_series = Series::build(parseMbox(f.readAll()));
    if (m_project.msgid.isEmpty() && !m_series.patches.isEmpty())
        m_project.msgid = m_series.msgs[m_series.cover >= 0 ? m_series.cover : m_series.patches[0].msg].msgid;
    m_notified = m_series.msgs.size();
    while (m_tabs->count() > 1)
        delete m_tabs->widget(1);
    refreshAll();
    if (m_series.patches.isEmpty()) {
        m_snack->show("No patches found in thread.", true);
        return;
    }
    openPatchTab(0, -1);
    showMessage(m_series.patches[0].msg);
    m_sessionTimer.start();
    addMaintainers(false);
}

void MainWindow::openProject()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString file = QFileDialog::getOpenFileName(this, "Open Project", dir, "Patchwerk project (*.json)");
    if (!file.isEmpty())
        openProjectFile(file);
}

void MainWindow::openProjectFile(const QString &file)
{
    if (m_dirty && QMessageBox::question(this, "Patchwerk", "Discard unsaved drafts?") != QMessageBox::Yes)
        return;
    QString err;
    Project p;
    if (!p.load(file, &err)) {
        m_snack->show("Cannot open project: " + err, true);
        return;
    }
    m_project = p;
    m_dirty = false;
    detectRepo();
    loadMbox();
}

bool MainWindow::saveProject()
{
    if (m_project.path.isEmpty())
        return false;
    QString err;
    if (!m_project.save(&err)) {
        m_snack->show("Save failed: " + err, true);
        return false;
    }
    m_dirty = false;
    m_snack->show("Saved " + m_project.path);
    return true;
}

void MainWindow::saveProjectAs()
{
    QString file = QFileDialog::getSaveFileName(this, "Save Project As", m_project.path, "Patchwerk project (*.json)");
    if (file.isEmpty())
        return;
    m_project.path = file;
    saveProject();
}

void MainWindow::refreshAll()
{
    setWindowTitle(m_series.msgs.isEmpty() ? "Patchwerk" : m_series.title() + " — Patchwerk");
    m_side->refresh();
    rebuildConversation();
    rebuildViews();
    updateOutgoing();
}

void MainWindow::changed()
{
    m_dirty = true;
    m_sessionTimer.start();
    m_side->refresh();
    rebuildViews();
    updateOutgoing();
}

void MainWindow::rebuildViews()
{
    for (int i = 1; i < m_tabs->count(); i++)
        if (auto *v = qobject_cast<DiffView *>(m_tabs->widget(i)))
            v->rebuild();
}

void MainWindow::rebuildConversation()
{
    m_convo->clear();
    QHash<int, QTreeWidgetItem *> items;
    for (int i : m_series.threadOrder()) {
        const Message &m = m_series.msgs[i];
        QStringList cols{m.subject, addressName(m.from), m.dateTime.toLocalTime().toString("yyyy-MM-dd hh:mm")};
        QTreeWidgetItem *parent = items.value(m_series.parent[i]);
        auto *it = parent ? new QTreeWidgetItem(parent, cols) : new QTreeWidgetItem(m_convo, cols);
        it->setData(0, Qt::UserRole, i);
        it->setToolTip(0, m.subject);
        items[i] = it;
    }
    m_convo->expandAll();
}

void MainWindow::updateOutgoing()
{
    QString t = m_side->target();
    m_outgoing->setPlainText(t.isEmpty() ? QString() : composeReply(m_series, m_project, t, m_me));
}

void MainWindow::showMessage(int msg)
{
    if (msg < 0 || msg >= m_series.msgs.size())
        return;
    m_mail->setPlainText(m_series.msgs[msg].preview());
    m_right->setCurrentWidget(m_mail);
}

DiffView *MainWindow::currentView() const
{
    return qobject_cast<DiffView *>(m_tabs->currentWidget());
}

int MainWindow::currentPatch() const
{
    if (DiffView *v = currentView(); v && v->patch() >= 0)
        return v->patch();
    int msg = m_series.byId.value(m_side->target(), -1);
    int pi = msg >= 0 ? m_series.patchForMessage(msg) : -1;
    return pi >= 0 ? pi : m_series.patches.isEmpty() ? -1 : 0;
}

DiffView *MainWindow::openPatchTab(int patch, int file)
{
    for (int i = 1; i < m_tabs->count(); i++)
        if (auto *v = qobject_cast<DiffView *>(m_tabs->widget(i)); v && v->patch() == patch && v->file() == file) {
            m_tabs->setCurrentIndex(i);
            return v;
        }
    const Patch &p = m_series.patches[patch];
    auto *v = new DiffView(&m_series, &m_project, patch, file, {});
    connect(v, &DiffView::draftsChanged, this, &MainWindow::changed);
    connect(v, &DiffView::messageSelected, this, &MainWindow::showMessage);
    QString title = file < 0 ? QString("%1/%2").arg(p.number).arg(p.total) : QFileInfo(p.files[file].path()).fileName();
    int i = m_tabs->addTab(v, title);
    m_tabs->setTabToolTip(i, file < 0 ? m_series.msgs[p.msg].subject : p.files[file].path());
    m_tabs->setCurrentIndex(i);
    return v;
}

void MainWindow::openPathTab(const QString &path)
{
    int pi = currentPatch();
    if (pi >= 0)
        for (int f = 0; f < m_series.patches[pi].files.size(); f++)
            if (m_series.patches[pi].files[f].path() == path) {
                openPatchTab(pi, f)->setWholeFile(true);
                m_whole->setChecked(true);
                return;
            }
    for (int i = 1; i < m_tabs->count(); i++)
        if (auto *v = qobject_cast<DiffView *>(m_tabs->widget(i)); v && v->patch() < 0 && v->path() == path) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    auto *v = new DiffView(&m_series, &m_project, -1, -1, path);
    m_tabs->setCurrentIndex(m_tabs->addTab(v, QFileInfo(path).fileName()));
    m_tabs->setTabToolTip(m_tabs->currentIndex(), path);
}

void MainWindow::fetchLatest(bool manual)
{
    if (m_project.msgid.isEmpty()) {
        if (manual)
            m_snack->show("No series loaded.", true);
        return;
    }
    QString dir = QFileInfo(m_project.mbox).absolutePath();
    runProcess(this, "b4", {"mbox", "-C", "-o", dir, "-n", "latest.mbox", m_project.msgid}, m_project.repo,
               [this, manual, dir](int code, const QString &out, const QString &err) {
                   if (code != 0) {
                       if (manual)
                           m_snack->show("b4 mbox failed: " + errorText(out, err), true);
                       return;
                   }
                   QFile f(dir + "/latest.mbox");
                   if (!f.open(QIODevice::ReadOnly))
                       return;
                   Series latest = Series::build(parseMbox(f.readAll()));
                   int fresh = 0;
                   for (const Message &m : latest.msgs)
                       fresh += !m_series.byId.contains(m.msgid);
                   if (!fresh) {
                       if (manual)
                           m_snack->show("Thread is up to date.");
                       return;
                   }
                   if (!manual && latest.msgs.size() <= m_notified)
                       return;
                   m_notified = latest.msgs.size();
                   QString text = latest.version > m_series.version
                                      ? QString("Version v%1 of this series is available. Reload, then apply or "
                                                "rebase it on your tree.")
                                            .arg(latest.version)
                                      : QString("%1 new message(s) in thread.").arg(fresh);
                   m_snack->show(text, false, "Reload", [this, dir] {
                       QFile::remove(m_project.mbox);
                       QFile::rename(dir + "/latest.mbox", m_project.mbox);
                       loadMbox();
                   });
               });
}

void MainWindow::applySeries()
{
    if (m_series.patches.isEmpty() || !requireRepo())
        return;
    if (QMessageBox::question(this, "Apply Series",
                              "Apply the series on top of HEAD in " + m_project.repo + " with b4 shazam?") !=
        QMessageBox::Yes)
        return;
    runProcess(this, "b4", {"shazam", "-m", m_project.mbox, m_project.msgid}, m_project.repo,
               [this](int code, const QString &out, const QString &err) {
                   updateTreeButton();
                   if (code == 0) {
                       m_snack->show("Series applied.");
                       return;
                   }
                   QString am = runSync("git", {"rev-parse", "--git-path", "rebase-apply"}, m_project.repo);
                   bool pending = QFileInfo::exists(QDir(m_project.repo).absoluteFilePath(am));
                   if (pending)
                       m_snack->show("Apply failed: " + errorText(out, err), true, "LLM Rebase", [this] { llmRebase(); });
                   else
                       m_snack->show("Apply failed: " + errorText(out, err), true);
               });
}

void MainWindow::llmRebase()
{
    if (!requireRepo())
        return;
    runProcess(this, "git", {"am", "--show-current-patch=diff"}, m_project.repo,
               [this](int code, const QString &out, const QString &err) {
                   if (code != 0) {
                       m_snack->show("No git am in progress: " + errorText(out, err), true);
                       return;
                   }
                   QList<FileDiff> files = parseDiff(out);
                   if (files.isEmpty())
                       return;
                   auto left = std::make_shared<int>(files.size());
                   auto failed = std::make_shared<QStringList>();
                   auto done = [this, left, failed](const QString &path, const QString &error) {
                       if (!error.isEmpty())
                           *failed << path + ": " + error;
                       if (--*left > 0)
                           return;
                       if (!failed->isEmpty()) {
                           m_snack->show("LLM rebase failed:\n" + failed->join('\n'), true);
                           return;
                       }
                       m_snack->show("LLM updated the files and staged them. Check them with git diff --cached.",
                                     false, "Continue am", [this] {
                                         runProcess(this, "git", {"am", "--continue"}, m_project.repo,
                                                    [this](int code, const QString &out, const QString &err) {
                                                        if (code == 0)
                                                            m_snack->show("git am completed.");
                                                        else
                                                            m_snack->show("git am stopped: " + errorText(out, err),
                                                                          true, "LLM Rebase", [this] { llmRebase(); });
                                                    });
                                     });
                   };
                   m_snack->show(QString("Asking LLM to apply %1 file(s)…").arg(files.size()));
                   for (const FileDiff &fd : files) {
                       QString path = fd.path();
                       if (fd.newPath == "/dev/null") {
                           runProcess(this, "git", {"rm", "-q", "--", path}, m_project.repo,
                                      [done, path](int code, const QString &, const QString &err) {
                                          done(path, code ? err : QString());
                                      });
                           continue;
                       }
                       QFile f(m_project.repo + "/" + path);
                       QString content = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
                       QString prompt = "Apply this patch to the file below. The file changed since the patch was "
                                        "written, so adapt the hunks with care. Resolve any conflict markers. "
                                        "Output only the complete new file content.\n\nPATCH:\n" +
                                        fileDiffText(fd) + "\nFILE " + path + ":\n" + content;
                       m_llm.chat("You are an expert at rebasing Linux kernel patches.", prompt,
                                  [this, done, path](const QString &reply, const QString &error) {
                                      if (!error.isEmpty()) {
                                          done(path, error);
                                          return;
                                      }
                                      QFile out(m_project.repo + "/" + path);
                                      QDir().mkpath(QFileInfo(out).absolutePath());
                                      QString text = stripFences(reply);
                                      if (!out.open(QIODevice::WriteOnly) || out.write((text + "\n").toUtf8()) < 0) {
                                          done(path, out.errorString());
                                          return;
                                      }
                                      out.close();
                                      runProcess(this, "git", {"add", "--", path}, m_project.repo,
                                                 [done, path](int code, const QString &, const QString &err) {
                                                     done(path, code ? err : QString());
                                                 });
                                  });
                   }
               });
}

void MainWindow::pullTree()
{
    if (m_series.patches.isEmpty())
        return;
    QStringList files;
    for (const Patch &p : m_series.patches)
        for (const FileDiff &f : p.files)
            files << f.path();
    withMaintainers([this, files](const QString &maintainers) { pickTree(findTrees(maintainers, files)); });
}

void MainWindow::withMaintainers(std::function<void(const QString &)> done)
{
    QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/MAINTAINERS";
    QString local = m_project.repo.isEmpty() ? QString() : m_project.repo + "/MAINTAINERS";
    QFileInfo fi(cache);
    if (fi.exists() && fi.lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600) {
        done(cache);
        return;
    }
    m_snack->show("Downloading upstream MAINTAINERS…");
    QNetworkReply *r = m_nam.get(QNetworkRequest(QUrl(MaintainersUrl)));
    connect(r, &QNetworkReply::finished, this, [this, r, cache, local, done] {
        r->deleteLater();
        QByteArray data = r->readAll();
        QFile f(cache);
        if (r->error() == QNetworkReply::NoError && QDir().mkpath(QFileInfo(cache).absolutePath()) &&
            f.open(QIODevice::WriteOnly) && f.write(data) == data.size()) {
            f.close();
            done(cache);
            return;
        }
        QString fallback = QFileInfo::exists(cache) ? cache : local;
        m_snack->show("Cannot download MAINTAINERS (" + r->errorString() + "), using " + fallback, true);
        done(fallback);
    });
}

void MainWindow::pickTree(const QList<Tree> &trees)
{
    if (trees.isEmpty()) {
        m_snack->show("No tree found in MAINTAINERS for the files in this series.", true);
        return;
    }
    QStringList items;
    for (const Tree &t : trees)
        items << t.section + "  —  " + t.url + " " + t.branch;
    bool ok;
    QString pick = QInputDialog::getItem(this, "Pull Tree",
                                         "Subsystems and drivers that touch this series, most specific first:", items,
                                         0, false, &ok);
    if (!ok)
        return;
    const Tree tree = trees[items.indexOf(pick)];
    if (m_project.repo.isEmpty()) {
        cloneTree(tree);
        return;
    }
    QMessageBox box(QMessageBox::Question, "Pull Tree", tree.url + " " + tree.branch, QMessageBox::Cancel, this);
    QPushButton *fetch = box.addButton("Fetch into " + m_treeButton->text(), QMessageBox::AcceptRole);
    QPushButton *clone = box.addButton("Clone New Tree…", QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == fetch)
        fetchTree(tree);
    else if (box.clickedButton() == clone)
        cloneTree(tree);
}

void MainWindow::cloneTree(const Tree &tree)
{
    QStringList parts = QUrl(tree.url).path().split('/', Qt::SkipEmptyParts);
    QString name = parts.mid(std::max<qsizetype>(0, parts.size() - 2)).join('-').remove(QRegularExpression("\\.git$"));
    QString parent = QFileDialog::getExistingDirectory(this, "Clone " + name + " into", QDir::homePath());
    if (parent.isEmpty())
        return;
    QString dir = parent + "/" + name;
    if (QFileInfo::exists(dir)) {
        m_snack->show(dir + " already exists.", true);
        return;
    }
    QStringList args{"clone", "--filter=blob:none"};
    if (!tree.branch.isEmpty())
        args << "--branch" << tree.branch;
    args << tree.url << dir;
    m_treeButton->setEnabled(false);
    m_treeButton->setText("Cloning " + name + "…");
    m_snack->show("Cloning " + tree.url + ". This can take a while.");
    runProcess(this, "git", args, parent, [this, dir](int code, const QString &out, const QString &err) {
        m_treeButton->setEnabled(true);
        if (code != 0) {
            updateTreeButton();
            m_snack->show("git clone failed: " + errorText(out, err), true);
            return;
        }
        setRepo(dir);
        m_dirty = true;
        refreshAll();
        m_snack->show("Cloned into " + dir + ".", false, "Apply Series", [this] { applySeries(); });
    });
}

void MainWindow::fetchTree(const Tree &tree)
{
    QString branch = "patchwerk/base";
    QStringList args{"fetch", tree.url};
    if (!tree.branch.isEmpty())
        args << tree.branch;
    m_snack->show("Fetching " + tree.url + " " + tree.branch + "…");
    runProcess(this, "git", args, m_project.repo, [this, branch](int code, const QString &out, const QString &err) {
        if (code != 0) {
            m_snack->show("git fetch failed: " + errorText(out, err), true);
            return;
        }
        runProcess(this, "git", {"checkout", "-B", branch, "FETCH_HEAD"}, m_project.repo,
                   [this, branch](int code, const QString &out, const QString &err) {
                       updateTreeButton();
                       if (code != 0)
                           m_snack->show("git checkout failed: " + errorText(out, err), true);
                       else
                           m_snack->show("Checked out " + branch + ".", false, "Apply Series", [this] { applySeries(); });
                   });
    });
}

void MainWindow::review()
{
    int pi = currentPatch();
    if (pi < 0)
        return;
    const Patch &p = m_series.patches[pi];
    QString text = m_series.msgs[p.msg].subject + "\n\n" + p.commitMsg + "\n";
    for (const FileDiff &f : p.files) {
        text += "\nFILE: " + f.path() + "\n";
        for (const Hunk &h : f.hunks) {
            text += h.header + "\n";
            for (const DiffLine &l : h.lines)
                text += QString("%1 %2%3\n").arg(l.type == '-' ? l.oldNo : l.newNo, 5).arg(l.type, l.text);
        }
    }
    QString target = m_series.msgs[p.msg].msgid;
    m_snack->show("LLM review started for " + QString("%1/%2").arg(p.number).arg(p.total) + "…");
    m_llm.chat(setting("llm/reviewPrompt").toString(), text, [this, pi, target](const QString &reply, const QString &error) {
        if (!error.isEmpty()) {
            m_snack->show("LLM review failed: " + error, true);
            return;
        }
        if (pi >= m_series.patches.size() || m_series.msgs[m_series.patches[pi].msg].msgid != target)
            return;
        QString json = stripFences(reply);
        json = json.mid(json.indexOf('['), json.lastIndexOf(']') - json.indexOf('[') + 1);
        const Patch &p = m_series.patches[pi];
        int added = 0;
        for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array()) {
            QJsonObject o = v.toObject();
            QString file = o["file"].toString();
            int line = o["line"].toInt();
            LineRef ref, old;
            for (int f = 0; f < p.files.size(); f++) {
                QString path = p.files[f].path();
                if (path != file && !path.endsWith("/" + file) && !file.endsWith("/" + path))
                    continue;
                for (int h = 0; h < p.files[f].hunks.size(); h++)
                    for (int l = 0; l < p.files[f].hunks[h].lines.size(); l++) {
                        const DiffLine &dl = p.files[f].hunks[h].lines[l];
                        if (dl.type != '-' && dl.newNo == line && !ref.valid())
                            ref = {f, h, l};
                        if (dl.type == '-' && dl.oldNo == line && !old.valid())
                            old = {f, h, l};
                    }
            }
            QString comment = o["comment"].toString().trimmed();
            if (comment.isEmpty())
                continue;
            m_project.drafts << Draft{target, ref.valid() ? ref : old, {}, comment, true};
            added++;
        }
        m_snack->show(QString("LLM added %1 draft comment(s).").arg(added));
        if (added)
            changed();
    });
}

void MainWindow::addMaintainers(bool prompt)
{
    QString script = m_project.repo + "/scripts/get_maintainer.pl";
    QString target = m_side->target();
    int msg = m_series.byId.value(target, -1);
    if (msg < 0 || !QFileInfo::exists(script)) {
        if (prompt)
            m_snack->show("No scripts/get_maintainer.pl in tree or no reply target.", true);
        return;
    }
    int pi = m_series.patchForMessage(msg);
    QString patches;
    for (int i = 0; i < m_series.patches.size(); i++)
        if (pi < 0 || i == pi)
            patches += m_series.msgs[m_series.patches[i].msg].body + "\n";
    QString tmp = QFileInfo(m_project.path).absolutePath() + "/maintainers.patch";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly) || f.write(patches.toUtf8()) < 0)
        return;
    f.close();
    runProcess(this, "perl", {script, "--no-rolestats", "--no-git-fallback", tmp}, m_project.repo,
               [this, prompt, target](int code, const QString &out, const QString &err) {
                   if (code != 0) {
                       if (prompt)
                           m_snack->show("get_maintainer.pl failed: " + errorText(out, err), true);
                       return;
                   }
                   Reply &r = m_project.reply(m_series, target, m_me);
                   QStringList have;
                   for (const QString &a : splitAddresses(r.to + "," + r.cc))
                       have << addressEmail(a);
                   QStringList missing;
                   for (const QString &line : out.split('\n', Qt::SkipEmptyParts))
                       if (line.contains('@') && !have.contains(addressEmail(line)))
                           missing << line.trimmed();
                   if (missing.isEmpty()) {
                       if (prompt)
                           m_snack->show("All maintainers are already addressed.");
                       return;
                   }
                   auto add = [this, missing, target] {
                       if (m_side->target() == target)
                           m_side->addCc(missing);
                   };
                   if (!prompt) {
                       m_snack->show(QString("%1 maintainer(s) not in To/Cc.").arg(missing.size()), false, "Add", add);
                       return;
                   }
                   if (QMessageBox::question(this, "Add MAINTAINERS", "Add to Cc?\n\n" + missing.join('\n')) ==
                       QMessageBox::Yes)
                       add();
               });
}

void MainWindow::sendReplies()
{
    QStringList targets;
    for (const Message &m : m_series.msgs)
        if (m_project.hasContent(m.msgid))
            targets << m.msgid;
    if (targets.isEmpty()) {
        m_snack->show("Nothing to send.");
        return;
    }
    QStringList list;
    for (const QString &t : targets)
        list << "• " + patchLabel(m_series, m_series.byId.value(t));
    QString cmd = setting("mail/sendCommand").toString();
    if (QMessageBox::question(this, "Send Replies",
                              QString("Send %1 email(s) with \"%2\"?\n\n%3").arg(targets.size()).arg(cmd, list.join('\n'))) !=
        QMessageBox::Yes)
        return;
    sendNext(targets, 0);
}

void MainWindow::sendNext(QStringList targets, int sent)
{
    if (targets.isEmpty()) {
        m_snack->show(QString("Sent %1 email(s).").arg(sent));
        changed();
        saveProject();
        return;
    }
    QString target = targets.takeFirst();
    QString dir = QFileInfo(m_project.path).absolutePath() + "/outbox";
    QDir().mkpath(dir);
    QString file = dir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".eml";
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(composeReply(m_series, m_project, target, m_me).toUtf8()) < 0) {
        m_snack->show("Cannot write " + file + ": " + f.errorString(), true);
        return;
    }
    f.close();

    QStringList args = QProcess::splitCommand(setting("mail/sendCommand").toString());
    if (args.contains("{file}"))
        args.replaceInStrings("{file}", file);
    else
        args << file;
    QString prog = args.takeFirst();
    runProcess(this, prog, args, m_project.repo, [this, targets, sent, target](int code, const QString &out, const QString &err) {
        if (code != 0) {
            m_snack->show("Send failed: " + errorText(out, err), true);
            changed();
            return;
        }
        m_project.drafts.removeIf([&](const Draft &d) { return d.target == target; });
        m_project.replies.remove(target);
        sendNext(targets, sent + 1);
    });
}

void MainWindow::updatePanelIcons()
{
    auto icon = [this](bool left) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = palette().color(QPalette::WindowText);
        p.setPen(QPen(c, 2.5));
        p.drawRoundedRect(QRectF(3, 5, 26, 22), 4, 4);
        p.fillRect(QRectF(left ? 4 : 19, 6, 9, 20), c);
        return QIcon(pm);
    };
    m_leftToggle->setIcon(icon(true));
    m_rightToggle->setIcon(icon(false));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSession();
    e->accept();
}

void MainWindow::saveSession()
{
    QString dir = sessionDir();
    QJsonArray tabs;
    for (int i = 1; i < m_tabs->count(); i++)
        if (auto *v = qobject_cast<DiffView *>(m_tabs->widget(i)))
            tabs << QJsonObject{{"patch", v->patch()}, {"file", v->file()}, {"whole", v->wholeFile()},
                                {"path", v->path()}, {"scroll", v->verticalScrollBar()->value()}};
    QJsonObject root{{"project", m_project.path}, {"dirty", m_dirty}, {"tabs", tabs},
                     {"current", m_tabs->currentIndex()},
                     {"geometry", QString::fromLatin1(saveGeometry().toBase64())},
                     {"state", QString::fromLatin1(saveState().toBase64())}};

    QString err;
    Project p = m_project;
    p.path = dir + "/project.json";
    if (m_project.mbox.isEmpty())
        QFile::remove(p.path);
    else if (!p.save(&err))
        m_snack->show("Cannot save session: " + err, true);
    QFile f(dir + "/session.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}

void MainWindow::restoreSession(bool layoutOnly)
{
    QString dir = sessionDir();
    QFile f(dir + "/session.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    restoreGeometry(QByteArray::fromBase64(root["geometry"].toString().toLatin1()));
    restoreState(QByteArray::fromBase64(root["state"].toString().toLatin1()));

    QString err;
    Project p;
    if (layoutOnly || !p.load(dir + "/project.json", &err) || !QFileInfo::exists(p.mbox))
        return;
    p.path = root["project"].toString();
    m_project = p;
    detectRepo();
    loadMbox();
    m_dirty = root["dirty"].toBool();

    QJsonArray tabs = root["tabs"].toArray();
    if (tabs.isEmpty())
        return;
    while (m_tabs->count() > 1)
        delete m_tabs->widget(1);
    for (const QJsonValue &t : tabs) {
        QJsonObject o = t.toObject();
        int patch = o["patch"].toInt(), file = o["file"].toInt();
        DiffView *v = nullptr;
        if (patch < 0) {
            openPathTab(o["path"].toString());
            v = currentView();
        } else if (patch < m_series.patches.size() && file < m_series.patches[patch].files.size()) {
            v = openPatchTab(patch, file);
            if (o["whole"].toBool())
                v->setWholeFile(true);
        }
        if (v)
            QTimer::singleShot(0, v, [v, s = o["scroll"].toInt()] { v->verticalScrollBar()->setValue(s); });
    }
    m_tabs->setCurrentIndex(std::min(root["current"].toInt(), int(m_tabs->count() - 1)));
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::PaletteChange)
        QTimer::singleShot(0, this, [this] {
            updatePanelIcons();
            rebuildViews();
        });
}
