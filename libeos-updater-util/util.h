/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

static inline GPtrArray *
object_array_new (void)
{
  return g_ptr_array_new_with_free_func (g_object_unref);
}

/* id returned by g_bus_own_name */
typedef guint EuuBusNameID;
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(EuuBusNameID, g_bus_unown_name, 0)

const gchar *eos_updater_get_envvar_or (const gchar *envvar,
                                        const gchar *default_value);

gboolean eos_updater_read_file_to_bytes (GFile *file,
                                         GCancellable *cancellable,
                                         GBytes **out_bytes,
                                         GError **error);

#define EUU_TYPE_QUIT_FILE euu_quit_file_get_type ()
G_DECLARE_FINAL_TYPE (EuuQuitFile, euu_quit_file, EUU, QUIT_FILE, GObject)

typedef enum
{
  EUU_QUIT_FILE_QUIT,
  EUU_QUIT_FILE_KEEP_CHECKING
} EuuQuitFileCheckResult;

typedef EuuQuitFileCheckResult (* EuuQuitFileCheckCallback) (gpointer user_data);

EuuQuitFile *eos_updater_setup_quit_file (const gchar *path,
                                          EuuQuitFileCheckCallback check_callback,
                                          gpointer user_data,
                                          GDestroyNotify notify,
                                          guint timeout_seconds,
                                          GError **error);

typedef enum
{
  EOS_UPDATER_FILE_FILTER_IGNORE = 0,
  EOS_UPDATER_FILE_FILTER_HANDLE = 1,
} EosUpdaterFileFilterReturn;

typedef EosUpdaterFileFilterReturn (*EosUpdaterFileFilterFunc) (GFile     *file,
                                                                GFileInfo *file_info);

gboolean eos_updater_remove_recursive (GFile                     *topdir,
                                       EosUpdaterFileFilterFunc   filter_func,
                                       GError                   **error);

G_END_DECLS
