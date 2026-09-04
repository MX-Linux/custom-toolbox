/**********************************************************************
 *  main.cpp
 **********************************************************************
 * Copyright (C) 2017-2026 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of custom-toolbox.
 *
 * custom-toolbox is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * custom-toolbox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with custom-toolbox.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTranslator>
#include <QUrl>
#include <QtGlobal>

#include "common.h"
#include "launchermodel.h"
#include <cstdlib>
#include <unistd.h>

#ifndef VERSION
    #define VERSION "?.?.?.?"
#endif

// Prompt the user to choose a .list file. Returns an empty string if the user
// cancels, so the caller can exit main() normally instead of calling exit().
// The user-facing strings are kept in the "MainWindow" translation context
// (where they lived before this moved out of MainWindow::getFileName) so their
// existing translations keep applying.
static QString promptForListFile()
{
    while (true) {
        const QString fname
            = QFileDialog::getOpenFileName(nullptr, QCoreApplication::translate("MainWindow", "Open List File"),
                                           Config::ConfigDir,
                                           QCoreApplication::translate("MainWindow", "List Files (*.list)"));
        if (fname.isEmpty()) {
            // User cancelled the dialog — exit silently.
            return {};
        }
        if (QFile::exists(fname)) {
            return fname;
        }
        const auto userChoice = QMessageBox::critical(
            nullptr, QCoreApplication::translate("MainWindow", "File Open Error"),
            QCoreApplication::translate("MainWindow", "Could not open file. Do you want to try again?"),
            QMessageBox::Yes | QMessageBox::No);
        if (userChoice == QMessageBox::No) {
            return {};
        }
    }
}

int main(int argc, char *argv[])
{
    if (getuid() == 0) {
        qputenv("XDG_RUNTIME_DIR", "/run/user/0");
        qunsetenv("SESSION_MANAGER");
    }
    // Set Qt platform to XCB (X11) if not already set and we're in X11 environment
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        if (!qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    }

    QApplication app(argc, argv);
    if (getuid() == 0) {
        qputenv("HOME", "/root");
    }

    QApplication::setApplicationName("custom-toolbox");
    QApplication::setApplicationDisplayName(QObject::tr("Custom Toolbox"));
    const auto bundledIcon =
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QStringLiteral(":/qt/qml/CustomToolbox/icons/custom-toolbox.svg");
#else
        QStringLiteral(":/CustomToolbox/icons/custom-toolbox.svg");
#endif
    QApplication::setWindowIcon(
        QIcon::fromTheme(QApplication::applicationName(), QIcon(bundledIcon)));
    QApplication::setOrganizationName("MX-Linux");
    QApplication::setApplicationVersion(VERSION);

    QTranslator qtTran;
    if (qtTran.load("qt_" + QLocale::system().name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtTran);
    }

    QTranslator qtbaseTran;
    if (qtbaseTran.load("qtbase_" + QLocale::system().name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtbaseTran);
    }

    QTranslator appTran;
    if (appTran.load(QApplication::applicationName() + '_' + QLocale::system().name(),
                     "/usr/share/" + QApplication::applicationName() + "/locale")) {
        QApplication::installTranslator(&appTran);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QObject::tr("This app can be used to create custom launchers: box of buttons/icons"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"r", "remove-checkbox"}, QObject::tr("Don't show 'show this dialog at startup' checkbox")});
    parser.addPositionalArgument(
        "file", QObject::tr("Full path and name of the .list file you want to load. "
                            "Supports both custom format (Key=Value) and INI format ([Section])."));
    parser.process(app);

    // Root guard
    QFile loginUidFile {"/proc/self/loginuid"};
    if (loginUidFile.open(QIODevice::ReadOnly)) {
        const QString loginUid = QString(loginUidFile.readAll()).trimmed();
        loginUidFile.close();
        if (loginUid == "0") {
            QMessageBox::critical(
                nullptr, QObject::tr("Error"),
                QObject::tr(
                    "You seem to be logged in as root, please log out and log in as normal user to use this program."));
            return EXIT_FAILURE;
        }
    }

    // Resolve the .list file before constructing MainWindow so that a missing
    // file or a cancelled dialog exits main() normally (unwinding QApplication)
    // instead of calling std::exit() from deep inside the widget.
    QString fileName;
    const QStringList argList = parser.positionalArguments();
    if (argList.isEmpty()) {
        fileName = promptForListFile();
        if (fileName.isEmpty()) {
            return EXIT_FAILURE;
        }
    } else if (QFile::exists(argList.first())) {
        fileName = argList.first();
    } else {
        QMessageBox::critical(nullptr, QCoreApplication::translate("MainWindow", "File Not Found"),
                              QCoreApplication::translate("MainWindow", "The file %1 does not exist.")
                                  .arg(argList.first()));
        return EXIT_FAILURE;
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")
        && qgetenv("QT_STYLE_OVERRIDE").toLower() == "gtk2") {
        // QT_STYLE_OVERRIDE=gtk2 is a widget-only style name with no corresponding Qt Quick
        // Controls style module. Left alone, the platform theme's style hint propagates it to
        // Quick Controls, which then fails to load. Only override in that specific case, so
        // other environments still get native platform styling.
        QQuickStyle::setStyle(QStringLiteral("Fusion"));
    }

    auto *iconProvider = new LauncherIconProvider;
    LauncherModel launcherModel(parser, fileName, iconProvider);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("launchericons"), iconProvider);
    engine.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&launcherModel)},
                                 {QStringLiteral("version"), QStringLiteral(VERSION)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule(QStringLiteral("CustomToolbox"), QStringLiteral("Main"));
#else
    engine.load(QUrl(QStringLiteral("qrc:/CustomToolbox/qml/Main.qml")));
#endif
    return QApplication::exec();
}
