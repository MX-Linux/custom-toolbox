#pragma once

#include <QHash>
#include <QIcon>
#include <QString>

class IconLoader
{
public:
    static QIcon loadIcon(const QString &icon_name);
    static void clearCache();

private:
    static QHash<QString, QIcon> icon_cache;
};
