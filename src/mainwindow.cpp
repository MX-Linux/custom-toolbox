/**********************************************************************
 *  MainWindow.cpp
 **********************************************************************
 * Copyright (C) 2017-2025 MX Authors
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
#include "ui_mainwindow.h"

#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStringView>
#include <QTextEdit>

#include "about.h"
#include "flatbutton.h"
#include <unistd.h>



MainWindow::MainWindow(const QCommandLineParser &arg_parser, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow),
      file_location {"/etc/custom-toolbox"}
{
    ui->setupUi(this);
    setConnections();
    connect(&fileWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::handleFileChanged);
    connect(&fileWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::handleDirectoryChanged);

    if (arg_parser.isSet("remove-checkbox")) {
        ui->checkBoxStartup->hide();
    }

    setWindowFlags(Qt::Window); // Enable close, minimize, and maximize buttons
    setup();

    const QStringList argList = arg_parser.positionalArguments();
    file_name = !argList.isEmpty() && QFile(argList.first()).exists() ? argList.first() : getFileName();

    readFile(file_name);
    watchFile(file_name);
    setGui();
}

MainWindow::~MainWindow()
{
    delete ui;
}

QIcon MainWindow::findIcon(const QString &iconName)
{
    // Simple icon cache to avoid repeated filesystem lookups
    static QHash<QString, QIcon> iconCache;

    // Check cache first
    if (iconCache.contains(iconName)) {
        return iconCache[iconName];
    }

    static const QRegularExpression re(R"(\.(png|svg|xpm)$)");
    static const QStringList extensions {".png", ".svg", ".xpm"};
    static const QStringList searchPaths {QDir::homePath() + "/.local/share/icons/",
                                          "/usr/share/pixmaps/",
                                          "/usr/local/share/icons/",
                                          "/usr/share/icons/",
                                          "/usr/share/icons/hicolor/scalable/apps/",
                                          "/usr/share/icons/hicolor/48x48/apps/",
                                          "/usr/share/icons/Adwaita/48x48/legacy/"};

    // Helper function to search for icon in all paths
    static const auto searchInPaths = [&](const QString &name) -> QIcon {
        for (const auto &path : searchPaths) {
            const QString fullPath = QDir(path).filePath(name);
            if (QFile::exists(fullPath)) {
                QIcon icon(fullPath);
                if (!icon.isNull()) {
                    return icon;
                }
            }
        }
        return QIcon();
    };

    // Initialize default terminal icon once when needed
    static const auto getDefaultIcon = []() {
        static QIcon defaultIcon;
        if (!defaultIcon.isNull()) {
            return defaultIcon;
        }

        const QString defaultIconName = "utilities-terminal";
        defaultIcon = QIcon::fromTheme(defaultIconName);
        if (!defaultIcon.isNull()) {
            return defaultIcon;
        }

        for (const auto &ext : extensions) {
            defaultIcon = searchInPaths(defaultIconName + ext);
            if (!defaultIcon.isNull()) {
                return defaultIcon;
            }
        }
        return QIcon();
    };

    // Handle empty or default icon name
    if (iconName.isEmpty() || iconName == "utilities-terminal") {
        QIcon result = getDefaultIcon();
        iconCache[iconName] = result;
        return result;
    }

    // Handle absolute paths
    if (QFileInfo::exists(iconName) && QFileInfo(iconName).isAbsolute()) {
        QIcon result(iconName);
        iconCache[iconName] = result;
        return result;
    }

    // Try themed icon first
    QString nameNoExt = iconName;
    nameNoExt.remove(re);

    if (!icon_theme.isEmpty()) {
        QIcon::setThemeName(icon_theme);
    }

    QIcon icon = QIcon::fromTheme(nameNoExt);
    if (!icon.isNull()) {
        iconCache[iconName] = icon;
        return icon;
    }

    // Try original name
    icon = searchInPaths(iconName);
    if (!icon.isNull()) {
        iconCache[iconName] = icon;
        return icon;
    }

    // Try with each extension
    for (const auto &ext : extensions) {
        icon = searchInPaths(nameNoExt + ext);
        if (!icon.isNull()) {
            iconCache[iconName] = icon;
            return icon;
        }
    }

    // Fall back to the default icon if nothing else was found.
    // Note: Default icons are not cached to allow future resolution of actual icons
    return getDefaultIcon();
}

// Strip %f, %F, %U, etc. if exec expects a file name since it's called without an argument from this launcher.
void MainWindow::fixExecItem(QString *item)
{
    static const QRegularExpression execPattern(QStringLiteral(R"( %[a-zA-Z])"));
    item->remove(execPattern);
}

void MainWindow::fixNameItem(QString *item)
{
    static const QString oldName = QStringLiteral("System Profiler and Benchmark");
    static const QString newName = QStringLiteral("System Information");

    if (*item == oldName) {
        *item = newName;
    }
}

void MainWindow::setup()
{
    setWindowTitle(tr("Custom Toolbox"));
    adjustSize();

    const int default_icon_size = 40;

    QSettings settings("/etc/custom-toolbox/custom-toolbox.conf", QSettings::NativeFormat);
    hideGUI = settings.value("hideGUI", false).toBool();
    min_height = settings.value("min_height").toInt();
    min_width = settings.value("min_width").toInt();
    gui_editor = settings.value("gui_editor").toString();
    fixed_number_col = settings.value("fixed_number_columns", 0).toInt();
    int size = settings.value("icon_size", default_icon_size).toInt();
    icon_size = {size, size};
}

// Add buttons and resize GUI
void MainWindow::setGui()
{
    clearGridLayout();
    adjustSize();
    setMinimumSize(min_width, min_height);

    QSettings settings(QApplication::organizationName(),
                       QApplication::applicationName() + '_' + QFileInfo(file_name).baseName());
    const auto size = this->size();
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
        if (isMaximized()) { // Add option to resize if maximized
            resize(size);
            centerWindow();
        }
    }
    addButtons(category_map);

    // Check if .desktop file is in autostart; same custom_name as .list file
    if (QFile::exists(QDir::homePath() + "/.config/autostart/" + custom_name + ".desktop")) {
        ui->checkBoxStartup->show();
        ui->checkBoxStartup->setChecked(true);
    }
    ui->textSearch->setFocus();
    ui->pushCancel->setDefault(true); // Otherwise some other button might be default
    ui->pushCancel->setDefault(false);
}

void MainWindow::btn_clicked()
{
    const QString cmd = sender()->property("cmd").toString();
    if (cmd.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Execution Error"), tr("Command is empty. Cannot execute."));
        return;
    }
    // pkexec cannot take &, it would block the GUI that's why we need to hide it
    if (hideGUI || cmd.startsWith("pkexec")) {
        hide();
        int exitCode = system(cmd.toUtf8());
        if (exitCode != 0) {
            QMessageBox::warning(this, tr("Execution Error"), tr("Failed to execute command: %1").arg(cmd));
        }
        show();
    } else {
        QStringList arguments = QProcess::splitCommand(cmd);
        QString program = arguments.takeFirst();
        if (!QProcess::startDetached(program, arguments)) {
            QMessageBox::warning(this, tr("Execution Error"), tr("Failed to start program: %1").arg(program));
        }
    }
}

void MainWindow::closeEvent(QCloseEvent * /*unused*/)
{
    QSettings settings(QApplication::organizationName(),
                       QApplication::applicationName() + "_" + QFileInfo(file_name).baseName());
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (firstRun || event->oldSize().width() == event->size().width() || fixed_number_col != 0) {
        firstRun = false;
        return;
    }

    const int item_size = 200; // Determined through trial and error
    const int new_count = width() / item_size;

    if (new_count == col_count || (new_count >= max_elements && col_count == max_elements)) {
        return;
    }

    col_count = new_count;

    if (ui->textSearch->text().isEmpty()) {
        addButtons(category_map);
    } else {
        textSearch_textChanged(ui->textSearch->text());
    }
}

