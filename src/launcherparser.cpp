#include "launcherparser.h"

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStringView>

#include <optional>

namespace
{
QRegularExpression &cachedRegex(const QString &pattern)
{
    static QHash<QString, QRegularExpression> cache;
    auto it = cache.find(pattern);
    if (it == cache.end()) {
        it = cache.insert(pattern, QRegularExpression(pattern, QRegularExpression::MultilineOption));
    }
    return it.value();
}

// INI "items" values are carried in an escaped form between reading and flag parsing:
// characters that came from an INI escape sequence and could be mistaken for alias quotes,
// flag separators or escapes (", ', : and \) are prefixed with a backslash, while the bare
// quotes the user wrote around an alias are kept as they are.
void appendIniLiteral(QString *out, QChar c)
{
    if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char(':') || c == QLatin1Char('\\')) {
        out->append(QLatin1Char('\\'));
    }
    out->append(c);
}

bool isEscapedAt(QStringView text, qsizetype index)
{
    qsizetype backslashes = 0;
    while (index - backslashes > 0 && text[index - backslashes - 1] == QLatin1Char('\\')) {
        ++backslashes;
    }
    return backslashes % 2 == 1;
}

// Decode a raw INI list value the way QSettings does (escapes, line continuations, ','
// separators, ';' comments, quotes wrapping a whole element), except that a double quote
// inside an element is kept, because QSettings would erase the boundaries of an alias such
// as alias="My:root:Tool" and let its contents be read as flags.
QStringList decodeRawIniList(QStringView value)
{
    QStringList elements;
    QString current;
    bool quoted = false;
    auto finishElement = [&elements, &current] {
        QStringView element = QStringView(current).trimmed();
        if (element.size() >= 2 && element.front() == QLatin1Char('"') && element.back() == QLatin1Char('"')
            && !isEscapedAt(element, element.size() - 1)) {
            qsizetype closing = 1;
            while (closing < element.size()
                   && (element[closing] != QLatin1Char('"') || isEscapedAt(element, closing))) {
                ++closing;
            }
            if (closing == element.size() - 1) {
                element = element.mid(1, element.size() - 2);
            }
        }
        elements.append(element.toString());
        current.clear();
    };
    auto isOctal = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('7'); };
    auto hexDigit = [](QChar c) -> int {
        const char16_t u = c.unicode();
        if (u >= u'0' && u <= u'9') {
            return u - u'0';
        }
        if (u >= u'a' && u <= u'f') {
            return u - u'a' + 10;
        }
        if (u >= u'A' && u <= u'F') {
            return u - u'A' + 10;
        }
        return -1;
    };
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value[i];
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            current.append(c);
        } else if (!quoted && c == QLatin1Char(',')) {
            finishElement();
        } else if (!quoted && c == QLatin1Char(';')) {
            break;
        } else if (c != QLatin1Char('\\')) {
            current.append(c);
        } else if (++i < value.size()) {
            const QChar next = value[i];
            switch (next.unicode()) {
            case 'a':
                current.append(QLatin1Char('\a'));
                break;
            case 'b':
                current.append(QLatin1Char('\b'));
                break;
            case 'f':
                current.append(QLatin1Char('\f'));
                break;
            case 'n':
                current.append(QLatin1Char('\n'));
                break;
            case 'r':
                current.append(QLatin1Char('\r'));
                break;
            case 't':
                current.append(QLatin1Char('\t'));
                break;
            case 'v':
                current.append(QLatin1Char('\v'));
                break;
            case '"':
            case '\'':
            case '?':
            case '\\':
                appendIniLiteral(&current, next);
                break;
            case 'x': {
                char32_t code = 0;
                while (i + 1 < value.size() && hexDigit(value[i + 1]) >= 0) {
                    code = code * 16 + static_cast<char32_t>(hexDigit(value[++i]));
                }
                appendIniLiteral(&current, QChar(static_cast<char16_t>(code)));
                break;
            }
            case '\n':
                break; // line continuation
            default:
                if (isOctal(next)) {
                    char32_t code = next.unicode() - '0';
                    while (i + 1 < value.size() && isOctal(value[i + 1])) {
                        code = code * 8 + (value[++i].unicode() - '0');
                    }
                    appendIniLiteral(&current, QChar(static_cast<char16_t>(code)));
                }
                // QSettings drops any other escaped character along with its backslash.
                break;
            }
        }
    }
    if (!current.isEmpty() || !elements.isEmpty()) {
        finishElement();
    }
    return elements;
}

