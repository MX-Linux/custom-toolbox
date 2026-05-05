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
        const QString full_path = dir.filePath(name);
        if (QFile::exists(full_path)) {
            QIcon icon(full_path);
            if (!icon.isNull()) {
                return icon;
            }
        }
    }
    return {};
}

QIcon getDefaultIcon()
{
    static const QString default_name = QStringLiteral("utilities-terminal");
    QIcon icon = QIcon::fromTheme(default_name);
    if (!icon.isNull()) {
        return icon;
    }
    for (const auto &ext : iconExtensions()) {
        icon = searchInPaths(default_name + ext);
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

QIcon IconLoader::loadIcon(const QString &icon_name)
{
    if (auto it = icon_cache.constFind(icon_name); it != icon_cache.constEnd()) {
        return it.value();
    }

    if (icon_name.isEmpty() || icon_name == QLatin1String("utilities-terminal")) {
        const QIcon result = getDefaultIcon();
        icon_cache.insert(icon_name, result);
        return result;
    }

    const QFileInfo icon_info(icon_name);
    if (icon_info.isAbsolute() && icon_info.exists()) {
        QIcon result(icon_name);
        icon_cache.insert(icon_name, result);
        return result;
    }

    static const QRegularExpression ext_re(QStringLiteral(R"(\.(png|svg|xpm)$)"));
    QString name_no_ext = icon_name;
    name_no_ext.remove(ext_re);

    QIcon icon = QIcon::fromTheme(name_no_ext);
    if (!icon.isNull()) {
        icon_cache.insert(icon_name, icon);
        return icon;
    }

    icon = searchInPaths(icon_name);
    if (!icon.isNull()) {
        icon_cache.insert(icon_name, icon);
        return icon;
    }

    for (const auto &ext : iconExtensions()) {
        icon = searchInPaths(name_no_ext + ext);
        if (!icon.isNull()) {
            icon_cache.insert(icon_name, icon);
            return icon;
        }
    }

    const QIcon default_icon = getDefaultIcon();
    icon_cache.insert(icon_name, default_icon);
    return default_icon;
}
