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

QTEST_MAIN(LauncherParserTest)

#include "test_launcherparser.moc"