// Select .list file to open
QString MainWindow::getFileName()
{
    while (true) {
        QString fileName
            = QFileDialog::getOpenFileName(this, tr("Open List File"), file_location, tr("List Files (*.list)"));
        if (fileName.isEmpty()) {
            QMessageBox::critical(this, tr("File Selection Error"), tr("No file selected. Application will now exit."));
            exit(EXIT_FAILURE);
        }
        if (QFile::exists(fileName)) {
            return fileName;
        } else {
            auto userChoice = QMessageBox::critical(this, tr("File Open Error"),
                                                    tr("Could not open file. Do you want to try again?"),
                                                    QMessageBox::Yes | QMessageBox::No);
            if (userChoice == QMessageBox::No) {
                exit(EXIT_FAILURE);
            }
        }
    }
}

// Find the .desktop file for the given app name
QString MainWindow::getDesktopFileName(const QString &appName) const
{
    // Simple static cache to avoid repeated lookups
    static QHash<QString, QString> desktopFileCache;

    // Check cache first
    if (desktopFileCache.contains(appName)) {
        return desktopFileCache[appName];
    }

    // Search for .desktop files in standard applications locations
    const QStringList searchPaths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    // Search for .desktop file in each path
    const QString desktopFileName = appName + ".desktop";
    QString result;

    for (const QString &searchPath : searchPaths) {
        const QString fullPath = QDir(searchPath).absoluteFilePath(desktopFileName);
        if (QFile::exists(fullPath)) {
            result = fullPath;
            break;
        }
    }

    // If .desktop file not found, fallback to finding the executable
    if (result.isEmpty()) {
        const QString executablePath = QStandardPaths::findExecutable(appName, {defaultPath});
        result = !executablePath.isEmpty() ? QFileInfo(executablePath).fileName() : QString();
    }

    // Cache the result (even if empty to avoid repeated failed lookups)
    desktopFileCache[appName] = result;
    return result;
}

