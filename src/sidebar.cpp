// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "sidebar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {

enum Role { PatchRole = Qt::UserRole, FileRole, MsgRole };

QString counts(int comments, int drafts)
{
    QStringList s;
    if (comments)
        s << QString("💬%1").arg(comments);
    if (drafts)
        s << QString("✎%1").arg(drafts);
    return s.isEmpty() ? QString() : "  " + s.join(' ');
}

}

Sidebar::Sidebar(const Series *s, Project *p, QWidget *parent) : QWidget(parent), m_series(s), m_project(p)
{
    auto *split = new QSplitter(Qt::Vertical);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(split);

    auto *top = new QWidget;
    auto *topLay = new QVBoxLayout(top);
    topLay->setContentsMargins(4, 4, 4, 4);
    m_all = new QCheckBox("Show all files in tree");
    m_stack = new QStackedWidget;
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_fs = new QFileSystemModel(this);
    m_fsView = new QTreeView;
    m_fsView->setModel(m_fs);
    for (int c = 1; c < m_fs->columnCount(); c++)
        m_fsView->hideColumn(c);
    m_fsView->header()->hide();
    m_stack->addWidget(m_tree);
    m_stack->addWidget(m_fsView);
    topLay->addWidget(m_stack);
    topLay->addWidget(m_all);
    split->addWidget(top);

    auto *bottom = new QWidget;
    auto *form = new QFormLayout(bottom);
    form->setContentsMargins(4, 4, 4, 4);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_meta = new QLabel;
    m_meta->setWordWrap(true);
    m_meta->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_meta->setOpenExternalLinks(true);
    form->addRow(m_meta);
    m_targets = new QComboBox;
    m_targets->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_targets->setMinimumContentsLength(16);
    m_targets->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    form->addRow("Reply to", m_targets);
    m_to = new QLineEdit;
    m_cc = new QLineEdit;
    form->addRow("To", m_to);
    form->addRow("Cc", m_cc);
    auto *buttons = new QHBoxLayout;
    auto *maint = new QPushButton("+ MAINTAINERS");
    auto *rb = new QPushButton("Reviewed-by");
    auto *ab = new QPushButton("Acked-by");
    buttons->addWidget(maint);
    buttons->addWidget(rb);
    buttons->addWidget(ab);
    form->addRow(buttons);
    m_body = new QPlainTextEdit;
    m_body->setPlaceholderText("Reply message");
    form->addRow(m_body);
    m_drafts = new QListWidget;
    m_drafts->setMaximumHeight(110);
    form->addRow("Inline", m_drafts);
    auto *send = new QPushButton("Send replies");
    form->addRow(send);
    split->addWidget(bottom);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);

    connect(m_all, &QCheckBox::toggled, this, [this](bool on) {
        if (on && m_fs->rootPath() != m_project->repo)
            m_fsView->setRootIndex(m_fs->setRootPath(m_project->repo));
        m_stack->setCurrentIndex(on ? 1 : 0);
    });
    connect(m_fsView, &QTreeView::activated, this, [this](const QModelIndex &i) {
        if (!m_fs->isDir(i))
            emit openPath(QDir(m_project->repo).relativeFilePath(m_fs->filePath(i)));
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
        int msg = it->data(0, MsgRole).toInt();
        setCurrentMessage(msg);
        emit messageSelected(msg);
        if (it->data(0, PatchRole).toInt() >= 0)
            emit openPatch(it->data(0, PatchRole).toInt(), it->data(0, FileRole).toInt());
    });
    connect(m_targets, &QComboBox::activated, this, [this] { selectTarget(m_targets->currentData().toString()); });
    auto edited = [this] {
        if (m_target.isEmpty())
            return;
        Reply &r = m_project->reply(*m_series, m_target, m_me);
        r.to = m_to->text();
        r.cc = m_cc->text();
        r.body = m_body->toPlainText();
        emit replyEdited();
    };
    connect(m_to, &QLineEdit::textEdited, this, edited);
    connect(m_cc, &QLineEdit::textEdited, this, edited);
    connect(m_body, &QPlainTextEdit::textChanged, this, edited);
    connect(m_drafts, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *it) {
                int i = it->data(Qt::UserRole).toInt();
                QTimer::singleShot(0, this, [this, i] {
                    if (i >= m_project->drafts.size())
                        return;
                    selectTarget(m_project->drafts[i].target);
                    emit draftActivated(i);
                });
            });
    connect(maint, &QPushButton::clicked, this, &Sidebar::addMaintainers);
    connect(rb, &QPushButton::clicked, this, [this] { insertTrailer("Reviewed-by"); });
    connect(ab, &QPushButton::clicked, this, [this] { insertTrailer("Acked-by"); });
    connect(send, &QPushButton::clicked, this, &Sidebar::sendRequested);
}

void Sidebar::refresh()
{
    if (m_msg >= m_series->msgs.size())
        m_msg = -1;
    if (m_msg < 0 && !m_series->patches.isEmpty())
        m_msg = m_series->patches[0].msg;
    if (!m_series->byId.contains(m_target))
        m_target.clear();
    m_all->setEnabled(!m_project->repo.isEmpty());
    if (m_project->repo.isEmpty())
        m_all->setChecked(false);
    if (m_fs->rootPath() != m_project->repo && m_all->isChecked())
        m_fsView->setRootIndex(m_fs->setRootPath(m_project->repo));
    refreshTree();
    setCurrentMessage(m_msg);
}

