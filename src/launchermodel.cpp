/**********************************************************************
 * Copyright (C) 2017-2026 MX Authors
 *
 * This file is part of custom-toolbox.
 **********************************************************************/

#include "launchermodel.h"

#include "common.h"
#include "iconloader.h"
#include "launcherparser.h"

#include <algorithm>
#include <pwd.h>
#include <unistd.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringView>
#include <QTextStream>
#include <QUrl>

namespace
{
QString commandDescription(const QString &program, const QStringList &arguments)
{
    QStringList parts {program};
    parts.append(arguments);
    return parts.join(QLatin1Char(' '));
}

QString homeDirectoryForUser(const QString &user)
{
    if (const passwd *entry = getpwnam(user.toLocal8Bit().constData())) {
        return QString::fromLocal8Bit(entry->pw_dir);
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

    const QStringView textView(text);
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
    return !exactValue.isEmpty() ? exactValue : (!shortValue.isEmpty() ? shortValue : fallbackValue);
}
}

LauncherIconProvider::LauncherIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

void LauncherIconProvider::clear()
{
    icons.clear();
}

void LauncherIconProvider::insert(const QString &key, const QIcon &icon)
{
    icons.insert(key, icon);
}

QPixmap LauncherIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize target = requestedSize.isValid() ? requestedSize : QSize(48, 48);
    const QPixmap pixmap = icons.value(id.section(QLatin1Char('?'), 0, 0)).pixmap(target);
    if (size != nullptr) {
        *size = pixmap.size();
    }
    return pixmap;
}

LauncherModel::LauncherModel(const QCommandLineParser &argParser, const QString &listFile,
                             LauncherIconProvider *provider, QObject *parent)
    : QAbstractListModel(parent),
      iconProvider(provider),
      fileLocation(Config::ConfigDir),
      fileName(listFile),
      removeStartupCheckbox(argParser.isSet(QStringLiteral("remove-checkbox")))
{
    QSettings settings(Config::ConfigFile, QSettings::NativeFormat);
    auto integerSetting = [&settings](const QString &key, int fallback, int minimum) {
        bool ok = false;
        const int value = settings.value(key, fallback).toInt(&ok);
        return ok ? std::max(minimum, value) : fallback;
    };
    configuredMinimumWidth = integerSetting(QStringLiteral("min_width"), 720, 300);
    configuredMinimumHeight = integerSetting(QStringLiteral("min_height"), 560, 300);
    configuredIconSize = integerSetting(QStringLiteral("icon_size"), 0, 0);
    configuredColumns = integerSetting(QStringLiteral("fixed_number_columns"), 0, 0);
    hideGui = settings.value(QStringLiteral("hideGUI"), false).toBool();
    guiEditor = settings.value(QStringLiteral("gui_editor")).toString();

    fileReloadTimer.setSingleShot(true);
    fileReloadTimer.setInterval(200);
    connect(&fileReloadTimer, &QTimer::timeout, this, &LauncherModel::refreshIfFileChanged);
    connect(&fileWatcher, &QFileSystemWatcher::fileChanged, this, &LauncherModel::handleFileChanged);
    connect(&fileWatcher, &QFileSystemWatcher::directoryChanged, this, &LauncherModel::handleDirectoryChanged);

    readFile(fileName);
    migrateLegacyAutostart();
    startupState = isManagedAutostartFile(autostartFilePath());
    watchFile(fileName);
    watchDesktopApplicationDirectories();
}

int LauncherModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(visibleRows.size());
}

QVariant LauncherModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= visibleRows.size()) {
        return {};
    }
    const int sourceIndex = visibleRows.at(index.row());
    const ItemInfo &item = allItems.at(sourceIndex);
    switch (role) {
    case NameRole:
        return item.name;
    case CommentRole:
        return item.comment;
    case CategoryRole:
        return item.category;
    case IconSourceRole:
        return QStringLiteral("image://launchericons/") + QString::number(sourceIndex)
               + QStringLiteral("?revision=") + QString::number(iconRevision);
    case SourceIndexRole:
        return sourceIndex;
    default:
        return {};
    }
}

