#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class QSettings;

class LauncherParser
{
public:
    struct ParsedItem {
        QString appName;
        QString category;
        QString alias;
        bool root {};
        bool user {};
        bool terminal {};
    };

    struct ParseResult {
        QString name;
        QString comment;
        QString iconTheme;
        QStringList categories;
        QList<ParsedItem> items;
    };

    static ParseResult parse(const QString &text, const QString &lang);
    static ParseResult parseIni(QSettings &settings, const QString &lang);
    static QString extractLocalizedValue(const QString &text, const QString &key, const QString &lang);
};
