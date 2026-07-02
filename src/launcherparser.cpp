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

// Return the index of the first '#' that begins an inline comment, i.e. one that
// is not enclosed in single or double quotes. Returns -1 if there is none.
qsizetype unquotedCommentIndex(QStringView line)
{
    QChar quote;
    bool inQuote = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (inQuote) {
            if (c == quote) {
                inQuote = false;
            }
        } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            inQuote = true;
            quote = c;
        } else if (c == QLatin1Char('#')) {
            return i;
        }
    }
    return -1;
}
} // namespace

QString LauncherParser::extractLocalizedValue(const QString &text, const QString &key, const QString &lang)
{
    // trimmed() drops trailing whitespace, including the stray '\r' left behind
    // when a .list/.desktop file uses Windows (CRLF) line endings. Without it an
    // Exec value would carry a '\r' and fail to launch.
    const QString fullPattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang);
    auto match = cached_re(fullPattern).match(text);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    const QString shortPattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang.section('_', 0, 0));
    match = cached_re(shortPattern).match(text);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    const QString fallbackPattern = QStringLiteral("^%1=(.*)$").arg(key);
    match = cached_re(fallbackPattern).match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

LauncherParser::ParseResult LauncherParser::parse(const QString &text, const QString &lang)
{
    ParseResult result;
    result.name = extractLocalizedValue(text, QStringLiteral("Name"), lang);
    result.comment = extractLocalizedValue(text, QStringLiteral("Comment"), lang);
    result.categories.reserve(20);

    // Skip the launcher's own Name=/Comment= header lines (with optional locale
    // suffix), comment lines and blank lines. Anchored on "=" so app entries whose
    // executable name merely starts with "Name"/"Comment" are not dropped.
    static const QRegularExpression skipPattern(QStringLiteral("^(Name|Comment)(\\[.*])?=|^#|^$"));

    QStringView textView(text);
    qsizetype pos = 0;
    while (pos < textView.size()) {
        qsizetype endPos = textView.indexOf(QLatin1Char('\n'), pos);
        if (endPos == -1) {
            endPos = textView.size();
        }
        const QStringView rawLine = textView.mid(pos, endPos - pos);
        pos = endPos + 1;

        // Strip inline comments: truncate at the first unquoted '#'. This keeps
        // trailing notes (e.g. `app root  # runs elevated`) from being tokenized
        // as flags, and prevents `app alias 'X'  # note` from bleeding into the
        // alias. '#' inside single/double quotes is preserved.
        const qsizetype commentPos = unquotedCommentIndex(rawLine);
        const QStringView lineView = commentPos >= 0 ? rawLine.left(commentPos) : rawLine;

        if (lineView.isEmpty()) {
            continue;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (skipPattern.matchView(lineView).hasMatch()) {
            continue;
        }
#else
        if (skipPattern.match(lineView.toString()).hasMatch()) {
            continue;
        }
#endif

        const qsizetype splitPos = lineView.indexOf(QLatin1Char('='));
        QStringView keyView;
        QStringView valueView;
        if (splitPos > 0) {
            keyView = lineView.left(splitPos).trimmed();
            valueView = lineView.mid(splitPos + 1).trimmed();
            if (valueView.startsWith(QLatin1Char('"')) && valueView.endsWith(QLatin1Char('"'))) {
                valueView = valueView.mid(1, valueView.size() - 2);
            }
        } else {
            keyView = lineView.trimmed();
        }

        if (keyView.isEmpty()) {
            continue;
        }

        const QString key = keyView.toString();
        const QString lowerKey = key.toLower();

        if (lowerKey == QLatin1String("category")) {
            result.categories.append(valueView.toString());
            continue;
        }
        if (lowerKey == QLatin1String("theme")) {
            result.iconTheme = valueView.toString();
            continue;
        }

        const QStringList keyTokens = key.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (keyTokens.isEmpty()) {
            continue;
        }
        if (result.categories.isEmpty()) {
            // Mirror original behaviour: items before any Category= are dropped.
            continue;
        }

        ParsedItem item;
        item.appName = keyTokens.first();
        item.category = result.categories.last();

        if (keyTokens.size() > 1) {
            // Everything after the "alias" keyword is alias text, so only the
            // tokens between the app name and "alias" may act as flags. This
            // keeps an alias like "run as root" from silently elevating the item.
            const int aliasIndex = keyTokens.indexOf(QLatin1String("alias"));
            const QStringList flagTokens = aliasIndex >= 0 ? keyTokens.mid(1, aliasIndex - 1) : keyTokens.mid(1);
            item.root = flagTokens.contains(QLatin1String("root"));
            item.user = flagTokens.contains(QLatin1String("user"));
            item.terminal = flagTokens.contains(QLatin1String("terminal"));

            if (aliasIndex >= 0) {
                QString alias;
                if (aliasIndex + 1 < keyTokens.size()) {
                    // Space-separated form: `app alias "My App"`
                    alias = keyTokens.mid(aliasIndex + 1).join(QLatin1Char(' ')).trimmed();
                } else if (!valueView.isEmpty()) {
                    // `=`-separated form: `app alias="My App"` — valueView holds the alias.
                    alias = valueView.toString();
                }
                if ((alias.startsWith(QLatin1Char('"')) && alias.endsWith(QLatin1Char('"')))
                    || (alias.startsWith(QLatin1Char('\'')) && alias.endsWith(QLatin1Char('\'')))) {
                    alias = alias.mid(1, alias.size() - 2);
                }
                if (alias.isEmpty()) {
                    qWarning() << "Alias keyword found but no valid alias name provided.";
                } else {
                    item.alias = alias;
                }
            }
        }

        result.items.append(item);
    }

    return result;
}

LauncherParser::ParseResult LauncherParser::parseIni(QSettings &settings, const QString &lang)
{
    ParseResult result;

    // Keys under [General] are stored at root scope by QSettings.
    auto getLocalized = [&](const QString &key) -> QString {
        QString val = settings.value(QStringLiteral("%1[%2]").arg(key, lang)).toString();
        if (val.isEmpty()) {
            val = settings.value(QStringLiteral("%1[%2]").arg(key, lang.section('_', 0, 0))).toString();
        }
        if (val.isEmpty()) {
            val = settings.value(key).toString();
        }
        return val;
    };

    result.name = getLocalized(QStringLiteral("Name"));
    result.comment = getLocalized(QStringLiteral("Comment"));
    result.iconTheme = settings.value(QStringLiteral("IconTheme")).toString();

    const QStringList categoryList = settings.value(QStringLiteral("Categories/list")).toStringList();
    for (const QString &catName : categoryList) {
        const QString trimmedCat = catName.trimmed();
        if (trimmedCat.isEmpty()) {
            continue;
        }
        result.categories.append(trimmedCat);

        settings.beginGroup(trimmedCat);
        const QStringList rawItems = settings.value(QStringLiteral("items")).toStringList();
        settings.endGroup();

        for (const QString &rawItem : rawItems) {
            const QString trimmedItem = rawItem.trimmed();
            if (trimmedItem.isEmpty()) {
                continue;
            }

            ParsedItem item;
            item.category = trimmedCat;

            // Handle flags: item:root:terminal:alias="My Name"
            const QStringList tokens = trimmedItem.split(QLatin1Char(':'));
            item.appName = tokens.first();

            for (int i = 1; i < tokens.size(); ++i) {
                const QString token = tokens.at(i);
                if (token == QLatin1String("root")) {
                    item.root = true;
                } else if (token == QLatin1String("user")) {
                    item.user = true;
                } else if (token == QLatin1String("terminal")) {
                    item.terminal = true;
                } else if (token.startsWith(QLatin1String("alias="))) {
                    // The alias is the last field: rejoin the remaining tokens so
                    // an alias containing ':' is not truncated by the split above.
                    QString alias = QStringList(tokens.mid(i)).join(QLatin1Char(':')).mid(6);
                    if ((alias.startsWith(QLatin1Char('"')) && alias.endsWith(QLatin1Char('"')))
                        || (alias.startsWith(QLatin1Char('\'')) && alias.endsWith(QLatin1Char('\'')))) {
                        alias = alias.mid(1, alias.size() - 2);
                    }
                    item.alias = alias;
                    break;
                }
            }
            result.items.append(item);
        }
    }

    return result;
}
