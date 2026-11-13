#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include <unistd.h>

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
    void launchesInDesktopWorkingDirectory_data();
    void launchesInDesktopWorkingDirectory();
    void tracksLaunchersPerWorkingDirectory();
    void leavesTargetAutostartWhenOpeningSymlink();
    void keepsAutostartAcrossSymlinkRetarget();
    void reloadsEditedDesktopFile();
};

namespace
{
bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QFile::WriteOnly | QFile::Truncate) && file.write(contents) == contents.size();
}

QString sourceHash(const QString &path)
{
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace

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
    LauncherModel model(parser, listPath, iconProvider);

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

    QTest::qWait(600);
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
    LauncherModel model(parser, listPath, iconProvider);
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
    LauncherModel model(parser, listPath, iconProvider);
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
    LauncherModel reopened(parser, listPath, reopenedIcons);
    QVERIFY(reopened.startupEnabled());
    QSignalSpy reopenedErrors(&reopened, &LauncherModel::errorOccurred);
    reopened.setStartupEnabled(false);
    QCOMPARE(reopenedErrors.count(), 0);
    QVERIFY(!reopened.startupEnabled());
    QVERIFY(autostart.entryList({"*.desktop"}, QDir::Files).isEmpty());
}

void TestLauncherModel::readsLayoutSettings_data()
{
    QTest::addColumn<QVariantMap>("config");
    QTest::addColumn<QList<int>>("expected");
    QTest::newRow("defaults") << QVariantMap() << QList<int> {720, 560, 0, 0};
    QTest::newRow("configured")
        << QVariantMap {{"min_width", 840}, {"min_height", 640}, {"icon_size", 64}, {"fixed_number_columns", 3}}
        << QList<int> {840, 640, 64, 3};
    QTest::newRow("minimums")
        << QVariantMap {{"min_width", 100}, {"min_height", -1}, {"icon_size", -2}, {"fixed_number_columns", -3}}
        << QList<int> {300, 300, 0, 0};
    QTest::newRow("invalid")
        << QVariantMap {{"min_width", "bad"}, {"min_height", "bad"}, {"icon_size", "bad"},
                        {"fixed_number_columns", "bad"}}
        << QList<int> {720, 560, 0, 0};
}

void TestLauncherModel::readsLayoutSettings()
{
    QFETCH(QVariantMap, config);
    QFETCH(QList<int>, expected);
    QVERIFY(QDir().mkpath(Config::ConfigDir));
    const auto removeConfig = qScopeGuard([] { QFile::remove(Config::ConfigFile); });
    // Update Qt's shared cache as well as the file: consecutive rows can have
    // identical file sizes and modification timestamps on fast build hosts.
    QSettings settings(Config::ConfigFile, QSettings::NativeFormat);
    settings.clear();
    for (auto it = config.cbegin(); it != config.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);

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
    LauncherModel model(parser, listPath, icons);
    QCOMPARE(model.minimumWidth(), expected.at(0));
    QCOMPARE(model.minimumHeight(), expected.at(1));
    QCOMPARE(model.iconSize(), expected.at(2));
    QCOMPARE(model.fixedNumberColumns(), expected.at(3));
}

void TestLauncherModel::launchesInDesktopWorkingDirectory_data()
{
    QTest::addColumn<QByteArray>("exec");
    QTest::addColumn<QByteArray>("listEntry");
    // The program pkexec is asked to authorize, or empty if pkexec is not used.
    QTest::addColumn<QString>("authorized");
    const QByteArray probe = R"(/bin/sh -c "pwd > cwd.txt")";
    QTest::newRow("detached") << probe << QByteArray("cwd-probe") << QString();
    QTest::newRow("root-flag") << probe << QByteArray("cwd-probe root") << QString(getuid() == 0 ? "" : "env");
    QTest::newRow("exec-pkexec") << "pkexec " + probe << QByteArray("cwd-probe") << QString("/bin/sh");
    QTest::newRow("exec-pkexec-terminal-flag")
        << "pkexec " + probe << QByteArray("cwd-probe terminal") << QString("/bin/sh");
}

void TestLauncherModel::launchesInDesktopWorkingDirectory()
{
    QFETCH(QByteArray, exec);
    QFETCH(QByteArray, listEntry);
    QFETCH(QString, authorized);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("HOME", directory.path().toUtf8());
    qputenv("XDG_DATA_HOME", directory.filePath("share").toUtf8());

    // Stand-ins for pkexec and the terminal. Like the real pkexec, the fake one starts the
    // command in $HOME unless given --keep-cwd; it records the program it would authorize.
    const QString bin = directory.filePath("bin");
    QVERIFY(QDir().mkpath(bin));
    QVERIFY(writeFile(bin + "/pkexec", R"(#!/bin/sh
keep=
while [ $# -gt 0 ]; do
    case "$1" in
        --keep-cwd) keep=1; shift ;;
        --user) shift 2 ;;
        --*) shift ;;
        *) break ;;
    esac
