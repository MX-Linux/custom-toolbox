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
      file_location {"/etc/custom-toolbox"},
      lang(locale.name())
{
    ui->setupUi(this);
    set_connections();
    connect(&file_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::handle_file_changed);
    connect(&file_watcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::handle_directory_changed);

    // Set up debounce timer for file change events
    file_reload_timer.setSingleShot(true);
    file_reload_timer.setInterval(200); // 200ms debounce delay
    connect(&file_reload_timer, &QTimer::timeout, this, &MainWindow::refresh_if_file_changed);

    if (arg_parser.isSet("remove-checkbox")) {
        ui->checkBoxStartup->hide();
    }

    setWindowFlags(Qt::Window); // Enable close, minimize, and maximize buttons
    setup();

    const QStringList arg_list = arg_parser.positionalArguments();
    file_name = !arg_list.isEmpty() && QFile(arg_list.first()).exists() ? arg_list.first() : get_file_name();

    read_file(file_name);
    watch_file(file_name);
    set_gui();
}

MainWindow::~MainWindow()
{
    delete ui;
}

QIcon MainWindow::find_icon(const QString &icon_name)
{
    // Check cache first
    if (icon_cache.contains(icon_name)) {
        return icon_cache[icon_name];
    }

    static const QRegularExpression re(R"(\.(png|svg|xpm)$)");
    static const QStringList extensions {".png", ".svg", ".xpm"};
    static const QStringList search_paths {QDir::homePath() + "/.local/share/icons/",
                                          "/usr/share/pixmaps/",
                                          "/usr/local/share/icons/",
                                          "/usr/share/icons/",
                                          "/usr/share/icons/hicolor/scalable/apps/",
                                          "/usr/share/icons/hicolor/48x48/apps/",
                                          "/usr/share/icons/Adwaita/48x48/legacy/"};

    // Helper function to search for icon in all paths
    static const auto search_in_paths = [](const QString &name) -> QIcon {
        for (const auto &path : search_paths) {
            const QString full_path = QDir(path).filePath(name);
            if (QFile::exists(full_path)) {
                QIcon icon(full_path);
                if (!icon.isNull()) {
                    return icon;
                }
            }
        }
        return QIcon();
    };

    // Initialize default terminal icon once when needed
    static const auto get_default_icon = []() {
        static QIcon default_icon;
        if (!default_icon.isNull()) {
            return default_icon;
        }

        const QString default_icon_name = "utilities-terminal";
        default_icon = QIcon::fromTheme(default_icon_name);
        if (!default_icon.isNull()) {
            return default_icon;
        }

        for (const auto &ext : extensions) {
            default_icon = search_in_paths(default_icon_name + ext);
            if (!default_icon.isNull()) {
                return default_icon;
            }
        }
        return QIcon();
    };

    // Handle empty or default icon name
    if (icon_name.isEmpty() || icon_name == "utilities-terminal") {
        QIcon result = get_default_icon();
        icon_cache[icon_name] = result;
        return result;
    }

    // Handle absolute paths
    if (QFileInfo::exists(icon_name) && QFileInfo(icon_name).isAbsolute()) {
        QIcon result(icon_name);
        icon_cache[icon_name] = result;
        return result;
    }

    // Try themed icon first
    QString name_no_ext = icon_name;
    name_no_ext.remove(re);

    if (!icon_theme.isEmpty()) {
        QIcon::setThemeName(icon_theme);
    }

    QIcon icon = QIcon::fromTheme(name_no_ext);
    if (!icon.isNull()) {
        icon_cache[icon_name] = icon;
        return icon;
    }

    // Try original name
    icon = search_in_paths(icon_name);
    if (!icon.isNull()) {
        icon_cache[icon_name] = icon;
        return icon;
    }

    // Try with each extension
    for (const auto &ext : extensions) {
        icon = search_in_paths(name_no_ext + ext);
        if (!icon.isNull()) {
            icon_cache[icon_name] = icon;
            return icon;
        }
    }

    // Fall back to the default icon if nothing else was found.
    // Note: Default icons are not cached to allow future resolution of actual icons
    return get_default_icon();
}

// Strip %f, %F, %U, etc. if exec expects a file name since it's called without an argument from this launcher.
void MainWindow::fix_exec_item(QString *item)
{
    static const QRegularExpression exec_pattern(QStringLiteral(R"( %[a-zA-Z])"));
    item->remove(exec_pattern);
}

