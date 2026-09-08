#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "launchermodel.h"
#include "launcherparser.h"

class TestLauncherModel : public QObject
{
    Q_OBJECT

private slots:
    void filtersAndExposesLauncherRoles();
    void escapesAutostartExec_data();
    void escapesAutostartExec();
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

QTEST_MAIN(TestLauncherModel)
#include "test_launchermodel.moc"
