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

typedef enum
{
  REPO_ARCHIVE_Z2,
  REPO_BARE
} RepoMode;

gboolean ostree_init (GFile *repo,
                      RepoMode mode,
                      CmdResult *cmd,
                      GError **error);

gboolean ostree_commit (GFile *repo,
                        GFile *tree_root,
                        const gchar *subject,
                        const gchar *ref,
                        GFile *gpg_home,
                        const gchar *keyid,
                        GDateTime *timestamp,
                        CmdResult *cmd,
                        GError **error);

gboolean ostree_summary (GFile *repo,
                         GFile *gpg_home,
                         const gchar *keyid,
                         CmdResult *cmd,
                         GError **error);

gboolean ostree_pull (GFile *repo,
                      const gchar *remote_name,
                      const gchar *ref,
                      CmdResult *cmd,
                      GError **error);

gboolean ostree_remote_add (GFile *repo,
                            const gchar *remote_name,
                            const gchar *remote_url,
                            const gchar *ref,
                            GFile *gpg_key,
                            CmdResult *cmd,
                            GError **error);

gboolean ostree_ref_create (GFile *repo,
                            const gchar *ref_name,
                            const gchar *commit_id,
                            CmdResult *cmd,
                            GError **error);

gboolean ostree_ref_delete (GFile *repo,
                            const gchar *ref_name,
                            CmdResult *cmd,
                            GError **error);

typedef enum
  {
    OSTREE_PRUNE_REFS_ONLY = 1 << 0,
    OSTREE_PRUNE_NO_PRUNE  = 1 << 1,
    OSTREE_PRUNE_VERBOSE   = 1 << 2,
  } OstreePruneFlags;

gboolean ostree_prune (GFile *repo,
                       OstreePruneFlags flags,
                       gint depth_opt,
                       CmdResult *cmd,
                       GError **error);

gboolean ostree_deploy (GFile *sysroot,
                        const gchar *osname,
                        const gchar *refspec,
                        CmdResult *cmd,
                        GError **error);

gboolean ostree_init_fs (GFile *sysroot,
                         CmdResult *cmd,
                         GError **error);

gboolean ostree_os_init (GFile *sysroot,
                         const gchar *remote_name,
                         CmdResult *cmd,
                         GError **error);

gboolean ostree_status (GFile *sysroot,
                        CmdResult *cmd,
                        GError **error);

gboolean ostree_undeploy (GFile *sysroot,
                          int deployment_index,
                          CmdResult *cmd,
                          GError **error);

/* due to some bug I don't know where (either my fault, or ostree
 * trivial-httpd's in lackluster or just cursory daemonizing or
 * g_spawn_sync's in pipe handling), we get no output here at all -
 * g_spawn_sync becomes stuck on reading pipes. */
gboolean ostree_httpd (GFile *served_dir,
                       GFile *port_file,
                       CmdResult *cmd,
                       GError **error);

G_END_DECLS
