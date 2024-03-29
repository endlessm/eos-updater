/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

static inline GPtrArray *
string_array_new (void)
{
  return g_ptr_array_new_with_free_func (g_free);
}

gboolean load_to_bytes (GFile *file,
                        GBytes **bytes,
                        GError **error);

gboolean create_file (GFile *path,
                      GBytes* bytes,
                      GError **error);

gboolean create_directory (GFile *path,
                           GError **error);

gboolean create_symlink (const gchar *target,
                         GFile *link,
                         GError **error);

gboolean load_key_file (GFile *file,
                        GKeyFile **out_keyfile,
                        GError **error);

gboolean save_key_file (GFile *file,
                        GKeyFile *keyfile,
                        GError **error);

GDateTime *days_ago (guint days);

gboolean input_stream_to_string (GInputStream *stream,
                                 gchar **out_str,
                                 GError **error);

gboolean cp (GFile *source,
             GFile *target,
             GError **error);

gboolean read_port_file (GFile *port_file,
                         guint16 *out_port,
                         GError **error);

G_END_DECLS
