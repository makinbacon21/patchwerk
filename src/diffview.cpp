// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "diffview.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QToolButton>
#include <QTextBlock>
#include <QVBoxLayout>

namespace {

struct Colors {
    QColor add, del, hunk, file, comment, draft, llm, highlight, dim;
};

Colors colors(const QPalette &pal)
{
    if (pal.color(QPalette::Base).lightness() < 128)
        return {"#1f3d2a", "#4a2226", "#23324d", "#30363d", "#3a3524", "#24384a", "#3a2a48", "#7a6a00", "#8b949e"};
    return {"#e6ffec", "#ffebe9", "#ddf4ff", "#eaeef2", "#fff8c5", "#ddeeff", "#f3e8ff", "#ffd33d", "#6e7781"};
}

constexpr int EditorHeight = 150;


}

DiffView::DiffView(const Series *s, Project *p, int patch, int file, const QString &path, QWidget *parent)
    : QTextEdit(parent), m_series(s), m_project(p), m_patch(patch), m_file(file), m_path(path)
{
    setReadOnly(true);
    setLineWrapMode(NoWrap);
    setUndoRedoEnabled(false);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    viewport()->setMouseTracking(true);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    document()->setDefaultFont(f);
    setTabStopDistance(QFontMetricsF(f).horizontalAdvance(' ') * 8);

    auto *editor = new QFrame(viewport());
    editor->setFrameShape(QFrame::StyledPanel);
    editor->setAutoFillBackground(true);
    m_editor = editor;
    m_editText = new QPlainTextEdit;
    m_editText->setFont(f);
    m_editText->setPlaceholderText("Write a comment");
    m_delete = new QPushButton("Delete");
    auto *cancel = new QPushButton("Cancel");
    auto *save = new QPushButton("Save draft");
    save->setDefault(true);
    auto *hint = new QLabel("Ctrl+Enter saves, Esc cancels");
    hint->setEnabled(false);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(hint);
    buttons->addStretch();
    buttons->addWidget(m_delete);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    auto *lay = new QVBoxLayout(editor);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(m_editText);
    lay->addLayout(buttons);
    editor->hide();

    connect(m_editText, &QPlainTextEdit::textChanged, this, [this] { m_edit.text = m_editText->toPlainText(); });
    connect(save, &QPushButton::clicked, this, [this] { endEdit(true); });
    connect(cancel, &QPushButton::clicked, this, [this] { endEdit(false); });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        int i = m_edit.index;
        m_edit = {};
        m_project->drafts.removeAt(i);
        emit draftsChanged();
    });
    for (auto key : {Qt::CTRL | Qt::Key_Return, Qt::CTRL | Qt::Key_Enter}) {
        auto *sc = new QShortcut(QKeySequence(key), editor);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [this] { endEdit(true); });
    }
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), editor);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, [this] { endEdit(false); });
    auto navButton = [this](const QString &text, bool next) {
        auto *b = new QToolButton(viewport());
        b->setText(text);
        b->setCursor(Qt::PointingHandCursor);
        b->hide();
        connect(b, &QToolButton::clicked, this, [this, next] {
            int y = navTarget(next);
            if (y >= 0)
                verticalScrollBar()->setValue(y - fontMetrics().height() * 3);
        });
        return b;
    };
    m_prev = navButton("▲ Previous comment", false);
    m_next = navButton("▼ Next comment", true);
    styleNav();

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &DiffView::updateNav);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &DiffView::updateNav);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &DiffView::placeEditor);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &DiffView::placeEditor);
    rebuild();
}

void DiffView::setWholeFile(bool whole)
{
    m_whole = whole;
    rebuild();
}