// Return the app info needed for the button
MainWindow::ItemInfo MainWindow::getDesktopFileInfo(const QString &fileName)
{
    ItemInfo item;

    // If not a .desktop file
    if (!fileName.endsWith(".desktop")) {
        item.name = item.icon_name = item.exec = fileName;
        item.root = item.user = false;
        item.terminal = true;
        return item;
    }

    QFile file(fileName);
    if (!file.open(QFile::Text | QFile::ReadOnly)) {
        return {};
    }
    const QString text = file.readAll();
    file.close();

    // Helper lambda to search for a pattern and extract the first capture group
    auto matchPattern = [&text](const QString &pattern) -> QString {
        QRegularExpression re(pattern, QRegularExpression::MultilineOption);
        auto match = re.match(text);
        return match.hasMatch() ? match.captured(1) : QString();
    };

    // Function to attempt matching localized fields first, then fall back to non-localized
    auto matchLocalizedField = [&](const QString &field) -> QString {
        QString value = matchPattern("^" + field + "\\[" + lang + "\\]=(.*)$");
        if (value.isEmpty()) {
            value = matchPattern("^" + field + "\\[" + lang.section('_', 0, 0) + "\\]=(.*)$");
        }
        if (value.isEmpty()) {
            value = matchPattern("^" + field + "=(.*)$");
        }
        return value;
    };

    item.name = matchLocalizedField("Name").remove(QRegularExpression("^MX "));
    item.comment = matchLocalizedField("Comment");
    item.exec = matchPattern("^Exec=(.*)$");
    item.icon_name = matchPattern("^Icon=(.*)$");
    item.terminal = matchPattern("^Terminal=(.*)$").toLower() == "true";

    return item;
}

