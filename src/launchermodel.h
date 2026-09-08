/**********************************************************************
 * Copyright (C) 2017-2026 MX Authors
 *
 * This file is part of custom-toolbox.
 **********************************************************************/
#pragma once

#include <QAbstractListModel>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QQuickImageProvider>
#include <QTimer>

#include "iteminfo.h"

class LauncherIconProvider final : public QQuickImageProvider
{
public:
    LauncherIconProvider();

    void clear();
    void insert(const QString &key, const QIcon &icon);
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QHash<QString, QIcon> icons;
};

class LauncherModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString selectedCategory READ selectedCategory WRITE setSelectedCategory NOTIFY selectedCategoryChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QString title READ title NOTIFY launcherChanged)
    Q_PROPERTY(QString description READ description NOTIFY launcherChanged)
    Q_PROPERTY(QString customName READ customName NOTIFY launcherChanged)
    Q_PROPERTY(bool startupEnabled READ startupEnabled WRITE setStartupEnabled NOTIFY startupEnabledChanged)
    Q_PROPERTY(bool startupVisible READ startupVisible CONSTANT)
    Q_PROPERTY(QString reloadMessage READ reloadMessage NOTIFY reloadMessageChanged)
    Q_PROPERTY(int minimumWidth READ minimumWidth CONSTANT)
    Q_PROPERTY(int minimumHeight READ minimumHeight CONSTANT)
    Q_PROPERTY(int iconSize READ iconSize CONSTANT)
    Q_PROPERTY(int fixedNumberColumns READ fixedNumberColumns CONSTANT)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        CommentRole,
        CategoryRole,
        IconSourceRole,
        SourceIndexRole
    };
    Q_ENUM(Role)

    explicit LauncherModel(const QCommandLineParser &argParser, const QString &listFile,
                           LauncherIconProvider *iconProvider, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString search() const;
    void setSearch(const QString &value);
    [[nodiscard]] QString selectedCategory() const;
    void setSelectedCategory(const QString &value);
    [[nodiscard]] QStringList categories() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString description() const;
    [[nodiscard]] QString customName() const;
    [[nodiscard]] bool startupEnabled() const;
    void setStartupEnabled(bool enabled);
    [[nodiscard]] bool startupVisible() const;
    [[nodiscard]] QString reloadMessage() const;

    [[nodiscard]] int minimumWidth() const { return configuredMinimumWidth; }
    [[nodiscard]] int minimumHeight() const { return configuredMinimumHeight; }
    [[nodiscard]] int iconSize() const { return configuredIconSize; }
    [[nodiscard]] int fixedNumberColumns() const { return configuredColumns; }

    Q_INVOKABLE void launch(int sourceIndex);
    Q_INVOKABLE void edit();
    Q_INVOKABLE void openHelp();
    Q_INVOKABLE void openLicense();

signals:
    void searchChanged();
    void selectedCategoryChanged();
    void categoriesChanged();
    void launcherChanged();
    void startupEnabledChanged();
    void reloadMessageChanged();
    void errorOccurred(const QString &title, const QString &message);
    void hideRequested();
    void showRequested();

private:
    QVector<ItemInfo> allItems;
    QVector<int> visibleRows;
    QStringList categoryNames;
    LauncherIconProvider *iconProvider;
    QString searchText;
    QString selectedCategoryName;
    QString launcherTitle {QCoreApplication::translate("MainWindow", "Custom Toolbox")};
    QString launcherDescription {QCoreApplication::translate("MainWindow", "This is a custom launcher")};
    QString launcherCustomName;
    QString fileLocation;
    QString fileName;
    QString guiEditor;
    QString iconTheme;
    QString reloadStatusMessage;
    const QString defaultIconTheme {QIcon::themeName()};
    const QStringList defaultPath {qEnvironmentVariable("PATH").split(':') << QStringLiteral("/usr/sbin")};
    QLocale locale;
    QString lang {locale.name()};
    QFileSystemWatcher fileWatcher;
    QTimer fileReloadTimer;
    QStringList desktopApplicationDirs;
    bool hideGui {};
    bool removeStartupCheckbox {};
    bool startupState {};
    int iconRevision {};
    int configuredMinimumWidth {720};
    int configuredMinimumHeight {560};
    int configuredIconSize {};
    int configuredColumns {};

    mutable QHash<QString, QString> desktopFileCache;
    mutable QHash<QString, QString> desktopFileIndex;
    QHash<QString, qint64> runningLaunchers;
    mutable bool desktopFileIndexBuilt {};

    void buildDesktopFileIndex() const;
    void clearDesktopFileCaches();
    [[nodiscard]] ItemInfo getDesktopFileInfo(const QString &path) const;
    [[nodiscard]] QString getDesktopFileName(const QString &appName) const;
    [[nodiscard]] QString getDefaultEditor() const;
    [[nodiscard]] QString invokingUser() const;
    bool prepareCommand(const ItemInfo &item, QString *program, QStringList *arguments,
                        QString *errorMessage) const;
    bool prepareEditorCommand(const QString &editor, QString *program, QStringList *arguments,
                              QString *errorMessage) const;
    bool readFile(const QString &path, bool reportErrors = true);
    void refilter();
    void runTracked(const QString &program, const QStringList &arguments,
                    const QString &trackingKey = {});

    [[nodiscard]] QString autostartFilePath() const;
    [[nodiscard]] QString autostartSourceHash() const;
    [[nodiscard]] bool isLegacyAutostartFile(const QString &path) const;
    [[nodiscard]] bool isManagedAutostartFile(const QString &path) const;
    void migrateLegacyAutostart();
    bool writeAutostartFile(QString *errorMessage) const;

    void handleDirectoryChanged(const QString &path);
    void handleFileChanged(const QString &path);
    void refreshIfFileChanged();
    void setReloadMessage(const QString &message);
    void watchDesktopApplicationDirectories();
    void watchFile(const QString &path);
};
