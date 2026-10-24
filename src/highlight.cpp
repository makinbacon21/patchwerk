// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "highlight.h"
#include "settings.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-bash.h>
#include <tree_sitter/tree-sitter-c.h>
#include <tree_sitter/tree-sitter-json.h>
#include <tree_sitter/tree-sitter-python.h>
#include <tree_sitter/tree-sitter-rust.h>
#include <tree_sitter/tree-sitter-yaml.h>

namespace {

namespace KSH = KSyntaxHighlighting;

KSH::Repository &repository()
{
    static KSH::Repository repo;
    return repo;
}

KSH::Theme theme(bool dark)
{
    QString name = setting("ui/syntaxTheme").toString();
    if (name == "Automatic")
        name = dark ? "GitHub Dark" : "GitHub Light";
    KSH::Theme t = repository().theme(name);
    return t.isValid() ? t : repository().defaultTheme(dark ? KSH::Repository::DarkTheme : KSH::Repository::LightTheme);
}

class LineHighlighter : public KSH::AbstractHighlighter {
public:
    QList<QTextLayout::FormatRange> ranges;

    KSH::State run(const QString &text, const KSH::State &state)
    {
        ranges.clear();
        return highlightLine(text, state);
    }

protected:
    void applyFormat(int offset, int length, const KSH::Format &f) override
    {
        if (length <= 0 || f.isDefaultTextStyle(theme()))
            return;
        QTextCharFormat cf;
        if (f.hasTextColor(theme()))
            cf.setForeground(f.textColor(theme()));
        if (f.isBold(theme()))
            cf.setFontWeight(QFont::Bold);
        cf.setFontItalic(f.isItalic(theme()));
        ranges.append({offset, length, cf});
    }
};

LineFormats highlightKsh(const KSH::Definition &def, const QStringList &lines, const KSH::Theme &t)
{
    LineHighlighter hl;
    hl.setDefinition(def);
    hl.setTheme(t);
    LineFormats out;
    KSH::State state;
    for (const QString &line : lines) {
        state = hl.run(line, state);
        out << hl.ranges;
    }
    return out;
}

struct Grammar {
    const char *name;
    const TSLanguage *(*language)();
    QStringList suffixes;
};

const Grammar grammars[] = {
    {"c", tree_sitter_c, {"c", "h"}},
    {"rust", tree_sitter_rust, {"rs"}},
    {"python", tree_sitter_python, {"py"}},
    {"bash", tree_sitter_bash, {"sh", "bash"}},
    {"yaml", tree_sitter_yaml, {"yaml", "yml"}},
    {"json", tree_sitter_json, {"json"}},
};

struct Query {
    TSQuery *query = nullptr;
    QList<int> styles;
    bool loaded = false;