void MainWindow::addButtons(const QMultiMap<QString, ItemInfo> &map)
{
    clearGridLayout();
    int col = 0;
    int row = 0;
    const int maxCols = fixed_number_col != 0 ? fixed_number_col : width() / 200;

    for (const auto &category : map.uniqueKeys()) {
        if (!category_map.values(category).isEmpty()) {
            addCategoryLabel(category, row, col);

            for (const auto &item : map.values(category)) {
                addItemButton(item, row, col, maxCols);
            }
        }
        addEmptyRowIfNeeded(category, map, row, col);
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
    col_count = std::max(col_count, col + 1);
    QString name = item.name;
    QString cmd = item.exec;
    fixNameItem(&name);
    fixExecItem(&cmd);

    auto *btn = new FlatButton(name);
    btn->setIconSize(icon_size);
    btn->setToolTip(item.comment);
    auto icon = findIcon(item.icon_name);
    btn->setIcon(icon.isNull() ? QIcon::fromTheme("utilities-terminal") : icon);
    ui->gridLayout_btn->addWidget(btn, row, col);
    ui->gridLayout_btn->setRowStretch(row, 0);

    prepareCommand(item, cmd);
    btn->setProperty("cmd", cmd);
    connect(btn, &QPushButton::clicked, this, &MainWindow::btn_clicked);

    if (++col >= maxCols) {
        col = 0;
        ++row;
    }
}

void MainWindow::prepareCommand(const ItemInfo &item, QString &cmd)
{
    if (item.terminal) {
        cmd.prepend("x-terminal-emulator -e ");
    }
    if (item.root && getuid() != 0) {
        cmd.prepend("pkexec env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY ");
    } else if (item.user && getuid() == 0) {
        cmd = QString("pkexec --user $(logname) env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY ") + cmd;
    }
}

void MainWindow::addEmptyRowIfNeeded(const QString &category, const QMultiMap<QString, ItemInfo> &map, int &row,
                                     int &col)
{
    if (category != map.lastKey()) {
        col = 0;
        auto *line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        ui->gridLayout_btn->addWidget(line, ++row, col, 1, -1);
        ui->gridLayout_btn->setRowStretch(row, 0);
    }
}

void MainWindow::centerWindow()
{
    const auto screenGeometry = QApplication::primaryScreen()->geometry();
    const auto x = (screenGeometry.width() - width()) / 2;
    const auto y = (screenGeometry.height() - height()) / 2;
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

void MainWindow::processLine(const QString &line)
{
    if (line.isEmpty()) {
        return;
    }
    const int splitPos = line.indexOf('=');
    const QString key = (splitPos > 0) ? line.left(splitPos).trimmed() : line.trimmed();
    if (key.isEmpty()) {
        return;
    }
    const QString value = (splitPos > 0) ? QString(line.mid(splitPos + 1)).trimmed().remove('"') : QString();
    const QString lowerKey = key.toLower();
    if (lowerKey == "category") {
        categories.append(value);
    } else if (lowerKey == "theme") {
        icon_theme = value;
    } else {
        const QStringList keyTokens = key.split(' ');
        if (keyTokens.isEmpty()) {
            return;
        }

        const QString desktop_file = getDesktopFileName(keyTokens.first());
        if (!desktop_file.isEmpty()) {
            ItemInfo info = getDesktopFileInfo(desktop_file);
            if (keyTokens.size() > 1) {
                const bool hasRoot = keyTokens.contains("root");
                const bool hasUser = keyTokens.contains("user");
                const bool hasAlias = keyTokens.contains("alias");

                info.root = hasRoot;
                info.user = hasUser;

                if (hasAlias) {
                    const int aliasIndex = keyTokens.indexOf("alias");
                    if (aliasIndex >= 0 && aliasIndex + 1 < keyTokens.size()) {
                        info.name = keyTokens.mid(aliasIndex + 1).join(' ').trimmed().remove('\'').remove('"');
                    } else {
                        qWarning() << "Alias keyword found but no valid alias name provided.";
                    }
                }
            }

            if (!categories.isEmpty()) {
                info.category = categories.last();
                category_map.insert(info.category, info);
            }
        }
    }
}

// Open the .list file and process it
void MainWindow::readFile(const QString &file_name)
{
    categories.clear();
    categories.reserve(20); // Reserve space for typical number of categories
    category_map.clear();
    icon_theme.clear();

    QFile file(file_name);
    if (!file.exists()) {
        QMessageBox::critical(this, tr("File Not Found"), tr("The file %1 does not exist.").arg(file_name));
        return;
    }

    custom_name = QFileInfo(file_name).baseName();
    file_location = QFileInfo(file_name).path();

    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::critical(this, tr("File Open Error"), tr("Could not open file: ") + file_name);
        return;
    }

    const QString text = file.readAll();
    file.close();

    QString name = extractPattern(text, "Name");
    QString comment = extractPattern(text, "Comment");

    setWindowTitle(name);
    ui->commentLabel->setText(comment);

    // Process line by line without creating intermediate QStringList
    static const QRegularExpression skipPattern(QStringLiteral("^(Name|Comment|#|$).*"));

    QStringView textView(text);
    qsizetype pos = 0;
    while (pos < textView.size()) {
        qsizetype endPos = textView.indexOf(QLatin1Char('\n'), pos);
        if (endPos == -1) {
            endPos = textView.size();
        }

        QStringView lineView = textView.mid(pos, endPos - pos);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (!lineView.isEmpty() && !skipPattern.matchView(lineView).hasMatch()) {
#else
        if (!lineView.isEmpty() && !skipPattern.match(lineView).hasMatch()) {
#endif
            processLine(lineView.toString());
        }

        pos = endPos + 1;
    }
}

QString MainWindow::extractPattern(const QString &text, const QString &key)
{
    // Cache regex patterns to avoid recompilation
    static QHash<QString, QRegularExpression> regexCache;

    QString pattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang);
    QString fallbackPattern = QStringLiteral("^%1=(.*)$").arg(key);
    QString langShortPattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang.section('_', 0, 0));

    // Check full language pattern
    if (!regexCache.contains(pattern)) {
        regexCache[pattern] = QRegularExpression(pattern, QRegularExpression::MultilineOption);
    }
    QRegularExpressionMatch match = regexCache[pattern].match(text);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    // Check short language pattern
    if (!regexCache.contains(langShortPattern)) {
        regexCache[langShortPattern] = QRegularExpression(langShortPattern, QRegularExpression::MultilineOption);
    }
    match = regexCache[langShortPattern].match(text);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    // Check fallback pattern
    if (!regexCache.contains(fallbackPattern)) {
        regexCache[fallbackPattern] = QRegularExpression(fallbackPattern, QRegularExpression::MultilineOption);
    }
    match = regexCache[fallbackPattern].match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

void MainWindow::setConnections()
{
    connect(ui->checkBoxStartup, &QPushButton::clicked, this, &MainWindow::checkBoxStartup_clicked);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushCancel, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(ui->pushEdit, &QPushButton::clicked, this, &MainWindow::pushEdit_clicked);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->textSearch, &QLineEdit::textChanged, this, &MainWindow::textSearch_textChanged);
}

