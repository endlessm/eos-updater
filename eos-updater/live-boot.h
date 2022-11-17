/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Endless Mobile
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Will Thompson <wjt@endlessm.com>
 */

#pragma once

#include <eos-updater/dbus.h>
#include <libeos-updater-util/util.h>
#include <ostree.h>

G_BEGIN_DECLS

gboolean is_installed_system (GError **error);
gboolean handle_on_live_boot (EosUpdater            *updater,
                              GDBusMethodInvocation *call,
                              gpointer               user_data);

G_END_DECLS