QList<DiffView::Shown> DiffView::fileLines(int file, bool whole, QString *note) const
{
    QList<Shown> out;
    const FileDiff *fd = m_patch >= 0 ? &m_series->patches[m_patch].files[file] : nullptr;
    if (fd && !whole) {
        for (int h = 0; h < fd->hunks.size(); h++) {
            out << Shown{{'@', 0, 0, fd->hunks[h].header}, {}};
            for (int l = 0; l < fd->hunks[h].lines.size(); l++)
                out << Shown{fd->hunks[h].lines[l], {file, h, l}};
        }
        return out;
    }

    QFile f(m_project->repo + "/" + (fd ? fd->path() : m_path));
    QStringList text;
    if (f.open(QIODevice::ReadOnly)) {
        text = QString::fromUtf8(f.readAll()).split('\n');
        if (!text.isEmpty() && text.back().isEmpty())
            text.removeLast();
    } else if (fd && fd->oldPath != "/dev/null" && fd->newPath != "/dev/null") {
        *note = "File not found in tree, showing diff only.";
        return fileLines(file, false, note);
    }
    if (!fd) {
        for (int i = 0; i < text.size(); i++)
            out << Shown{{' ', i + 1, i + 1, text[i]}, {}};
        return out;
    }

    bool applied = !fd->hunks.isEmpty();
    if (applied) {
        int pos = fd->hunks[0].newStart - 1;
        for (const DiffLine &l : fd->hunks[0].lines) {
            if (l.type == '-')
                continue;
            if (pos < 0 || pos >= text.size() || text[pos] != l.text) {
                applied = false;
                break;
            }
            pos++;
        }
    }
    *note = applied ? "Tree contains this patch." : "Tree does not contain this patch, showing it applied.";

    int pos = 0, oldNo = 1, newNo = 1;
    for (int h = 0; h < fd->hunks.size(); h++) {
        const Hunk &hk = fd->hunks[h];
        int start = (applied ? hk.newStart : hk.oldStart) - 1;
        for (; pos < start && pos < text.size(); pos++)
            out << Shown{{' ', oldNo++, newNo++, text[pos]}, {}};
        for (int l = 0; l < hk.lines.size(); l++) {
            const DiffLine &dl = hk.lines[l];
            out << Shown{{dl.type, dl.type == '+' ? 0 : oldNo, dl.type == '-' ? 0 : newNo, dl.text}, {file, h, l}};
            oldNo += dl.type != '+';
            newNo += dl.type != '-';
            pos += applied ? dl.type != '-' : dl.type != '+';
        }
    }
    for (; pos < text.size(); pos++)
        out << Shown{{' ', oldNo++, newNo++, text[pos]}, {}};
    return out;
}

void DiffView::addBlock(const QColor &bg, int indent, const QList<Span> &parts, const Row &row)
{
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    if (!m_rows.isEmpty())
        c.insertBlock();
    QTextBlockFormat bf;
    if (bg.isValid())
        bf.setBackground(bg);
    bf.setLeftMargin(indent);
    if (indent) {
        bf.setTopMargin(2);
        bf.setBottomMargin(row.kind == Row::Editor ? EditorHeight : 2);
    }
    c.setBlockFormat(bf);
    for (const Span &sp : parts) {
        QTextCharFormat cf;
        cf.setFontWeight(sp.bold ? QFont::Bold : QFont::Normal);
        if (!sp.href.isEmpty()) {
            cf.setAnchor(true);
            cf.setAnchorHref(sp.href);
            cf.setForeground(palette().color(QPalette::Link));
            cf.setFontUnderline(true);
        }
        c.insertText(QString(sp.text).replace('\n', QChar::LineSeparator), cf);
    }
    m_rows << row;
}

void DiffView::addLine(const Shown &s)
{
    Colors col = colors(palette());
    const DiffLine &l = s.line;
    if (l.type == '@') {
        addBlock(col.hunk, 0, {{l.text, false}}, {});
        return;
    }
    QString num = QString("%1 %2 ").arg(l.oldNo ? QString::number(l.oldNo) : "", 5).arg(l.newNo ? QString::number(l.newNo) : "", 5);
    QColor bg = l.type == '+' ? col.add : l.type == '-' ? col.del : QColor();
    addBlock(bg, 0, {{num + l.type + l.text, false}}, {Row::Line, s.ref, -1, l.key()});
    if (s.ref.valid())
        addComments(s.ref);
}

