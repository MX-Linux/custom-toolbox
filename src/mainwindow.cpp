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
#include <QFile>
#include <QFileDialog>
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
#include <unistd.h>



MainWindow::MainWindow(const QCommandLineParser &arg_parser, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow),
      file_location {Config::ConfigDir},
      lang(locale.name()),
      remove_startup_checkbox {arg_parser.isSet("remove-checkbox")}
{
    ui->setupUi(this);
    set_connections();
    connect(&file_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::handle_file_changed);
    connect(&file_watcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::handle_directory_changed);

    // Set up debounce timer for file change events
    file_reload_timer.setSingleShot(true);
    file_reload_timer.setInterval(200);
    connect(&file_reload_timer, &QTimer::timeout, this, &MainWindow::refresh_if_file_changed);

    if (remove_startup_checkbox) {
        ui->checkBoxStartup->hide();
    }

    setWindowFlags(Qt::Window); // Enable close, minimize, and maximize buttons
    setup();

    const QStringList arg_list = arg_parser.positionalArguments();
    if (arg_list.isEmpty()) {
        file_name = get_file_name();
    } else if (QFile::exists(arg_list.first())) {
        file_name = arg_list.first();
    } else {
        QMessageBox::critical(this, tr("File Not Found"), tr("The file %1 does not exist.").arg(arg_list.first()));
        exit(EXIT_FAILURE);
    }

    read_file(file_name);
    watch_file(file_name);
    set_gui();
}

MainWindow::~MainWindow()
{
    delete ui;
}

QIcon MainWindow::find_icon(const QString &icon_name) const
{
    return IconLoader::loadIcon(icon_name);
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

    QSettings settings(Config::ConfigFile, QSettings::NativeFormat);
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
    setMinimumSize(min_width, min_height);

    QSettings settings(QApplication::organizationName(),
                       QApplication::applicationName() + '_' + custom_name);
    const QSize old_size = size();
    if (!geometry_restored) {
        geometry_restored = true;
        if (settings.contains("geometry")) {
            restoreGeometry(settings.value("geometry").toByteArray());
            if (isMaximized()) { // Add option to resize if maximized
                resize(old_size);
                center_window();
            }
        }
    }
    add_buttons(category_map);

    // Check if .desktop file is in autostart; same custom_name as .list file
    if (!remove_startup_checkbox
        && QFile::exists(QDir::homePath() + "/.config/autostart/" + custom_name + ".desktop")) {
        ui->checkBoxStartup->show();
        ui->checkBoxStartup->setChecked(true);
    }
    ui->textSearch->setFocus();
    ui->pushCancel->setDefault(true); // Otherwise some other button might be default
    ui->pushCancel->setDefault(false);
}

void MainWindow::run_synchronous(const QString &cmd, bool use_shell)
{
    if (hide_gui) {
        hide();
    }

    QProcess proc;
    if (use_shell) {
        proc.start("/bin/sh", {"-c", cmd});
    } else {
        QStringList arguments = QProcess::splitCommand(cmd);
        const QString program = arguments.takeFirst();
        proc.start(program, arguments);
    }

    if (!proc.waitForStarted()) {
        QMessageBox::warning(this, tr("Execution Error"), tr("Failed to start command: %1").arg(cmd));
    } else {
        while (!proc.waitForFinished(100)) {
            QApplication::processEvents();
        }
        if (proc.exitCode() != 0) {
            QMessageBox::warning(this, tr("Execution Error"), tr("Failed to execute command: %1").arg(cmd));
        }
    }

    if (hide_gui) {
        show();
    }
}

