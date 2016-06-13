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

G_BEGIN_DECLS

typedef struct
{
  gchar *cmdline;
  gchar *standard_output;
  gchar *standard_error;
  gint exit_status;
} CmdStuff;

#define CMD_STUFF_CLEARED { NULL, NULL, NULL, 0 }

void cmd_stuff_clear (CmdStuff *cmd);
void cmd_stuff_free (CmdStuff *cmd);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (CmdStuff, cmd_stuff_clear)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CmdStuff, cmd_stuff_free)

gboolean cmd_stuff_ensure_ok (CmdStuff *cmd,
			      GError **error);

gboolean cmd_stuff_ensure_all_ok_verbose (GPtrArray *cmds);

typedef struct
{
  gchar *cmdline;
  GOutputStream *in_stream;
  GInputStream *out_stream;
  GInputStream *err_stream;
  GPid pid;
} CmdAsyncStuff;

#define CMD_ASYNC_STUFF_CLEARED { NULL, NULL, NULL, NULL, 0 }

void cmd_async_stuff_clear (CmdAsyncStuff *cmd);
void cmd_async_stuff_free (CmdAsyncStuff *cmd);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (CmdAsyncStuff, cmd_async_stuff_clear)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CmdAsyncStuff, cmd_async_stuff_free)

gboolean test_spawn_cwd_async (const gchar *cwd,
			       gchar **argv,
			       gchar **envp,
			       gboolean autoreap,
			       CmdAsyncStuff *cmd,
			       GError **error);

gboolean test_spawn_async (gchar **argv,
			   gchar **envp,
			   gboolean autoreap,
			   CmdAsyncStuff *cmd,
			   GError **error);

gboolean test_spawn_cwd_full (const gchar *cwd,
			      gchar **argv,
			      gchar **envp,
			      gboolean to_dev_null,
			      CmdStuff *cmd,
			      GError **error);

gboolean test_spawn_cwd (const gchar *cwd,
			 gchar **argv,
			 gchar **envp,
			 CmdStuff *cmd,
			 GError **error);

gboolean test_spawn (gchar **argv,
		     gchar **envp,
		     CmdStuff *cmd,
		     GError **error);

gchar **merge_parent_and_child_env (gchar **child_env);

static inline gchar *
flag (const gchar *name,
      const gchar *value)
{
  return g_strdup_printf ("--%s=%s", name, value);
}

static inline gchar *
envvar (const gchar *key,
        const gchar *value)
{
  return g_strdup_printf ("%s=%s", key, value);
}

gboolean reap_async_cmd (CmdAsyncStuff *cmd,
			 CmdStuff *reaped,
			 GError **error);

G_END_DECLS
