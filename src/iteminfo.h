#pragma once

#include <QString>
#include <QStringList>

struct ItemInfo {
    QString category {};
    QString name {};
    QString comment {};
    QString iconName {};
    QString exec {};
    QStringList execArgs {};
    bool terminal {};
    bool root {};
    bool user {};
};