void MainWindow::btn_clicked()
{
    const auto *button = sender();
    if (!button) {
        return;
    }
    const QString cmd = button->property("cmd").toString();
    if (cmd.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Execution Error"), tr("Command is empty. Cannot execute."));
        return;
    }

    // pkexec requires shell for variable expansion; hide_gui requires synchronous execution
    if (cmd.startsWith("pkexec") || hide_gui) {
        run_synchronous(cmd, cmd.startsWith("pkexec"));
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
                       QApplication::applicationName() + '_' + custom_name);
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (first_run || event->oldSize().width() == event->size().width() || fixed_number_col != 0) {
        first_run = false;
        return;
    }

    const int item_size = 200; // Determined through trial and error
    const int new_count = qMax(1, width() / item_size);

    if (new_count == col_count) {
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
        QDirIterator it(searchPath, {desktop_file_name}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            result = it.next();
            break;
        }
    }

    // Fallback: enumerate /usr/share/applications and match by Exec= first token
    // or reverse-DNS suffix (e.g. "ghex" -> "org.gnome.GHex.desktop").
    if (result.isEmpty()) {
        build_desktop_file_index();
        const auto it = desktop_file_index.constFind(app_name.toLower());
        if (it != desktop_file_index.constEnd()) {
            result = it.value();
        }
    }

    // If still not found, fallback to finding the executable
    if (result.isEmpty()) {
        const QString executable_path = QStandardPaths::findExecutable(app_name, {default_path});
        result = !executable_path.isEmpty() ? QFileInfo(executable_path).fileName() : QString();
    }

    // Cache the result (even if empty to avoid repeated failed lookups)
    desktop_file_cache[app_name] = result;
    return result;
}

// Build a lazy fallback index of .desktop files keyed by short names.
// Priority (highest wins): Exec= first token > reverse-DNS suffix.
// Used when an exact "<name>.desktop" match fails so that .list entries like
// "ghex" still resolve to "org.gnome.GHex.desktop".
void MainWindow::build_desktop_file_index() const
{
    QMutexLocker locker(&desktop_file_index_mutex);
    if (desktop_file_index_built) {
        return;
    }
    desktop_file_index_built = true;

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
    desktop_file_index = std::move(by_suffix);
    for (auto it = by_exec.constBegin(); it != by_exec.constEnd(); ++it) {
        desktop_file_index.insert(it.key(), it.value());
    }
}

// Return the app info needed for the button
ItemInfo MainWindow::get_desktop_file_info(const QString &file_name) const
{
    ItemInfo item;

    // If not a .desktop file, initialize all fields explicitly
    if (!file_name.endsWith(".desktop")) {
        item.name = file_name;
        item.icon_name = file_name;
        item.exec = file_name;
        item.terminal = true;
        return item;
    }

    QFile file(file_name);
    if (!file.open(QFile::Text | QFile::ReadOnly)) {
        return {};
    }
    const QString text = file.readAll();
    file.close();

    static const QRegularExpression mx_prefix("^MX ");
    item.name = LauncherParser::extract_localized_value(text, "Name", lang).remove(mx_prefix);
    item.comment = LauncherParser::extract_localized_value(text, "Comment", lang);
    item.icon_name = LauncherParser::extract_localized_value(text, "Icon", lang);
    item.exec = LauncherParser::extract_localized_value(text, "Exec", lang);
    item.terminal = LauncherParser::extract_localized_value(text, "Terminal", lang).toLower() == "true";

    return item;
}

