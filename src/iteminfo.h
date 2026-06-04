#pragma once

#include <QString>

struct ItemInfo {
    QString category {};
    QString name {};
    QString comment {};
    QString iconName {};
    QString exec {};
    bool terminal {};
    bool root {};
    bool user {};
};
