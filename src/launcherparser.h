#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class LauncherParser
{
public:
    struct ParsedItem {
        QString app_name;
        QString category;
        QString alias;
        bool root {};
        bool user {};
        bool terminal {};
    };

    struct ParseResult {
        QString name;
        QString comment;
        QString icon_theme;
        QStringList categories;
        QList<ParsedItem> items;
    };

    static ParseResult parse(const QString &text, const QString &lang);
    static ParseResult parse_ini(const QString &file_path, const QString &lang);
    static QString extract_localized_value(const QString &text, const QString &key, const QString &lang);
};
