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
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
void setupDocDialog(QDialog &dialog, QTextBrowser *browser, const QString &title, bool largeWindow)
{
    dialog.setWindowTitle(title);
    if (largeWindow) {
        dialog.setWindowFlags(Qt::Window);
        dialog.resize(1000, 800);
    } else {
        dialog.resize(700, 600);
    }

    browser->setOpenExternalLinks(true);

    auto *btn_close = new QPushButton(QObject::tr("&Close"), &dialog);
    btn_close->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    QObject::connect(btn_close, &QPushButton::clicked, &dialog, &QDialog::close);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(browser);
    layout->addWidget(btn_close);
}

void showHtmlDoc(const QString &url, const QString &title, bool largeWindow)
{
    QDialog dialog;
    auto *browser = new QTextBrowser(&dialog);
    setupDocDialog(dialog, browser, title, largeWindow);

    const QUrl source_url = QUrl::fromUserInput(url);
    const QString local_path = source_url.isLocalFile() ? source_url.toLocalFile() : url;
    if (source_url.isLocalFile() ? QFileInfo::exists(local_path) : QFileInfo::exists(url)) {
        browser->setSource(source_url.isLocalFile() ? source_url : QUrl::fromLocalFile(url));
    } else {
        browser->setText(QObject::tr("Could not load %1").arg(url));
    }

    dialog.exec();
}
} // namespace

void displayDoc(const QString &url, const QString &title, bool largeWindow)
{
    showHtmlDoc(url, title, largeWindow);
}

void displayHelpDoc(const QString &path, const QString &title)
{
    showHtmlDoc(path, title, true);
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
        const QString app_name = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        const QString changelog_path = QStringLiteral("/usr/share/doc/") + app_name + QStringLiteral("/changelog.gz");
        proc.start(QStringLiteral("zcat"), {changelog_path}, QIODevice::ReadOnly);
        if (proc.waitForStarted(3000) && proc.waitForFinished(3000)) {
            text->setText(proc.readAllStandardOutput());
        } else {
            text->setText(QObject::tr("Could not load changelog."));
        }

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
