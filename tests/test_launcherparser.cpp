#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "launcherparser.h"

class LauncherParserTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesCustomFormatWithQuotedAliasAndComment();
    void skipsConflictingCustomFlags();
    void parsesIniFormatWithColonInAlias();
    void keepsLiteralIniAliasBoundaries_data();
    void keepsLiteralIniAliasBoundaries();
    void readsItemsFromEncodedSections_data();
    void readsItemsFromEncodedSections();
    void skipsConflictingIniFlags();
    void extractsLocalizedValuesFromCrLfText();
    void expandsEnvironmentVariablesInDesktopExec();
    void keepsEscapedAndMalformedReferencesLiteral();
    void dropsUnsetVariableToken();
    void decodesDesktopExecEscapes_data();
    void decodesDesktopExecEscapes();
};

void LauncherParserTest::parsesCustomFormatWithQuotedAliasAndComment()
{
    const auto result = LauncherParser::parse(
        "Name=Tools\nComment=My launcher\nCategory=Utilities\nexample root terminal alias 'Example #1' # note\n",
        "en_US");

    QCOMPARE(result.name, "Tools");
    QCOMPARE(result.comment, "My launcher");
    QCOMPARE(result.categories, QStringList {"Utilities"});
    QCOMPARE(result.items.size(), 1);
    const auto &item = result.items.first();
    QCOMPARE(item.appName, "example");
    QCOMPARE(item.category, "Utilities");
    QCOMPARE(item.alias, "Example #1");
    QVERIFY(item.root);
    QVERIFY(item.terminal);
    QVERIFY(!item.user);
}

void LauncherParserTest::skipsConflictingCustomFlags()
{
    const auto result = LauncherParser::parse("Category=Utilities\nexample root user\n", "en_US");

    QVERIFY(result.items.isEmpty());
}

void LauncherParserTest::parsesIniFormatWithColonInAlias()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("launcher.list");
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("Categories/list", QStringList {"Utilities"});
    settings.beginGroup("Utilities");
    settings.setValue("items", QStringList {"example:terminal:alias=\"Example: Tool\""});
    settings.endGroup();
    settings.sync();

    const auto result = LauncherParser::parseIni(settings, "en_US");

    QCOMPARE(result.items.size(), 1);
    const auto &item = result.items.first();
    QCOMPARE(item.appName, "example");
    QCOMPARE(item.alias, "Example: Tool");
    QVERIFY(item.terminal);
}

void LauncherParserTest::keepsLiteralIniAliasBoundaries_data()
{
    QTest::addColumn<QByteArray>("items");
    // Each expected entry is "appName|alias|flags", flags being r(oot), t(erminal), u(ser).
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("colon-flag-words") << QByteArray(R"(sample:alias="My:root:Tool")")
                                      << QStringList {"sample|My:root:Tool|"};
    QTest::newRow("escaped-quote") << QByteArray(R"(sample:alias="My\":root:Tool")")
                                   << QStringList {R"(sample|My":root:Tool|)"};
    QTest::newRow("comma-and-comment") << QByteArray(R"(other:terminal:alias="A, B":root ; note)")
                                       << QStringList {"other|A, B|rt"};
    QTest::newRow("continuation") << QByteArray("one:root,\\\n  two:terminal") << QStringList {"one||r", "two||t"};
    QTest::newRow("hex-escape") << QByteArray(R"(sample:alias="\x41lias")") << QStringList {"sample|Alias|"};
    QTest::newRow("single-quotes") << QByteArray("sample:alias='It:root'") << QStringList {"sample|It:root|"};
    QTest::newRow("unquoted-apostrophe") << QByteArray("sample:alias=Bob's:root") << QStringList {"sample|Bob's|r"};
    QTest::newRow("qsettings-quoted") << QByteArray(R"("sample:alias=\"Quoted, Item\":terminal", next)")
                                      << QStringList {"sample|Quoted, Item|t", "next||"};
    QTest::newRow("unterminated") << QByteArray(R"(sample:alias="Open:root)")
                                  << QStringList {R"(sample|"Open:root|)"};
}

void LauncherParserTest::keepsLiteralIniAliasBoundaries()
{
    QFETCH(QByteArray, items);
    QFETCH(QStringList, expected);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("launcher.list");
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly | QFile::Text));
    QVERIFY(file.write("[Categories]\nlist=Utilities\n\n[Utilities]\nitems=" + items + "\n") > 0);
    file.close();
    QSettings settings(path, QSettings::IniFormat);

    const auto result = LauncherParser::parseIni(settings, "en_US");

    QStringList actual;
    for (const auto &item : result.items) {
        QString flags;
        if (item.root) {
            flags += 'r';
        }
        if (item.terminal) {
            flags += 't';
        }
        if (item.user) {
            flags += 'u';
        }
        actual << item.appName + '|' + item.alias + '|' + flags;
    }
    QCOMPARE(actual, expected);
}

void LauncherParserTest::readsItemsFromEncodedSections_data()
{
    QTest::addColumn<QString>("category");
    QTest::addColumn<QByteArray>("section");
    QTest::newRow("unicode-escape") << QString::fromUtf8("工具") << QByteArray("[%U5DE5%U5177]\nitems=");
    QTest::newRow("lowercase-unicode-escape") << QString::fromUtf8("工具") << QByteArray("[%u5de5%u5177]\nitems=");
    QTest::newRow("percent-escape") << QString("Custom Tools") << QByteArray("[Custom%20Tools]\nitems=");
    QTest::newRow("latin1-escape") << QString::fromUtf8("Ünï") << QByteArray("[%DCn%EF]\nitems=");
    QTest::newRow("general-group-key") << QString("Utilities") << QByteArray("[General]\nUtilities\\items=");
    QTest::newRow("encoded-key") << QString("Utilities") << QByteArray("[ Utilities ]\n%69tems =");
}

