// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "mainwindow.h"
#include "settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("patchwerk");
    app.setOrganizationName("patchwerk");
    app.setApplicationDisplayName("Patchwerk");
    app.setDesktopFileName("patchwerk");
    app.setWindowIcon(QIcon::fromTheme("patchwerk", QIcon(":/patchwerk.png")));
    applyTheme();

    QCommandLineParser cli;
    cli.setApplicationDescription("Inline review of mailing list patches");
    cli.addHelpOption();
    cli.addPositionalArgument("input", "Message-ID, lore URL, mbox file or project .json");
    cli.process(app);

    MainWindow w;
    QString input = cli.positionalArguments().value(0);
    w.restoreSession(!input.isEmpty());
    w.show();
    if (input.endsWith(".json"))
        w.openProjectFile(input);
    else if (!input.isEmpty())
        w.openInput(input);
    return app.exec();
}