void DiffView::addComments(LineRef ref)
{
    Colors col = colors(palette());
    const Patch &p = m_series->patches[m_patch];
    for (int i = 0; i < p.comments.size(); i++) {
        const Comment &c = p.comments[i];
        if (c.anchor != ref)
            continue;
        const Message &m = m_series->msgs[c.msg];
        QString head = QString("%1%2  ·  %3    ").arg(QString(m_series->depth(c.msg) - m_series->depth(p.msg) - 1, QChar(0x21b3)), addressName(m.from),
                                                   m.dateTime.toLocalTime().toString("yyyy-MM-dd hh:mm"));
        addBlock(col.comment, 48, {{head, true}, {"Reply", false, QString("reply:%1").arg(i)}, {"\n" + c.text}},
                 {Row::Comment, c.anchor, i, {}});
    }
    for (int i = 0; i < m_project->drafts.size(); i++) {
        const Draft &d = m_project->drafts[i];
        if (d.anchor != ref || m_series->patchForMessage(m_series->byId.value(d.target, -1)) != m_patch ||
            (m_edit.mode == Editing::Edit && m_edit.index == i))
            continue;
        QString who = d.target == m_series->msgs[p.msg].msgid ? QString() : " (reply)";
        addBlock(d.llm ? col.llm : col.draft, 48,
                 {{(d.llm ? "LLM draft" : "Draft") + who + "    ", true},
                  {"Edit", false, QString("edit:%1").arg(i)},
                  {"  "},
                  {"Delete", false, QString("delete:%1").arg(i)},
                  {"\n" + d.text}},
                 {Row::Draft, d.anchor, i, {}});
    }
    if (m_edit.mode != Editing::None && m_edit.ref == ref)
        addBlock({}, 48, {}, {Row::Editor, ref, -1, {}});
}

void DiffView::rebuild()
{
    int scroll = verticalScrollBar()->value();
    int hscroll = horizontalScrollBar()->value();
    clear();
    m_rows.clear();
    if ((m_edit.mode == Editing::Edit && m_edit.index >= m_project->drafts.size()) ||
        (m_edit.mode == Editing::Reply && m_edit.index >= m_series->patches[m_patch].comments.size()))
        m_edit = {};
    m_hover = -1;
    setExtraSelections({});
    Colors col = colors(palette());

    if (m_patch < 0) {
        QString note;
        for (const Shown &s : fileLines(-1, true, &note))
            addLine(s);
    } else if (m_file < 0) {
        const Patch &p = m_series->patches[m_patch];
        addBlock({}, 0, {{m_series->msgs[p.msg].subject + "\n", true}, {p.commitMsg, false}}, {});
        addComments({});
        for (int f = 0; f < p.files.size(); f++) {
            addBlock(col.file, 0, {{p.files[f].path(), true}}, {});
            QString note;
            for (const Shown &s : fileLines(f, false, &note))
                addLine(s);
        }
    } else {
        QString note;
        QList<Shown> lines = fileLines(m_file, m_whole, &note);
        if (!note.isEmpty())
            addBlock(col.file, 0, {{note, false}}, {});
        for (const Shown &s : lines)
            addLine(s);
    }
    setTextCursor(QTextCursor(document()));
    verticalScrollBar()->setValue(scroll);
    horizontalScrollBar()->setValue(hscroll);
    updateNav();
    if (m_edit.mode == Editing::None) {
        m_editor->hide();
        return;
    }
    if (m_editText->toPlainText() != m_edit.text)
        m_editText->setPlainText(m_edit.text);
    m_delete->setVisible(m_edit.mode == Editing::Edit);
    m_editor->show();
    placeEditor();
}