void MainWindow::fix_name_item(QString *item)
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
    hide_gui = settings.value("hideGUI", false).toBool();
    min_height = qBound(300, settings.value("min_height").toInt(), 3000);
    min_width = qBound(300, settings.value("min_width").toInt(), 3000);
    gui_editor = settings.value("gui_editor").toString();
    fixed_number_col = qBound(0, settings.value("fixed_number_columns", 0).toInt(), 20);
    const int size = qBound(8, settings.value("icon_size", default_icon_size).toInt(), 1024);
    icon_size = {size, size};
}

// Add buttons and resize GUI
void MainWindow::set_gui()
{
    clear_grid_layout();
    adjustSize();
    setMinimumSize(min_width, min_height);

    QSettings settings(QApplication::organizationName(),
                       QApplication::applicationName() + '_' + QFileInfo(file_name).baseName());
    const QSize old_size = size();
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
        if (isMaximized()) { // Add option to resize if maximized
            resize(old_size);
            center_window();
        }
    }
    add_buttons(category_map);

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
    if (hide_gui || cmd.startsWith("pkexec")) {
        hide();
        const int exit_code = system(cmd.toUtf8());
        if (exit_code != 0) {
            QMessageBox::warning(this, tr("Execution Error"), tr("Failed to execute command: %1").arg(cmd));
        }
        show();
    } else {
        QStringList arguments = QProcess::splitCommand(cmd);
        const QString program = arguments.takeFirst();
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
    if (first_run || event->oldSize().width() == event->size().width() || fixed_number_col != 0) {
        first_run = false;
        return;
    }

    const int item_size = 200; // Determined through trial and error
    const int new_count = width() / item_size;

    if (new_count == col_count || (new_count >= max_elements && col_count == max_elements)) {
        return;
    }

    col_count = new_count;

    if (ui->textSearch->text().isEmpty()) {
        add_buttons(category_map);
    } else {
        text_search_text_changed(ui->textSearch->text());
    }
}

// Select .list file to open
QString MainWindow::get_file_name()
{
    while (true) {
        const QString file_name
            = QFileDialog::getOpenFileName(this, tr("Open List File"), file_location, tr("List Files (*.list)"));
        if (file_name.isEmpty()) {
            QMessageBox::critical(this, tr("File Selection Error"), tr("No file selected. Application will now exit."));
            exit(EXIT_FAILURE);
        }
        if (QFile::exists(file_name)) {
            return file_name;
        } else {
            const auto user_choice = QMessageBox::critical(this, tr("File Open Error"),
                                                           tr("Could not open file. Do you want to try again?"),
                                                           QMessageBox::Yes | QMessageBox::No);
            if (user_choice == QMessageBox::No) {
                exit(EXIT_FAILURE);
            }
        }
    }
}

// Find the .desktop file for the given app name
QString MainWindow::get_desktop_file_name(const QString &app_name) const
{
    // Check cache first
    if (desktop_file_cache.contains(app_name)) {
        return desktop_file_cache[app_name];
    }

    // Search for .desktop files in standard applications locations
    const QStringList searchPaths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    // Search for .desktop file in each path
    const QString desktop_file_name = app_name + ".desktop";
    QString result;

    for (const QString &searchPath : searchPaths) {
        const QString full_path = QDir(searchPath).absoluteFilePath(desktop_file_name);
        if (QFile::exists(full_path)) {
            result = full_path;
            break;
        }
    }

    // If .desktop file not found, fallback to finding the executable
    if (result.isEmpty()) {
        const QString executable_path = QStandardPaths::findExecutable(app_name, {default_path});
        result = !executable_path.isEmpty() ? QFileInfo(executable_path).fileName() : QString();
    }

    // Cache the result (even if empty to avoid repeated failed lookups)
    desktop_file_cache[app_name] = result;
    return result;
}

