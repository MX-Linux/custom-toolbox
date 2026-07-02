/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2024-2026 MX Authors
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

#include "common.h"

namespace
{
void setupDocDialog(QDialog &dialog, QWidget *content, const QString &title, bool largeWindow,
                    const QSize &normalSize = QSize(700, 600))
{
    dialog.setWindowTitle(title);
    if (largeWindow) {
        dialog.setWindowFlags(Qt::Window);
        dialog.resize(1000, 800);
    } else {
        dialog.resize(normalSize);
    }

    if (auto *browser = qobject_cast<QTextBrowser *>(content)) {
        browser->setOpenExternalLinks(true);
    }

    auto *btnClose = new QPushButton(QObject::tr("&Close"), &dialog);
    btnClose->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    QObject::connect(btnClose, &QPushButton::clicked, &dialog, &QDialog::close);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(content);
    layout->addWidget(btnClose);
}

// QTextBrowser cannot fetch remote URLs, so only local files are supported.
void showHtmlDoc(const QString &path, const QString &title, bool largeWindow)
{
    QDialog dialog;
    auto *browser = new QTextBrowser(&dialog);
    setupDocDialog(dialog, browser, title, largeWindow);

    if (QFileInfo::exists(path)) {
        browser->setSource(QUrl::fromLocalFile(path));
    } else {
        browser->setText(QObject::tr("Could not load %1").arg(path));
    }

    dialog.exec();
}
} // namespace

void displayDoc(const QString &path, const QString &title, bool largeWindow)
{
    showHtmlDoc(path, title, largeWindow);
}

void displayHelpDoc(const QString &path, const QString &title)
{
    showHtmlDoc(path, title, true);
}

void displayAboutMsgBox(const QString &title, const QString &message, const QString &licencePath,
                        const QString &licenseTitle)
{
    constexpr int dialogWidth = 600;
    constexpr int dialogHeight = 500;
    QMessageBox msgBox(QMessageBox::NoIcon, title, message);
    auto *btnLicense = msgBox.addButton(QObject::tr("License"), QMessageBox::HelpRole);
    auto *btnChangelog = msgBox.addButton(QObject::tr("Changelog"), QMessageBox::HelpRole);
    auto *btnCancel = msgBox.addButton(QObject::tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        displayDoc(licencePath, licenseTitle);
    } else if (msgBox.clickedButton() == btnChangelog) {
        QDialog changelog;

        auto *text = new QTextEdit(&changelog);
        setupDocDialog(changelog, text, QObject::tr("Changelog"), false, QSize(dialogWidth, dialogHeight));
        text->setReadOnly(true);
        QProcess proc;
        proc.start(QStringLiteral("zcat"), {Config::ChangelogFile}, QIODevice::ReadOnly);
        if (proc.waitForStarted(3000) && proc.waitForFinished(3000)) {
            text->setText(proc.readAllStandardOutput());
        } else {
            text->setText(QObject::tr("Could not load changelog."));
        }

        changelog.exec();
    }
}