void MainWindow::pushAbout_clicked()
{
    hide();
    QString aboutText
        = QString("<p align=\"center\"><b><h2>%1</h2></b></p>"
                  "<p align=\"center\">%2 %3</p>"
                  "<p align=\"center\"><h3>%4</h3></p>"
                  "<p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p>"
                  "<p align=\"center\">%5<br /><br /></p>")
              .arg(windowTitle(), tr("Version:"), QApplication::applicationVersion(),
                   tr("Custom Toolbox is a tool used for creating a custom launcher"), tr("Copyright (c) MX Linux"));

    displayAboutMsgBox(tr("About %1").arg(windowTitle()), aboutText, "/usr/share/doc/custom-toolbox/license.html",
                       tr("%1 License").arg(windowTitle()));
    show();
}

void MainWindow::pushHelp_clicked()
{
    const QString url = "/usr/share/doc/custom-toolbox/help.html";
    displayDoc(url, tr("%1 Help").arg(windowTitle()));
}

void MainWindow::textSearch_textChanged(const QString &searchText)
{
    // Early return for empty search - show all items
    if (searchText.isEmpty()) {
        addButtons(category_map);
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

    // Filter category_map to only include items that match the search text
    QMultiMap<QString, ItemInfo> filtered_map;
    for (const auto &item : category_map) {
        if (matchesSearchText(item)) {
            filtered_map.insert(item.category, item);
        }
    }

    // Update the buttons with the filtered map or the original map if no matches found
    addButtons(filtered_map.isEmpty() ? category_map : filtered_map);
}

// Add a .desktop file to the ~/.config/autostart
void MainWindow::checkBoxStartup_clicked(bool checked)
{
    const QString fileName
        = QDir::homePath() + "/.config/autostart/" + custom_name + ".desktop"; // Same name as .list file
    if (checked) {
        if (QFile file(fileName); file.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&file);
            out << "[Desktop Entry]\n"
                << "Name=" << windowTitle() << '\n'
                << "Comment=" << ui->commentLabel->text() << '\n'
                << "Exec=custom-toolbox \"" << file_location << '/' << custom_name << ".list\"\n"
                << "Terminal=false\n"
                << "Type=Application\n"
                << "Icon=custom-toolbox\n"
                << "Categories=XFCE;System\n"
                << "StartupNotify=false";
        } else {
            QMessageBox::critical(this, tr("File Open Error"), tr("Could not write file: %1").arg(fileName));
        }
    } else {
        if (!QFile::remove(fileName)) {
            QMessageBox::warning(this, tr("File Removal Error"), tr("Could not remove file: %1").arg(fileName));
        }
    }
}

