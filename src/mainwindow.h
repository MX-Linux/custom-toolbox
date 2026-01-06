/**********************************************************************
 *  customtoolbox.h
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
#pragma once

#include <QCommandLineParser>
#include <QDialog>
#include <QFileSystemWatcher>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QMultiMap>
#include <QRegularExpression>
#include <QTimer>

namespace Ui
{
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(const QCommandLineParser &arg_parser, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void closeEvent(QCloseEvent * /*unused*/) override;
    void resizeEvent(QResizeEvent *event) override;
    void btn_clicked();
    void push_about_clicked();
    void push_edit_clicked();
    void push_help_clicked();
    void checkbox_startup_clicked(bool checked);
    void text_search_text_changed(const QString &text);

private:
    struct ItemInfo {
        QString category {};
        QString name {};
        QString comment {};
        QString icon_name {};
        QString exec {};
        bool terminal {};
        bool root {};
        bool user {};
    };

    Ui::MainWindow *ui;
    QMultiMap<QString, ItemInfo> category_map;
    QSize icon_size;
    QString custom_name;
    QString file_location;
    QString file_name;
    QString gui_editor;
    QString icon_theme;
    QLocale locale;
    QString lang;
    QStringList categories;
    QFileSystemWatcher file_watcher;
    QTimer file_reload_timer;
    bool first_run {true};
    bool hide_gui {};
    const QStringList default_path {qEnvironmentVariable("PATH").split(':') << "/usr/sbin"};
    int col_count {};
    int fixed_number_col {};
    int min_height {};
    int min_width {};

    // Caches for performance optimization (mutable to allow caching in const methods)
    mutable QHash<QString, QIcon> icon_cache;
    mutable QHash<QString, QString> desktop_file_cache;
    mutable QHash<QString, QRegularExpression> regex_cache;

    [[nodiscard]] ItemInfo get_desktop_file_info(const QString &file_name);
    [[nodiscard]] QIcon find_icon(const QString &icon_name);
    [[nodiscard]] QString extract_pattern(const QString &text, const QString &key);
    [[nodiscard]] QString get_default_editor();
    [[nodiscard]] QString get_desktop_file_name(const QString &app_name) const;
    [[nodiscard]] QString get_file_name();
    [[nodiscard]] QStringList build_editor_command(const QString &editor);
    static void fix_exec_item(QString *item);
    static void fix_name_item(QString *item);
    void add_buttons(const QMultiMap<QString, ItemInfo> &map);
    void add_category_label(const QString &category, int &row, int &col);
    void add_empty_row_if_needed(const QString &category, const QMultiMap<QString, ItemInfo> &map, int &row,
                                 int &col);
    void add_item_button(const ItemInfo &item, int &row, int &col, int max_cols);
    void center_window();
    void clear_grid_layout();
    void prepare_command(const ItemInfo &item, QString &cmd);
    void process_line(const QString &line);
    void read_file(const QString &file_name);
    void set_connections();
    void set_gui();
    void setup();
    void handle_directory_changed(const QString &path);
    void handle_file_changed(const QString &path);
    void refresh_if_file_changed();
    void watch_file(const QString &path);
};
