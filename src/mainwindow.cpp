/**********************************************************************
 *  MainWindow.cpp
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

#include "mainwindow.h"
#include "launcherparser.h"
#include "iconloader.h"
#include "common.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QLabel>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringView>

#include "about.h"
#include "flatbutton.h"
#include <pwd.h>
#include <unistd.h>

namespace
{
// Approximate width of one button column; determined through trial and error.
constexpr int buttonColumnWidth = 200;

QString commandDescription(const QString &program, const QStringList &arguments)
{
    QStringList parts {program};
    parts.append(arguments);
    return parts.join(QLatin1Char(' '));
}

QString homeDirectoryForUser(const QString &user)
{
    if (const passwd *pw = getpwnam(user.toLocal8Bit().constData())) {
        return QString::fromLocal8Bit(pw->pw_dir);
    }
    return {};
}

struct DesktopFileCandidate {
    QString path;
    int sourcePriority {};
};

QString desktopEntryValue(const QString &text, const QString &key, const QString &lang)
{
    const QString exactKey = key + QLatin1Char('[') + lang + QLatin1String("]=");
    const QString shortKey = key + QLatin1Char('[') + lang.section('_', 0, 0) + QLatin1String("]=");
    const QString fallbackKey = key + QLatin1Char('=');
    QString exactValue;
    QString shortValue;
    QString fallbackValue;
    bool inDesktopEntry {};

    QStringView textView(text);
    qsizetype pos = 0;
    while (pos < textView.size()) {
        qsizetype endPos = textView.indexOf(QLatin1Char('\n'), pos);
        if (endPos < 0) {
            endPos = textView.size();
        }
        const QStringView line = textView.mid(pos, endPos - pos).trimmed();
        pos = endPos + 1;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inDesktopEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inDesktopEntry) {
            continue;
        }

        if (line.startsWith(exactKey)) {
            exactValue = line.mid(exactKey.size()).toString();
        } else if (line.startsWith(shortKey)) {
            shortValue = line.mid(shortKey.size()).toString();
        } else if (line.startsWith(fallbackKey)) {
            fallbackValue = line.mid(fallbackKey.size()).toString();
        }
    }

    if (!exactValue.isEmpty()) {
        return exactValue;
    }
    if (!shortValue.isEmpty()) {
        return shortValue;
    }
    return fallbackValue;
}

bool parseDesktopExec(const QString &exec, const QString &applicationName, const QString &iconName,
                      const QString &desktopFile, QString *program, QStringList *arguments)
{
    QStringList parts;
    QString current;
    bool inQuotes {};
    bool tokenStarted {};

    auto appendCurrent = [&]() {
        if (tokenStarted) {
            parts.append(current);
            current.clear();
            tokenStarted = false;
        }
    };

    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            tokenStarted = true;
            continue;
        }
        if (c == QLatin1Char('\\')) {
            if (++i >= exec.size()) {
                return false;
            }
            current.append(exec.at(i));
            tokenStarted = true;
            continue;
        }
        if (c.isSpace() && !inQuotes) {
            appendCurrent();
            continue;
        }
        if (c != QLatin1Char('%')) {
            current.append(c);
            tokenStarted = true;
            continue;
        }
        if (++i >= exec.size()) {
            return false;
        }

        switch (exec.at(i).unicode()) {
        case '%':
            current.append(QLatin1Char('%'));
            tokenStarted = true;
            break;
        case 'f':
        case 'F':
        case 'u':
        case 'U':
        case 'd':
        case 'D':
        case 'n':
        case 'N':
        case 'v':
        case 'm':
            break;
        case 'c':
            current.append(applicationName);
            tokenStarted = true;
            break;
        case 'k':
            current.append(desktopFile);
            tokenStarted = true;
            break;
        case 'i':
            if (inQuotes || tokenStarted) {
                return false;
            }
            if (!iconName.isEmpty()) {
                parts << QStringLiteral("--icon") << iconName;
            }
            break;
        default:
            return false;
        }
    }

    if (inQuotes) {
        return false;
    }
    appendCurrent();
    if (parts.isEmpty()) {
        return false;
    }

    if (program != nullptr) {
        *program = parts.takeFirst();
    }
    if (arguments != nullptr) {
        *arguments = parts;
    }
    return true;
}
} // namespace



MainWindow::MainWindow(const QCommandLineParser &argParser, const QString &listFile, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow),
      fileLocation {Config::ConfigDir},
      fileName {listFile},
      lang(locale.name()),
      removeStartupCheckbox {argParser.isSet("remove-checkbox")}
{
    ui->setupUi(this);
    reloadStatus = new QLabel(this);
    reloadStatus->setWordWrap(true);
    ui->gridLayout_2->addWidget(reloadStatus, 3, 0, 1, 2);
    reloadStatus->hide();
    setConnections();
    connect(&fileWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::handleFileChanged);
    connect(&fileWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::handleDirectoryChanged);

    // Set up debounce timer for file change events
    fileReloadTimer.setSingleShot(true);
    fileReloadTimer.setInterval(200);
    connect(&fileReloadTimer, &QTimer::timeout, this, &MainWindow::refreshIfFileChanged);

    if (removeStartupCheckbox) {
        ui->checkBoxStartup->hide();
    }

    setWindowFlags(Qt::Window); // Enable close, minimize, and maximize buttons
    setup();

    readFile(fileName);
    watchFile(fileName);
    watchDesktopApplicationDirectories();
    setGui();
}

MainWindow::~MainWindow()
{
    delete ui;
}

QIcon MainWindow::findIcon(const QString &iconName) const
{
    return IconLoader::loadIcon(iconName);
}

void MainWindow::setup()
{
    setWindowTitle(tr("Custom Toolbox"));
    adjustSize();

    constexpr int defaultIconSize = 40;

    QSettings settings(Config::ConfigFile, QSettings::NativeFormat);
    hideGui = settings.value("hideGUI", false).toBool();
    minHeight = qBound(300, settings.value("min_height").toInt(), 3000);
    minWidth = qBound(300, settings.value("min_width").toInt(), 3000);
    guiEditor = settings.value("gui_editor").toString();
    fixedNumberCol = qBound(0, settings.value("fixed_number_columns", 0).toInt(), 20);
    const int size = qBound(8, settings.value("icon_size", defaultIconSize).toInt(), 1024);
    iconSize = {size, size};
}

// Add buttons and resize GUI
void MainWindow::setGui()
{
    setMinimumSize(minWidth, minHeight);

    QSettings settings(QApplication::organizationName(),
                       QApplication::applicationName() + '_' + customName);
    const QSize oldSize = size();
    if (!geometryRestored) {
        geometryRestored = true;
        if (settings.contains("geometry")) {
            restoreGeometry(settings.value("geometry").toByteArray());
            if (isMaximized()) { // Add option to resize if maximized
                resize(oldSize);
                centerWindow();
            }
        }
    }
    addButtons(categoryMap);

    migrateLegacyAutostart();

    if (!removeStartupCheckbox) {
        ui->checkBoxStartup->show();
        ui->checkBoxStartup->setChecked(isManagedAutostartFile(autostartFilePath()));
    }
    ui->textSearch->setFocus();
    for (auto *button : findChildren<QPushButton *>()) {
        button->setDefault(false);
    }
}

// Run a command through a tracked QProcess so its outcome can be reported (and,
// with hideGUI, the window re-shown) when it finishes. Runs asynchronously — no
// nested event loop — so the GUI stays responsive while the command is open.
void MainWindow::runTracked(const QString &program, const QStringList &arguments)
{
    if (hideGui) {
        hide();
    }

    const QString command = commandDescription(program, arguments);
    const bool isPkexec = program == QLatin1String("pkexec");
    auto *proc = new QProcess(this);
    // Forward output to our own stdout/stderr; the default channel mode would
    // buffer a long-running command's output in memory indefinitely.
    proc->setProcessChannelMode(QProcess::ForwardedChannels);

    connect(proc, &QProcess::errorOccurred, this, [this, proc, command](QProcess::ProcessError error) {
        // finished() is never emitted for a process that could not start.
        if (error == QProcess::FailedToStart) {
            proc->deleteLater();
            if (hideGui) {
                show();
            }
            QMessageBox::warning(this, tr("Execution Error"), tr("Failed to start command: %1").arg(command));
        }
    });
    connect(proc, &QProcess::finished, this, [this, proc, command, isPkexec](int exitCode, QProcess::ExitStatus exitStatus) {
        proc->deleteLater();
        if (hideGui) {
            show();
        }
        // pkexec exits 126 when the user dismisses the authentication dialog — a
        // choice, not an error. 127 is still reported: it also means "command not
        // found" or "no polkit agent", which the user needs to know about.
        const bool authDeclined = isPkexec && exitCode == 126;
        if (exitStatus != QProcess::NormalExit || (exitCode != 0 && !authDeclined)) {
            QMessageBox::warning(this, tr("Execution Error"), tr("Failed to execute command: %1").arg(command));
        }
    });

    proc->start(program, arguments);
}

void MainWindow::btnClicked()
{
    const auto *button = sender();
    if (!button) {
        return;
    }
    const QString commandError = button->property("commandError").toString();
    if (!commandError.isEmpty()) {
        QMessageBox::warning(this, tr("Execution Error"), commandError);
        return;
    }
    const QString program = button->property("program").toString();
    const QStringList arguments = button->property("arguments").toStringList();
    if (program.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Execution Error"), tr("Command is empty. Cannot execute."));
        return;
    }

    // pkexec and hideGui need a tracked process to report the outcome / re-show
    // the window on finish. Arguments are always launched directly: Desktop
    // Entry Exec values are not shell programs.
    if (program == QLatin1String("pkexec") || hideGui) {
        runTracked(program, arguments);
    } else {
        if (!QProcess::startDetached(program, arguments)) {
            QMessageBox::warning(this, tr("Execution Error"), tr("Failed to start program: %1").arg(program));
        }
    }
}

// Connected to QApplication::aboutToQuit so the geometry is saved on every exit
// path: window-manager close, the Cancel button (QApplication::quit) and the
// Escape key (QDialog::reject), the latter two of which bypass closeEvent().
void MainWindow::saveWindowGeometry() const
{
    QSettings settings(QApplication::organizationName(),
                       QApplication::applicationName() + '_' + customName);
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (firstRun || event->oldSize().width() == event->size().width() || fixedNumberCol != 0) {
        firstRun = false;
        return;
    }

    const int newCount = qMax(1, width() / buttonColumnWidth);

    if (newCount == colCount) {
        return;
    }

    colCount = newCount;

    if (ui->textSearch->text().isEmpty()) {
        addButtons(categoryMap);
    } else {
        textSearchTextChanged(ui->textSearch->text());
    }
}

// Find the .desktop file for the given app name
QString MainWindow::getDesktopFileName(const QString &appName) const
{
    // Check cache first
    if (auto it = desktopFileCache.constFind(appName); it != desktopFileCache.constEnd()) {
        return it.value();
    }

    // Accept an absolute path to a .desktop file directly (with or without the
    // .desktop suffix) instead of treating it as a short name to search for.
    if (QFileInfo(appName).isAbsolute()) {
        const QString resolved
            = appName.endsWith(QLatin1String(".desktop")) ? appName : appName + QLatin1String(".desktop");
        if (QFile::exists(resolved)) {
            desktopFileCache[appName] = resolved;
            return resolved;
        }
    }

    // Search for .desktop files in standard applications locations
    const QStringList searchPaths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    // Search for .desktop file in each path
    const QString desktopFileName
        = appName.endsWith(QLatin1String(".desktop")) ? appName : appName + QLatin1String(".desktop");
    QString result;

    for (const QString &searchPath : searchPaths) {
        QDirIterator it(searchPath, {desktopFileName}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            result = it.next();
            break;
        }
    }

    // Fallback: enumerate /usr/share/applications and match by Exec= first token
    // or reverse-DNS suffix (e.g. "ghex" -> "org.gnome.GHex.desktop").
    if (result.isEmpty()) {
        buildDesktopFileIndex();
        const auto it = desktopFileIndex.constFind(appName.toLower());
        if (it != desktopFileIndex.constEnd()) {
            result = it.value();
        }
    }

    // If still not found, fallback to finding the executable. Keep the absolute
    // path: defaultPath includes /usr/sbin, which may be missing from the PATH
    // the command is eventually launched with.
    if (result.isEmpty()) {
        result = QStandardPaths::findExecutable(appName, {defaultPath});
    }

    // Cache the result (even if empty to avoid repeated failed lookups)
    desktopFileCache[appName] = result;
    return result;
}

// Build a lazy fallback index of .desktop files keyed by short names.
// Priority (highest wins): Exec= first token > reverse-DNS suffix.
// Used when an exact "<name>.desktop" match fails so that .list entries like
// "ghex" still resolve to "org.gnome.GHex.desktop".
void MainWindow::buildDesktopFileIndex() const
{
    if (desktopFileIndexBuilt) {
        return;
    }
    desktopFileIndexBuilt = true;

    static const QRegularExpression execRegex(QStringLiteral(R"(^Exec=(.*)$)"),
                                            QRegularExpression::MultilineOption);

    QHash<QString, DesktopFileCandidate> bySuffix;
    QHash<QString, DesktopFileCandidate> byExec;
    auto insertCandidate = [](QHash<QString, DesktopFileCandidate> &index, const QString &key, const QString &path,
                              int sourcePriority) {
        const DesktopFileCandidate candidate {.path = path, .sourcePriority = sourcePriority};
        const auto existing = index.constFind(key);
        if (existing == index.constEnd() || candidate.sourcePriority < existing->sourcePriority
            || (candidate.sourcePriority == existing->sourcePriority && candidate.path < existing->path)) {
            index.insert(key, candidate);
        }
    };

    const QStringList searchPaths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (qsizetype sourcePriority = 0; sourcePriority < searchPaths.size(); ++sourcePriority) {
        const QString &dir = searchPaths.at(sourcePriority);
        QDirIterator it(dir, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            const QString basename = fi.completeBaseName(); // e.g. "org.gnome.GHex"
            const QString fullPath = fi.absoluteFilePath();

            // Reverse-DNS suffix: "org.gnome.GHex" -> "ghex"
            const int dot = basename.lastIndexOf('.');
            if (dot >= 0 && dot + 1 < basename.size()) {
                const QString suffix = basename.mid(dot + 1).toLower();
                if (!suffix.isEmpty()) {
                    insertCandidate(bySuffix, suffix, fullPath, sourcePriority);
                }
            }

            // Exec= first token (basename of any path), e.g. "wireshark %f" -> "wireshark"
            QFile f(fullPath);
            if (f.open(QFile::ReadOnly | QFile::Text)) {
                const QString text = QString::fromUtf8(f.readAll());
                f.close();
                const auto match = execRegex.match(text);
                if (match.hasMatch()) {
                    QString first = match.captured(1).trimmed().section(' ', 0, 0);
                    if (first.startsWith('"') && first.endsWith('"') && first.size() >= 2) {
                        first = first.mid(1, first.size() - 2);
                    }
                    first = QFileInfo(first).fileName().toLower();
                    if (!first.isEmpty()) {
                        insertCandidate(byExec, first, fullPath, sourcePriority);
                    }
                }
            }
        }
    }

    // Source-directory priority wins over match type. If two candidates come
    // from the same directory, prefer an Exec-token match over a suffix match.
    QHash<QString, DesktopFileCandidate> resolved = std::move(bySuffix);
    for (auto it = byExec.constBegin(); it != byExec.constEnd(); ++it) {
        const auto existing = resolved.constFind(it.key());
        if (existing == resolved.constEnd() || it->sourcePriority < existing->sourcePriority
            || it->sourcePriority == existing->sourcePriority) {
            resolved.insert(it.key(), it.value());
        }
    }

    desktopFileIndex.clear();
    for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it) {
        desktopFileIndex.insert(it.key(), it->path);
    }
}

void MainWindow::clearDesktopFileCaches()
{
    desktopFileCache.clear();
    desktopFileIndex.clear();
    desktopFileIndexBuilt = false;
}

// Return the app info needed for the button
ItemInfo MainWindow::getDesktopFileInfo(const QString &fname) const
{
    // If not a .desktop file, treat it as an executable (possibly an absolute
    // path); show and look up the icon by the bare name, launch by full path.
    // The exec path is quoted when it contains spaces so QProcess::splitCommand
    // and the shell keep it as a single argument.
    if (!fname.endsWith(".desktop")) {
        const QString displayName = QFileInfo(fname).fileName();
        return {.name = displayName,
                .iconName = displayName,
                .exec = fname,
                .terminal = true};
    }

    ItemInfo item;

    QFile file(fname);
    if (!file.open(QFile::Text | QFile::ReadOnly)) {
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    const QString type = desktopEntryValue(text, QStringLiteral("Type"), lang);
    const bool hidden = desktopEntryValue(text, QStringLiteral("Hidden"), lang).compare(QLatin1String("true"),
                                                                                         Qt::CaseInsensitive)
                        == 0;
    if (type != QLatin1String("Application") || hidden) {
        return {};
    }

    const QString tryExec = desktopEntryValue(text, QStringLiteral("TryExec"), lang);
    if (!tryExec.isEmpty()) {
        const QString tryExecProgram = QProcess::splitCommand(tryExec).value(0);
        if (tryExecProgram.isEmpty() || QStandardPaths::findExecutable(tryExecProgram, defaultPath).isEmpty()) {
            return {};
        }
    }

    static const QRegularExpression mxPrefix("^MX ");
    item.name = desktopEntryValue(text, QStringLiteral("Name"), lang).remove(mxPrefix);
    item.comment = desktopEntryValue(text, QStringLiteral("Comment"), lang);
    item.iconName = desktopEntryValue(text, QStringLiteral("Icon"), lang);
    item.terminal = desktopEntryValue(text, QStringLiteral("Terminal"), lang).compare(QLatin1String("true"),
                                                                                         Qt::CaseInsensitive)
                    == 0;
    if (item.name.isEmpty()
        || !parseDesktopExec(desktopEntryValue(text, QStringLiteral("Exec"), lang), item.name, item.iconName, fname,
                             &item.exec, &item.execArgs)) {
        return {};
    }

    return item;
}

void MainWindow::addButtons(const QMultiMap<QString, ItemInfo> &map)
{
    clearGridLayout();
    int col = 0;
    int row = 0;
    const int maxCols = fixedNumberCol != 0 ? fixedNumberCol : qMax(1, width() / buttonColumnWidth);
    colCount = maxCols;

    QString prevCategory;
    for (auto it = map.constBegin(); it != map.constEnd();) {
        const QString &category = it.key();

        // Add category label on first encounter
        if (category != prevCategory) {
            if (!prevCategory.isEmpty()) {
                addEmptyRowIfNeeded(prevCategory, map, row, col);
            }
            addCategoryLabel(category, row, col);
            prevCategory = category;
        }

        addItemButton(it.value(), row, col, maxCols);
        ++it;
    }
    if (!prevCategory.isEmpty()) {
        addEmptyRowIfNeeded(prevCategory, map, row, col);
    }
    ui->gridLayout_btn->setRowStretch(row, 1);
}

void MainWindow::addCategoryLabel(const QString &category, int &row, int &col)
{
    auto *label = new QLabel(category, this);
    QFont font;
    font.setBold(true);
    font.setUnderline(true);
    label->setFont(font);
    ui->gridLayout_btn->addWidget(label, ++row, col);
    ui->gridLayout_btn->setRowStretch(++row, 0);
}

void MainWindow::addItemButton(const ItemInfo &item, int &row, int &col, int maxCols)
{
    QString name = item.name;

    auto *btn = new FlatButton(name);
    btn->setIconSize(iconSize);
    btn->setToolTip(item.comment);
    const QIcon icon = findIcon(item.iconName);
    btn->setIcon(icon.isNull() ? QIcon::fromTheme("utilities-terminal") : icon);
    ui->gridLayout_btn->addWidget(btn, row, col);
    ui->gridLayout_btn->setRowStretch(row, 0);

    QString commandError;
    QString program;
    QStringList arguments;
    prepareCommand(item, item.exec, item.execArgs, &program, &arguments, &commandError);
    btn->setProperty("program", program);
    btn->setProperty("arguments", arguments);
    btn->setProperty("commandError", commandError);
    connect(btn, &QPushButton::clicked, this, &MainWindow::btnClicked);

    if (++col >= maxCols) {
        col = 0;
        ++row;
    }
}

// Resolve the unprivileged user that elevated this (now-root) process from the
// variables the elevation tool exports. Returns empty if none are set; logname(1)
// is deliberately not used as it reads utmp, which is empty in the GUI/SSH/container
// sessions this needs to cover.
QString MainWindow::invokingUser() const
{
    auto nameForUid = [](const QByteArray &value) -> QString {
        bool ok = false;
        const uint uid = value.toUInt(&ok);
        if (ok) {
            if (const passwd *pw = getpwuid(uid)) {
                return QString::fromLocal8Bit(pw->pw_name);
            }
        }
        return {};
    };

    for (const char *var : {"PKEXEC_UID", "SUDO_UID"}) {
        if (const QByteArray value = qgetenv(var); !value.isEmpty()) {
            if (const QString name = nameForUid(value); !name.isEmpty()) {
                return name;
            }
        }
    }
    if (const QByteArray sudoUser = qgetenv("SUDO_USER"); !sudoUser.isEmpty()) {
        return QString::fromLocal8Bit(sudoUser);
    }
    return {};
}

bool MainWindow::prepareCommand(const ItemInfo &item, const QString &commandProgram,
                                const QStringList &commandArguments, QString *program, QStringList *arguments,
                                QString *errorMessage) const
{
    if (commandProgram.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Command is empty. Cannot execute.");
        }
        return false;
    }

    QString programName = commandProgram;
    QStringList commandParts = commandArguments;
    if (item.terminal) {
        commandParts.prepend(programName);
        commandParts.prepend(QStringLiteral("-e"));
        programName = QStringLiteral("x-terminal-emulator");
    }

    QStringList environmentArgs;
    environmentArgs << QStringLiteral("env")
                    << QStringLiteral("DISPLAY=") + qEnvironmentVariable("DISPLAY")
                    << QStringLiteral("XAUTHORITY=")
                           + qEnvironmentVariable("XAUTHORITY", QDir::homePath() + QStringLiteral("/.Xauthority"));

    if (item.root && getuid() != 0) {
        commandParts.prepend(programName);
        commandParts = environmentArgs + commandParts;
        programName = QStringLiteral("pkexec");
    } else if (item.user && getuid() == 0) {
        if (const QString user = invokingUser(); !user.isEmpty()) {
            const QString userHome = homeDirectoryForUser(user);
            if (userHome.isEmpty()) {
                const QString message = tr("Could not determine the unprivileged user's home directory.");
                if (errorMessage != nullptr) {
                    *errorMessage = message;
                }
                qWarning() << message << user;
                return false;
            }
            environmentArgs[2] = QStringLiteral("XAUTHORITY=")
                                 + qEnvironmentVariable("XAUTHORITY", userHome + QStringLiteral("/.Xauthority"));
            commandParts.prepend(programName);
            QStringList prefix {QStringLiteral("--user"), user};
            prefix += environmentArgs;
            prefix += commandParts;
            commandParts = std::move(prefix);
            programName = QStringLiteral("pkexec");
        } else {
            const QString message
                = tr("Could not determine the unprivileged user. Refusing to run this launcher as root.");
            if (errorMessage != nullptr) {
                *errorMessage = message;
            }
            qWarning() << message << commandProgram;
            return false;
        }
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (program != nullptr) {
        *program = programName;
    }
    if (arguments != nullptr) {
        *arguments = commandParts;
    }
    return true;
}

void MainWindow::addEmptyRowIfNeeded(const QString &category, const QMultiMap<QString, ItemInfo> &map, int &row,
                                         int &col)
{
    if (category != map.lastKey()) {
        col = 0;
        auto *line = new QFrame();
        line->setFixedHeight(1);
        // Theme-adaptive separator: palette(mid) tracks the active light/dark palette,
        // whereas a Sunken QFrame's etched 3D shading can look faint or wrong on dark themes.
        line->setStyleSheet(QStringLiteral("background-color: palette(mid);"));
        ui->gridLayout_btn->addWidget(line, ++row, col, 1, -1);
        ui->gridLayout_btn->setRowStretch(row, 0);
    }
}

void MainWindow::centerWindow()
{
    // Center on the screen the window is on (not necessarily the primary one),
    // offset by the screen's origin so multi-monitor layouts work.
    const QRect screenGeometry = screen()->availableGeometry();
    const int x = screenGeometry.x() + (screenGeometry.width() - width()) / 2;
    const int y = screenGeometry.y() + (screenGeometry.height() - height()) / 2;
    move(x, y);
}

// Remove all items from the grid layout
void MainWindow::clearGridLayout()
{
    QLayoutItem *childItem {};
    while ((childItem = ui->gridLayout_btn->takeAt(0)) != nullptr) {
        if (QWidget *widget = childItem->widget()) {
            // deleteLater: a reload triggered from within a nested event loop
            // (file watcher firing during runSynchronous) can land here while a
            // button's clicked() emission is still on the stack — deleting it
            // directly would be a use-after-free.
            widget->hide();
            widget->deleteLater();
        }
        delete childItem;
    }
}

// Open the .list file and process it
bool MainWindow::readFile(const QString &fname, bool showErrors)
{
    if (!QFile::exists(fname)) {
        if (showErrors) {
            QMessageBox::critical(this, tr("File Not Found"), tr("The file %1 does not exist.").arg(fname));
        }
        return false;
    }

    // Update file location early so watchFile always watches the correct directory
    // even if parsing or resolution fails.
    fileLocation = QFileInfo(fname).path();

    // Detect INI format. QSettings puts [General] keys at root scope, so we
    // identify the new format by the presence of the [Categories] section.
    QSettings iniSettings(fname, QSettings::IniFormat);
    const bool isIni = iniSettings.status() == QSettings::NoError
                        && iniSettings.contains(QStringLiteral("Categories/list"));

    LauncherParser::ParseResult parsed;
    if (isIni) {
        parsed = LauncherParser::parseIni(iniSettings, lang);
    } else {
        QFile file(fname);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            if (showErrors) {
                QMessageBox::critical(this, tr("File Open Error"), tr("Could not open file: ") + fname);
            }
            return false;
        }
        parsed = LauncherParser::parse(file.readAll(), lang);
    }

    if (parsed.items.isEmpty()) {
        if (showErrors) {
            QMessageBox::critical(this, tr("Parse Error"),
                                  tr("The file %1 contains no recognizable launcher entries.").arg(fname));
        }
        return false;
    }

    clearDesktopFileCaches();

    // Resolve items against installed .desktop files into a temporary map.
    // If nothing resolves, the launcher would be empty — bail without touching state.
    QMultiMap<QString, ItemInfo> newMap;
    for (const auto &p : parsed.items) {
        const QString desktopFile = getDesktopFileName(p.appName);
        if (desktopFile.isEmpty()) {
            continue;
        }

        ItemInfo info = getDesktopFileInfo(desktopFile);
        if (info.name.isEmpty() && info.exec.isEmpty() && info.iconName.isEmpty() && info.comment.isEmpty()) {
            qWarning() << "Skipping unreadable desktop file:" << desktopFile;
            continue;
        }
        info.root = p.root;
        info.user = p.user;
        info.terminal = info.terminal || p.terminal;
        if (!p.alias.isEmpty()) {
            info.name = p.alias;
        }
        info.category = p.category;
        newMap.insert(info.category, info);
    }

    if (newMap.isEmpty()) {
        if (showErrors) {
            QMessageBox::critical(this, tr("Parse Error"),
                                  tr("None of the entries in %1 match an installed application.").arg(fname));
        }
        return false;
    }

    // Swap state in. (fileLocation was already set near the top of readFile.)
    const QFileInfo fileInfo(fname);
    // completeBaseName: baseName() would truncate "my.toolbox.list" to "my",
    // corrupting the settings key and the autostart .desktop name.
    customName = fileInfo.completeBaseName();

    const QString newIconTheme = parsed.iconTheme;
    const QString effectiveOldTheme = iconTheme.isEmpty() ? defaultIconTheme : iconTheme;
    const QString effectiveNewTheme = newIconTheme.isEmpty() ? defaultIconTheme : newIconTheme;

    categoryMap = std::move(newMap);
    if (effectiveNewTheme != effectiveOldTheme) {
        IconLoader::clearCache();
    }
    iconTheme = newIconTheme;
    setWindowTitle(parsed.name);
    ui->commentLabel->setText(parsed.comment);

    QIcon::setThemeName(effectiveNewTheme);
    return true;
}

void MainWindow::setConnections()
{
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::saveWindowGeometry);
    connect(ui->checkBoxStartup, &QPushButton::clicked, this, &MainWindow::checkboxStartupClicked);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAboutClicked);
    connect(ui->pushCancel, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(ui->pushEdit, &QPushButton::clicked, this, &MainWindow::pushEditClicked);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelpClicked);
    connect(ui->textSearch, &QLineEdit::textChanged, this, &MainWindow::textSearchTextChanged);
}

void MainWindow::pushAboutClicked()
{
    const QString aboutText
        = QString("<p align=\"center\"><b><h2>%1</h2></b></p>"
                  "<p align=\"center\">%2 %3</p>"
                  "<p align=\"center\"><h3>%4</h3></p>"
                  "<p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p>"
                  "<p align=\"center\">%5<br /><br /></p>")
              .arg(windowTitle().toHtmlEscaped(), tr("Version:"), QApplication::applicationVersion(),
                   tr("Custom Toolbox is a tool used for creating a custom launcher"), tr("Copyright (c) MX Linux"));

    displayAboutMsgBox(tr("About %1").arg(windowTitle()), aboutText, Config::LicenseFile,
                       tr("%1 License").arg(windowTitle()));
}

void MainWindow::pushHelpClicked()
{
    displayHelpDoc(Config::HelpFile, tr("%1 Help").arg(windowTitle()));
}

void MainWindow::textSearchTextChanged(const QString &searchText)
{
    // Early return for empty search - show all items
    if (searchText.isEmpty()) {
        addButtons(categoryMap);
        return;
    }

    // Use QStringView for efficient string matching
    const QStringView searchView(searchText);

    // Create a lambda function to check if an item matches the search text
    auto matchesSearchText = [&searchView](const ItemInfo &item) {
        return QStringView(item.name).contains(searchView, Qt::CaseInsensitive)
               || QStringView(item.comment).contains(searchView, Qt::CaseInsensitive)
               || QStringView(item.category).contains(searchView, Qt::CaseInsensitive);
    };

    // Filter categoryMap to only include items that match the search text
    QMultiMap<QString, ItemInfo> filteredMap;
    for (const auto &item : categoryMap) {
        if (matchesSearchText(item)) {
            filteredMap.insert(item.category, item);
        }
    }

    // Update the buttons with the filtered map (empty map shows no results)
    addButtons(filteredMap);
}

QString MainWindow::autostartSourceHash() const
{
    const QFileInfo info(fileName);
    const QString sourcePath = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    return QString::fromLatin1(QCryptographicHash::hash(sourcePath.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString MainWindow::autostartFilePath() const
{
    return QDir::homePath() + QStringLiteral("/.config/autostart/custom-toolbox-") + autostartSourceHash().left(16)
           + QStringLiteral(".desktop");
}

bool MainWindow::isManagedAutostartFile(const QString &path) const
{
    if (!QFile::exists(path)) {
        return false;
    }

    QSettings settings(path, QSettings::IniFormat);
    return settings.status() == QSettings::NoError
           && settings.value(QStringLiteral("Desktop Entry/X-Custom-Toolbox-Managed")).toBool()
           && settings.value(QStringLiteral("Desktop Entry/X-Custom-Toolbox-Source-SHA256")).toString()
                  == autostartSourceHash();
}

bool MainWindow::isLegacyAutostartFile(const QString &path) const
{
    if (!QFile::exists(path)) {
        return false;
    }

    QSettings settings(path, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        return false;
    }

    const QStringList execParts
        = QProcess::splitCommand(settings.value(QStringLiteral("Desktop Entry/Exec")).toString());
    if (execParts.size() < 2 || execParts.first() != QLatin1String("custom-toolbox")) {
        return false;
    }

    const QFileInfo info(fileName);
    const QString absolutePath = info.absoluteFilePath();
    const QString canonicalPath = info.canonicalFilePath();
    const QString listPath = execParts.mid(1).join(QLatin1Char(' '));
    return listPath == absolutePath || (!canonicalPath.isEmpty() && listPath == canonicalPath);
}

bool MainWindow::writeAutostartFile(QString *errorMessage) const
{
    const QString autostartPath = autostartFilePath();
    if (QFile::exists(autostartPath) && !isManagedAutostartFile(autostartPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Refusing to overwrite a non-Custom Toolbox autostart file: %1").arg(autostartPath);
        }
        return false;
    }

    auto escapeDesktopString = [](QString value) {
        value.replace('\\', "\\\\");
        value.replace('\n', "\\n");
        value.replace('\r', "\\r");
        value.replace('\t', "\\t");
        return value;
    };
    auto quoteExecArg = [](QString value) {
        value.replace('\\', "\\\\");
        value.replace('"', "\\\"");
        value.replace('$', "\\$");
        value.replace('`', "\\`");
        return '"' + value + '"';
    };

    QSaveFile file(autostartPath);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Could not write file: %1").arg(autostartPath);
        }
        return false;
    }

    const QString listPath = QFileInfo(fileName).absoluteFilePath();
    QTextStream out(&file);
    out << "[Desktop Entry]\n"
        << "Name=" << escapeDesktopString(windowTitle()) << '\n'
        << "Comment=" << escapeDesktopString(ui->commentLabel->text()) << '\n'
        << "Exec=custom-toolbox " << quoteExecArg(listPath) << '\n'
        << "Terminal=false\n"
        << "Type=Application\n"
        << "Icon=custom-toolbox\n"
        << "Categories=XFCE;System\n"
        << "StartupNotify=false\n"
        << "X-Custom-Toolbox-Managed=true\n"
        << "X-Custom-Toolbox-Source-SHA256=" << autostartSourceHash() << '\n';

    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Could not write file: %1").arg(autostartPath);
        }
        return false;
    }
    return true;
}

// Older versions used the list basename as the autostart name. Migrate only
// entries whose exact Exec command points to this list; unrelated files are
// never overwritten or removed.
void MainWindow::migrateLegacyAutostart()
{
    const QString autostartDir = QDir::homePath() + QStringLiteral("/.config/autostart/");
    const QFileInfo info(fileName);
    const QStringList legacyNames {customName, info.baseName()};
    for (const QString &name : legacyNames) {
        const QString legacyPath = autostartDir + name + QStringLiteral(".desktop");
        if (legacyPath == autostartFilePath() || !isLegacyAutostartFile(legacyPath)) {
            continue;
        }

        QString errorMessage;
        if (writeAutostartFile(&errorMessage) && !QFile::remove(legacyPath)) {
            qWarning() << "Could not remove migrated legacy autostart file:" << legacyPath;
        } else if (!errorMessage.isEmpty()) {
            qWarning() << errorMessage;
        }
    }
}

// Add a .desktop file to the ~/.config/autostart
void MainWindow::checkboxStartupClicked(bool checked)
{
    const QString autostartDir = QDir::homePath() + QStringLiteral("/.config/autostart/");
    const QString autostartPath = autostartFilePath();
    if (checked) {
        // Ensure the autostart directory exists
        if (!QDir(autostartDir).exists() && !QDir().mkpath(autostartDir)) {
            QMessageBox::critical(this, tr("Directory Creation Error"),
                                  tr("Could not create directory: %1").arg(autostartDir));
            ui->checkBoxStartup->setChecked(false);
            return;
        }

        QString errorMessage;
        if (!writeAutostartFile(&errorMessage)) {
            QMessageBox::critical(this, tr("File Open Error"), errorMessage);
            ui->checkBoxStartup->setChecked(false);
        }
    } else {
        // Never remove a file that does not carry this launcher's ownership marker.
        if (isManagedAutostartFile(autostartPath) && !QFile::remove(autostartPath)) {
            QMessageBox::warning(this, tr("File Removal Error"), tr("Could not remove file: %1").arg(autostartPath));
        }
    }
}

void MainWindow::pushEditClicked()
{
    const QString guiEditorProgram = QProcess::splitCommand(guiEditor).value(0);
    const bool useDefaultEditor = guiEditor.isEmpty() || guiEditorProgram.isEmpty()
                                    || QStandardPaths::findExecutable(guiEditorProgram, {defaultPath}).isEmpty();
    const QString editor = useDefaultEditor ? getDefaultEditor() : guiEditor;
    const QStringList editorParts = QProcess::splitCommand(editor);
    if (editorParts.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Editor command is empty."));
        return;
    }

    QString editorError;
    QString program;
    QStringList arguments;
    prepareEditorCommand(editor, &program, &arguments, &editorError);
    if (!editorError.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), editorError);
        return;
    }

    if (!QProcess::startDetached(program, arguments)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to launch the editor."));
    }

    // The editor runs detached so the toolbox stays responsive; edits saved in
    // the editor are picked up automatically by the file/directory watcher,
    // which debounces and reloads. Re-arm the watch in case it was dropped.
    watchFile(fileName);
}

QString MainWindow::getDefaultEditor() const
{
    QProcess proc;
    proc.start("xdg-mime", {"query", "default", "text/plain"});
    if (!proc.waitForFinished(3000) || proc.exitCode() != 0) {
        qWarning() << "xdg-mime failed to query default editor";
        return "nano"; // Fallback to nano
    }
    const QString defaultEditor = proc.readAllStandardOutput().trimmed();
    const QString desktopFile
        = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, defaultEditor, QStandardPaths::LocateFile);

    QFile file(desktopFile);
    if (desktopFile.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return "nano"; // Fallback to nano
    }

    QTextStream in(&file);
    QString line;
    while (in.readLineInto(&line)) {
        if (line.startsWith("Exec=")) {
            // Extract the command from the Exec line and remove common desktop file parameters
            static const QRegularExpression execPrefixPattern("^Exec=");
            static const QRegularExpression paramPattern(" %[a-zA-Z]");
            return line.remove(execPrefixPattern).remove(paramPattern).trimmed();
        }
    }

    return "nano"; // Fallback to nano
}

bool MainWindow::prepareEditorCommand(const QString &editor, QString *program, QStringList *arguments,
                                      QString *errorMessage) const
{
    QStringList commandParts = QProcess::splitCommand(editor);
    if (commandParts.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Editor command is empty.");
        }
        return false;
    }

    const bool isRoot = getuid() == 0;
    static const QRegularExpression elevatesPattern(R"(\b(kate|kwrite|featherpad|code|codium)$)");
    static const QRegularExpression cliPattern(R"(\b(nano|vi|vim|nvim|micro|emacs)\b)");
    QString editorProgram = commandParts.takeFirst();
    const QString editorName = QFileInfo(editorProgram).baseName();
    const bool isEditorThatElevates = elevatesPattern.match(editorName).hasMatch();
    const bool isCliEditor = cliPattern.match(editorName).hasMatch();

    commandParts << fileName;
    if (isCliEditor) {
        commandParts.prepend(editorProgram);
        commandParts.prepend(QStringLiteral("-e"));
        editorProgram = QStringLiteral("x-terminal-emulator");
    }

    QStringList environmentArgs;
    environmentArgs << QStringLiteral("env")
                    << QStringLiteral("DISPLAY=") + qEnvironmentVariable("DISPLAY")
                    << QStringLiteral("XAUTHORITY=")
                           + qEnvironmentVariable("XAUTHORITY", QDir::homePath() + QStringLiteral("/.Xauthority"));

    if (isRoot && isEditorThatElevates) {
        if (const QString user = invokingUser(); !user.isEmpty()) {
            const QString userHome = homeDirectoryForUser(user);
            if (userHome.isEmpty()) {
                const QString message = tr("Could not determine the unprivileged user's home directory.");
                if (errorMessage != nullptr) {
                    *errorMessage = message;
                }
                qWarning() << message << user;
                return false;
            }
            environmentArgs[2] = QStringLiteral("XAUTHORITY=")
                                 + qEnvironmentVariable("XAUTHORITY", userHome + QStringLiteral("/.Xauthority"));
            commandParts.prepend(editorProgram);
            QStringList prefix {QStringLiteral("--user"), user};
            prefix += environmentArgs;
            prefix += commandParts;
            commandParts = std::move(prefix);
            editorProgram = QStringLiteral("pkexec");
        } else {
            const QString message
                = tr("Could not determine the unprivileged user. Refusing to launch the editor as root.");
            if (errorMessage != nullptr) {
                *errorMessage = message;
            }
            qWarning() << message << editor;
            return false;
        }
    } else if (!isEditorThatElevates) {
        const QFileInfo fi(fileName);
        const bool needsElevation = fi.exists() ? !fi.isWritable() : !QFileInfo(fi.path()).isWritable();
        if (needsElevation) {
            commandParts.prepend(editorProgram);
            commandParts = environmentArgs + commandParts;
            editorProgram = QStringLiteral("pkexec");
        }
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (program != nullptr) {
        *program = editorProgram;
    }
    if (arguments != nullptr) {
        *arguments = commandParts;
    }
    return true;
}

void MainWindow::handleFileChanged(const QString &path)
{
    if (path != fileName) {
        return;
    }

    // Re-add watch immediately to avoid missing further changes
    watchFile(fileName);

    // Debounce: restart timer on each change to avoid redundant reloads
    fileReloadTimer.start();
}

void MainWindow::handleDirectoryChanged(const QString &path)
{
    if (desktopApplicationDirs.contains(path)) {
        fileReloadTimer.start();
        return;
    }

    if (path != fileLocation) {
        return;
    }

    // Re-add watch if file exists to avoid missing further changes
    if (QFile::exists(fileName)) {
        watchFile(fileName);
    }

    // Debounce: restart timer on each change to avoid redundant reloads
    fileReloadTimer.start();
}

void MainWindow::refreshIfFileChanged()
{
    if (!QFile::exists(fileName)) {
        showReloadStatus(tr("Could not reload the configuration. The previous configuration is still in use."));
        return;
    }

    if (readFile(fileName, false)) {
        setGui();
        reloadStatus->hide();
    } else {
        showReloadStatus(tr("Could not reload the configuration. The previous configuration is still in use."));
    }
}

void MainWindow::showReloadStatus(const QString &message)
{
    reloadStatus->setText(message);
    reloadStatus->show();
}

void MainWindow::watchFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    if (fileWatcher.files().contains(path)) {
        fileWatcher.removePath(path);
    }

    if (QFile::exists(path)) {
        if (!fileWatcher.addPath(path)) {
            qWarning() << "Failed to add watch for file:" << path;
        }
    }

    if (!fileLocation.isEmpty() && !fileWatcher.directories().contains(fileLocation)) {
        if (!fileWatcher.addPath(fileLocation)) {
            qWarning() << "Failed to add watch for directory:" << fileLocation;
        }
    }
}

void MainWindow::watchDesktopApplicationDirectories()
{
    desktopApplicationDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &path : desktopApplicationDirs) {
        if (!fileWatcher.directories().contains(path) && !fileWatcher.addPath(path)) {
            qWarning() << "Failed to add watch for application directory:" << path;
        }
    }
}