// Decode an INI section or key name the way QSettings does: %Uhhhh is a UTF-16 code
// unit, %XX a Latin-1 character and '\' a group separator.
QString decodeIniKey(QStringView key)
{
    auto hexValue = [](QStringView digits) {
        int value = 0;
        for (const QChar digit : digits) {
            const int nibble = QStringLiteral("0123456789abcdef").indexOf(digit.toLower());
            if (nibble < 0) {
                return -1;
            }
            value = value * 16 + nibble;
        }
        return value;
    };
    QString result;
    result.reserve(key.size());
    for (qsizetype i = 0; i < key.size(); ++i) {
        const QChar c = key[i];
        if (c == QLatin1Char('\\')) {
            result.append(QLatin1Char('/'));
            continue;
        }
        if (c == QLatin1Char('%') && i + 5 < key.size()
            && (key[i + 1] == QLatin1Char('U') || key[i + 1] == QLatin1Char('u'))) {
            if (const int code = hexValue(key.mid(i + 2, 4)); code >= 0) {
                result.append(QChar(static_cast<char16_t>(code)));
                i += 5;
                continue;
            }
        }
        if (c == QLatin1Char('%') && i + 2 < key.size()) {
            if (const int code = hexValue(key.mid(i + 1, 2)); code >= 0) {
                result.append(QChar(static_cast<char16_t>(code)));
                i += 2;
                continue;
            }
        }
        result.append(c);
    }
    return result;
}

// Read every raw "items" value, keyed by its full QSettings key (e.g. "Utilities/items").
// Lines are joined at backslash-newline continuations, as QSettings does. Returns
// std::nullopt if the file cannot be read.
std::optional<QHash<QString, QStringList>> rawIniItems(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QFile::ReadOnly | QFile::Text)) {
        return std::nullopt;
    }
    const QString text = QString::fromUtf8(file.readAll());
    QStringList lines;
    QString line;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('\\') && i + 1 < text.size()) {
            line.append(text[i]);
            line.append(text[++i]);
        } else if (text[i] == QLatin1Char('\n')) {
            lines.append(line);
            line.clear();
        } else {
            line.append(text[i]);
        }
    }
    lines.append(line);

    QHash<QString, QStringList> items;
    QString section;
    for (const QString &rawLine : std::as_const(lines)) {
        const QStringView logicalLine = QStringView(rawLine).trimmed();
        if (logicalLine.startsWith(QLatin1Char('['))) {
            const qsizetype end = logicalLine.lastIndexOf(QLatin1Char(']'));
            if (end > 0) {
                const QStringView rawSection = logicalLine.mid(1, end - 1).trimmed();
                if (rawSection.compare(QLatin1String("general"), Qt::CaseInsensitive) == 0) {
                    section.clear();
                } else if (rawSection == QLatin1String("%General")) {
                    section = QStringLiteral("General");
                } else {
                    section = decodeIniKey(rawSection);
                }
            }
            continue;
        }
        const qsizetype separator = logicalLine.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        const QString key = decodeIniKey(logicalLine.left(separator).trimmed());
        if (key == QLatin1String("items") || key.endsWith(QLatin1String("/items"))) {
            items.insert(section.isEmpty() ? key : section + QLatin1Char('/') + key,
                         decodeRawIniList(logicalLine.mid(separator + 1)));
        }
    }
    return items;
}

