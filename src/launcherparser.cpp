#include "launcherparser.h"

#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStringView>

namespace
{
QRegularExpression &cached_re(const QString &pattern)
{
    static QHash<QString, QRegularExpression> cache;
    auto it = cache.find(pattern);
    if (it == cache.end()) {
        it = cache.insert(pattern, QRegularExpression(pattern, QRegularExpression::MultilineOption));
    }
    return it.value();
}
} // namespace

QString LauncherParser::extract_localized_value(const QString &text, const QString &key, const QString &lang)
{
    const QString full_pattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang);
    auto match = cached_re(full_pattern).match(text);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    const QString short_pattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang.section('_', 0, 0));
    match = cached_re(short_pattern).match(text);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    const QString fallback_pattern = QStringLiteral("^%1=(.*)$").arg(key);
    match = cached_re(fallback_pattern).match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

LauncherParser::ParseResult LauncherParser::parse(const QString &text, const QString &lang)
{
    ParseResult result;
    result.name = extract_localized_value(text, QStringLiteral("Name"), lang);
    result.comment = extract_localized_value(text, QStringLiteral("Comment"), lang);
    result.categories.reserve(20);

    static const QRegularExpression skip_pattern(QStringLiteral("^(Name|Comment|#|$).*"));

    QStringView text_view(text);
    qsizetype pos = 0;
    while (pos < text_view.size()) {
        qsizetype end_pos = text_view.indexOf(QLatin1Char('\n'), pos);
        if (end_pos == -1) {
            end_pos = text_view.size();
        }
        const QStringView line_view = text_view.mid(pos, end_pos - pos);
        pos = end_pos + 1;

        if (line_view.isEmpty()) {
            continue;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (skip_pattern.matchView(line_view).hasMatch()) {
            continue;
        }
#else
        if (skip_pattern.match(line_view.toString()).hasMatch()) {
            continue;
        }
#endif

        const qsizetype split_pos = line_view.indexOf(QLatin1Char('='));
        QStringView key_view;
        QStringView value_view;
        if (split_pos > 0) {
            key_view = line_view.left(split_pos).trimmed();
            value_view = line_view.mid(split_pos + 1).trimmed();
            if (value_view.startsWith(QLatin1Char('"')) && value_view.endsWith(QLatin1Char('"'))) {
                value_view = value_view.mid(1, value_view.size() - 2);
            }
        } else {
            key_view = line_view.trimmed();
        }

        if (key_view.isEmpty()) {
            continue;
        }

        const QString key = key_view.toString();
        const QString lower_key = key.toLower();

        if (lower_key == QLatin1String("category")) {
            result.categories.append(value_view.toString());
            continue;
        }
        if (lower_key == QLatin1String("theme")) {
            result.icon_theme = value_view.toString();
            continue;
        }

        const QStringList key_tokens = key.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (key_tokens.isEmpty()) {
            continue;
        }
        if (result.categories.isEmpty()) {
            // Mirror original behaviour: items before any Category= are dropped.
            continue;
        }

        ParsedItem item;
        item.app_name = key_tokens.first();
        item.category = result.categories.last();

        if (key_tokens.size() > 1) {
            item.root = key_tokens.contains(QLatin1String("root"));
            item.user = key_tokens.contains(QLatin1String("user"));
            item.terminal = key_tokens.contains(QLatin1String("terminal"));

            const int alias_index = key_tokens.indexOf(QLatin1String("alias"));
            if (alias_index >= 0) {
                if (alias_index + 1 < key_tokens.size()) {
                    QString alias = key_tokens.mid(alias_index + 1).join(QLatin1Char(' ')).trimmed();
                    if ((alias.startsWith(QLatin1Char('"')) && alias.endsWith(QLatin1Char('"')))
                        || (alias.startsWith(QLatin1Char('\'')) && alias.endsWith(QLatin1Char('\'')))) {
                        alias = alias.mid(1, alias.size() - 2);
                    }
                    item.alias = alias;
                } else {
                    qWarning() << "Alias keyword found but no valid alias name provided.";
                }
            }
        }

        result.items.append(item);
    }

    return result;
}

LauncherParser::ParseResult LauncherParser::parse_ini(const QString &file_path, const QString &lang)
{
    ParseResult result;
    QSettings settings(file_path, QSettings::IniFormat);

    auto get_localized = [&](const QString &group, const QString &key) -> QString {
        if (!group.isEmpty()) {
            settings.beginGroup(group);
        }
        QString val = settings.value(QStringLiteral("%1[%2]").arg(key, lang)).toString();
        if (val.isEmpty()) {
            val = settings.value(QStringLiteral("%1[%2]").arg(key, lang.section('_', 0, 0))).toString();
        }
        if (val.isEmpty()) {
            val = settings.value(key).toString();
        }
        if (!group.isEmpty()) {
            settings.endGroup();
        }
        return val;
    };

    result.name = get_localized(QStringLiteral("General"), QStringLiteral("Name"));
    result.comment = get_localized(QStringLiteral("General"), QStringLiteral("Comment"));
    result.icon_theme = settings.value(QStringLiteral("General/IconTheme")).toString();

    const QStringList category_list = settings.value(QStringLiteral("Categories/list")).toStringList();
    for (const QString &cat_name : category_list) {
        const QString trimmed_cat = cat_name.trimmed();
        if (trimmed_cat.isEmpty()) {
            continue;
        }
        result.categories.append(trimmed_cat);

        settings.beginGroup(trimmed_cat);
        const QStringList raw_items = settings.value(QStringLiteral("items")).toStringList();
        settings.endGroup();

        for (const QString &raw_item : raw_items) {
            const QString trimmed_item = raw_item.trimmed();
            if (trimmed_item.isEmpty()) {
                continue;
            }

            ParsedItem item;
            item.category = trimmed_cat;

            // Handle flags: item:root:terminal:alias="My Name"
            // Simple split by ':' but need to respect quotes for alias
            QString name_part;
            QString remaining = trimmed_item;

            // Basic parsing for flags
            const QStringList tokens = remaining.split(QLatin1Char(':'));
            if (tokens.isEmpty()) {
                continue;
            }
            item.app_name = tokens.first();

            for (int i = 1; i < tokens.size(); ++i) {
                const QString token = tokens.at(i);
                if (token == QLatin1String("root")) {
                    item.root = true;
                } else if (token == QLatin1String("user")) {
                    item.user = true;
                } else if (token == QLatin1String("terminal")) {
                    item.terminal = true;
                } else if (token.startsWith(QLatin1String("alias="))) {
                    QString alias = token.mid(6);
                    if ((alias.startsWith(QLatin1Char('"')) && alias.endsWith(QLatin1Char('"')))
                        || (alias.startsWith(QLatin1Char('\'')) && alias.endsWith(QLatin1Char('\'')))) {
                        alias = alias.mid(1, alias.size() - 2);
                    }
                    item.alias = alias;
                }
            }
            result.items.append(item);
        }
    }

    return result;
}
