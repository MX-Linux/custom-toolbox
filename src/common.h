/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2023-2026 MX Authors
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
#pragma once

#include <QString>

#ifndef CUSTOM_TOOLBOX_SYSCONFDIR
    #define CUSTOM_TOOLBOX_SYSCONFDIR "/etc"
#endif

#ifndef CUSTOM_TOOLBOX_DATADIR
    #define CUSTOM_TOOLBOX_DATADIR "/usr/share"
#endif

namespace Config {
    inline const QString ConfigDir = QStringLiteral(CUSTOM_TOOLBOX_SYSCONFDIR) + "/custom-toolbox";
    inline const QString ConfigFile = ConfigDir + "/custom-toolbox.conf";
    inline const QString DocDir = QStringLiteral(CUSTOM_TOOLBOX_DATADIR) + "/doc/custom-toolbox";
    inline const QString HelpFile = DocDir + "/help.html";
    inline const QString LicenseFile = DocDir + "/license.html";
    inline const QString ChangelogFile = DocDir + "/changelog.gz";
    inline const QString ChangelogTextFile = DocDir + "/changelog";
}