QHash<int, QByteArray> LauncherModel::roleNames() const
{
    return {{NameRole, "name"}, {CommentRole, "comment"}, {CategoryRole, "category"},
            {IconSourceRole, "iconSource"}, {SourceIndexRole, "sourceIndex"}};
}

QString LauncherModel::search() const
{
    return searchText;
}

void LauncherModel::setSearch(const QString &value)
{
    if (searchText == value) {
        return;
    }
    searchText = value;
    emit searchChanged();
    refilter();
}

QString LauncherModel::selectedCategory() const
{
    return selectedCategoryName;
}

void LauncherModel::setSelectedCategory(const QString &value)
{
    if (selectedCategoryName == value) {
        return;
    }
    selectedCategoryName = value;
    emit selectedCategoryChanged();
    refilter();
}

QStringList LauncherModel::categories() const
{
    return categoryNames;
}

QString LauncherModel::title() const
{
    return launcherTitle;
}

QString LauncherModel::description() const
{
    return launcherDescription;
}

QString LauncherModel::customName() const
{
    return launcherCustomName;
}

bool LauncherModel::startupEnabled() const
{
    return startupState;
}

bool LauncherModel::startupVisible() const
{
    return !removeStartupCheckbox;
}

QString LauncherModel::reloadMessage() const
{
    return reloadStatusMessage;
}

void LauncherModel::refilter()
{
    beginResetModel();
    visibleRows.clear();
    const QString allCategory = categoryNames.value(0);
    for (int i = 0; i < allItems.size(); ++i) {
        const ItemInfo &item = allItems.at(i);
        const bool hasSearch = !searchText.trimmed().isEmpty();
        const bool categoryMatches = hasSearch || selectedCategoryName.isEmpty()
                                     || selectedCategoryName == allCategory
                                     || item.category == selectedCategoryName;
        const bool textMatches = !hasSearch || item.name.contains(searchText, Qt::CaseInsensitive)
                                 || item.comment.contains(searchText, Qt::CaseInsensitive)
                                 || item.category.contains(searchText, Qt::CaseInsensitive);
        if (categoryMatches && textMatches) {
            visibleRows.append(i);
        }
    }
    endResetModel();
}

QString LauncherModel::getDesktopFileName(const QString &appName) const
{
    if (const auto cached = desktopFileCache.constFind(appName); cached != desktopFileCache.constEnd()) {
        return cached.value();
    }
    if (QFileInfo(appName).isAbsolute()) {
        const QString resolved = appName.endsWith(QLatin1String(".desktop"))
                                     ? appName : appName + QLatin1String(".desktop");
        if (QFile::exists(resolved)) {
            desktopFileCache.insert(appName, resolved);
            return resolved;
        }
    }

    const QString desktopName = appName.endsWith(QLatin1String(".desktop"))
                                    ? appName : appName + QLatin1String(".desktop");
    QString result;
    for (const QString &path : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        QDirIterator iterator(path, {desktopName}, QDir::Files, QDirIterator::Subdirectories);
        if (iterator.hasNext()) {
            result = iterator.next();
            break;
        }
    }
    if (result.isEmpty()) {
        buildDesktopFileIndex();
        result = desktopFileIndex.value(appName.toLower());
    }
    if (result.isEmpty()) {
        result = QStandardPaths::findExecutable(appName, defaultPath);
    }
    desktopFileCache.insert(appName, result);
    return result;
}

