// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "settings.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

namespace {

const QMap<QString, QVariant> defaults{
    {"llm/url", "http://localhost:11434/v1"},
    {"llm/model", "qwen2.5-coder"},
    {"llm/key", ""},
    {"llm/timeout", 600},
    {"llm/reviewPrompt",
     "You are an experienced Linux kernel maintainer reviewing a patch. Find real bugs, races, leaks, "
     "API misuse and style problems. Each diff line starts with its line number. Reply only with a JSON "
     "array of objects with keys \"file\", \"line\" and \"comment\". Use [] when you find nothing."},
    {"refresh/minutes", 5},
    {"mail/sendCommand", "git send-email --confirm=never"},
    {"ui/theme", "system"},
    {"ui/syntaxTheme", "Automatic"},
};

QPalette makePalette(bool dark)
{
    struct {
        QPalette::ColorRole role;
        QColor light, dark;
    } const roles[] = {
        {QPalette::Window, "#eff0f1", "#2a2c2f"},
        {QPalette::WindowText, "#232629", "#dcdcdc"},
        {QPalette::Base, "#ffffff", "#1e1f22"},
        {QPalette::AlternateBase, "#f7f7f7", "#26282b"},
        {QPalette::Text, "#232629", "#dcdcdc"},
        {QPalette::Button, "#fcfcfc", "#33363a"},
        {QPalette::ButtonText, "#232629", "#dcdcdc"},
        {QPalette::BrightText, "#ffffff", "#ffffff"},
        {QPalette::Light, "#ffffff", "#45484d"},
        {QPalette::Midlight, "#e3e5e7", "#3a3d41"},
        {QPalette::Mid, "#c4c8cc", "#2f3135"},
        {QPalette::Dark, "#a0a5aa", "#1b1c1f"},
        {QPalette::Shadow, "#777b80", "#101112"},
        {QPalette::Highlight, "#3daee9", "#3daee9"},
        {QPalette::HighlightedText, "#ffffff", "#ffffff"},
        {QPalette::ToolTipBase, "#f7f7f7", "#2a2c2f"},
        {QPalette::ToolTipText, "#232629", "#dcdcdc"},
        {QPalette::PlaceholderText, "#8a8f94", "#8a8f94"},
        {QPalette::Link, "#2980b9", "#5eb1ec"},
        {QPalette::LinkVisited, "#7f8c8d", "#a58fd0"},
        {QPalette::Accent, "#3daee9", "#3daee9"},
    };
    QPalette p;
    for (const auto &r : roles)
        p.setColor(r.role, dark ? r.dark : r.light);
    QColor disabled = dark ? QColor("#6e7175") : QColor("#a0a4a8");
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    return p;
}

void setIconTheme(bool dark)
{
    QString name = QIcon::themeName();
    QString base = name.endsWith("-dark") ? name.chopped(5) : name;
    QString want = dark ? base + "-dark" : base;
    for (const QString &dir : QIcon::themeSearchPaths())
        if (QFileInfo::exists(dir + "/" + want + "/index.theme")) {
            QIcon::setThemeName(want);
            return;
        }
}

}

QVariant setting(const QString &key)
{
    return QSettings().value(key, defaults.value(key));
}

void applyTheme()
{
    static const QString style = QApplication::style()->name();
    static const QPalette palette = QApplication::palette();
    static const QString icons = QIcon::themeName();
    QString t = setting("ui/theme").toString();
    QGuiApplication::styleHints()->setColorScheme(t == "dark"    ? Qt::ColorScheme::Dark
                                                  : t == "light" ? Qt::ColorScheme::Light
                                                                 : Qt::ColorScheme::Unknown);
    if (t == "system") {
        if (QApplication::style()->name() != style) {
            QApplication::setStyle(style);
            QApplication::setPalette(palette);
            QIcon::setThemeName(icons);
        }
        return;
    }
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(makePalette(t == "dark"));
    setIconTheme(t == "dark");
}

bool showSettings(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle("Settings");
    auto *form = new QFormLayout(&dlg);

    auto *url = new QLineEdit(setting("llm/url").toString());
    auto *model = new QLineEdit(setting("llm/model").toString());
    auto *key = new QLineEdit(setting("llm/key").toString());
    key->setEchoMode(QLineEdit::Password);
    auto *timeout = new QSpinBox;
    timeout->setRange(10, 3600);
    timeout->setSuffix(" s");
    timeout->setValue(setting("llm/timeout").toInt());
    auto *prompt = new QPlainTextEdit(setting("llm/reviewPrompt").toString());
    auto *refresh = new QSpinBox;
    refresh->setRange(0, 1440);
    refresh->setSpecialValueText("Off");
    refresh->setSuffix(" min");
    refresh->setValue(setting("refresh/minutes").toInt());
    auto *send = new QLineEdit(setting("mail/sendCommand").toString());
    send->setToolTip("The path of the message file is added at the end, or replaces {file}.");
    auto *theme = new QComboBox;
    theme->addItems({"system", "light", "dark"});
    theme->setCurrentText(setting("ui/theme").toString());

    form->addRow("LLM API base URL", url);
    form->addRow("LLM model", model);
    form->addRow("LLM API key", key);
    form->addRow("LLM timeout", timeout);
    form->addRow("Review prompt", prompt);
    form->addRow("Auto refresh", refresh);
    form->addRow("Send command", send);
    form->addRow("Theme", theme);
    auto *syntax = new QComboBox;
    syntax->addItem("Automatic");
    for (const KSyntaxHighlighting::Theme &t : KSyntaxHighlighting::Repository().themes())
        syntax->addItem(t.name());
    syntax->setCurrentText(setting("ui/syntaxTheme").toString());
    form->addRow("Syntax theme", syntax);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    dlg.resize(640, 480);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    QSettings s;
    s.setValue("llm/url", url->text());
    s.setValue("llm/model", model->text());
    s.setValue("llm/key", key->text());
    s.setValue("llm/timeout", timeout->value());
    s.setValue("llm/reviewPrompt", prompt->toPlainText());
    s.setValue("refresh/minutes", refresh->value());
    s.setValue("mail/sendCommand", send->text());
    s.setValue("ui/theme", theme->currentText());
    s.setValue("ui/syntaxTheme", syntax->currentText());
    applyTheme();
    return true;
}
