/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#pragma once

#include <glib.h>
#include <ostree.h>

G_BEGIN_DECLS

gboolean eos_sysroot_get_advertisable_commit (OstreeSysroot  *sysroot,
                                              gchar         **commit_checksum,
                                              gchar         **commit_ostree_path,
                                              guint64        *commit_timestamp,
                                              GError        **error);

OstreeRepo *eos_updater_local_repo (GError **error);

OstreeDeployment *eos_updater_get_booted_deployment_from_loaded_sysroot (OstreeSysroot *sysroot,
                                                                         GError **error);

OstreeDeployment *eos_updater_get_booted_deployment (GError **error);

gchar *eos_updater_get_booted_checksum (GError **error);

gboolean eos_updater_get_ostree_path (OstreeRepo *repo,
                                      const gchar *osname,
                                      gchar **ostree_path,
                                      GError **error);

G_END_DECLS