void MainWindow::pushEdit_clicked()
{
    QString editor = gui_editor.isEmpty() || QStandardPaths::findExecutable(gui_editor, {defaultPath}).isEmpty()
                         ? getDefaultEditor()
                         : gui_editor;

    QStringList cmdList = buildEditorCommand(editor) << editor << file_name;
    int exitCode = QProcess::execute("/bin/sh", {"-c", cmdList.join(' ')});
    if (exitCode != 0) {
        QMessageBox::warning(this, tr("Error"), tr("Editor command failed with code %1").arg(exitCode));
    }
    refreshIfFileChanged();
    watchFile(file_name);
}

QString MainWindow::getDefaultEditor()
{
    QProcess proc;
    proc.start("xdg-mime", {"query", "default", "text/plain"});
    if (!proc.waitForFinished(3000) || proc.exitCode() != 0) {
        qWarning() << "xdg-mime failed to query default editor";
        return "nano"; // Fallback to nano
    }
    QString default_editor = proc.readAllStandardOutput().trimmed();
    QString desktop_file
        = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, default_editor, QStandardPaths::LocateFile);

    QFile file(desktop_file);
    if (desktop_file.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return "nano"; // Fallback to nano
    }

    QTextStream in(&file);
    QString line;
    while (in.readLineInto(&line)) {
        if (line.startsWith("Exec=")) {
            // Extract the command from the Exec line and remove common desktop file parameters
            static const QRegularExpression execPrefixPattern("^Exec=");
            static const QRegularExpression paramPattern("(%[a-zA-Z]|%[a-zA-Z]{1,2}|-{1,2}[a-zA-Z-]+)\\b");
            return line.remove(execPrefixPattern).remove(paramPattern).trimmed();
        }
    }

    return "nano"; // Fallback to nano
}

QStringList MainWindow::buildEditorCommand(const QString &editor)
{
    const bool isRoot = getuid() == 0;
    static const QRegularExpression elevatesPattern(R"((kate|kwrite|featherpad|code|codium)$)");
    static const QRegularExpression cliPattern(R"(nano|vi|vim|nvim|micro|emacs)");
    const bool isEditorThatElevates = elevatesPattern.match(editor).hasMatch();
    const bool isCliEditor = cliPattern.match(editor).hasMatch();

    QStringList editorCommands;
    if (isRoot && isEditorThatElevates) {
        editorCommands << "pkexec --user $(logname)";
    } else if (!QFileInfo(file_name).isWritable() && !isEditorThatElevates) {
        editorCommands << "pkexec";
    }

    editorCommands << "env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY";

    if (isCliEditor) {
        editorCommands << "x-terminal-emulator -e";
    }

    return editorCommands;
}

void MainWindow::handleFileChanged(const QString &path)
{
    if (path != file_name) {
        return;
    }

    refreshIfFileChanged();
    watchFile(file_name);
}

void MainWindow::handleDirectoryChanged(const QString &path)
{
    const QString currentDirectory = QFileInfo(file_name).absolutePath();
    if (path != currentDirectory) {
        return;
    }

    refreshIfFileChanged();

    if (QFile::exists(file_name)) {
        watchFile(file_name);
    }
}

void MainWindow::refreshIfFileChanged()
{
    QFileInfo info(file_name);
    if (!info.exists()) {
        return;
    }

    readFile(file_name);
    setGui();
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

    const QString directoryPath = QFileInfo(path).absolutePath();
    if (!directoryPath.isEmpty() && !fileWatcher.directories().contains(directoryPath)) {
        if (!fileWatcher.addPath(directoryPath)) {
            qWarning() << "Failed to add watch for directory:" << directoryPath;
        }
    }
}
