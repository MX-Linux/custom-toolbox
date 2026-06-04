/**********************************************************************
 * Copyright (C) 2014-2026 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of MX Tools.
 *
 * MX Tools is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Tools is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Tools.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include "flatbutton.h"

FlatButton::FlatButton(QWidget *parent)
    : QPushButton(parent)
{
    setFlat(true);
    setStyleSheet(QStringLiteral("QPushButton { text-align: left; } "
                                 "QPushButton:hover { text-decoration: underline; } "
                                 "QToolTip { text-decoration: none; }"));
}

FlatButton::FlatButton(const QString &name, QWidget *parent)
    : FlatButton(parent)
{
    setText(name);
}