done
printf '%s\n' "$1" > "$HOME/authorized.txt"
[ -n "$keep" ] || cd "$HOME" || exit 1
exec "$@"
)"));
    QVERIFY(writeFile(bin + "/x-terminal-emulator", "#!/bin/sh\n[ \"$1\" = -e ] && shift\nexec \"$@\"\n"));
    for (const char *tool : {"/pkexec", "/x-terminal-emulator"}) {
        QVERIFY(QFile::setPermissions(bin + tool, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
    const QByteArray previousPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", bin.toUtf8() + ':' + previousPath);
    QCOMPARE(QStandardPaths::findExecutable("pkexec"), bin + "/pkexec");

    const QString applications = directory.filePath("share/applications");
    const QString workDir = directory.filePath("work dir");
    QVERIFY(QDir().mkpath(applications));
    QVERIFY(QDir().mkpath(workDir));
    // Path is a Desktop Entry string, so the space may be written as \s.
    QByteArray path = workDir.toUtf8();
    path.replace(' ', "\\s");
    QVERIFY(writeFile(applications + "/cwd-probe.desktop",
                      "[Desktop Entry]\nType=Application\nName=Cwd Probe\nExec=" + exec + "\nPath=" + path + "\n"));
    const QString listPath = directory.filePath("cwd.list");
    QVERIFY(writeFile(listPath, "Name=Cwd\nCategory=Utilities\n" + listEntry + "\n"));

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherIconProvider icons;
    LauncherModel model(parser, listPath, icons);
    QCOMPARE(model.rowCount(), 1);
    QSignalSpy errors(&model, &LauncherModel::errorOccurred);
    model.launch(model.data(model.index(0), LauncherModel::SourceIndexRole).toInt());

    QFile output(workDir + "/cwd.txt");
    QTRY_VERIFY(output.exists() && output.size() > 0);
    QVERIFY(output.open(QFile::ReadOnly));
    QCOMPARE(QString::fromUtf8(output.readAll()).trimmed(), QDir(workDir).canonicalPath());
    QFile authorization(directory.filePath("authorized.txt"));
    if (authorized.isEmpty()) {
        QVERIFY(!authorization.exists());
    } else {
        QVERIFY(authorization.open(QFile::ReadOnly));
        QCOMPARE(QString::fromUtf8(authorization.readAll()).trimmed(), authorized);
    }
    QTest::qWait(100);
    QCOMPARE(errors.count(), 0);
}

void TestLauncherModel::tracksLaunchersPerWorkingDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("HOME", directory.path().toUtf8());
    qputenv("XDG_DATA_HOME", directory.filePath("share").toUtf8());
    const QString applications = directory.filePath("share/applications");
    QVERIFY(QDir().mkpath(applications));
    for (const char *name : {"first", "second"}) {
        const QString workDir = directory.filePath(name);
        QVERIFY(QDir().mkpath(workDir));
        QVERIFY(writeFile(applications + "/sleep-" + name + ".desktop",
                          "[Desktop Entry]\nType=Application\nName=Sleep " + QByteArray(name)
                              + "\nExec=/bin/sleep 0.5\nPath=" + workDir.toUtf8() + "\n"));
    }
    const QString listPath = directory.filePath("sleep.list");
    QVERIFY(writeFile(listPath, "Name=Sleep\nCategory=Utilities\nsleep-first\nsleep-second\n"));

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherIconProvider icons;
    LauncherModel model(parser, listPath, icons);
    QCOMPARE(model.rowCount(), 2);
    QSignalSpy errors(&model, &LauncherModel::errorOccurred);
    model.launch(model.data(model.index(0), LauncherModel::SourceIndexRole).toInt());
    model.launch(model.data(model.index(1), LauncherModel::SourceIndexRole).toInt());
    QCOMPARE(errors.count(), 0);
    model.launch(model.data(model.index(1), LauncherModel::SourceIndexRole).toInt());
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.constFirst().at(0).toString(), QStringLiteral("Launcher already running"));
}

void TestLauncherModel::leavesTargetAutostartWhenOpeningSymlink()
{
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
    qunsetenv("XDG_CONFIG_HOME");
    const QString real = directory.filePath("real.list");
    const QString link = directory.filePath("link.list");
    QVERIFY(writeFile(real, "Name=Real\nCategory=Utilities\n/bin/true\n"));
    QVERIFY(QFile::link(real, link));

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherIconProvider realIcons;
    LauncherModel realModel(parser, real, realIcons);
    realModel.setStartupEnabled(true);
    QVERIFY(realModel.startupEnabled());
    const QDir autostart(directory.filePath(".config/autostart"));
    const QStringList realEntries = autostart.entryList({"*.desktop"}, QDir::Files);
    QCOMPARE(realEntries.size(), 1);

    LauncherIconProvider linkIcons;
    LauncherModel linkModel(parser, link, linkIcons);
    QVERIFY(!linkModel.startupEnabled());
    QCOMPARE(autostart.entryList({"*.desktop"}, QDir::Files), realEntries);

    LauncherIconProvider reopenedIcons;
    LauncherModel reopened(parser, real, reopenedIcons);
    QVERIFY(reopened.startupEnabled());
}