    ~Query()
    {
        if (query)
            ts_query_delete(query);
    }
};

// Capture names resolve like Zed and Helix: "function.method" falls back to "function".
// Colors are GitHub Primer "prettylights" syntax tokens. Types use entity-tag.
struct Role {
    const char *capture;
    KSH::Theme::TextStyle style;
    const char *light;
    const char *dark;
};

const Role roles[] = {
    {"comment", KSH::Theme::Comment, "#59636e", "#9198a1"},
    {"string", KSH::Theme::String, "#0a3069", "#a5d6ff"},
    {"string.special.key", KSH::Theme::Variable, "#953800", "#ffa657"},
    {"escape", KSH::Theme::SpecialChar, "#0550ae", "#79c0ff"},
    {"number", KSH::Theme::DecVal, "#0550ae", "#79c0ff"},
    {"boolean", KSH::Theme::Constant, "#0550ae", "#79c0ff"},
    {"constant", KSH::Theme::Constant, "#0550ae", "#79c0ff"},
    {"variable.builtin", KSH::Theme::Constant, "#0550ae", "#79c0ff"},
    {"keyword", KSH::Theme::Keyword, "#cf222e", "#ff7b72"},
    {"type", KSH::Theme::DataType, "#0550ae", "#7ee787"},
    {"constructor", KSH::Theme::DataType, "#0550ae", "#7ee787"},
    {"function", KSH::Theme::Function, "#6639ba", "#d2a8ff"},
    {"property", KSH::Theme::Variable, "#953800", "#ffa657"},
    {"label", KSH::Theme::Attribute, "#953800", "#ffa657"},
    {"attribute", KSH::Theme::Attribute, "#0550ae", "#79c0ff"},
    {"operator", KSH::Theme::Operator, nullptr, nullptr},
};

int captureRole(QString name)
{
    for (;;) {
        for (int i = 0; i < int(std::size(roles)); i++)
            if (name == roles[i].capture)
                return i;
        qsizetype dot = name.lastIndexOf('.');
        if (dot < 0)
            return -1;
        name.truncate(dot);
    }
}

const Query *queryFor(const Grammar &g)
{
    static Query cache[std::size(grammars)];
    Query &q = cache[&g - grammars];
    if (q.loaded)
        return q.query ? &q : nullptr;
    q.loaded = true;
    QString file = QString(":/queries/%1/highlights.scm").arg(g.name);
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    QByteArray src = f.readAll();
    uint32_t errOffset;
    TSQueryError err;
    q.query = ts_query_new(g.language(), src.constData(), src.size(), &errOffset, &err);
    if (!q.query) {
        qWarning("tree-sitter query %s failed at byte %u (error %d)", qPrintable(file), errOffset, int(err));
        return nullptr;
    }
    for (uint32_t i = 0; i < ts_query_capture_count(q.query); i++) {
        uint32_t len;
        const char *name = ts_query_capture_name_for_id(q.query, i, &len);
        q.styles << captureRole(QString::fromUtf8(name, len));
    }
    return &q;
}

QByteArray nodeText(TSNode n, const QByteArray &src)
{
    return src.mid(ts_node_start_byte(n), ts_node_end_byte(n) - ts_node_start_byte(n));
}

bool predicatesPass(TSQuery *q, const TSQueryMatch &m, const QByteArray &src)
{
    uint32_t count;
    const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(q, m.pattern_index, &count);
    for (uint32_t i = 0; i < count;) {
        QList<TSQueryPredicateStep> args;
        for (; i < count && steps[i].type != TSQueryPredicateStepTypeDone; i++)
            args << steps[i];
        i++;
        if (args.size() < 3 || args[1].type != TSQueryPredicateStepTypeCapture)
            continue;
        uint32_t len;
        const char *opName = ts_query_string_value_for_id(q, args[0].value_id, &len);
        QString op = QString::fromUtf8(opName, len);
        QByteArray text;
        for (uint16_t c = 0; c < m.capture_count; c++)
            if (m.captures[c].index == args[1].value_id)
                text = nodeText(m.captures[c].node, src);
        auto value = [&](const TSQueryPredicateStep &s) {
            if (s.type == TSQueryPredicateStepTypeString) {
                const char *str = ts_query_string_value_for_id(q, s.value_id, &len);
                return QByteArray(str, len);
            }
            for (uint16_t c = 0; c < m.capture_count; c++)
                if (m.captures[c].index == s.value_id)
                    return nodeText(m.captures[c].node, src);
            return QByteArray();
        };
        bool neg = op.startsWith("not-");
        bool ok;
        if (op.endsWith("match?"))
            ok = QRegularExpression(QString::fromUtf8(value(args[2]))).match(QString::fromUtf8(text)).hasMatch();
        else if (op.endsWith("eq?"))
            ok = text == value(args[2]);
        else if (op.endsWith("any-of?"))
            ok = std::any_of(args.begin() + 2, args.end(), [&](const auto &s) { return text == value(s); });
        else
            continue;
        if (ok == neg)
            return false;
    }
    return true;
}

LineFormats highlightTreeSitter(const Grammar &g, const Query &q, const QStringList &lines, const KSH::Theme &t,
                               bool primer, bool dark)
{
    QByteArray src = lines.join('\n').toUtf8();
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, g.language());
    TSTree *tree = ts_parser_parse_string(parser, nullptr, src.constData(), src.size());
    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, q.query, ts_tree_root_node(tree));

    struct Hit {
        uint32_t start, end, pattern;
        int style;
    };
    QList<Hit> hits;
    TSQueryMatch m;
    while (ts_query_cursor_next_match(cursor, &m)) {
        if (!predicatesPass(q.query, m, src))
            continue;
        for (uint16_t c = 0; c < m.capture_count; c++) {
            TSNode n = m.captures[c].node;
            hits << Hit{ts_node_start_byte(n), ts_node_end_byte(n), m.pattern_index, q.styles[m.captures[c].index]};
        }
    }
    ts_query_cursor_delete(cursor);
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        uint32_t la = a.end - a.start, lb = b.end - b.start;
        return la != lb ? la > lb : a.pattern < b.pattern;
    });
    QList<qint8> style(src.size(), -1);
    for (const Hit &h : hits)
        std::fill(style.begin() + h.start, style.begin() + std::min<qsizetype>(h.end, src.size()), qint8(h.style));

    QHash<int, QTextCharFormat> formats;
    auto format = [&](int s) {
        if (!formats.contains(s)) {
            const Role &r = roles[s];
            QTextCharFormat cf;
            if (primer) {
                if (r.light)
                    cf.setForeground(QColor(dark ? r.dark : r.light));
            } else {
                if (QRgb c = t.textColor(r.style))
                    cf.setForeground(QColor::fromRgba(c));
                if (t.isBold(r.style))
                    cf.setFontWeight(QFont::Bold);
                cf.setFontItalic(t.isItalic(r.style));
            }
            formats[s] = cf;
        }
        return formats[s];
    };

    LineFormats out;
    qsizetype base = 0;
    for (const QString &line : lines) {
        QByteArray bytes = line.toUtf8();
        bool ascii = bytes.size() == line.size();
        auto col = [&](qsizetype b) { return ascii ? int(b) : int(QString::fromUtf8(bytes.left(b)).size()); };
        QList<QTextLayout::FormatRange> ranges;
        for (qsizetype i = 0; i < bytes.size();) {
            qint8 s = style[base + i];
            qsizetype j = i;
            while (j < bytes.size() && style[base + j] == s)
                j++;
            if (s >= 0)
                ranges.append({col(i), col(j) - col(i), format(s)});
            i = j;
        }
        out << ranges;
        base += bytes.size() + 1;
    }
    return out;
}

}

LineFormats highlightLines(const QString &path, const QStringList &lines, bool dark)
{
    KSH::Theme t = theme(dark);
    QString suffix = QFileInfo(path).suffix().toLower();
    for (const Grammar &g : grammars)
        if (g.suffixes.contains(suffix))
            if (const Query *q = queryFor(g))
                return highlightTreeSitter(g, *q, lines, t, setting("ui/syntaxTheme").toString() == "Automatic", dark);
    KSH::Definition def = repository().definitionForFileName(path);
    return def.isValid() ? highlightKsh(def, lines, t) : LineFormats(lines.size());
}