void LauncherModel::buildDesktopFileIndex() const
{
    if (desktopFileIndexBuilt) {
        return;
    }
    desktopFileIndexBuilt = true;
    static const QRegularExpression execRegex(QStringLiteral(R"(^Exec=(.*)$)"),
                                              QRegularExpression::MultilineOption);
    QHash<QString, DesktopFileCandidate> candidates;
    const QStringList paths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    auto insert = [&candidates](const QString &key, const QString &path, int priority) {
        const auto current = candidates.constFind(key);
        if (current == candidates.constEnd() || priority < current->sourcePriority
            || (priority == current->sourcePriority && path < current->path)) {
            candidates.insert(key, {.path = path, .sourcePriority = priority});
        }
    };
    for (qsizetype priority = 0; priority < paths.size(); ++priority) {
        QDirIterator iterator(paths.at(priority), {QStringLiteral("*.desktop")}, QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QString baseName = QFileInfo(path).completeBaseName();
            const QString suffix = baseName.section(QLatin1Char('.'), -1).toLower();
            if (!suffix.isEmpty()) {
                insert(suffix, path, static_cast<int>(priority));
            }
            QFile file(path);
            if (file.open(QFile::ReadOnly | QFile::Text)) {
                const auto match = execRegex.match(QString::fromUtf8(file.readAll()));
                if (match.hasMatch()) {
                    QString command = QProcess::splitCommand(match.captured(1)).value(0);
                    command = QFileInfo(command).fileName().toLower();
                    if (!command.isEmpty()) {
                        insert(command, path, static_cast<int>(priority));
                    }
                }
            }
        }
    }
    for (auto iterator = candidates.constBegin(); iterator != candidates.constEnd(); ++iterator) {
        desktopFileIndex.insert(iterator.key(), iterator->path);
    }
}

void LauncherModel::clearDesktopFileCaches()
{
    desktopFileCache.clear();
    desktopFileIndex.clear();
    desktopFileIndexBuilt = false;
}

ItemInfo LauncherModel::getDesktopFileInfo(const QString &path) const
{
    if (!path.endsWith(QLatin1String(".desktop"))) {
        const QString name = QFileInfo(path).fileName();
        return {.name = name, .iconName = name, .exec = path, .terminal = true};
    }
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    const QString type = desktopEntryValue(text, QStringLiteral("Type"), lang);
    const bool hidden = desktopEntryValue(text, QStringLiteral("Hidden"), lang)
                            .compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    if (type != QLatin1String("Application") || hidden) {
        return {};
    }
    const QString tryExec = desktopEntryValue(text, QStringLiteral("TryExec"), lang);
    if (!tryExec.isEmpty()) {
        const QString program = QProcess::splitCommand(tryExec).value(0);
        if (program.isEmpty() || QStandardPaths::findExecutable(program, defaultPath).isEmpty()) {
            return {};
        }
    }

    ItemInfo item;
    static const QRegularExpression mxPrefix(QStringLiteral("^MX "));
    item.name = desktopEntryValue(text, QStringLiteral("Name"), lang).remove(mxPrefix);
    item.comment = desktopEntryValue(text, QStringLiteral("Comment"), lang);
    item.iconName = desktopEntryValue(text, QStringLiteral("Icon"), lang);
    item.terminal = desktopEntryValue(text, QStringLiteral("Terminal"), lang)
                        .compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    if (item.name.isEmpty()
        || !LauncherParser::parseDesktopExec(desktopEntryValue(text, QStringLiteral("Exec"), lang), item.name,
                                             item.iconName, path, &item.exec, &item.execArgs)) {
        return {};
    }
    return item;
}