void TestLauncherModel::keepsAutostartAcrossSymlinkRetarget()
{
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
    qunsetenv("XDG_CONFIG_HOME");
    const QString one = directory.filePath("one.list");
    const QString two = directory.filePath("two.list");
    const QString link = directory.filePath("link.list");
    QVERIFY(writeFile(one, "Name=One\nCategory=Utilities\n/bin/true\n"));
    QVERIFY(writeFile(two, "Name=Two\nCategory=Utilities\n/bin/true\n"));
    QVERIFY(QFile::link(one, link));

    // An entry written by an earlier release, keyed by the symlink target, is migrated.
    const QDir autostart(directory.filePath(".config/autostart"));
    QVERIFY(QDir().mkpath(autostart.path()));
    const QString canonicalHash = sourceHash(QFileInfo(one).canonicalFilePath());
    const QString oldEntry = autostart.filePath("custom-toolbox-" + canonicalHash.left(16) + ".desktop");
    QVERIFY(writeFile(oldEntry, "[Desktop Entry]\nExec=custom-toolbox " + link.toUtf8()
                                    + "\nX-Custom-Toolbox-Managed=true\nX-Custom-Toolbox-Source-SHA256="
                                    + canonicalHash.toUtf8() + "\n"));

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherIconProvider icons;
    LauncherModel model(parser, link, icons);
    QVERIFY(model.startupEnabled());
    QVERIFY(!QFile::exists(oldEntry));
    QCOMPARE(autostart.entryList({"*.desktop"}, QDir::Files),
             QStringList {"custom-toolbox-" + sourceHash(link).left(16) + ".desktop"});

    QVERIFY(QFile::remove(link));
    QVERIFY(QFile::link(two, link));
    LauncherIconProvider reopenedIcons;
    LauncherModel reopened(parser, link, reopenedIcons);
    QVERIFY(reopened.startupEnabled());

    QSignalSpy errors(&model, &LauncherModel::errorOccurred);
    model.setStartupEnabled(false);
    QCOMPARE(errors.count(), 0);
    QVERIFY(!model.startupEnabled());
    QVERIFY(autostart.entryList({"*.desktop"}, QDir::Files).isEmpty());
}

void TestLauncherModel::reloadsEditedDesktopFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("HOME", directory.path().toUtf8());
    qputenv("XDG_DATA_HOME", directory.filePath("share").toUtf8());
    const QString vendor = directory.filePath("share/applications/vendor");
    QVERIFY(QDir().mkpath(vendor));
    const QString desktopPath = vendor + "/watched-tool.desktop";
    QVERIFY(writeFile(desktopPath, "[Desktop Entry]\nType=Application\nName=Original Tool\nExec=/bin/true\n"));
    const QString listPath = directory.filePath("watch.list");
    QVERIFY(writeFile(listPath, "Name=Watch\nCategory=Utilities\nwatched-tool\n"));

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("remove-checkbox"), QStringLiteral("test option")});
    QVERIFY(parser.parse({QStringLiteral("custom-toolbox")}));
    LauncherIconProvider icons;
    LauncherModel model(parser, listPath, icons);
    QCOMPARE(model.data(model.index(0), LauncherModel::NameRole).toString(), QStringLiteral("Original Tool"));

    // Truncate and rewrite in place (same inode), which no directory watch reports.
    QVERIFY(writeFile(desktopPath, "[Desktop Entry]\nType=Application\nName=Edited Tool\nExec=/bin/true\n"));
    QTRY_COMPARE(model.data(model.index(0), LauncherModel::NameRole).toString(), QStringLiteral("Edited Tool"));

    // Replace the file (new inode); the restored watch must still see later in-place edits.
    QVERIFY(writeFile(desktopPath + ".new", "[Desktop Entry]\nType=Application\nName=Replaced Tool\nExec=/bin/true\n"));
    QVERIFY(QFile::remove(desktopPath));
    QVERIFY(QFile::rename(desktopPath + ".new", desktopPath));
    QTRY_COMPARE(model.data(model.index(0), LauncherModel::NameRole).toString(), QStringLiteral("Replaced Tool"));
    QVERIFY(writeFile(desktopPath, "[Desktop Entry]\nType=Application\nName=Final Tool\nExec=/bin/true\n"));
    QTRY_COMPARE(model.data(model.index(0), LauncherModel::NameRole).toString(), QStringLiteral("Final Tool"));
}

QTEST_MAIN(TestLauncherModel)
#include "test_launchermodel.moc"