// Return the app info needed for the button
MainWindow::ItemInfo MainWindow::get_desktop_file_info(const QString &file_name)
{
    ItemInfo item;

    // If not a .desktop file, initialize all fields explicitly
    if (!file_name.endsWith(".desktop")) {
        item.category.clear();
        item.name = file_name;
        item.comment.clear();
        item.icon_name = file_name;
        item.exec = file_name;
        item.terminal = true;
        item.root = false;
        item.user = false;
        return item;
    }

    QFile file(file_name);
    if (!file.open(QFile::Text | QFile::ReadOnly)) {
        return {};
    }
    const QString text = file.readAll();
    file.close();

    // Helper lambda to search for a pattern and extract the first capture group
    auto match_pattern = [&text](const QString &pattern) -> QString {
        QRegularExpression re(pattern, QRegularExpression::MultilineOption);
        const auto match = re.match(text);
        return match.hasMatch() ? match.captured(1) : QString();
    };

    // Function to attempt matching localized fields first, then fall back to non-localized
    auto match_localized_field = [&](const QString &field) -> QString {
        QString value = match_pattern("^" + field + "\\[" + lang + "\\]=(.*)$");
        if (value.isEmpty()) {
            value = match_pattern("^" + field + "\\[" + lang.section('_', 0, 0) + "\\]=(.*)$");
        }
        if (value.isEmpty()) {
            value = match_pattern("^" + field + "=(.*)$");
        }
        return value;
    };

    item.category.clear(); // Will be set by caller in process_line()
    item.name = match_localized_field("Name").remove(QRegularExpression("^MX "));
    item.comment = match_localized_field("Comment");
    item.icon_name = match_pattern("^Icon=(.*)$");
    item.exec = match_pattern("^Exec=(.*)$");
    item.terminal = match_pattern("^Terminal=(.*)$").toLower() == "true";
    item.root = false; // Will be set by caller based on keywords
    item.user = false; // Will be set by caller based on keywords

    return item;
}

void MainWindow::add_buttons(const QMultiMap<QString, ItemInfo> &map)
{
    clear_grid_layout();
    int col = 0;
    int row = 0;
    const int max_cols = fixed_number_col != 0 ? fixed_number_col : width() / 200;

    for (const auto &category : map.uniqueKeys()) {
        if (!map.values(category).isEmpty()) {
            add_category_label(category, row, col);

            for (const auto &item : map.values(category)) {
                add_item_button(item, row, col, max_cols);
            }
        }
        add_empty_row_if_needed(category, map, row, col);
    }
    ui->gridLayout_btn->setRowStretch(row, 1);
}

void MainWindow::add_category_label(const QString &category, int &row, int &col)
{
    auto *label = new QLabel(category, this);
    QFont font;
    font.setBold(true);
    font.setUnderline(true);
    label->setFont(font);
    ui->gridLayout_btn->addWidget(label, ++row, col);
    ui->gridLayout_btn->setRowStretch(++row, 0);
}

void MainWindow::add_item_button(const ItemInfo &item, int &row, int &col, int max_cols)
{
    col_count = std::max(col_count, col + 1);
    QString name = item.name;
    QString cmd = item.exec;
    fix_name_item(&name);
    fix_exec_item(&cmd);

    auto *btn = new FlatButton(name);
    btn->setIconSize(icon_size);
    btn->setToolTip(item.comment);
    const QIcon icon = find_icon(item.icon_name);
    btn->setIcon(icon.isNull() ? QIcon::fromTheme("utilities-terminal") : icon);
    ui->gridLayout_btn->addWidget(btn, row, col);
    ui->gridLayout_btn->setRowStretch(row, 0);

    prepare_command(item, cmd);
    btn->setProperty("cmd", cmd);
    connect(btn, &QPushButton::clicked, this, &MainWindow::btn_clicked);

    if (++col >= max_cols) {
        col = 0;
        ++row;
    }
}

void MainWindow::prepare_command(const ItemInfo &item, QString &cmd)
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