bool LauncherModel::readFile(const QString &path, bool reportErrors)
{
    auto report = [this, reportErrors](const QString &title, const QString &message) {
        if (reportErrors) {
            emit errorOccurred(title, message);
        }
        setReloadMessage(message);
    };
    if (!QFile::exists(path)) {
        report(QCoreApplication::translate("MainWindow", "File Not Found"), QCoreApplication::translate("MainWindow", "The file %1 does not exist.").arg(path));
        return false;
    }
    fileLocation = QFileInfo(path).path();
    QSettings iniSettings(path, QSettings::IniFormat);
    const bool isIni = iniSettings.status() == QSettings::NoError
                       && iniSettings.contains(QStringLiteral("Categories/list"));
    LauncherParser::ParseResult parsed;
    if (isIni) {
        parsed = LauncherParser::parseIni(iniSettings, lang);
    } else {
        QFile file(path);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            report(QCoreApplication::translate("MainWindow", "File Open Error"), QCoreApplication::translate("MainWindow", "Could not open file: ") + path);
            return false;
        }
        parsed = LauncherParser::parse(file.readAll(), lang);
    }
    if (parsed.items.isEmpty()) {
        report(QCoreApplication::translate("MainWindow", "Parse Error"), QCoreApplication::translate("MainWindow", "The file %1 contains no recognizable launcher entries.").arg(path));
        return false;
    }

    clearDesktopFileCaches();
    QVector<ItemInfo> items;
    QStringList newCategories {QCoreApplication::translate("MainWindow", "All launchers")};
    for (const auto &parsedItem : parsed.items) {
        const QString desktopFile = getDesktopFileName(parsedItem.appName);
        if (desktopFile.isEmpty()) {
            continue;
        }
        ItemInfo item = getDesktopFileInfo(desktopFile);
        if (item.name.isEmpty()) {
            continue;
        }
        item.root = parsedItem.root;
        item.user = parsedItem.user;
        item.terminal = item.terminal || parsedItem.terminal;
        item.category = parsedItem.category;
        if (!parsedItem.alias.isEmpty()) {
            item.name = parsedItem.alias;
        }
        items.append(item);
        if (!newCategories.contains(item.category)) {
            newCategories.append(item.category);
        }
    }
    if (items.isEmpty()) {
        report(QCoreApplication::translate("MainWindow", "Parse Error"), QCoreApplication::translate("MainWindow", "None of the entries in %1 match an installed application.").arg(path));
        return false;
    }

    const QString newTheme = parsed.iconTheme;
    const QString effectiveOldTheme = iconTheme.isEmpty() ? defaultIconTheme : iconTheme;
    const QString effectiveNewTheme = newTheme.isEmpty() ? defaultIconTheme : newTheme;
    if (effectiveOldTheme != effectiveNewTheme) {
        IconLoader::clearCache();
    }
    iconTheme = newTheme;
    QIcon::setThemeName(effectiveNewTheme);

    beginResetModel();
    allItems = std::move(items);
    visibleRows.clear();
    endResetModel();
    categoryNames = std::move(newCategories);
    if (!selectedCategoryName.isEmpty() && !categoryNames.contains(selectedCategoryName)) {
        selectedCategoryName.clear();
        emit selectedCategoryChanged();
    }
    launcherTitle = parsed.name;
    launcherDescription = parsed.comment;
    launcherCustomName = QFileInfo(path).completeBaseName();

    iconProvider->clear();
    ++iconRevision;
    const QIcon fallback = QIcon::fromTheme(QStringLiteral("applications-utilities"),
                                            QIcon(QStringLiteral(":/qt/qml/CustomToolbox/icons/custom-toolbox.svg")));
    for (int i = 0; i < allItems.size(); ++i) {
        const QIcon icon = IconLoader::loadIcon(allItems.at(i).iconName);
        iconProvider->insert(QString::number(i), icon.isNull() ? fallback : icon);
    }
    setReloadMessage({});
    emit categoriesChanged();
    emit launcherChanged();
    refilter();
    return true;
}

QString LauncherModel::invokingUser() const
{
    auto nameForUid = [](const QByteArray &value) -> QString {
        bool ok = false;
        const uint uid = value.toUInt(&ok);
        if (ok) {
            if (const passwd *entry = getpwuid(uid)) {
                return QString::fromLocal8Bit(entry->pw_name);
            }
        }
        return {};
    };
    for (const char *variable : {"PKEXEC_UID", "SUDO_UID"}) {
        if (const QString name = nameForUid(qgetenv(variable)); !name.isEmpty()) {
            return name;
        }
    }
    return QString::fromLocal8Bit(qgetenv("SUDO_USER"));
}

