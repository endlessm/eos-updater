/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2016 Kinvolk GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Krzesimir Nowak <krzesimir@kinvolk.io>
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
		      CmdStuff *cmd,
		      GError **error);

gboolean ostree_commit (GFile *repo,
			GFile *tree_root,
			const gchar *subject,
			const gchar *ref,
			const gchar *keyid,
			GDateTime *timestamp,
			CmdStuff *cmd,
			GError **error);

gboolean ostree_summary (GFile *repo,
			 const gchar *keyid,
			 CmdStuff *cmd,
			 GError **error);

gboolean ostree_pull (GFile *repo,
		      const gchar *remote_name,
		      const gchar *ref,
		      CmdStuff *cmd,
		      GError **error);

gboolean ostree_remote_add (GFile *repo,
			    const gchar *remote_name,
			    const gchar *remote_url,
			    const gchar *ref,
			    GFile *gpg_key,
			    CmdStuff *cmd,
			    GError **error);

gboolean ostree_deploy (GFile *sysroot,
			const gchar *osname,
			const gchar *refspec,
			CmdStuff *cmd,
			GError **error);

gboolean ostree_init_fs (GFile *sysroot,
			 CmdStuff *cmd,
			 GError **error);

gboolean ostree_os_init (GFile *sysroot,
			 const gchar *remote_name,
			 CmdStuff *cmd,
			 GError **error);

gboolean ostree_status (GFile *sysroot,
			CmdStuff *cmd,
			GError **error);

/* due to some bug I don't know where (either my fault, or ostree
 * trivial-httpd's in lackluster or just cursory daemonizing or
 * g_spawn_sync's in pipe handling), we get no output here at all -
 * g_spawn_sync becomes stuck on reading pipes. */
gboolean ostree_httpd (GFile *served_dir,
		       guint16 *port,
		       CmdStuff *cmd,
		       GError **error);

G_END_DECLS
