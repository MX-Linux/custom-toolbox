#include "iconloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

QHash<QString, QIcon> IconLoader::iconCache;

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
    iconCache.clear();
}

QIcon IconLoader::loadIcon(const QString &iconName)
{
    if (auto it = iconCache.constFind(iconName); it != iconCache.constEnd()) {
        return it.value();
    }

    if (iconName.isEmpty() || iconName == QLatin1String("utilities-terminal")) {
        const QIcon result = getDefaultIcon();
        iconCache.insert(iconName, result);
        return result;
    }

    const QFileInfo iconInfo(iconName);
    if (iconInfo.isAbsolute() && iconInfo.exists()) {
        QIcon result(iconName);
        iconCache.insert(iconName, result);
        return result;
    }

    static const QRegularExpression extRe(QStringLiteral(R"(\.(png|svg|xpm)$)"));
    QString nameNoExt = iconName;
    nameNoExt.remove(extRe);

    QIcon icon = QIcon::fromTheme(nameNoExt);
    if (!icon.isNull()) {
        iconCache.insert(iconName, icon);
        return icon;
    }

    icon = searchInPaths(iconName);
    if (!icon.isNull()) {
        iconCache.insert(iconName, icon);
        return icon;
    }

    for (const auto &ext : iconExtensions()) {
        icon = searchInPaths(nameNoExt + ext);
        if (!icon.isNull()) {
            iconCache.insert(iconName, icon);
            return icon;
        }
    }

    const QIcon defaultIcon = getDefaultIcon();
    iconCache.insert(iconName, defaultIcon);
    return defaultIcon;
}
