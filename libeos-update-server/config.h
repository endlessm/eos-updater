/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
 * Copyright © 2017 Endless Mobile, Inc.
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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * EusRepoConfig:
 * @index: index from the `[Repository 0–65535]` group name
 * @path: value of the `Path=` option
 * @remote_name: value of the `RemoteName=` option
 *
 * Structure containing a local repository configuration loaded from the
 * config file (a `[Repository 0–65535]` section). This is enough information to
 * create an #EosRepo for the repository.
 *
 * For more information about the config options, see the
 * [`eos-update-server.conf(5)` man page](man:eos-update-server.conf(5)).
 *
 * Since: UNRELEASED
 */
typedef struct
{
  guint16 index;
  gchar *path;
  gchar *remote_name;
} EusRepoConfig;

void eus_repo_config_free (EusRepoConfig *config);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EusRepoConfig, eus_repo_config_free)

gboolean eus_read_config_file (const gchar  *config_file_path,
                               gboolean     *out_advertise_updates,
                               GPtrArray   **out_repository_configs,
                               GError      **error);

G_END_DECLS