void DiffView::placeEditor()
{
    int row = -1;
    for (int i = 0; i < m_rows.size() && row < 0; i++)
        if (m_rows[i].kind == Row::Editor)
            row = i;
    if (row < 0 || m_editor->isHidden())
        return;
    QRectF r = document()->documentLayout()->blockBoundingRect(document()->findBlockByNumber(row));
    int x = 48 - horizontalScrollBar()->value();
    int y = int(r.top()) + 4 - verticalScrollBar()->value();
    m_editor->setGeometry(x, y, std::min(viewport()->width() - x - 16, 900), EditorHeight - 4);
}

void DiffView::styleNav()
{
    const QPalette &p = palette();
    QString css = QString("QToolButton { background: %1; color: %2; border: 1px solid %3;"
                          " border-radius: 10px; padding: 2px 12px; }"
                          "QToolButton:hover { background: %4; }")
                      .arg(p.color(QPalette::Button).name(), p.color(QPalette::ButtonText).name(),
                           p.color(QPalette::Mid).name(), p.color(QPalette::Midlight).name());
    m_prev->setStyleSheet(css);
    m_next->setStyleSheet(css);
}

void DiffView::changeEvent(QEvent *e)
{
    QTextEdit::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        styleNav();
}

void DiffView::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    placeEditor();
    updateNav();
}

int DiffView::navTarget(bool next) const
{
    auto isNote = [this](int i) { return m_rows[i].kind == Row::Comment || m_rows[i].kind == Row::Draft; };
    QScrollBar *bar = verticalScrollBar();
    int top = bar->value() + fontMetrics().height() * 3;
    if ((next && bar->value() >= bar->maximum()) || (!next && bar->value() <= 0))
        return -1;
    int found = -1;
    for (int i = 0; i < m_rows.size(); i++) {
        if (!isNote(i) || (i > 0 && isNote(i - 1)))
            continue;
        int y = int(document()->documentLayout()->blockBoundingRect(document()->findBlockByNumber(i)).top());
        if (next && y > top + 1)
            return y;
        if (!next && y < top - 1)
            found = y;
    }
    return found;
}

void DiffView::updateNav()
{
    QRect vp = viewport()->rect();
    m_prev->setVisible(navTarget(false) >= 0);
    m_next->setVisible(navTarget(true) >= 0);
    m_prev->adjustSize();
    m_next->adjustSize();
    m_prev->move(vp.center().x() - m_prev->width() / 2, 8);
    m_next->move(vp.center().x() - m_next->width() / 2, vp.bottom() - m_next->height() - 8);
    m_prev->raise();
    m_next->raise();
}

void DiffView::startEdit(Editing::Mode mode, LineRef ref, int index)
{
    if (m_patch < 0)
        return;
    bool changed = commitEdit();
    m_edit = {mode, ref, index, mode == Editing::Edit ? m_project->drafts[index].text : QString()};
    if (changed)
        emit draftsChanged();
    else
        rebuild();
    m_editText->setFocus();
    m_editText->moveCursor(QTextCursor::End);
    if (int below = m_editor->geometry().bottom() - viewport()->height(); m_editor->isVisible() && below > 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() + below + 16);
}

bool DiffView::commitEdit()
{
    QString text = m_edit.text.trimmed();
    if (m_edit.mode == Editing::None || text.isEmpty())
        return false;
    const Patch &p = m_series->patches[m_patch];
    if (m_edit.mode == Editing::Edit) {
        m_project->drafts[m_edit.index].text = text;
    } else if (m_edit.mode == Editing::Reply) {
        const Comment &c = p.comments[m_edit.index];
        m_project->drafts << Draft{m_series->msgs[c.msg].msgid, c.anchor, c.quote + c.text.split('\n'), text, false};
    } else {
        m_project->drafts << Draft{m_series->msgs[p.msg].msgid, m_edit.ref, {}, text, false};
    }
    return true;
}

void DiffView::endEdit(bool save)
{
    bool changed = save && commitEdit();
    m_edit = {};
    if (changed)
        emit draftsChanged();
    else
        rebuild();
}

const DiffView::Row *DiffView::rowAt(const QPoint &pos) const
{
    int b = cursorForPosition(pos).blockNumber();
    return b >= 0 && b < m_rows.size() ? &m_rows[b] : nullptr;
}