// Split an escaped item at ':' flag separators. A quote right after "alias=" opens a
// region that runs to the matching closing quote, so a quoted alias may contain ':'.
// The closing quote must be of the same kind as the opening one: a bare quote (as written
// by hand) or an escaped one (as QSettings writes it). Inside a bare-quoted alias an
// escaped quote is literal. An unterminated quote runs to the end, so nothing after it is
// read as a flag.
QStringList splitIniItem(QStringView item)
{
    QStringList tokens;
    QString current;
    QChar quote;
    bool escapedQuote = false;
    auto isQuote = [](QChar c) { return c == QLatin1Char('"') || c == QLatin1Char('\''); };
    for (qsizetype i = 0; i < item.size(); ++i) {
        const QChar c = item[i];
        if (c == QLatin1Char('\\') && i + 1 < item.size()) {
            const QChar next = item[++i];
            current.append(c);
            current.append(next);
            if (quote.isNull() && isQuote(next) && current.size() == 8 && current.startsWith(QLatin1String("alias="))) {
                quote = next;
                escapedQuote = true;
            } else if (escapedQuote && next == quote) {
                quote = QChar();
                escapedQuote = false;
            }
        } else if (!quote.isNull()) {
            current.append(c);
            if (!escapedQuote && c == quote) {
                quote = QChar();
            }
        } else if (isQuote(c) && current == QLatin1String("alias=")) {
            quote = c;
            current.append(c);
        } else if (c == QLatin1Char(':')) {
            tokens.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    tokens.append(current);
    return tokens;
}

// Remove the quotes around an escaped alias value, whether bare or escaped.
QStringView stripAliasQuotes(QStringView alias)
{
    auto isQuote = [](QChar c) { return c == QLatin1Char('"') || c == QLatin1Char('\''); };
    if (alias.size() >= 2 && isQuote(alias.front()) && alias.back() == alias.front()
        && !isEscapedAt(alias, alias.size() - 1)) {
        return alias.mid(1, alias.size() - 2);
    }
    if (alias.size() >= 4 && alias.front() == QLatin1Char('\\') && isQuote(alias[1]) && alias.back() == alias[1]
        && isEscapedAt(alias, alias.size() - 1)) {
        return alias.mid(2, alias.size() - 4);
    }
    return alias;
}

QString unescapeIniToken(QStringView token)
{
    QString result;
    result.reserve(token.size());
    for (qsizetype i = 0; i < token.size(); ++i) {
        if (token[i] == QLatin1Char('\\') && i + 1 < token.size()) {
            ++i;
        }
        result.append(token[i]);
    }
    return result;
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

// Decode the Desktop Entry string escapes \s, \n, \t, \r and \\; other escapes are kept.
QString LauncherParser::decodeDesktopString(const QString &value)
{
    QString decoded;
    decoded.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\') || i + 1 == value.size()) {
            decoded.append(c);
            continue;
        }
        switch (value.at(i + 1).unicode()) {
        case 's':
            decoded.append(QLatin1Char(' '));
            break;
        case 'n':
            decoded.append(QLatin1Char('\n'));
            break;
        case 't':
            decoded.append(QLatin1Char('\t'));
            break;
        case 'r':
            decoded.append(QLatin1Char('\r'));
            break;
        case '\\':
            decoded.append(QLatin1Char('\\'));
            break;
        default:
            // Preserve nonstandard escapes such as \$ for the Exec pass,
            // maintaining compatibility with existing launcher wrappers.
            decoded.append(c);
            continue;
        }
        ++i;
    }
    return decoded;
}

bool LauncherParser::parseDesktopExec(const QString &rawExec, const QString &applicationName, const QString &iconName,
                                      const QString &desktopFile, QString *program, QStringList *arguments)
{
    // Desktop Entry string escaping is a separate layer from Exec argument
    // quoting: four backslashes in the file become two here, then one below.
    const QString exec = decodeDesktopString(rawExec);

    QStringList parts;
    QString current;
    bool inQuotes {};
    bool tokenStarted {};

    auto appendCurrent = [&]() {
        if (tokenStarted) {
            parts.append(current);
            current.clear();
            tokenStarted = false;
        }
    };

    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            tokenStarted = true;
            continue;
        }
        if (c == QLatin1Char('\\')) {
            if (++i >= exec.size()) {
                return false;
            }
            current.append(exec.at(i));
            tokenStarted = true;
            continue;
        }
        if (c.isSpace() && !inQuotes) {
            appendCurrent();
            continue;
        }
        if (c == QLatin1Char('$')) {
            // Environment references are not part of the Desktop Entry spec, but
            // files in the wild (e.g. "pkexec env DISPLAY=$DISPLAY ..." wrappers)
            // rely on the shell expansion the launcher used to provide. Expand
            // unescaped $NAME/${NAME} like a shell; anything else stays literal.
            const bool braced = i + 1 < exec.size() && exec.at(i + 1) == QLatin1Char('{');
            const qsizetype nameStart = i + (braced ? 2 : 1);
            qsizetype nameEnd = nameStart;
            while (nameEnd < exec.size()) {
                const char16_t u = exec.at(nameEnd).unicode();
                const bool nameChar = u == u'_' || (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z')
                                      || (nameEnd > nameStart && u >= u'0' && u <= u'9');
                if (!nameChar) {
                    break;
                }
                ++nameEnd;
            }
            const bool closed = !braced || (nameEnd < exec.size() && exec.at(nameEnd) == QLatin1Char('}'));
            if (nameEnd > nameStart && closed) {
                const QString value
                    = qEnvironmentVariable(exec.mid(nameStart, nameEnd - nameStart).toUtf8().constData());
                current.append(value);
                // Like a shell, an unquoted reference to an unset/empty variable
                // produces no token on its own.
                tokenStarted = tokenStarted || !value.isEmpty();
                i = braced ? nameEnd : nameEnd - 1;
                continue;
            }
            current.append(c);
            tokenStarted = true;
            continue;
        }
        if (c != QLatin1Char('%')) {
            current.append(c);
            tokenStarted = true;
            continue;
        }
        if (++i >= exec.size()) {
            return false;
        }

        switch (exec.at(i).unicode()) {
        case '%':
            current.append(QLatin1Char('%'));
            tokenStarted = true;
            break;
        case 'f':
        case 'F':
        case 'u':
        case 'U':
        case 'd':
        case 'D':
        case 'n':
        case 'N':
        case 'v':
        case 'm':
            break;
        case 'c':
            current.append(applicationName);
            tokenStarted = true;
            break;
        case 'k':
            current.append(desktopFile);
            tokenStarted = true;
            break;
        case 'i':
            if (inQuotes || tokenStarted) {
                return false;
            }
            if (!iconName.isEmpty()) {
                parts << QStringLiteral("--icon") << iconName;
            }
            break;
        default:
            return false;
        }
    }

    if (inQuotes) {
        return false;
    }
    appendCurrent();
    if (parts.isEmpty()) {
        return false;
    }

    if (program != nullptr) {
        *program = parts.takeFirst();
    }
    if (arguments != nullptr) {
        *arguments = parts;
    }
    return true;
}

