/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
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

#include "spawn-utils.h"

G_BEGIN_DECLS

gboolean flatpak_remote_add (GFile        *updater_dir,
                             const gchar  *repo_name,
                             const gchar  *repo_directory,
                             GFile        *gpg_key,
                             GError      **error);

gboolean flatpak_install (GFile        *updater_dir,
                          const gchar  *remote,
                          const gchar  *app_id,
                          GError      **error);

gboolean flatpak_uninstall (GFile        *updater_dir,
                            const gchar  *app_id,
                            GError      **error);

gboolean flatpak_build_init (GFile        *updater_dir,
                             const gchar  *bundle_path,
                             const gchar  *app_id,
                             const gchar  *runtime_name,
                             const gchar  *branch,
                             GError      **error);

gboolean flatpak_build_export (GFile        *updater_dir,
                               const gchar  *bundle_path,
                               const gchar  *repo_path,
                               const gchar  *branch,
                               const gchar  *collection_id,
                               GFile        *gpg_home_dir,
                               const gchar  *key_id,
                               GError      **error);

gboolean flatpak_build_finish (GFile        *updater_dir,
                               const gchar  *bundle_path,
                               const gchar  *binary,
                               GError      **error);

gboolean flatpak_list (GFile      *updater_dir,
                       CmdResult  *cmd,
                       GError    **error);

gboolean flatpak_populate_app (GFile        *updater_dir,
                               GFile        *app_directory_path,
                               const gchar  *app_name,
                               const gchar  *runtime_name,
                               const gchar  *branch,
                               GPtrArray    *extension_infos,
                               const gchar  *repo_directory,
                               const gchar  *repo_collection_id,
                               GFile        *gpg_home_dir,
                               const gchar  *keyid,
                               GError      **error);

gboolean flatpak_populate_runtime (GFile        *updater_dir,
                                   GFile        *runtime_directory_path,
                                   const gchar  *repo_directory,
                                   const gchar  *name,
                                   const gchar  *runtime_name,
                                   const gchar  *branch,
                                   GPtrArray    *extension_infos,
                                   const gchar  *repo_collection_id,
                                   GFile        *gpg_home_dir,
                                   const gchar  *keyid,
                                   GError      **error);

gboolean flatpak_populate_extension (GFile        *updater_dir,
                                     GFile        *extension_directory,
                                     const gchar  *repo_directory,
                                     const gchar  *name,
                                     const gchar  *runtime_name,
                                     const gchar  *branch,
                                     const gchar  *extension_of_ref,
                                     const gchar  *repo_collection_id,
                                     GFile        *gpg_homedir,
                                     const gchar  *keyid,
                                     GError      **error);

G_END_DECLS
