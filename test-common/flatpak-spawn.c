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

#include "eos-test-utils.h"
#include "flatpak-spawn.h"
#include "ostree-spawn.h"

#include <string.h>

#ifndef FLATPAK_BINARY
#error FLATPAK_BINARY is not defined
#endif

static gboolean
test_spawn_flatpak_cmd_in_local_env (GFile                *updater_dir,
                                     const gchar * const  *argv,
                                     CmdResult            *cmd,
                                     GError              **error)
{
  g_autoptr(GFile) flatpak_user_dir = get_flatpak_user_dir_for_updater_dir (updater_dir);
  CmdEnvVar envv[] =
    {
      { "FLATPAK_USER_DIR", NULL, flatpak_user_dir },
      { "OSTREE_SYSROOT_DEBUG", "no-xattrs", NULL },
      { NULL, NULL, NULL }
    };
  g_auto(GStrv) envp = build_cmd_env (envv);

  return test_spawn (argv, (const gchar * const *) envp, cmd, error);
}

gboolean
flatpak_remote_add (GFile        *updater_dir,
                    const gchar  *repo_name,
                    const gchar  *repo_directory,
                    GError      **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "remote-add" },
      { "user", NULL },
      { "no-gpg-verify", NULL },
      { NULL, repo_name },
      { NULL, repo_directory },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  if (!test_spawn_flatpak_cmd_in_local_env (updater_dir,
                                            (const gchar * const *) argv,
                                            &cmd,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

gboolean
flatpak_install (GFile        *updater_dir,
                 const gchar  *remote,
                 const gchar  *app_id,
                 GError      **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "install" },
      { "user", NULL },
      { NULL, remote },
      { NULL, app_id },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  if (!test_spawn_flatpak_cmd_in_local_env (updater_dir,
                                            (const gchar * const *) argv,
                                            &cmd,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

gboolean
flatpak_uninstall (GFile        *updater_dir,
                   const gchar  *app_id,
                   GError      **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "uninstall" },
      { "user", NULL },
      { NULL, app_id },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  if (!test_spawn_flatpak_cmd_in_local_env (updater_dir,
                                            (const gchar * const *) argv,
                                            &cmd,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

gboolean
flatpak_build_init (GFile        *updater_dir,
                    const gchar  *bundle_path,
                    const gchar  *app_id,
                    const gchar  *runtime_name,
                    GError      **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "build-init" },
      { NULL, bundle_path },
      { NULL, app_id },
      /* Once as the SDK, once as the Runtime */
      { NULL, runtime_name },
      { NULL, runtime_name },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  if (!test_spawn_flatpak_cmd_in_local_env (updater_dir,
                                            (const gchar * const *) argv,
                                            &cmd,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

gboolean
flatpak_build_export (GFile        *updater_dir,
                      const gchar  *bundle_path,
                      const gchar  *repo_path,
                      GError      **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "build-export" },
      { NULL, repo_path },
      { NULL, bundle_path },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  if (!test_spawn_flatpak_cmd_in_local_env (updater_dir,
                                            (const gchar * const *) argv,
                                            &cmd,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

gboolean
flatpak_build_finish (GFile        *updater_dir,
                      const gchar  *bundle_path,
                      const gchar  *binary,
                      GError      **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "build-finish" },
      { NULL, bundle_path },
      { "command", binary },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  if (!test_spawn_flatpak_cmd_in_local_env (updater_dir,
                                            (const gchar * const *) argv,
                                            &cmd,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

gboolean
flatpak_list (GFile      *updater_dir,
              CmdResult  *cmd,
              GError    **error)
{
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "list" },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  g_return_val_if_fail (cmd != NULL, FALSE);

  if (!test_spawn_flatpak_cmd_in_local_env (updater_dir,
                                            (const gchar * const *) argv,
                                            cmd,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (cmd, error);
}

gboolean
flatpak_populate_app (GFile        *updater_dir,
                      GFile        *app_directory_path,
                      const gchar  *app_name,
                      const gchar  *runtime_name,
                      const gchar  *repo_directory,
                      GError      **error)
{
  g_autofree gchar *app_bin_dir = g_build_filename (g_file_get_path (app_directory_path),
                                                    "files",
                                                    "bin",
                                                    NULL);
  g_autofree gchar *app_executable = g_build_filename (app_bin_dir, "test", NULL);
  g_autoptr(GFile) app_bin_path = g_file_new_for_path (app_bin_dir);

  if (!flatpak_build_init (updater_dir,
                           g_file_get_path (app_directory_path),
                           app_name,
                           runtime_name,
                           error))
    return FALSE;

  if (!g_file_make_directory_with_parents (app_bin_path, NULL, error))
    return FALSE;

  if (!g_file_set_contents (app_executable, "#!/bin/bash\nexit 0\n", -1, error))
    return FALSE;

  if (!flatpak_build_finish (updater_dir,
                             g_file_get_path (app_directory_path),
                             "test",
                             error))
    return FALSE;

  if (!flatpak_build_export (updater_dir,
                             g_file_get_path (app_directory_path),
                             repo_directory,
                             error))
    return FALSE;

  return TRUE;
}

gboolean
flatpak_populate_runtime (GFile        *updater_dir,
                          GFile        *runtime_directory_path,
                          const gchar  *repo_directory,
                          const gchar  *runtime_name,
                          const gchar  *collection_id,
                          GError      **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;

  g_autofree gchar *metadata_path = g_build_filename (g_file_get_path (runtime_directory_path),
                                                      "metadata",
                                                      NULL);
  g_autofree gchar *files_dir = g_build_filename (g_file_get_path (runtime_directory_path),
                                                  "files",
                                                  NULL);
  g_autoptr(GFile) files_dir_path = g_file_new_for_path (files_dir);
  g_autofree gchar *usr_dir = g_build_filename (g_file_get_path (runtime_directory_path),
                                                "usr",
                                                NULL);
  g_autoptr(GFile) usr_dir_path = g_file_new_for_path (usr_dir);
  g_autoptr(GKeyFile) metadata = g_key_file_new ();

  g_autoptr(GFile) repo_directory_path = g_file_new_for_path (repo_directory);

  g_key_file_set_string (metadata, "Runtime", "name", runtime_name);

  if (!g_file_make_directory_with_parents (runtime_directory_path, NULL, error))
    return FALSE;

  if (!g_file_make_directory_with_parents (files_dir_path, NULL, error))
    return FALSE;

  if (!g_file_make_directory_with_parents (usr_dir_path, NULL, error))
    return FALSE;

  if (!g_key_file_save_to_file (metadata, metadata_path, error))
    return FALSE;

  if (!ostree_init (repo_directory_path,
                    REPO_ARCHIVE_Z2,
                    collection_id,
                    &cmd,
                    error))
    return FALSE;

  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  if (!flatpak_build_export (updater_dir,
                             g_file_get_path (runtime_directory_path),
                             repo_directory,
                             error))
    return FALSE;

  return TRUE;
}