bool LauncherModel::prepareCommand(const ItemInfo &item, QString *program, QStringList *arguments,
                                   QString *errorMessage) const
{
    if (item.exec.isEmpty()) {
        *errorMessage = QCoreApplication::translate("MainWindow", "Command is empty. Cannot execute.");
        return false;
    }
    QString launchProgram = item.exec;
    QStringList launchArguments = item.execArgs;
    if (item.terminal) {
        launchArguments.prepend(launchProgram);
        launchArguments.prepend(QStringLiteral("-e"));
        launchProgram = QStringLiteral("x-terminal-emulator");
    }
    QStringList environment {QStringLiteral("env"), QStringLiteral("DISPLAY=") + qEnvironmentVariable("DISPLAY"),
                             QStringLiteral("XAUTHORITY=")
                                 + qEnvironmentVariable("XAUTHORITY", QDir::homePath() + QStringLiteral("/.Xauthority"))};
    if (item.root && getuid() != 0) {
        launchArguments.prepend(launchProgram);
        launchArguments = environment + launchArguments;
        launchProgram = QStringLiteral("pkexec");
    } else if (item.user && getuid() == 0) {
        const QString user = invokingUser();
        const QString home = homeDirectoryForUser(user);
        if (user.isEmpty() || home.isEmpty()) {
            *errorMessage = QCoreApplication::translate("MainWindow", "Could not determine the unprivileged user. Refusing to run this launcher as root.");
            return false;
        }
        environment[2] = QStringLiteral("XAUTHORITY=")
                         + qEnvironmentVariable("XAUTHORITY", home + QStringLiteral("/.Xauthority"));
        launchArguments.prepend(launchProgram);
        launchArguments = QStringList {QStringLiteral("--user"), user} + environment + launchArguments;
        launchProgram = QStringLiteral("pkexec");
    }
    errorMessage->clear();
    *program = launchProgram;
    *arguments = launchArguments;
    return true;
}

void LauncherModel::launch(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= allItems.size()) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Execution Error"), QCoreApplication::translate("MainWindow", "The selected launcher is no longer available."));
        return;
    }
    QString program;
    QStringList arguments;
    QString error;
    if (!prepareCommand(allItems.at(sourceIndex), &program, &arguments, &error)) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Execution Error"), error);
        return;
    }
    const QString trackingKey = program + QChar() + arguments.join(QChar());
    const qint64 runningProcessId = runningLaunchers.value(trackingKey);
    if (runningProcessId < 0
        || (runningProcessId > 0
            && QFileInfo::exists(QStringLiteral("/proc/%1").arg(runningProcessId)))) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Launcher already running"),
                           QCoreApplication::translate("MainWindow", "%1 is already running.")
                               .arg(allItems.at(sourceIndex).name));
        return;
    }
    runningLaunchers.remove(trackingKey);

    if (program == QLatin1String("pkexec") || hideGui) {
        runningLaunchers.insert(trackingKey, -1);
        runTracked(program, arguments, trackingKey);
        return;
    }
    qint64 processId = 0;
    if (!QProcess::startDetached(program, arguments, {}, &processId)) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Execution Error"), QCoreApplication::translate("MainWindow", "Failed to start program: %1").arg(program));
    } else if (processId > 0) {
        runningLaunchers.insert(trackingKey, processId);
    }
}