void LauncherParserTest::readsItemsFromEncodedSections()
{
    QFETCH(QString, category);
    QFETCH(QByteArray, section);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("launcher.list");
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly | QFile::Text));
    QVERIFY(file.write("[Categories]\nlist=" + category.toUtf8() + "\n\n" + section
                       + R"(sample:alias="My:root:Tool")" + "\n")
            > 0);
    file.close();
    QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value("Categories/list").toString(), category);

    const auto result = LauncherParser::parseIni(settings, "en_US");

    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().category, category);
    QCOMPARE(result.items.first().alias, "My:root:Tool");
    QVERIFY(!result.items.first().root);
}

void LauncherParserTest::skipsConflictingIniFlags()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("launcher.list");
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("Categories/list", QStringList {"Utilities"});
    settings.beginGroup("Utilities");
    settings.setValue("items", QStringList {"example:root:user"});
    settings.endGroup();
    settings.sync();

    const auto result = LauncherParser::parseIni(settings, "en_US");

    QVERIFY(result.items.isEmpty());
}

void LauncherParserTest::extractsLocalizedValuesFromCrLfText()
{
    const QString text = "Name=Fallback\r\nName[en]=English\r\nName[en_US]=American English\r\n";

    QCOMPARE(LauncherParser::extractLocalizedValue(text, "Name", "en_US"), "American English");
    QCOMPARE(LauncherParser::extractLocalizedValue(text, "Name", "en_GB"), "English");
}

// Real-world desktop files (e.g. MX's nwipe.desktop) wrap their command in
// "pkexec env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY ..." and rely on the
// shell-style expansion the launcher historically provided.
void LauncherParserTest::expandsEnvironmentVariablesInDesktopExec()
{
    qputenv("CT_TEST_DISPLAY", ":7");
    qputenv("CT_TEST_XAUTH", "/home/user/.Xauthority");

    QString program;
    QStringList arguments;
    QVERIFY(LauncherParser::parseDesktopExec(
        "pkexec env DISPLAY=$CT_TEST_DISPLAY XAUTHORITY=${CT_TEST_XAUTH} x-terminal-emulator -e nwipe", "nwipe", "",
        "/usr/share/applications/nwipe.desktop", &program, &arguments));
    QCOMPARE(program, "pkexec");
    QCOMPARE(arguments,
             (QStringList {"env", "DISPLAY=:7", "XAUTHORITY=/home/user/.Xauthority", "x-terminal-emulator", "-e",
                           "nwipe"}));

    qunsetenv("CT_TEST_DISPLAY");
    qunsetenv("CT_TEST_XAUTH");
}

void LauncherParserTest::keepsEscapedAndMalformedReferencesLiteral()
{
    qputenv("CT_TEST_VAR", "value");

    QString program;
    QStringList arguments;
    QVERIFY(LauncherParser::parseDesktopExec(R"(app \$CT_TEST_VAR $1 ${CT_TEST_VAR file.txt)", "app", "", "app.desktop",
                                             &program, &arguments));
    QCOMPARE(program, "app");
    QCOMPARE(arguments, (QStringList {"$CT_TEST_VAR", "$1", "${CT_TEST_VAR", "file.txt"}));

    qunsetenv("CT_TEST_VAR");
}

void LauncherParserTest::dropsUnsetVariableToken()
{
    qunsetenv("CT_TEST_UNSET");

    QString program;
    QStringList arguments;
    QVERIFY(LauncherParser::parseDesktopExec("app $CT_TEST_UNSET arg VAR=$CT_TEST_UNSET", "app", "", "app.desktop",
                                             &program, &arguments));
    QCOMPARE(program, "app");
    QCOMPARE(arguments, (QStringList {"arg", "VAR="}));
}

void LauncherParserTest::decodesDesktopExecEscapes_data()
{
    QTest::addColumn<QString>("exec");
    QTest::addColumn<QStringList>("expected");

    // Raw strings represent the bytes in the desktop file, before either decoding pass.
    QTest::newRow("literal-backslash") << QString(R"(app "a\\\\b")") << QStringList {R"(a\b)"};
    QTest::newRow("trailing-backslash") << QString(R"(app "a\\\\")") << QStringList {R"(a\)"};
    QTest::newRow("literal-backslash-n") << QString(R"(app "a\\\\nb")") << QStringList {R"(a\nb)"};
    QTest::newRow("quoted-reserved-characters")
        << QString(R"(app "a\\"b" "\\$PATH" "\\`literal\\`")")
        << QStringList {"a\"b", "$PATH", "`literal`"};
    QTest::newRow("general-string-escapes")
        << QString(R"(app "a\sb\tc\nd\re" one\stwo)")
        << QStringList {"a b\tc\nd\re", "one", "two"};
}

void LauncherParserTest::decodesDesktopExecEscapes()
{
    QFETCH(QString, exec);
    QFETCH(QStringList, expected);
    QString program;
    QStringList arguments;
    QVERIFY(LauncherParser::parseDesktopExec(exec, "app", "", "app.desktop", &program, &arguments));
    QCOMPARE(program, "app");
    QCOMPARE(arguments, expected);
}

QTEST_MAIN(LauncherParserTest)

#include "test_launcherparser.moc"
