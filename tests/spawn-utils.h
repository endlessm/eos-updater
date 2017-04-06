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

G_BEGIN_DECLS

typedef struct
{
  gchar *cmdline;
  gchar *standard_output;
  gchar *standard_error;
  gint exit_status;
} CmdResult;

#define CMD_RESULT_CLEARED { NULL, NULL, NULL, 0 }

void cmd_result_clear (CmdResult *cmd);
void cmd_result_free (CmdResult *cmd);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (CmdResult, cmd_result_clear)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CmdResult, cmd_result_free)

gboolean cmd_result_ensure_ok (CmdResult *cmd,
                               GError **error);

gboolean cmd_result_ensure_all_ok_verbose (GPtrArray *cmds);

gchar *cmd_result_dump (CmdResult *cmd);

typedef struct
{
  gchar *cmdline;
  GOutputStream *in_stream;
  GInputStream *out_stream;
  GInputStream *err_stream;
  GPid pid;
} CmdAsyncResult;

#define CMD_ASYNC_RESULT_CLEARED { NULL, NULL, NULL, NULL, 0 }

void cmd_async_result_clear (CmdAsyncResult *cmd);
void cmd_async_result_free (CmdAsyncResult *cmd);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (CmdAsyncResult, cmd_async_result_clear)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CmdAsyncResult, cmd_async_result_free)

gboolean test_spawn_cwd_async (const gchar *cwd,
                               const gchar * const *argv,
                               const gchar * const *envp,
                               gboolean autoreap,
                               CmdAsyncResult *cmd,
                               GError **error);

gboolean test_spawn_async (const gchar * const *argv,
                           const gchar * const *envp,
                           gboolean autoreap,
                           CmdAsyncResult *cmd,
                           GError **error);

gboolean test_spawn_cwd_full (const gchar *cwd,
                              const gchar * const *argv,
                              const gchar * const *envp,
                              gboolean to_dev_null,
                              CmdResult *cmd,
                              GError **error);

gboolean test_spawn_cwd (const gchar *cwd,
                         const gchar * const *argv,
                         const gchar * const *envp,
                         CmdResult *cmd,
                         GError **error);

gboolean test_spawn (const gchar * const *argv,
                     const gchar * const *envp,
                     CmdResult *cmd,
                     GError **error);

gchar **merge_parent_and_child_env (const gchar * const *child_env);

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

gboolean reap_async_cmd (CmdAsyncResult *cmd,
                         CmdResult *reaped,
                         GError **error);

typedef struct
{
  const gchar *flag_name;
  const gchar *value;
} CmdArg;

gchar **
build_cmd_args (CmdArg *args);

typedef struct
{
  const gchar *name;
  /* only one of those should be set */
  const gchar *raw_value;
  GFile *file_value;
} CmdEnvVar;

gchar **
build_cmd_env (CmdEnvVar *vars);

G_END_DECLS