void MainWindow::add_buttons(const QMultiMap<QString, ItemInfo> &map)
{
    clear_grid_layout();
    int col = 0;
    int row = 0;
    const int max_cols = fixed_number_col != 0 ? fixed_number_col : qMax(1, width() / 200);
    col_count = max_cols;

    QString prev_category;
    for (auto it = map.constBegin(); it != map.constEnd();) {
        const QString &category = it.key();

        // Add category label on first encounter
        if (category != prev_category) {
            if (!prev_category.isEmpty()) {
                add_empty_row_if_needed(prev_category, map, row, col);
            }
            add_category_label(category, row, col);
            prev_category = category;
        }

        add_item_button(it.value(), row, col, max_cols);
        ++it;
    }
    if (!prev_category.isEmpty()) {
        add_empty_row_if_needed(prev_category, map, row, col);
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

void MainWindow::prepare_command(const ItemInfo &item, QString &cmd) const
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

// Open the .list file and process it
void MainWindow::read_file(const QString &file_name)
{
    if (!QFile::exists(file_name)) {
        QMessageBox::critical(this, tr("File Not Found"), tr("The file %1 does not exist.").arg(file_name));
        return;
    }

    // Detect INI format. QSettings puts [General] keys at root scope, so we
    // identify the new format by the presence of the [Categories] section.
    QSettings ini_settings(file_name, QSettings::IniFormat);
    const bool is_ini = ini_settings.status() == QSettings::NoError
                        && ini_settings.contains(QStringLiteral("Categories/list"));

    LauncherParser::ParseResult parsed;
    if (is_ini) {
        parsed = LauncherParser::parse_ini(ini_settings, lang);
    } else {
        QFile file(file_name);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            QMessageBox::critical(this, tr("File Open Error"), tr("Could not open file: ") + file_name);
            return;
        }
        parsed = LauncherParser::parse(file.readAll(), lang);
    }

    if (parsed.items.isEmpty()) {
        QMessageBox::critical(this, tr("Parse Error"),
                              tr("The file %1 contains no recognizable launcher entries.").arg(file_name));
        return;
    }

    // Resolve items against installed .desktop files into a temporary map.
    // If nothing resolves, the launcher would be empty — bail without touching state.
    QMultiMap<QString, ItemInfo> new_map;
    for (const auto &p : parsed.items) {
        const QString desktop_file = get_desktop_file_name(p.app_name);
        if (desktop_file.isEmpty()) {
            continue;
        }

        ItemInfo info = get_desktop_file_info(desktop_file);
        info.root = p.root;
        info.user = p.user;
        info.terminal = info.terminal || p.terminal;
        if (!p.alias.isEmpty()) {
            info.name = p.alias;
        }
        info.category = p.category;
        new_map.insert(info.category, info);
    }

    if (new_map.isEmpty()) {
        QMessageBox::critical(this, tr("Parse Error"),
                              tr("None of the entries in %1 match an installed application.").arg(file_name));
        return;
    }

    // Swap state in.
    const QFileInfo file_info(file_name);
    custom_name = file_info.baseName();
    file_location = file_info.path();

    category_map = std::move(new_map);
    IconLoader::clearCache();
    desktop_file_cache.clear();
    desktop_file_index.clear();
    desktop_file_index_built = false;
    icon_theme = parsed.icon_theme;
    setWindowTitle(parsed.name);
    ui->commentLabel->setText(parsed.comment);

    QIcon::setThemeName(icon_theme.isEmpty() ? default_icon_theme : icon_theme);
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

    displayAboutMsgBox(tr("About %1").arg(windowTitle()), about_text, Config::LicenseFile,
                       tr("%1 License").arg(windowTitle()));
    show();
}

void MainWindow::push_help_clicked()
{
    displayHelpDoc(Config::HelpFile, tr("%1 Help").arg(windowTitle()));
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

    // Update the buttons with the filtered map (empty map shows no results)
    add_buttons(filtered_map);
}

// Add a .desktop file to the ~/.config/autostart
void MainWindow::checkbox_startup_clicked(bool checked)
{
    const QString autostart_dir = QDir::homePath() + "/.config/autostart/";
    const QString file_name = autostart_dir + custom_name + ".desktop"; // Same name as .list file
    if (checked) {
        // Ensure the autostart directory exists
        if (!QDir(autostart_dir).exists() && !QDir().mkpath(autostart_dir)) {
            QMessageBox::critical(this, tr("Directory Creation Error"),
                                  tr("Could not create directory: %1").arg(autostart_dir));
            ui->checkBoxStartup->setChecked(false);
            return;
        }

        if (QFile file(file_name); file.open(QFile::WriteOnly | QFile::Text)) {
            auto escape_desktop_string = [](QString value) {
                value.replace('\\', "\\\\");
                value.replace('\n', "\\n");
                value.replace('\r', "\\r");
                value.replace('\t', "\\t");
                return value;
            };
            auto quote_exec_arg = [](QString value) {
                value.replace('\\', "\\\\");
                value.replace('"', "\\\"");
                value.replace('$', "\\$");
                value.replace('`', "\\`");
                return '"' + value + '"';
            };

            const QString list_path = QDir(file_location).filePath(custom_name + ".list");
            QTextStream out(&file);
            out << "[Desktop Entry]\n"
                << "Name=" << escape_desktop_string(windowTitle()) << '\n'
                << "Comment=" << escape_desktop_string(ui->commentLabel->text()) << '\n'
                << "Exec=custom-toolbox " << quote_exec_arg(list_path) << '\n'
                << "Terminal=false\n"
                << "Type=Application\n"
                << "Icon=custom-toolbox\n"
                << "Categories=XFCE;System\n"
                << "StartupNotify=false";
        } else {
            QMessageBox::critical(this, tr("File Open Error"), tr("Could not write file: %1").arg(file_name));
            ui->checkBoxStartup->setChecked(false);
        }
    } else {
        if (!QFile::remove(file_name)) {
            QMessageBox::warning(this, tr("File Removal Error"), tr("Could not remove file: %1").arg(file_name));
        }
    }
}

void MainWindow::push_edit_clicked()
{
    const QString gui_editor_program = QProcess::splitCommand(gui_editor).value(0);
    const bool use_default_editor = gui_editor.isEmpty() || gui_editor_program.isEmpty()
                                    || QStandardPaths::findExecutable(gui_editor_program, {default_path}).isEmpty();
    const QString editor = use_default_editor ? get_default_editor() : gui_editor;
    const QStringList editor_parts = QProcess::splitCommand(editor);
    if (editor_parts.isEmpty()) {
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

    QStringList cmd_parts = build_editor_prefix(editor);
    for (const auto &part : editor_parts) {
        cmd_parts << shell_quote(part);
    }
    cmd_parts << shell_quote(file_name);

    const int exit_code = QProcess::execute("/bin/sh", {"-c", cmd_parts.join(' ')});
    if (exit_code != 0) {
        QMessageBox::warning(this, tr("Error"), tr("Editor command failed with code %1").arg(exit_code));
    }

    // Stop any pending debounced refresh and refresh immediately for instant feedback
    file_reload_timer.stop();
    refresh_if_file_changed();
    watch_file(file_name);
}

QString MainWindow::get_default_editor() const
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
            static const QRegularExpression paramPattern(" %[a-zA-Z]");
            return line.remove(execPrefixPattern).remove(paramPattern).trimmed();
        }
    }

    return "nano"; // Fallback to nano
}

