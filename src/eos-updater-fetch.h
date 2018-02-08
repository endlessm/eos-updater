/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 */

#pragma once

#include "eos-updater-generated.h"

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean handle_fetch (EosUpdater            *updater,
                       GDBusMethodInvocation *call,
                       gpointer               user_data);
gboolean handle_fetch_full (EosUpdater            *updater,
                            GDBusMethodInvocation *call,
                            GVariant              *options,
                            gpointer               user_data);

G_END_DECLS
