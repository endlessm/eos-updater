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

#include "misc-utils.h"
#include "spawn-utils.h"

#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include <string.h>

void
cmd_result_clear (CmdResult *cmd)
{
  g_clear_pointer (&cmd->cmdline, g_free);
  g_clear_pointer (&cmd->standard_output, g_free);
  g_clear_pointer (&cmd->standard_error, g_free);
  cmd->exit_status = 0;
}

void
cmd_result_free (CmdResult *cmd)
{
  cmd_result_clear (cmd);
  g_free (cmd);
}

gboolean
cmd_result_ensure_ok (CmdResult *cmd,
                      GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *dump = cmd_result_dump (cmd);

  g_printerr ("**\n%s", dump);
  g_test_message ("%s", dump);
  if (!g_spawn_check_exit_status (cmd->exit_status, &local_error))
    {
      g_autofree gchar *msg = local_error->message;

      local_error->message = g_strdup_printf ("Program %s failed: %s\n\n%s",
                                              cmd->cmdline,
                                              msg,
                                              dump);
      g_propagate_error (error, g_steal_pointer (&local_error));

      return FALSE;
    }

  return TRUE;
}

gboolean
cmd_result_ensure_all_ok_verbose (GPtrArray *cmds)
{
  guint idx;
  gboolean ok = TRUE;

  for (idx = 0; idx < cmds->len; ++idx)
    {
      CmdResult *cmd = g_ptr_array_index (cmds, idx);
      g_autoptr(GError) error = NULL;
      g_autofree gchar *msg = NULL;

      if (cmd_result_ensure_ok (cmd, &error))
        continue;

      msg = g_strdup_printf ("%s failure:\n%s", cmd->cmdline, error->message);
      g_printerr ("**\n%s", msg);
      g_test_message ("%s", msg);
      ok = FALSE;
    }

  return ok;
}

gchar *
cmd_result_dump (CmdResult *cmd)
{
  return g_strdup_printf ("Output from %s (exit status: %d):\nStandard output:\n\n%s\n\nStandard error:\n\n%s\n\n",
                          cmd->cmdline,
                          cmd->exit_status,
                          cmd->standard_output,
                          cmd->standard_error);
}

void
cmd_async_result_clear (CmdAsyncResult *cmd)
{
  g_clear_pointer (&cmd->cmdline, g_free);
  g_clear_object (&cmd->in_stream);
  g_clear_object (&cmd->out_stream);
  g_clear_object (&cmd->err_stream);
  g_spawn_close_pid (cmd->pid);
  cmd->pid = 0;
}

void
cmd_async_result_free (CmdAsyncResult *cmd)
{
  cmd_async_result_clear (cmd);
  g_free (cmd);
}

gboolean
test_spawn_cwd_async (const gchar *cwd,
                      const gchar * const *argv,
                      const gchar * const *envp,
                      gboolean autoreap,
                      CmdAsyncResult *cmd,
                      GError **error)
{
  GSpawnFlags flags = G_SPAWN_DEFAULT;
  g_auto(GStrv) merged_env = merge_parent_and_child_env (envp);
  g_autofree gchar *argv_joined = g_strjoinv (" ", (gchar **) argv);
  g_autofree gchar *envp_joined = g_strjoinv ("\n - ", merged_env);

  if (!autoreap && cmd != NULL)
    flags |= G_SPAWN_DO_NOT_REAP_CHILD;

  g_test_message ("Spawning ‘%s’ in ‘%s’ with environment:\n%s",
                  argv_joined,
                  cwd,
                  envp_joined);

  if (cmd != NULL)
    {
      gint input_fd;
      gint output_fd;
      gint error_fd;

      cmd->cmdline = g_strdup (argv_joined);
      if (!g_spawn_async_with_pipes (cwd,
                                     (gchar **) argv,
                                     merged_env,
                                     flags,
                                     NULL,
                                     NULL,
                                     &cmd->pid,
                                     &input_fd,
                                     &output_fd,
                                     &error_fd,
                                     error))
        return FALSE;

      cmd->in_stream = g_unix_output_stream_new (input_fd, TRUE);
      cmd->out_stream = g_unix_input_stream_new (output_fd, TRUE);
      cmd->err_stream = g_unix_input_stream_new (error_fd, TRUE);

      return TRUE;
    }

  return g_spawn_async_with_pipes (cwd,
                                   (gchar **) argv,
                                   merged_env,
                                   flags,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   error);
}

gboolean
test_spawn_async (const gchar * const *argv,
                  const gchar * const *envp,
                  gboolean autoreap,
                  CmdAsyncResult *cmd,
                  GError **error)
{
  return test_spawn_cwd_async (NULL,
                               argv,
                               envp,
                               autoreap,
                               cmd,
                               error);
}

gboolean
test_spawn_cwd_full (const gchar *cwd,
                     const gchar * const *argv,
                     const gchar * const *envp,
                     gboolean to_dev_null,
                     CmdResult *cmd,
                     GError **error)
{
  GSpawnFlags flags = G_SPAWN_DEFAULT;
  g_auto(GStrv) merged_env = merge_parent_and_child_env (envp);
  g_autofree gchar *argv_joined = g_strjoinv (" ", (gchar **) argv);
  g_autofree gchar *envp_joined = g_strjoinv ("\n - ", merged_env);
  gchar **out = NULL;
  gchar **err = NULL;
  gint *status = NULL;

  if (cmd != NULL)
    {
      status = &cmd->exit_status;
      cmd->cmdline = g_strjoinv (" ", (gchar **) argv);
    }

  if (cmd != NULL && !to_dev_null)
    {
      out = &cmd->standard_output;
      err = &cmd->standard_error;
    }
  else
    flags |= (G_SPAWN_STDOUT_TO_DEV_NULL |
              G_SPAWN_STDERR_TO_DEV_NULL);

  if (strstr (argv[0], "/") == NULL)
    flags |= G_SPAWN_SEARCH_PATH;

  g_test_message ("Spawning ‘%s’ in ‘%s’ with environment:\n%s",
                  argv_joined,
                  cwd,
                  envp_joined);

  return g_spawn_sync (cwd,
                       (gchar **) argv,
                       merged_env,
                       flags,
                       NULL,
                       NULL,
                       out,
                       err,
                       status,
                       error);
}