QString LauncherParser::extractLocalizedValue(const QString &text, const QString &key, const QString &lang)
{
    // trimmed() drops trailing whitespace, including the stray '\r' left behind
    // when a .list/.desktop file uses Windows (CRLF) line endings. Without it an
    // Exec value would carry a '\r' and fail to launch.
    const QString fullPattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang);
    auto match = cachedRegex(fullPattern).match(text);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    const QString shortPattern = QStringLiteral("^%1\\[%2]=(.*)$").arg(key, lang.section('_', 0, 0));
    match = cachedRegex(shortPattern).match(text);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    const QString fallbackPattern = QStringLiteral("^%1=(.*)$").arg(key);
    match = cachedRegex(fallbackPattern).match(text);
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
            const qsizetype aliasIndex = keyTokens.indexOf(QLatin1String("alias"));
            const QStringList flagTokens = aliasIndex >= 0 ? keyTokens.mid(1, aliasIndex - 1) : keyTokens.mid(1);
            item.root = flagTokens.contains(QLatin1String("root"));
            item.user = flagTokens.contains(QLatin1String("user"));
            item.terminal = flagTokens.contains(QLatin1String("terminal"));

            if (item.root && item.user) {
                qWarning() << "Skipping launcher entry with conflicting root and user flags:" << item.appName;
                continue;
            }

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
    // Items come from the raw file so alias quotes survive (see decodeRawIniList). Values
    // decoded by QSettings are used only when the file cannot be read directly.
    const std::optional<QHash<QString, QStringList>> rawFileItems = rawIniItems(settings.fileName());
    for (const QString &catName : categoryList) {
        const QString trimmedCat = catName.trimmed();
        if (trimmedCat.isEmpty()) {
            continue;
        }
        result.categories.append(trimmedCat);

        QStringList rawItems;
        if (rawFileItems) {
            rawItems = rawFileItems->value(trimmedCat + QLatin1String("/items"));
        } else {
            // Values already decoded by QSettings carry no escapes of their own.
            settings.beginGroup(trimmedCat);
            rawItems = settings.value(QStringLiteral("items")).toStringList();
            settings.endGroup();
            for (QString &rawItem : rawItems) {
                rawItem.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
            }
        }

        for (const QString &rawItem : rawItems) {
            const QString trimmedItem = rawItem.trimmed();
            if (trimmedItem.isEmpty()) {
                continue;
            }

            ParsedItem item;
            item.category = trimmedCat;

            // Handle flags: item:root:terminal:alias="My Name"
            const QStringList tokens = splitIniItem(trimmedItem);
            item.appName = unescapeIniToken(tokens.first());

            for (qsizetype i = 1; i < tokens.size(); ++i) {
                const QStringView token = tokens.at(i);
                if (token == QLatin1String("root")) {
                    item.root = true;
                } else if (token == QLatin1String("user")) {
                    item.user = true;
                } else if (token == QLatin1String("terminal")) {
                    item.terminal = true;
                } else if (token.startsWith(QLatin1String("alias="))) {
                    item.alias = unescapeIniToken(stripAliasQuotes(token.mid(6)));
                }
            }
            if (item.root && item.user) {
                qWarning() << "Skipping launcher entry with conflicting root and user flags:" << item.appName;
                continue;
            }
            result.items.append(item);
        }
    }

    return result;
}
