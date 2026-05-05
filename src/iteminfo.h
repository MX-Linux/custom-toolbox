#pragma once

#include <QString>

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