QStringList MainWindow::build_editor_prefix(const QString &editor) const
{
    const bool is_root = getuid() == 0;
    static const QRegularExpression elevates_pattern(R"(\b(kate|kwrite|featherpad|code|codium)$)");
    static const QRegularExpression cli_pattern(R"(\b(nano|vi|vim|nvim|micro|emacs)\b)");
    const QString editor_program = QProcess::splitCommand(editor).value(0);
    const QString editor_name = QFileInfo(editor_program).baseName();
    const bool is_editor_that_elevates = elevates_pattern.match(editor_name).hasMatch();
    const bool is_cli_editor = cli_pattern.match(editor_name).hasMatch();

    QStringList prefix;
    if (is_root && is_editor_that_elevates) {
        prefix << "pkexec --user $(logname)";
    } else if (!QFileInfo(file_name).isWritable() && !is_editor_that_elevates) {
        prefix << "pkexec";
    }

    prefix << "env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY";

    if (is_cli_editor) {
        prefix << "x-terminal-emulator -e";
    }

    return prefix;
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
    if (path != file_location) {
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
    if (!QFile::exists(file_name)) {
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

    if (!file_location.isEmpty() && !file_watcher.directories().contains(file_location)) {
        if (!file_watcher.addPath(file_location)) {
            qWarning() << "Failed to add watch for directory:" << file_location;
        }
    }
}
