#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "common.h"
#include "launchermodel.h"
#include "launcherparser.h"

class TestLauncherModel : public QObject
{
    Q_OBJECT

private slots:
    void filtersAndExposesLauncherRoles();
    void escapesAutostartExec_data();
    void escapesAutostartExec();
    void autostartLocation_data();
    void autostartLocation();
    void readsLayoutSettings_data();
    void readsLayoutSettings();
};

void TestLauncherModel::filtersAndExposesLauncherRoles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("HOME", directory.path().toUtf8());
    qputenv("XDG_DATA_HOME", (directory.path() + QStringLiteral("/share")).toUtf8());

    const QString applications = directory.path() + QStringLiteral("/share/applications");
    QVERIFY(QDir().mkpath(applications));
    QFile desktopFile(applications + QStringLiteral("/sample.desktop"));
    QVERIFY(desktopFile.open(QFile::WriteOnly | QFile::Text));
    QTextStream desktop(&desktopFile);
    desktop << "[Desktop Entry]\nType=Application\nName=Sample Tool\nComment=A test launcher\n"
               "Icon=applications-utilities\nExec=/bin/sleep 0.2\nTerminal=false\n";
    desktopFile.close();

    const QString listPath = directory.path() + QStringLiteral("/model.list");
    QFile listFile(listPath);
    QVERIFY(listFile.open(QFile::WriteOnly | QFile::Text));
    QTextStream list(&listFile);
    list << "Name=Model Test\nComment=Model description\nCategory=First\n"
            "sample alias 'First Alias'\nCategory=Second\nsample alias 'Second Alias'\n";
    listFile.close();

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    LauncherIconProvider iconProvider;
    LauncherModel model(parser, listPath, &iconProvider);

    QCOMPARE(model.title(), QStringLiteral("Model Test"));
    QCOMPARE(model.description(), QStringLiteral("Model description"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.categories(), QStringList({QStringLiteral("All launchers"), QStringLiteral("First"),
                                               QStringLiteral("Second")}));
    QCOMPARE(model.data(model.index(0), LauncherModel::NameRole).toString(), QStringLiteral("First Alias"));
    QVERIFY(model.data(model.index(0), LauncherModel::IconSourceRole).toString().startsWith(
        QStringLiteral("image://launchericons/")));

    model.setSearch(QStringLiteral("second"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), LauncherModel::CategoryRole).toString(), QStringLiteral("Second"));

    model.setSearch({});
    model.setSelectedCategory(QStringLiteral("First"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), LauncherModel::NameRole).toString(), QStringLiteral("First Alias"));

    model.setSelectedCategory({});
    QSignalSpy errors(&model, &LauncherModel::errorOccurred);
    model.launch(model.data(model.index(0), LauncherModel::SourceIndexRole).toInt());
    model.launch(model.data(model.index(1), LauncherModel::SourceIndexRole).toInt());
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.constFirst().at(0).toString(), QStringLiteral("Launcher already running"));

    QTest::qWait(300);
    model.launch(model.data(model.index(1), LauncherModel::SourceIndexRole).toInt());
    QCOMPARE(errors.count(), 1);
}

void TestLauncherModel::escapesAutostartExec_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::newRow("invalid-field-code") << "100%tools.list";
    QTest::newRow("valid-field-code") << "tools%f.list";
    QTest::newRow("reserved-characters") << QString("tools \\\"$PATH`name`.list");
    QTest::newRow("whitespace-escapes") << QString("tools\n\r\t.list");
}

void TestLauncherModel::escapesAutostartExec()
{
    QFETCH(QString, fileName);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("HOME", directory.path().toUtf8());
    const QByteArray previousConfig = qgetenv("XDG_CONFIG_HOME");
    const auto restoreConfig = qScopeGuard([&] {
        if (previousConfig.isNull()) {
            qunsetenv("XDG_CONFIG_HOME");
        } else {
            qputenv("XDG_CONFIG_HOME", previousConfig);
        }
    });
    qunsetenv("XDG_CONFIG_HOME");
    const QString listPath = directory.filePath(fileName);
    QFile listFile(listPath);
    QVERIFY(listFile.open(QFile::WriteOnly));
    QVERIFY(listFile.write("Name=Autostart Test\nCategory=Utilities\n/bin/true\n") > 0);
    listFile.close();

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    LauncherIconProvider iconProvider;
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherModel model(parser, listPath, &iconProvider);
    QCOMPARE(model.rowCount(), 1);
    QSignalSpy errors(&model, &LauncherModel::errorOccurred);
    model.setStartupEnabled(true);
    QCOMPARE(errors.count(), 0);
    QVERIFY(model.startupEnabled());

    const QDir autostart(directory.filePath(".config/autostart"));
    const QStringList entries = autostart.entryList({"*.desktop"}, QDir::Files);
    QCOMPARE(entries.size(), 1);
    QFile desktopFile(autostart.filePath(entries.first()));
    QVERIFY(desktopFile.open(QFile::ReadOnly));
    const QString text = QString::fromUtf8(desktopFile.readAll());
    const QString exec = LauncherParser::extractLocalizedValue(text, "Exec", "en_US");
    QString program;
    QStringList arguments;
    QVERIFY(LauncherParser::parseDesktopExec(exec, "Autostart Test", "", desktopFile.fileName(),
                                             &program, &arguments));
    QCOMPARE(program, "custom-toolbox");
    QCOMPARE(arguments, QStringList {listPath});
}

