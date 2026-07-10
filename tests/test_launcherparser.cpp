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
    void skipsConflictingIniFlags();
    void extractsLocalizedValuesFromCrLfText();
    void expandsEnvironmentVariablesInDesktopExec();
    void keepsEscapedAndMalformedReferencesLiteral();
    void dropsUnsetVariableToken();
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

QTEST_MAIN(LauncherParserTest)

#include "test_launcherparser.moc"
