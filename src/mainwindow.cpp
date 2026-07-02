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
#include <QDirIterator>
#include <QEventLoop>
#include <QLabel>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QStringView>

#include "about.h"
#include "flatbutton.h"
#include <pwd.h>
#include <unistd.h>



MainWindow::MainWindow(const QCommandLineParser &argParser, const QString &listFile, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow),
      fileLocation {Config::ConfigDir},
      fileName {listFile},
      lang(locale.name()),
      removeStartupCheckbox {argParser.isSet("remove-checkbox")}
{
    ui->setupUi(this);
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

// Strip %f, %F, %U, etc. if exec expects a file name since it's called without an argument from this launcher.
void MainWindow::fixExecItem(QString *item)
{
    static const QRegularExpression exec_pattern(QStringLiteral(R"( %[a-zA-Z])"));
    item->remove(exec_pattern);
}

void MainWindow::setup()
{
    setWindowTitle(tr("Custom Toolbox"));
    adjustSize();

    const int defaultIconSize = 40;

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

    // Check if .desktop file is in autostart; same customName as .list file
    if (!removeStartupCheckbox
        && QFile::exists(QDir::homePath() + "/.config/autostart/" + customName + ".desktop")) {
        ui->checkBoxStartup->show();
        ui->checkBoxStartup->setChecked(true);
    }
    ui->textSearch->setFocus();
    for (auto *button : findChildren<QPushButton *>()) {
        button->setDefault(false);
    }
}

void MainWindow::runSynchronous(const QString &cmd, bool useShell)
{
    if (hideGui) {
        hide();
    } else {
        // Block further button clicks while the nested event loop runs, so
        // commands cannot be launched concurrently from the same window.
        setEnabled(false);
    }

    QProcess proc;
    QEventLoop loop;
    connect(&proc, &QProcess::finished, &loop, &QEventLoop::quit);
    if (useShell) {
        proc.start("/bin/sh", {"-c", cmd});
    } else {
        QStringList arguments = QProcess::splitCommand(cmd);
        const QString program = arguments.takeFirst();
        proc.start(program, arguments);
    }

    const bool started = proc.waitForStarted();
    if (started && proc.state() != QProcess::NotRunning) {
        loop.exec();
    }

    if (hideGui) {
        show();
    } else {
        setEnabled(true);
    }

    if (!started) {
        QMessageBox::warning(this, tr("Execution Error"), tr("Failed to start command: %1").arg(cmd));
        return;
    }
    // pkexec exits 126 when the user dismisses the authentication dialog — a
    // choice, not an error. 127 is still reported: it also means "command not
    // found" or "no polkit agent", which the user needs to know about.
    const bool authDeclined = cmd.startsWith("pkexec") && proc.exitCode() == 126;
    if (proc.exitStatus() != QProcess::NormalExit || (proc.exitCode() != 0 && !authDeclined)) {
        QMessageBox::warning(this, tr("Execution Error"), tr("Failed to execute command: %1").arg(cmd));
    }
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
    const QString cmd = button->property("cmd").toString();
    if (cmd.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Execution Error"), tr("Command is empty. Cannot execute."));
        return;
    }

    // pkexec requires shell for variable expansion; hideGui requires synchronous execution
    if (cmd.startsWith("pkexec") || hideGui) {
        runSynchronous(cmd, cmd.startsWith("pkexec"));
    } else {
        QStringList arguments = QProcess::splitCommand(cmd);
        const QString program = arguments.takeFirst();
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

    const int itemSize = 200; // Determined through trial and error
    const int newCount = qMax(1, width() / itemSize);

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
    const QString desktopFileName = appName + ".desktop";
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

    static const QRegularExpression exec_re(QStringLiteral(R"(^Exec=(.*)$)"),
                                            QRegularExpression::MultilineOption);

    QHash<QString, QString> by_suffix;
    QHash<QString, QString> by_exec;

    const QStringList searchPaths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : searchPaths) {
        QDirIterator it(dir, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            const QString basename = fi.completeBaseName(); // e.g. "org.gnome.GHex"
            const QString full_path = fi.absoluteFilePath();

            // Reverse-DNS suffix: "org.gnome.GHex" -> "ghex"
            const int dot = basename.lastIndexOf('.');
            if (dot >= 0 && dot + 1 < basename.size()) {
                const QString suffix = basename.mid(dot + 1).toLower();
                if (!suffix.isEmpty() && !by_suffix.contains(suffix)) {
                    by_suffix.insert(suffix, full_path);
                }
            }

            // Exec= first token (basename of any path), e.g. "wireshark %f" -> "wireshark"
            QFile f(full_path);
            if (f.open(QFile::ReadOnly | QFile::Text)) {
                const QString text = QString::fromUtf8(f.readAll());
                f.close();
                const auto match = exec_re.match(text);
                if (match.hasMatch()) {
                    QString first = match.captured(1).trimmed().section(' ', 0, 0);
                    if (first.startsWith('"') && first.endsWith('"') && first.size() >= 2) {
                        first = first.mid(1, first.size() - 2);
                    }
                    first = QFileInfo(first).fileName().toLower();
                    if (!first.isEmpty() && !by_exec.contains(first)) {
                        by_exec.insert(first, full_path);
                    }
                }
            }
        }
    }

    // Apply lowest priority first; higher priority overwrites.
    desktopFileIndex = std::move(by_suffix);
    for (auto it = by_exec.constBegin(); it != by_exec.constEnd(); ++it) {
        desktopFileIndex.insert(it.key(), it.value());
    }
}

// Return the app info needed for the button
ItemInfo MainWindow::getDesktopFileInfo(const QString &fname) const
{
    ItemInfo item;

    // If not a .desktop file, treat it as an executable (possibly an absolute
    // path); show and look up the icon by the bare name, launch by full path.
    if (!fname.endsWith(".desktop")) {
        const QString displayName = QFileInfo(fname).fileName();
        item.name = displayName;
        item.iconName = displayName;
        item.exec = fname;
        item.terminal = true;
        return item;
    }

    QFile file(fname);
    if (!file.open(QFile::Text | QFile::ReadOnly)) {
        return {};
    }
    const QString text = file.readAll();
    file.close();

    static const QRegularExpression mx_prefix("^MX ");
    item.name = LauncherParser::extractLocalizedValue(text, "Name", lang).remove(mx_prefix);
    item.comment = LauncherParser::extractLocalizedValue(text, "Comment", lang);
    item.iconName = LauncherParser::extractLocalizedValue(text, "Icon", lang);
    item.exec = LauncherParser::extractLocalizedValue(text, "Exec", lang);
    item.terminal = LauncherParser::extractLocalizedValue(text, "Terminal", lang).toLower() == "true";

    return item;
}

void MainWindow::addButtons(const QMultiMap<QString, ItemInfo> &map)
{
    clearGridLayout();
    int col = 0;
    int row = 0;
    const int max_cols = fixedNumberCol != 0 ? fixedNumberCol : qMax(1, width() / 200);
    colCount = max_cols;

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

        addItemButton(it.value(), row, col, max_cols);
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
    QString cmd = item.exec;
    fixExecItem(&cmd);

    auto *btn = new FlatButton(name);
    btn->setIconSize(iconSize);
    btn->setToolTip(item.comment);
    const QIcon icon = findIcon(item.iconName);
    btn->setIcon(icon.isNull() ? QIcon::fromTheme("utilities-terminal") : icon);
    ui->gridLayout_btn->addWidget(btn, row, col);
    ui->gridLayout_btn->setRowStretch(row, 0);

    QString commandError;
    prepareCommand(item, cmd, &commandError);
    btn->setProperty("cmd", cmd);
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
    if (const QByteArray sudo_user = qgetenv("SUDO_USER"); !sudo_user.isEmpty()) {
        return QString::fromLocal8Bit(sudo_user);
    }
    return {};
}

bool MainWindow::prepareCommand(const ItemInfo &item, QString &cmd, QString *errorMessage) const
{
    if (item.terminal) {
        cmd.prepend("x-terminal-emulator -e ");
    }
    if (item.root && getuid() != 0) {
        cmd.prepend("pkexec env DISPLAY=$DISPLAY XAUTHORITY=${XAUTHORITY:-$HOME/.Xauthority} ");
    } else if (item.user && getuid() == 0) {
        if (const QString user = invokingUser(); !user.isEmpty()) {
            // $HOME was overwritten to /root in main(); use the original (pre-root)
            // home so the demoted user reads their own .Xauthority, not root's.
            const QString userHome = starting_home();
            cmd = QStringLiteral("pkexec --user %1 env DISPLAY=$DISPLAY XAUTHORITY=${XAUTHORITY:-%2/.Xauthority} ")
                      .arg(user, userHome)
                  + cmd;
        } else {
            const QString message
                = tr("Could not determine the unprivileged user. Refusing to run this launcher as root.");
            if (errorMessage != nullptr) {
                *errorMessage = message;
            }
            qWarning() << message << cmd;
            return false;
        }
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
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
        // Delete the widget and the layout item
        delete childItem->widget();
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
              .arg(windowTitle(), tr("Version:"), QApplication::applicationVersion(),
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

// Older versions derived the autostart name with baseName(), truncating
// multi-dot list names ("my.toolbox.list" -> "my.desktop") and writing an Exec
// that pointed at a non-existent .list — popping an error box at every login.
// Replace such an entry with one under the correct name. Skipped when a sibling
// "<legacy>.list" exists, since the entry may legitimately belong to that list.
void MainWindow::migrateLegacyAutostart()
{
    const QString legacyName = QFileInfo(fileName).baseName();
    if (legacyName == customName) {
        return;
    }
    const QString legacyDesktop = QDir::homePath() + "/.config/autostart/" + legacyName + ".desktop";
    if (!QFile::exists(legacyDesktop) || QFile::exists(QDir(fileLocation).filePath(legacyName + ".list"))) {
        return;
    }
    QFile file(legacyDesktop);
    const bool ours = file.open(QFile::ReadOnly | QFile::Text)
                      && QString::fromUtf8(file.readAll()).contains(QLatin1String("Exec=custom-toolbox"));
    file.close();
    if (ours && QFile::remove(legacyDesktop)) {
        checkboxStartupClicked(true);
    }
}

// Add a .desktop file to the ~/.config/autostart
void MainWindow::checkboxStartupClicked(bool checked)
{
    const QString autostartDir = QDir::homePath() + "/.config/autostart/";
    const QString autostartFileName = autostartDir + customName + ".desktop"; // Same name as .list file
    if (checked) {
        // Ensure the autostart directory exists
        if (!QDir(autostartDir).exists() && !QDir().mkpath(autostartDir)) {
            QMessageBox::critical(this, tr("Directory Creation Error"),
                                  tr("Could not create directory: %1").arg(autostartDir));
            ui->checkBoxStartup->setChecked(false);
            return;
        }

        if (QFile file(autostartFileName); file.open(QFile::WriteOnly | QFile::Text)) {
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

            // Use the loaded file's real path; reconstructing it from customName
            // would break for list files with a different extension.
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
                << "StartupNotify=false";
        } else {
            QMessageBox::critical(this, tr("File Open Error"), tr("Could not write file: %1").arg(autostartFileName));
            ui->checkBoxStartup->setChecked(false);
        }
    } else {
        // Only warn on a genuine removal failure; an absent file is not an error.
        if (QFile::exists(autostartFileName) && !QFile::remove(autostartFileName)) {
            QMessageBox::warning(this, tr("File Removal Error"), tr("Could not remove file: %1").arg(autostartFileName));
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

    // Helper to shell-quote arguments that may contain spaces or special characters
    auto shell_quote = [](const QString &arg) -> QString {
        QString quoted = arg;
        quoted.replace('\\', "\\\\");  // Escape backslashes first
        quoted.replace('\'', "'\\''"); // Escape single quotes
        return "'" + quoted + "'";
    };

    QString editorError;
    QStringList cmdParts = buildEditorPrefix(editor, &editorError);
    if (!editorError.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), editorError);
        return;
    }
    for (const auto &part : editorParts) {
        cmdParts << shell_quote(part);
    }
    cmdParts << shell_quote(fileName);

    if (!QProcess::startDetached("/bin/sh", {"-c", cmdParts.join(' ')})) {
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

QStringList MainWindow::buildEditorPrefix(const QString &editor, QString *errorMessage) const
{
    const bool isRoot = getuid() == 0;
    static const QRegularExpression elevatesPattern(R"(\b(kate|kwrite|featherpad|code|codium)$)");
    static const QRegularExpression cliPattern(R"(\b(nano|vi|vim|nvim|micro|emacs)\b)");
    const QString editorProgram = QProcess::splitCommand(editor).value(0);
    const QString editorName = QFileInfo(editorProgram).baseName();
    const bool isEditorThatElevates = elevatesPattern.match(editorName).hasMatch();
    const bool isCliEditor = cliPattern.match(editorName).hasMatch();

    QStringList prefix;
    QString xauthorityHome = QStringLiteral("$HOME");
    if (isRoot && isEditorThatElevates) {
        if (const QString user = invokingUser(); !user.isEmpty()) {
            prefix << QStringLiteral("pkexec --user ") + user;
            xauthorityHome = starting_home();
        } else {
            const QString message
                = tr("Could not determine the unprivileged user. Refusing to launch the editor as root.");
            if (errorMessage != nullptr) {
                *errorMessage = message;
            }
            qWarning() << message << editor;
            return {};
        }
    } else if (!isEditorThatElevates) {
        const QFileInfo fi(fileName);
        const bool needsElevation = fi.exists() ? !fi.isWritable() : !QFileInfo(fi.path()).isWritable();
        if (needsElevation) {
            prefix << "pkexec";
        }
    }

    prefix << QStringLiteral("env DISPLAY=$DISPLAY XAUTHORITY=${XAUTHORITY:-%1/.Xauthority}").arg(xauthorityHome);

    if (isCliEditor) {
        prefix << "x-terminal-emulator -e";
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return prefix;
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
        return;
    }

    if (readFile(fileName, false)) {
        setGui();
    }
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
