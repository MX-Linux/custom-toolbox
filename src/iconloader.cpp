#include "iconloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

QHash<QString, QIcon> IconLoader::icon_cache;

namespace
{
const QStringList &searchPaths()
{
    static const QStringList paths = []() {
        QStringList p = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        for (auto &path : p) {
            path = QDir(path).filePath(QStringLiteral("icons"));
        }
        p << QStringLiteral("/usr/share/pixmaps");
        return p;
    }();
    return paths;
}

const QStringList &iconExtensions()
{
    static const QStringList exts {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm")};
    return exts;
}

QIcon searchInPaths(const QString &name)
{
    for (const auto &path : searchPaths()) {
        const QDir dir(path);
        if (!dir.exists()) {
            continue;
        }
        const QString fullPath = dir.filePath(name);
        if (QFile::exists(fullPath)) {
            QIcon icon(fullPath);
            if (!icon.isNull()) {
                return icon;
            }
        }
    }
    return {};
}

QIcon getDefaultIcon()
{
    static const QString defaultName = QStringLiteral("utilities-terminal");
    QIcon icon = QIcon::fromTheme(defaultName);
    if (!icon.isNull()) {
        return icon;
    }
    for (const auto &ext : iconExtensions()) {
        icon = searchInPaths(defaultName + ext);
        if (!icon.isNull()) {
            return icon;
        }
    }
    return {};
}
} // namespace

void IconLoader::clearCache()
{
    icon_cache.clear();
}

QIcon IconLoader::loadIcon(const QString &iconName)
{
    if (auto it = icon_cache.constFind(iconName); it != icon_cache.constEnd()) {
        return it.value();
    }

    if (iconName.isEmpty() || iconName == QLatin1String("utilities-terminal")) {
        const QIcon result = getDefaultIcon();
        icon_cache.insert(iconName, result);
        return result;
    }

    const QFileInfo iconInfo(iconName);
    if (iconInfo.isAbsolute() && iconInfo.exists()) {
        QIcon result(iconName);
        icon_cache.insert(iconName, result);
        return result;
    }

    static const QRegularExpression extRe(QStringLiteral(R"(\.(png|svg|xpm)$)"));
    QString nameNoExt = iconName;
    nameNoExt.remove(extRe);

    QIcon icon = QIcon::fromTheme(nameNoExt);
    if (!icon.isNull()) {
        icon_cache.insert(iconName, icon);
        return icon;
    }

    icon = searchInPaths(iconName);
    if (!icon.isNull()) {
        icon_cache.insert(iconName, icon);
        return icon;
    }

    for (const auto &ext : iconExtensions()) {
        icon = searchInPaths(nameNoExt + ext);
        if (!icon.isNull()) {
            icon_cache.insert(iconName, icon);
            return icon;
        }
    }

    const QIcon default_icon = getDefaultIcon();
    icon_cache.insert(iconName, default_icon);
    return default_icon;
}