void MainWindow::add_empty_row_if_needed(const QString &category, const QMultiMap<QString, ItemInfo> &map, int &row,
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

void MainWindow::center_window()
{
    const QRect screen_geometry = QApplication::primaryScreen()->geometry();
    const int x = (screen_geometry.width() - width()) / 2;
    const int y = (screen_geometry.height() - height()) / 2;
    move(x, y);
}

// Remove all items from the grid layout
void MainWindow::clear_grid_layout()
{
    QLayoutItem *child_item {};
    while ((child_item = ui->gridLayout_btn->takeAt(0)) != nullptr) {
        // Delete the widget and the layout item
        delete child_item->widget();
        delete child_item;
    }
}

void MainWindow::process_line(const QString &line)
{
    if (line.isEmpty()) {
        return;
    }
    const int split_pos = line.indexOf('=');
    const QString key = (split_pos > 0) ? line.left(split_pos).trimmed() : line.trimmed();
    if (key.isEmpty()) {
        return;
    }
    const QString value = (split_pos > 0) ? QString(line.mid(split_pos + 1)).trimmed().remove('"') : QString();
    const QString lower_key = key.toLower();
    if (lower_key == "category") {
        categories.append(value);
    } else if (lower_key == "theme") {
        icon_theme = value;
    } else {
        const QStringList key_tokens = key.split(' ');
        if (key_tokens.isEmpty()) {
            return;
        }

        const QString desktop_file = get_desktop_file_name(key_tokens.first());
        if (!desktop_file.isEmpty()) {
            ItemInfo info = get_desktop_file_info(desktop_file);
            if (key_tokens.size() > 1) {
                const bool has_root = key_tokens.contains("root");
                const bool has_user = key_tokens.contains("user");
                const bool has_alias = key_tokens.contains("alias");

                info.root = has_root;
                info.user = has_user;

                if (has_alias) {
                    const int alias_index = key_tokens.indexOf("alias");
                    if (alias_index >= 0 && alias_index + 1 < key_tokens.size()) {
                        info.name = key_tokens.mid(alias_index + 1).join(' ').trimmed().remove('\'').remove('"');
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
void MainWindow::read_file(const QString &file_name)
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

    const QString name = extract_pattern(text, "Name");
    const QString comment = extract_pattern(text, "Comment");

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
            process_line(lineView.toString());
        }

        pos = endPos + 1;
    }
}

QString MainWindow::extract_pattern(const QString &text, const QString &key)
{
    const QString pattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang);
    const QString fallback_pattern = QStringLiteral("^%1=(.*)$").arg(key);
    const QString lang_short_pattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang.section('_', 0, 0));

    // Check full language pattern
    if (!regex_cache.contains(pattern)) {
        regex_cache[pattern] = QRegularExpression(pattern, QRegularExpression::MultilineOption);
    }
    QRegularExpressionMatch match = regex_cache[pattern].match(text);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    // Check short language pattern
    if (!regex_cache.contains(lang_short_pattern)) {
        regex_cache[lang_short_pattern] = QRegularExpression(lang_short_pattern, QRegularExpression::MultilineOption);
    }
    match = regex_cache[lang_short_pattern].match(text);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    // Check fallback pattern
    if (!regex_cache.contains(fallback_pattern)) {
        regex_cache[fallback_pattern] = QRegularExpression(fallback_pattern, QRegularExpression::MultilineOption);
    }
    match = regex_cache[fallback_pattern].match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

void MainWindow::set_connections()
{
    connect(ui->checkBoxStartup, &QPushButton::clicked, this, &MainWindow::checkbox_startup_clicked);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::push_about_clicked);
    connect(ui->pushCancel, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(ui->pushEdit, &QPushButton::clicked, this, &MainWindow::push_edit_clicked);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::push_help_clicked);
    connect(ui->textSearch, &QLineEdit::textChanged, this, &MainWindow::text_search_text_changed);
}

void MainWindow::push_about_clicked()
{
    hide();
    const QString about_text
        = QString("<p align=\"center\"><b><h2>%1</h2></b></p>"
                  "<p align=\"center\">%2 %3</p>"
                  "<p align=\"center\"><h3>%4</h3></p>"
                  "<p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p>"
                  "<p align=\"center\">%5<br /><br /></p>")
              .arg(windowTitle(), tr("Version:"), QApplication::applicationVersion(),
                   tr("Custom Toolbox is a tool used for creating a custom launcher"), tr("Copyright (c) MX Linux"));

    displayAboutMsgBox(tr("About %1").arg(windowTitle()), about_text, "/usr/share/doc/custom-toolbox/license.html",
                       tr("%1 License").arg(windowTitle()));
    show();
}

void MainWindow::push_help_clicked()
{
    const QString url = "/usr/share/doc/custom-toolbox/help.html";
    displayDoc(url, tr("%1 Help").arg(windowTitle()));
}

void MainWindow::text_search_text_changed(const QString &search_text)
{
    // Early return for empty search - show all items
    if (search_text.isEmpty()) {
        add_buttons(category_map);
        return;
    }

    // Use QStringView for efficient string matching
    const QStringView search_view(search_text);

    // Create a lambda function to check if an item matches the search text
    auto matches_search_text = [&search_view](const ItemInfo &item) {
        return QStringView(item.name).contains(search_view, Qt::CaseInsensitive)
               || QStringView(item.comment).contains(search_view, Qt::CaseInsensitive)
               || QStringView(item.category).contains(search_view, Qt::CaseInsensitive);
    };

    // Filter category_map to only include items that match the search text
    QMultiMap<QString, ItemInfo> filtered_map;
    for (const auto &item : category_map) {
        if (matches_search_text(item)) {
            filtered_map.insert(item.category, item);
        }
    }

    // Update the buttons with the filtered map or the original map if no matches found
    add_buttons(filtered_map.isEmpty() ? category_map : filtered_map);
}

// Add a .desktop file to the ~/.config/autostart
void MainWindow::checkbox_startup_clicked(bool checked)
{
    const QString file_name
        = QDir::homePath() + "/.config/autostart/" + custom_name + ".desktop"; // Same name as .list file
    if (checked) {
        if (QFile file(file_name); file.open(QFile::WriteOnly | QFile::Text)) {
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
            QMessageBox::critical(this, tr("File Open Error"), tr("Could not write file: %1").arg(file_name));
        }
    } else {
        if (!QFile::remove(file_name)) {
            QMessageBox::warning(this, tr("File Removal Error"), tr("Could not remove file: %1").arg(file_name));
        }
    }
}

void MainWindow::push_edit_clicked()
{
    const QString editor
        = gui_editor.isEmpty() || QStandardPaths::findExecutable(gui_editor, {default_path}).isEmpty()
              ? get_default_editor()
              : gui_editor;

    const QStringList cmd_list = build_editor_command(editor) << editor << file_name;
    const int exit_code = QProcess::execute("/bin/sh", {"-c", cmd_list.join(' ')});
    if (exit_code != 0) {
        QMessageBox::warning(this, tr("Error"), tr("Editor command failed with code %1").arg(exit_code));
    }

    // Stop any pending debounced refresh and refresh immediately for instant feedback
    file_reload_timer.stop();
    refresh_if_file_changed();
    watch_file(file_name);
}

QString MainWindow::get_default_editor()
{
    QProcess proc;
    proc.start("xdg-mime", {"query", "default", "text/plain"});
    if (!proc.waitForFinished(3000) || proc.exitCode() != 0) {
        qWarning() << "xdg-mime failed to query default editor";
        return "nano"; // Fallback to nano
    }
    const QString default_editor = proc.readAllStandardOutput().trimmed();
    const QString desktop_file
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

QStringList MainWindow::build_editor_command(const QString &editor)
{
    const bool is_root = getuid() == 0;
    static const QRegularExpression elevates_pattern(R"((kate|kwrite|featherpad|code|codium)$)");
    static const QRegularExpression cli_pattern(R"(nano|vi|vim|nvim|micro|emacs)");
    const bool is_editor_that_elevates = elevates_pattern.match(editor).hasMatch();
    const bool is_cli_editor = cli_pattern.match(editor).hasMatch();

    QStringList editor_commands;
    if (is_root && is_editor_that_elevates) {
        editor_commands << "pkexec --user $(logname)";
    } else if (!QFileInfo(file_name).isWritable() && !is_editor_that_elevates) {
        editor_commands << "pkexec";
    }

    editor_commands << "env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY";

    if (is_cli_editor) {
        editor_commands << "x-terminal-emulator -e";
    }

    return editor_commands;
}

void MainWindow::handle_file_changed(const QString &path)
{
    if (path != file_name) {
        return;
    }

    // Re-add watch immediately to avoid missing further changes
    watch_file(file_name);

    // Debounce: restart timer on each change to avoid redundant reloads
    file_reload_timer.start();
}

void MainWindow::handle_directory_changed(const QString &path)
{
    const QString current_directory = QFileInfo(file_name).absolutePath();
    if (path != current_directory) {
        return;
    }

    // Re-add watch if file exists to avoid missing further changes
    if (QFile::exists(file_name)) {
        watch_file(file_name);
    }

    // Debounce: restart timer on each change to avoid redundant reloads
    file_reload_timer.start();
}

void MainWindow::refresh_if_file_changed()
{
    const QFileInfo info(file_name);
    if (!info.exists()) {
        return;
    }

    read_file(file_name);
    set_gui();
}

void MainWindow::watch_file(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    if (file_watcher.files().contains(path)) {
        file_watcher.removePath(path);
    }

    if (QFile::exists(path)) {
        if (!file_watcher.addPath(path)) {
            qWarning() << "Failed to add watch for file:" << path;
        }
    }

    const QString directory_path = QFileInfo(path).absolutePath();
    if (!directory_path.isEmpty() && !file_watcher.directories().contains(directory_path)) {
        if (!file_watcher.addPath(directory_path)) {
            qWarning() << "Failed to add watch for directory:" << directory_path;
        }
    }
}