void Sidebar::refreshTree()
{
    m_tree->clear();
    auto draftCount = [this](int patch, int file) {
        return int(std::count_if(m_project->drafts.begin(), m_project->drafts.end(), [&](const Draft &d) {
            return m_series->patchForMessage(m_series->byId.value(d.target, -1)) == patch &&
                   (file < 0 || d.anchor.file == file);
        }));
    };
    if (m_series->cover >= 0) {
        auto *it = new QTreeWidgetItem(m_tree, {m_series->msgs[m_series->cover].subject});
        it->setData(0, PatchRole, -1);
        it->setData(0, MsgRole, m_series->cover);
    }
    for (int pi = 0; pi < m_series->patches.size(); pi++) {
        const Patch &p = m_series->patches[pi];
        auto *it = new QTreeWidgetItem(
            m_tree, {cleanSubject(m_series->msgs[p.msg].subject) + counts(p.comments.size(), draftCount(pi, -1))});
        it->setToolTip(0, m_series->msgs[p.msg].subject);
        it->setData(0, PatchRole, pi);
        it->setData(0, FileRole, -1);
        it->setData(0, MsgRole, p.msg);
        for (int f = 0; f < p.files.size(); f++) {
            int c = std::count_if(p.comments.begin(), p.comments.end(), [f](const Comment &c) { return c.anchor.file == f; });
            auto *fi = new QTreeWidgetItem(it, {p.files[f].path() + counts(c, draftCount(pi, f))});
            fi->setToolTip(0, p.files[f].path());
            fi->setData(0, PatchRole, pi);
            fi->setData(0, FileRole, f);
            fi->setData(0, MsgRole, p.msg);
        }
    }
    m_tree->expandAll();
}

void Sidebar::setCurrentMessage(int msg)
{
    m_msg = msg;
    if (msg < 0 || msg >= m_series->msgs.size()) {
        m_meta->setText(m_series->msgs.isEmpty() ? "No series loaded." : "");
        refreshDrafter();
        return;
    }
    const Message &m = m_series->msgs[msg];
    int pi = m_series->patchForMessage(msg);
    QString html = QString("<b>%1</b><br>%2<br>%3<br>v%4")
                       .arg(m.subject.toHtmlEscaped(), m.from.toHtmlEscaped(),
                            m.dateTime.toLocalTime().toString("yyyy-MM-dd hh:mm"))
                       .arg(m_series->version);
    if (pi >= 0)
        html += QString(" · %1/%2").arg(m_series->patches[pi].number).arg(m_series->patches[pi].total);
    html += QString(" · <a href=\"https://lore.kernel.org/r/%1\">lore</a>").arg(m.msgid.toHtmlEscaped());
    if (pi >= 0)
        for (const QString &t : m_series->patches[pi].trailers)
            html += "<br><small>" + t.toHtmlEscaped() + "</small>";
    m_meta->setText(html);
    selectTarget(m.msgid);
}

void Sidebar::selectTarget(const QString &msgid)
{
    m_target = msgid;
    refreshDrafter();
    emit targetChanged();
}

void Sidebar::refreshDrafter()
{
    m_targets->clear();
    QStringList ids;
    for (const Message &m : m_series->msgs)
        if (m.msgid == m_target || m_project->hasContent(m.msgid))
            ids << m.msgid;
    for (const QString &id : ids) {
        QString label = patchLabel(*m_series, m_series->byId.value(id));
        m_targets->addItem((m_project->hasContent(id) ? "● " : "") + label, id);
    }
    m_targets->setCurrentIndex(m_targets->findData(m_target));

    const QSignalBlocker b1(m_to), b2(m_cc), b3(m_body);
    bool on = !m_target.isEmpty();
    for (QWidget *w : std::initializer_list<QWidget *>{m_to, m_cc, m_body})
        w->setEnabled(on);
    Reply r = on ? m_project->reply(*m_series, m_target, m_me) : Reply{};
    m_to->setText(r.to);
    m_cc->setText(r.cc);
    if (m_body->toPlainText() != r.body)
        m_body->setPlainText(r.body);

    m_drafts->clear();
    for (int i = 0; i < m_project->drafts.size(); i++) {
        const Draft &d = m_project->drafts[i];
        int mi = m_series->byId.value(d.target, -1);
        if (mi < 0)
            continue;
        int pi = m_series->patchForMessage(mi);
        QString to = pi >= 0 && m_series->patches[pi].msg == mi
                         ? QString("%1/%2").arg(m_series->patches[pi].number).arg(m_series->patches[pi].total)
                         : QString(QChar(0x21b3)) + " " + addressName(m_series->msgs[mi].from);
        auto *it = new QListWidgetItem(to + ": " + (d.llm ? "[LLM] " : "") + d.text.simplified(), m_drafts);
        it->setData(Qt::UserRole, i);
        if (d.target == m_target) {
            QFont f = it->font();
            f.setBold(true);
            it->setFont(f);
        }
    }
}

void Sidebar::addCc(const QStringList &addrs)
{
    if (m_target.isEmpty())
        return;
    Reply &r = m_project->reply(*m_series, m_target, m_me);
    QStringList cc = splitAddresses(r.cc);
    cc += addrs;
    r.cc = cc.join(", ");
    refreshDrafter();
    emit replyEdited();
}

void Sidebar::insertTrailer(const QString &tag)
{
    if (m_target.isEmpty())
        return;
    QString text = m_body->toPlainText();
    if (!text.isEmpty() && !text.endsWith('\n'))
        text += '\n';
    m_body->setPlainText(text + (text.isEmpty() ? "" : "\n") + tag + ": " + m_me + "\n");
}