void TestLauncherModel::autostartLocation_data()
{
    QTest::addColumn<bool>("customConfig");
    QTest::addColumn<bool>("legacy");
    QTest::newRow("default-create") << false << false;
    QTest::newRow("custom-create") << true << false;
    QTest::newRow("default-migrate") << false << true;
    QTest::newRow("custom-migrate") << true << true;
}

void TestLauncherModel::autostartLocation()
{
    QFETCH(bool, customConfig);
    QFETCH(bool, legacy);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousConfig = qgetenv("XDG_CONFIG_HOME");
    const auto restoreConfig = qScopeGuard([&] {
        if (previousConfig.isNull()) {
            qunsetenv("XDG_CONFIG_HOME");
        } else {
            qputenv("XDG_CONFIG_HOME", previousConfig);
        }
    });
    qputenv("HOME", directory.path().toUtf8());
    const QString config = directory.filePath(customConfig ? "custom config" : ".config");
    if (customConfig) {
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
    } else {
        qunsetenv("XDG_CONFIG_HOME");
    }
    const QString listPath = directory.filePath("tools.list");
    QFile listFile(listPath);
    QVERIFY(listFile.open(QFile::WriteOnly));
    QVERIFY(listFile.write("Name=Tools\nCategory=Utilities\n/bin/true\n") > 0);
    listFile.close();
    const QDir autostart(config + "/autostart");
    const QString legacyPath = autostart.filePath("tools.desktop");
    if (legacy) {
        QVERIFY(QDir().mkpath(autostart.path()));
        QFile legacyFile(legacyPath);
        QVERIFY(legacyFile.open(QFile::WriteOnly));
        QVERIFY(legacyFile.write(("[Desktop Entry]\nExec=custom-toolbox " + listPath + "\n").toUtf8()) > 0);
    }

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherIconProvider iconProvider;
    LauncherModel model(parser, listPath, &iconProvider);
    QCOMPARE(model.startupEnabled(), legacy);
    QSignalSpy errors(&model, &LauncherModel::errorOccurred);
    model.setStartupEnabled(true);
    QCOMPARE(errors.count(), 0);
    QVERIFY(model.startupEnabled());
    QVERIFY(!QFile::exists(legacyPath));
    QCOMPARE(autostart.entryList({"custom-toolbox-*.desktop"}, QDir::Files).size(), 1);
    if (customConfig) {
        QVERIFY(!QDir(directory.filePath(".config/autostart")).exists());
    }

    LauncherIconProvider reopenedIcons;
    LauncherModel reopened(parser, listPath, &reopenedIcons);
    QVERIFY(reopened.startupEnabled());
    QSignalSpy reopenedErrors(&reopened, &LauncherModel::errorOccurred);
    reopened.setStartupEnabled(false);
    QCOMPARE(reopenedErrors.count(), 0);
    QVERIFY(!reopened.startupEnabled());
    QVERIFY(autostart.entryList({"*.desktop"}, QDir::Files).isEmpty());
}

void TestLauncherModel::readsLayoutSettings_data()
{
    QTest::addColumn<QByteArray>("config");
    QTest::addColumn<QList<int>>("expected");
    QTest::newRow("defaults") << QByteArray() << QList<int> {720, 560, 0, 0};
    QTest::newRow("configured")
        << QByteArray("min_width=840\nmin_height=640\nicon_size=64\nfixed_number_columns=3\n")
        << QList<int> {840, 640, 64, 3};
    QTest::newRow("minimums")
        << QByteArray("min_width=100\nmin_height=-1\nicon_size=-2\nfixed_number_columns=-3\n")
        << QList<int> {300, 300, 0, 0};
    QTest::newRow("invalid")
        << QByteArray("min_width=bad\nmin_height=bad\nicon_size=bad\nfixed_number_columns=bad\n")
        << QList<int> {720, 560, 0, 0};
}

void TestLauncherModel::readsLayoutSettings()
{
    QFETCH(QByteArray, config);
    QFETCH(QList<int>, expected);
    QVERIFY(QDir().mkpath(Config::ConfigDir));
    QFile configFile(Config::ConfigFile);
    QVERIFY(configFile.open(QFile::WriteOnly));
    QCOMPARE(configFile.write(config), config.size());
    configFile.close();
    const auto removeConfig = qScopeGuard([] { QFile::remove(Config::ConfigFile); });

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString listPath = directory.filePath("layout.list");
    QFile listFile(listPath);
    QVERIFY(listFile.open(QFile::WriteOnly));
    QVERIFY(listFile.write("Name=Layout\nCategory=Utilities\n/bin/true\n") > 0);
    listFile.close();
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherIconProvider icons;
    LauncherModel model(parser, listPath, &icons);
    QCOMPARE(model.minimumWidth(), expected.at(0));
    QCOMPARE(model.minimumHeight(), expected.at(1));
    QCOMPARE(model.iconSize(), expected.at(2));
    QCOMPARE(model.fixedNumberColumns(), expected.at(3));
}

QTEST_MAIN(TestLauncherModel)
#include "test_launchermodel.moc"