void LauncherModel::runTracked(const QString &program, const QStringList &arguments,
                               const QString &trackingKey)
{
    if (hideGui) {
        emit hideRequested();
    }
    const QString command = commandDescription(program, arguments);
    const bool isPkexec = program == QLatin1String("pkexec");
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(process, &QProcess::started, this, [this, process, trackingKey] {
        if (!trackingKey.isEmpty()) {
            runningLaunchers.insert(trackingKey, process->processId());
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, command, trackingKey](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    runningLaunchers.remove(trackingKey);
                    process->deleteLater();
                    if (hideGui) {
                        emit showRequested();
                    }
                    emit errorOccurred(QCoreApplication::translate("MainWindow", "Execution Error"), QCoreApplication::translate("MainWindow", "Failed to start command: %1").arg(command));
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process, command, isPkexec, trackingKey](int exitCode, QProcess::ExitStatus status) {
                runningLaunchers.remove(trackingKey);
                process->deleteLater();
                if (hideGui) {
                    emit showRequested();
                }
                const bool declined = isPkexec && exitCode == 126;
                if (status != QProcess::NormalExit || (exitCode != 0 && !declined)) {
                    emit errorOccurred(QCoreApplication::translate("MainWindow", "Execution Error"), QCoreApplication::translate("MainWindow", "Failed to execute command: %1").arg(command));
                }
            });
    process->start(program, arguments);
}

QString LauncherModel::getDefaultEditor() const
{
    QProcess process;
    process.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"),
                                                QStringLiteral("text/plain")});
    if (!process.waitForFinished(3000) || process.exitCode() != 0) {
        return QStringLiteral("nano");
    }
    const QString desktopFile = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                        QString::fromUtf8(process.readAllStandardOutput()).trimmed());
    QFile file(desktopFile);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return QStringLiteral("nano");
    }
    QTextStream stream(&file);
    QString line;
    while (stream.readLineInto(&line)) {
        if (line.startsWith(QLatin1String("Exec="))) {
            static const QRegularExpression fields(QStringLiteral(" %[a-zA-Z]"));
            return line.mid(5).remove(fields).trimmed();
        }
    }
    return QStringLiteral("nano");
}

bool LauncherModel::prepareEditorCommand(const QString &editor, QString *program, QStringList *arguments,
                                         QString *errorMessage) const
{
    QStringList parts = QProcess::splitCommand(editor);
    if (parts.isEmpty()) {
        *errorMessage = QCoreApplication::translate("MainWindow", "Editor command is empty.");
        return false;
    }
    QString editorProgram = parts.takeFirst();
    const QString editorName = QFileInfo(editorProgram).baseName();
    static const QRegularExpression elevates(QStringLiteral(R"(\b(kate|kwrite|featherpad|code|codium)$)"));
    static const QRegularExpression cli(QStringLiteral(R"(\b(nano|vi|vim|nvim|micro|emacs)\b)"));
    const bool selfElevating = elevates.match(editorName).hasMatch();
    parts << fileName;
    if (cli.match(editorName).hasMatch()) {
        parts.prepend(editorProgram);
        parts.prepend(QStringLiteral("-e"));
        editorProgram = QStringLiteral("x-terminal-emulator");
    }
    QStringList environment {QStringLiteral("env"), QStringLiteral("DISPLAY=") + qEnvironmentVariable("DISPLAY"),
                             QStringLiteral("XAUTHORITY=")
                                 + qEnvironmentVariable("XAUTHORITY", QDir::homePath() + QStringLiteral("/.Xauthority"))};
    if (getuid() == 0 && selfElevating) {
        const QString user = invokingUser();
        const QString home = homeDirectoryForUser(user);
        if (user.isEmpty() || home.isEmpty()) {
            *errorMessage = QCoreApplication::translate("MainWindow", "Could not determine the unprivileged user. Refusing to launch the editor as root.");
            return false;
        }
        environment[2] = QStringLiteral("XAUTHORITY=")
                         + qEnvironmentVariable("XAUTHORITY", home + QStringLiteral("/.Xauthority"));
        parts.prepend(editorProgram);
        parts = QStringList {QStringLiteral("--user"), user} + environment + parts;
        editorProgram = QStringLiteral("pkexec");
    } else if (!selfElevating) {
        const QFileInfo info(fileName);
        const bool needsElevation = info.exists() ? !info.isWritable() : !QFileInfo(info.path()).isWritable();
        if (needsElevation) {
            parts.prepend(editorProgram);
            parts = environment + parts;
            editorProgram = QStringLiteral("pkexec");
        }
    }
    errorMessage->clear();
    *program = editorProgram;
    *arguments = parts;
    return true;
}