void DiffView::highlightRows(const QList<int> &rows, bool scroll)
{
    QList<ExtraSelection> sel;
    for (int r : rows) {
        ExtraSelection s;
        s.cursor = QTextCursor(document()->findBlockByNumber(r));
        s.format.setBackground(colors(palette()).highlight);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel << s;
    }
    setExtraSelections(sel);
    if (!scroll || rows.isEmpty())
        return;
    QRect vis = viewport()->rect();
    for (int r : rows)
        if (vis.intersects(cursorRect(QTextCursor(document()->findBlockByNumber(r)))))
            return;
    int y = cursorRect(QTextCursor(document()->findBlockByNumber(rows.first()))).top();
    verticalScrollBar()->setValue(verticalScrollBar()->value() + y - vis.height() / 3);
}

void DiffView::highlightQuote(const QString &line)
{
    QString s = stripQuote(line);
    QList<int> rows;
    if (line.startsWith('>') && s.trimmed().size() >= 4)
        for (int i = 0; i < m_rows.size(); i++)
            if (m_rows[i].kind == Row::Line && (m_rows[i].key == s || m_rows[i].key == " " + s))
                rows << i;
    highlightRows(rows, true);
}

void DiffView::highlightRefs(const QList<LineRef> &refs)
{
    QList<int> rows;
    for (int i = 0; i < m_rows.size(); i++)
        if (m_rows[i].kind == Row::Line && refs.contains(m_rows[i].ref))
            rows << i;
    highlightRows(rows, false);
}

void DiffView::scrollTo(LineRef ref)
{
    for (int i = 0; i < m_rows.size(); i++)
        if (m_rows[i].ref == ref) {
            highlightRows({i}, true);
            return;
        }
}

void DiffView::mouseMoveEvent(QMouseEvent *e)
{
    QTextEdit::mouseMoveEvent(e);
    viewport()->setCursor(anchorAt(e->pos()).isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
    const Row *r = rowAt(e->pos());
    int idx = r ? int(r - m_rows.data()) : -1;
    if (idx == m_hover)
        return;
    bool wasNote = m_hover >= 0 && m_rows[m_hover].kind != Row::Line && m_rows[m_hover].kind != Row::Text;
    m_hover = idx;
    if (r && r->kind == Row::Comment)
        highlightRefs(m_series->patches[m_patch].comments[r->index].refs);
    else if (r && r->kind == Row::Draft)
        highlightRefs({r->ref});
    else if (wasNote)
        setExtraSelections({});
}

void DiffView::mousePressEvent(QMouseEvent *e)
{
    QTextEdit::mousePressEvent(e);
    const Row *r = rowAt(e->pos());
    if (r && r->kind == Row::Comment && anchorAt(e->pos()).isEmpty())
        emit messageSelected(m_series->patches[m_patch].comments[r->index].msg);
}

void DiffView::mouseReleaseEvent(QMouseEvent *e)
{
    QTextEdit::mouseReleaseEvent(e);
    QStringList link = anchorAt(e->pos()).split(':');
    if (link.size() != 2 || e->button() != Qt::LeftButton)
        return;
    int i = link[1].toInt();
    if (link[0] == "reply") {
        startEdit(Editing::Reply, m_series->patches[m_patch].comments[i].anchor, i);
    } else if (link[0] == "edit") {
        startEdit(Editing::Edit, m_project->drafts[i].anchor, i);
    } else if (link[0] == "delete") {
        m_project->drafts.removeAt(i);
        if (m_edit.mode == Editing::Edit && m_edit.index > i)
            m_edit.index--;
        emit draftsChanged();
    }
}

void DiffView::mouseDoubleClickEvent(QMouseEvent *e)
{
    const Row *r = rowAt(e->pos());
    if (!r || m_patch < 0 || r->kind != Row::Line || !r->ref.valid()) {
        QTextEdit::mouseDoubleClickEvent(e);
        return;
    }
    startEdit(Editing::New, r->ref, -1);
}
