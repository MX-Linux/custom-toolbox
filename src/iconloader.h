#pragma once

#include <QHash>
#include <QIcon>
#include <QString>

class IconLoader
{
public:
    static QIcon loadIcon(const QString &iconName);
    static void clearCache();

private:
    static QHash<QString, QIcon> iconCache;
};