void LauncherModel::edit()
{
    const QString configuredProgram = QProcess::splitCommand(guiEditor).value(0);
    const bool useDefault = guiEditor.isEmpty() || configuredProgram.isEmpty()
                            || QStandardPaths::findExecutable(configuredProgram, defaultPath).isEmpty();
    const QString editor = useDefault ? getDefaultEditor() : guiEditor;
    QString program;
    QStringList arguments;
    QString error;
    if (!prepareEditorCommand(editor, &program, &arguments, &error)) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Error"), error);
        return;
    }
    if (program == QLatin1String("pkexec")) {
        runTracked(program, arguments);
    } else if (!QProcess::startDetached(program, arguments)) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Error"), QCoreApplication::translate("MainWindow", "Failed to launch the editor."));
    }
    watchFile(fileName);
}

void LauncherModel::openHelp()
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(Config::HelpFile))) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Help"), QCoreApplication::translate("MainWindow", "Could not open %1.").arg(Config::HelpFile));
    }
}

void LauncherModel::openLicense()
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(Config::LicenseFile))) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "Error"), QCoreApplication::translate("MainWindow", "Could not open %1.").arg(Config::LicenseFile));
    }
}

QString LauncherModel::autostartSourceHash() const
{
    const QFileInfo info(fileName);
    const QString path = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString LauncherModel::autostartFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/autostart/custom-toolbox-")
           + autostartSourceHash().left(16) + QStringLiteral(".desktop");
}

bool LauncherModel::isManagedAutostartFile(const QString &path) const
{
    QSettings settings(path, QSettings::IniFormat);
    return QFile::exists(path) && settings.status() == QSettings::NoError
           && settings.value(QStringLiteral("Desktop Entry/X-Custom-Toolbox-Managed")).toBool()
           && settings.value(QStringLiteral("Desktop Entry/X-Custom-Toolbox-Source-SHA256")).toString()
                  == autostartSourceHash();
}

bool LauncherModel::isLegacyAutostartFile(const QString &path) const
{
    if (!QFile::exists(path)) {
        return false;
    }
    QSettings settings(path, QSettings::IniFormat);
    const QStringList parts = QProcess::splitCommand(settings.value(QStringLiteral("Desktop Entry/Exec")).toString());
    if (settings.status() != QSettings::NoError || parts.size() < 2
        || parts.first() != QLatin1String("custom-toolbox")) {
        return false;
    }
    const QFileInfo info(fileName);
    const QString listPath = parts.mid(1).join(QLatin1Char(' '));
    return listPath == info.absoluteFilePath()
           || (!info.canonicalFilePath().isEmpty() && listPath == info.canonicalFilePath());
}

bool LauncherModel::writeAutostartFile(QString *errorMessage) const
{
    const QString path = autostartFilePath();
    if (QFile::exists(path) && !isManagedAutostartFile(path)) {
        *errorMessage = QCoreApplication::translate("MainWindow", "Refusing to overwrite a non-Custom Toolbox autostart file: %1").arg(path);
        return false;
    }
    auto escape = [](QString value) {
        value.replace('\\', QStringLiteral("\\\\"));
        value.replace('\n', QStringLiteral("\\n"));
        value.replace('\r', QStringLiteral("\\r"));
        value.replace('\t', QStringLiteral("\\t"));
        return value;
    };
    auto quote = [](QString value) {
        value.replace('\\', QStringLiteral("\\\\"));
        value.replace('"', QStringLiteral("\\\""));
        value.replace('$', QStringLiteral("\\$"));
        value.replace('%', QStringLiteral("%%"));
        value.replace('`', QStringLiteral("\\`"));
        return QLatin1Char('"') + value + QLatin1Char('"');
    };
    QSaveFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        *errorMessage = QCoreApplication::translate("MainWindow", "Could not write file: %1").arg(path);
        return false;
    }
    QTextStream stream(&file);
    stream << "[Desktop Entry]\nName=" << escape(launcherTitle)
           << "\nComment=" << escape(launcherDescription)
           << "\nExec=custom-toolbox " << escape(quote(QFileInfo(fileName).absoluteFilePath()))
           << "\nTerminal=false\nType=Application\nIcon=custom-toolbox\nCategories=XFCE;System\n"
              "StartupNotify=false\nX-Custom-Toolbox-Managed=true\nX-Custom-Toolbox-Source-SHA256="
           << autostartSourceHash() << '\n';
    if (!file.commit()) {
        *errorMessage = QCoreApplication::translate("MainWindow", "Could not write file: %1").arg(path);
        return false;
    }
    errorMessage->clear();
    return true;
}

