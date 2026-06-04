/**********************************************************************
 *  customtoolbox.h
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
#pragma once

#include <QCommandLineParser>
#include <QDialog>
#include <QFileSystemWatcher>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QMultiMap>
#include <QMutex>
#include <QTimer>

#include "iteminfo.h"

namespace Ui
{
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(const QCommandLineParser &argParser, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void btnClicked();
    void pushAboutClicked();
    void pushEditClicked();
    void pushHelpClicked();
    void checkboxStartupClicked(bool checked);
    void textSearchTextChanged(const QString &text);

private:
    void closeEvent(QCloseEvent * /*unused*/) override;
    void resizeEvent(QResizeEvent *event) override;

    Ui::MainWindow *ui;
    QMultiMap<QString, ItemInfo> categoryMap;
    QSize iconSize;
    QString customName;
    QString fileLocation;
    QString fileName;
    QString guiEditor;
    QString iconTheme;
    const QString defaultIconTheme {QIcon::themeName()};
    QLocale locale;
    QString lang;
    QFileSystemWatcher fileWatcher;
    QTimer fileReloadTimer;
    bool firstRun {true};
    bool geometryRestored {};
    bool hideGui {};
    bool removeStartupCheckbox {};
    const QStringList defaultPath {qEnvironmentVariable("PATH").split(':') << "/usr/sbin"};
    int colCount {};
    int fixedNumberCol {};
    int minHeight {};
    int minWidth {};

    // Caches for performance optimization (mutable to allow caching in const methods)
    mutable QHash<QString, QString> desktopFileCache;
    mutable QHash<QString, QString> desktopFileIndex;
    mutable bool desktopFileIndexBuilt {false};
    mutable QMutex desktopFileIndexMutex;

    void buildDesktopFileIndex() const;
    [[nodiscard]] ItemInfo getDesktopFileInfo(const QString &fname) const;
    [[nodiscard]] QIcon findIcon(const QString &iconName) const;
    [[nodiscard]] QString getDefaultEditor() const;
    [[nodiscard]] QString getDesktopFileName(const QString &appName) const;
    [[nodiscard]] QString getFileName();
    [[nodiscard]] QStringList buildEditorPrefix(const QString &editor) const;
    [[nodiscard]] QString invokingUser() const;
    static void fixExecItem(QString *item);
    static void fixNameItem(QString *item);
    void addButtons(const QMultiMap<QString, ItemInfo> &map);
    void addCategoryLabel(const QString &category, int &row, int &col);
    void addEmptyRowIfNeeded(const QString &category, const QMultiMap<QString, ItemInfo> &map, int &row,
                                 int &col);
    void addItemButton(const ItemInfo &item, int &row, int &col, int maxCols);
    void centerWindow();
    void clearGridLayout();
    void prepareCommand(const ItemInfo &item, QString &cmd) const;
    void runSynchronous(const QString &cmd, bool useShell);
    void readFile(const QString &fname);
    void setConnections();
    void setGui();
    void setup();
    void handleDirectoryChanged(const QString &path);
    void handleFileChanged(const QString &path);
    void refreshIfFileChanged();
    void watchFile(const QString &path);
};
