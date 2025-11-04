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
#include <QLocale>
#include <QMessageBox>
#include <QMultiMap>

class QIcon;

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
    void pushAbout_clicked();
    void pushEdit_clicked();
    void pushHelp_clicked();
    void checkBoxStartup_clicked(bool checked);
    void textSearch_textChanged(const QString &arg1);

private:
    struct ItemInfo {
        QString category;
        QString name;
        QString comment;
        QString icon_name;
        QString exec;
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
    QString lang = locale.name();
    QStringList categories;
    bool firstRun {true};
    bool hideGUI {};
    const QStringList defaultPath {qEnvironmentVariable("PATH").split(':') << "/usr/sbin"};
    int col_count {};
    int fixed_number_col {};
    int max_elements {};
    int min_height {};
    int min_width {};

    [[nodiscard]] ItemInfo getDesktopFileInfo(const QString &fileName);
    [[nodiscard]] QIcon findIcon(const QString &icon_name);
    [[nodiscard]] QString extractPattern(const QString &text, const QString &key);
    [[nodiscard]] QString getDefaultEditor();
    [[nodiscard]] QString getDesktopFileName(const QString &app_name) const;
    [[nodiscard]] QString getFileName();
    [[nodiscard]] QStringList buildEditorCommand(const QString &editor);
    static void fixExecItem(QString *item);
    static void fixNameItem(QString *item);
    void addButtons(const QMultiMap<QString, ItemInfo> &map);
    void addCategoryLabel(const QString &category, int &row, int &col);
    void addEmptyRowIfNeeded(const QString &category, const QMultiMap<QString, ItemInfo> &map, int &row, int &col);
    void addItemButton(const ItemInfo &item, int &row, int &col, int maxCols);
    void centerWindow();
    void clearGridLayout();
    void prepareCommand(const ItemInfo &item, QString &cmd);
    void processLine(const QString &line);
    void readFile(const QString &file_name);
    void setConnections();
    void setGui();
    void setup();
};
