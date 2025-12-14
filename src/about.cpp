/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2024-2025 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
 *          MX Linux <http://mxlinux.org>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include "about.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextEdit>
#include <QVBoxLayout>

#include "common.h"
#include <unistd.h>

// Display doc as normal user when run as root
void displayDoc(const QString &url, const QString &title)
{
    bool started_as_root = false;
    if (QFileInfo(qEnvironmentVariable("HOME")).canonicalFilePath() == "/root") {
        started_as_root = true;
        qputenv("HOME", starting_home.toUtf8()); // Use original home for theming purposes
    }
    // Prefer mx-viewer otherwise use xdg-open (use runuser to run that as logname user)
    const QString executable_path = QStandardPaths::findExecutable("mx-viewer");
    if (!executable_path.isEmpty()) {
        if (!QProcess::startDetached("mx-viewer", {url, title})) {
            QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to start mx-viewer"));
        }
    } else {
        if (getuid() != 0) {
            if (!QProcess::startDetached("xdg-open", {url})) {
                QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to start xdg-open"));
            }
        } else {
            QProcess proc;
            proc.start("logname", {}, QIODevice::ReadOnly);
            proc.waitForFinished(3000);
            const QString user = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!QProcess::startDetached("runuser", {"-u", user, "--", "xdg-open", url})) {
                QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to start runuser"));
            }
        }
    }
    if (started_as_root) {
        qputenv("HOME", "/root");
    }
}

void displayAboutMsgBox(const QString &title, const QString &message, const QString &licence_url,
                        const QString &license_title)
{
    constexpr int dialog_width = 600;
    constexpr int dialog_height = 500;
    QMessageBox msgBox(QMessageBox::NoIcon, title, message);
    auto *btn_license = msgBox.addButton(QObject::tr("License"), QMessageBox::HelpRole);
    auto *btn_changelog = msgBox.addButton(QObject::tr("Changelog"), QMessageBox::HelpRole);
    auto *btn_cancel = msgBox.addButton(QObject::tr("Cancel"), QMessageBox::NoRole);
    btn_cancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btn_license) {
        displayDoc(licence_url, license_title);
    } else if (msgBox.clickedButton() == btn_changelog) {
        QDialog changelog;
        changelog.setWindowTitle(QObject::tr("Changelog"));
        changelog.resize(dialog_width, dialog_height);

        auto *text = new QTextEdit(&changelog);
        text->setReadOnly(true);
        QProcess proc;
        proc.start(
            "zless",
            {"/usr/share/doc/" + QFileInfo(QCoreApplication::applicationFilePath()).fileName() + "/changelog.gz"},
            QIODevice::ReadOnly);
        proc.waitForFinished(5000);
        text->setText(proc.readAllStandardOutput());

        auto *btn_close = new QPushButton(QObject::tr("&Close"), &changelog);
        btn_close->setIcon(QIcon::fromTheme("window-close"));
        QObject::connect(btn_close, &QPushButton::clicked, &changelog, &QDialog::close);

        auto *layout = new QVBoxLayout(&changelog);
        layout->addWidget(text);
        layout->addWidget(btn_close);
        changelog.setLayout(layout);
        changelog.exec();
    }
}