gboolean
test_spawn_cwd (const gchar *cwd,
                const gchar * const *argv,
                const gchar * const *envp,
                CmdResult *cmd,
                GError **error)
{
  return test_spawn_cwd_full (cwd,
                              argv,
                              envp,
                              cmd == NULL,
                              cmd,
                              error);
}

gboolean
test_spawn (const gchar * const *argv,
            const gchar * const *envp,
            CmdResult *cmd,
            GError **error)
{
  return test_spawn_cwd (NULL,
                         argv,
                         envp,
                         cmd,
                         error);
}

static void
env_to_hash_table (const gchar * const *envp,
                   GHashTable *hash_table,
                   const gchar *desc)
{
  const gchar * const *iter;

  for (iter = envp; *iter != NULL; ++iter)
    {
      g_auto(GStrv) key_and_value = g_strsplit (*iter, "=", 2);

      if (g_strv_length (key_and_value) != 2)
        g_error ("Invalid %s environment value %s", desc, *iter);
      g_hash_table_insert (hash_table,
                           g_strdup (key_and_value[0]),
                           g_strdup (key_and_value[1]));
    }
}

static gchar **
hash_table_to_env (GHashTable *hash_table)
{
  g_autoptr(GPtrArray) envp = NULL;
  GHashTableIter hiter;
  gpointer key_ptr;
  gpointer value_ptr;

  envp = string_array_new ();
  g_hash_table_iter_init (&hiter, hash_table);
  while (g_hash_table_iter_next (&hiter, &key_ptr, &value_ptr))
    g_ptr_array_add (envp, envvar(key_ptr, value_ptr));
  g_ptr_array_add (envp, NULL);

  return (gchar **)g_ptr_array_free (g_steal_pointer (&envp), FALSE);
}

gchar **
merge_parent_and_child_env (const gchar * const *child_env)
{
  g_auto(GStrv) parent = g_get_environ ();
  g_autoptr(GHashTable) henv = NULL;

  if (child_env == NULL)
    return g_steal_pointer (&parent);

  henv = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  env_to_hash_table ((const gchar * const *) parent, henv, "parent");
  env_to_hash_table (child_env, henv, "child");

  return hash_table_to_env (henv);
}

typedef struct
{
  CmdResult *cmd;
  CmdAsyncResult *async_cmd;
  GMainLoop *loop;
  GError **error;
} ReapData;

static void
collect_output (GPid pid,
                gint status,
                gpointer reap_data_ptr)
{
  ReapData *reap_data = reap_data_ptr;

  g_main_loop_quit (reap_data->loop);
  reap_data->cmd->exit_status = status;
  if (!input_stream_to_string (reap_data->async_cmd->out_stream,
                               &reap_data->cmd->standard_output,
                               reap_data->error))
    return;

  if (!input_stream_to_string (reap_data->async_cmd->err_stream,
                               &reap_data->cmd->standard_error,
                               reap_data->error))
    return;
}

gboolean
reap_async_cmd (CmdAsyncResult *cmd,
                CmdResult *reaped,
                GError **error)
{
  g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr(GError) local_error = NULL;
  ReapData reap_data = { reaped, cmd, loop, &local_error };

  g_free (reaped->cmdline);
  reaped->cmdline = g_strdup (cmd->cmdline);
  g_child_watch_add (cmd->pid, collect_output, &reap_data);
  g_main_loop_run (loop);

  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

gchar **
build_cmd_args (CmdArg *args)
{
  gsize idx;
  g_autoptr(GPtrArray) vec = string_array_new ();

  for (idx = 0;
       args[idx].flag_name != NULL || args[idx].value != NULL;
       ++idx)
    {
      CmdArg *arg = &args[idx];
      g_autofree gchar* arg_str = NULL;

      if (arg->flag_name != NULL &&
          arg->value != NULL)
        arg_str = flag (arg->flag_name, arg->value);
      else if (arg->flag_name != NULL)
        arg_str = g_strdup_printf ("--%s", arg->flag_name);
      else
        arg_str = g_strdup (arg->value);

      g_ptr_array_add (vec, g_steal_pointer (&arg_str));
    }

  g_ptr_array_add (vec, NULL);

  return (gchar **)g_ptr_array_free (g_steal_pointer (&vec), FALSE);
}

gchar **
build_cmd_env (CmdEnvVar *vars)
{
  gsize idx;
  g_autoptr(GPtrArray) vec = string_array_new ();

  for (idx = 0; vars[idx].name != NULL; ++idx)
    {
      CmdEnvVar *var = &vars[idx];
      g_autofree gchar* env_str = NULL;

      if (var->raw_value)
        env_str = envvar (var->name, var->raw_value);
      else
        {
          g_autofree gchar *raw_path = g_file_get_path (var->file_value);

          env_str = envvar (var->name, raw_path);
        }

      g_ptr_array_add (vec, g_steal_pointer (&env_str));
    }

  g_ptr_array_add (vec, NULL);

  return (gchar **)g_ptr_array_free (g_steal_pointer (&vec), FALSE);
}