void LauncherModel::migrateLegacyAutostart()
{
    const QString directory = QFileInfo(autostartFilePath()).absolutePath();
    const QFileInfo info(fileName);
    for (const QString &name : {launcherCustomName, info.baseName()}) {
        const QString legacyPath = QDir(directory).filePath(name + QStringLiteral(".desktop"));
        if (legacyPath == autostartFilePath() || !isLegacyAutostartFile(legacyPath)) {
            continue;
        }
        QString error;
        if (writeAutostartFile(&error)) {
            if (!QFile::remove(legacyPath)) {
                qWarning() << "Could not remove migrated legacy autostart file:" << legacyPath;
            }
        } else {
            qWarning() << error;
        }
    }
}

void LauncherModel::setStartupEnabled(bool enabled)
{
    if (startupState == enabled) {
        return;
    }
    const QString directory = QFileInfo(autostartFilePath()).absolutePath();
    QString error;
    bool success = true;
    if (enabled) {
        success = (QDir(directory).exists() || QDir().mkpath(directory)) && writeAutostartFile(&error);
        if (!success && error.isEmpty()) {
            error = QCoreApplication::translate("MainWindow", "Could not create directory: %1").arg(directory);
        }
    } else if (isManagedAutostartFile(autostartFilePath())) {
        success = QFile::remove(autostartFilePath());
        if (!success) {
            error = QCoreApplication::translate("MainWindow", "Could not remove file: %1").arg(autostartFilePath());
        }
    }
    if (!success) {
        emit errorOccurred(QCoreApplication::translate("MainWindow", "File Open Error"), error);
        emit startupEnabledChanged();
        return;
    }
    startupState = enabled;
    emit startupEnabledChanged();
}

void LauncherModel::handleFileChanged(const QString &path)
{
    if (path == fileName) {
        watchFile(fileName);
        fileReloadTimer.start();
    }
}

void LauncherModel::handleDirectoryChanged(const QString &path)
{
    if (desktopApplicationDirs.contains(path) || path == fileLocation) {
        if (path == fileLocation && QFile::exists(fileName)) {
            watchFile(fileName);
        }
        fileReloadTimer.start();
    }
}

void LauncherModel::refreshIfFileChanged()
{
    if (!QFile::exists(fileName) || !readFile(fileName, false)) {
        setReloadMessage(QCoreApplication::translate("MainWindow", "Could not reload the configuration. The previous configuration is still in use."));
    }
}

void LauncherModel::setReloadMessage(const QString &message)
{
    if (reloadStatusMessage == message) {
        return;
    }
    reloadStatusMessage = message;
    emit reloadMessageChanged();
}

void LauncherModel::watchFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (fileWatcher.files().contains(path)) {
        fileWatcher.removePath(path);
    }
    if (QFile::exists(path) && !fileWatcher.addPath(path)) {
        qWarning() << "Failed to add watch for file:" << path;
    }
    if (!fileLocation.isEmpty() && !fileWatcher.directories().contains(fileLocation)
        && !fileWatcher.addPath(fileLocation)) {
        qWarning() << "Failed to add watch for directory:" << fileLocation;
    }
}

void LauncherModel::watchDesktopApplicationDirectories()
{
    desktopApplicationDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &path : std::as_const(desktopApplicationDirs)) {
        if (!fileWatcher.directories().contains(path) && !fileWatcher.addPath(path)) {
            qWarning() << "Failed to add watch for application directory:" << path;
        }
    }
}
