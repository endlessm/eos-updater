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

#include "utils.h"
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
  g_autoptr(GFile) flatpak_system_dir = g_file_get_child (updater_dir, "flatpak-system");
  g_autoptr(GFile) flatpak_system_cache_dir = g_file_get_child (updater_dir, "flatpak-system-cache");
  CmdEnvVar envv[] =
    {
      /* All operations are done in the user repository, since it’s easy to override: */
      { "FLATPAK_USER_DIR", NULL, flatpak_user_dir },
      { "FLATPAK_SYSTEM_DIR", NULL, flatpak_system_dir },
      { "FLATPAK_SYSTEM_CACHE_DIR", NULL, flatpak_system_cache_dir },
      { "OSTREE_SYSROOT_DEBUG", "no-xattrs", NULL },
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
      { NULL, NULL, NULL }
    };
  g_auto(GStrv) envp = build_cmd_env (envv);

  return test_spawn (argv, (const gchar * const *) envp, cmd, error);
}

gboolean
flatpak_remote_add (GFile        *updater_dir,
                    const gchar  *repo_name,
                    const gchar  *repo_directory,
                    GFile        *gpg_key,
                    GError      **error)
{
  g_autofree gchar *raw_key_path = g_file_get_path (gpg_key);
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "remote-add" },
      { "user", NULL },
      { "gpg-import", raw_key_path },
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
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "install" },
      { "user", NULL },
      { "assumeyes", NULL },
      { "noninteractive", NULL },
      { "verbose", NULL },
      { "ostree-verbose", NULL },
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
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "uninstall" },
      { "user", NULL },
      { "assumeyes", NULL },
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
                    const gchar  *branch,
                    GError      **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "build-init" },
      { "verbose", NULL },
      { "ostree-verbose", NULL },
      { NULL, bundle_path },
      { NULL, app_id },
      /* Once as the SDK, once as the Runtime */
      { NULL, runtime_name },
      { NULL, runtime_name },
      { NULL, branch },
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
                      const gchar  *branch,
                      const gchar  *collection_id,
                      gboolean      is_runtime,
                      GFile        *gpg_homedir,
                      const gchar  *keyid,
                      GError      **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autofree gchar *gpg_homedir_path = g_file_get_path (gpg_homedir);
  CmdArg args[] =
    {
      { NULL, FLATPAK_BINARY },
      { NULL, "build-export" },
      { NULL, repo_path },
      { NULL, bundle_path },
      { NULL, branch },
      { "gpg-sign", keyid },
      { "gpg-homedir", gpg_homedir_path },
      { NULL, NULL },  /* replaced with runtime below */
      { NULL, NULL },  /* replaced with collection ID below */
      { NULL, NULL }
    };
  g_auto(GStrv) argv = NULL;

  gsize next_arg = G_N_ELEMENTS (args) - 3;
  if (is_runtime)
    {
      args[next_arg].flag_name = "runtime";
      args[next_arg++].value = NULL;
    }
  if (collection_id != NULL)
    {
      args[next_arg].flag_name = "collection-id";
      args[next_arg++].value = collection_id;
    }

  argv = build_cmd_args (args);

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
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
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
      { "user", NULL },
      { "columns", "ref" },
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

static gboolean
customize_extensions (const gchar  *app_directory_path,
                      GPtrArray    *extension_infos,
                      GError      **error)
{
  g_autofree gchar *metadata_file_path = NULL;
  g_autoptr(GKeyFile) key_file = NULL;

  /* No extension information, so no changes need to be made to
   * the key file */
  if (extension_infos == NULL)
    return TRUE;

  metadata_file_path = g_build_filename (app_directory_path, "metadata", NULL);
  key_file = g_key_file_new ();

  if (!g_key_file_load_from_file (key_file,
                                  metadata_file_path,
                                  G_KEY_FILE_NONE,
                                  error))
    return FALSE;

  for (gsize i = 0; i < extension_infos->len; ++i)
    {
      FlatpakExtensionPointInfo *extension_info = g_ptr_array_index (extension_infos, i);
      g_autofree gchar *extension_group_name = g_strdup_printf ("Extension %s", extension_info->name);

      g_key_file_set_string (key_file, extension_group_name, "directory", extension_info->directory);

      if (extension_info->versions != NULL)
        {
          guint n_versions = g_strv_length (extension_info->versions);
          g_key_file_set_string_list (key_file,
                                      extension_group_name,
                                      "versions",
                                      (const gchar * const *) extension_info->versions,
                                      n_versions);
        }

      g_key_file_set_boolean (key_file,
                              extension_group_name,
                              "no-autodownload",
                              (extension_info->flags & FLATPAK_EXTENSION_POINT_NO_AUTODOWNLOAD) != 0);
      g_key_file_set_boolean (key_file,
                              extension_group_name,
                              "locale-subset",
                              (extension_info->flags & FLATPAK_EXTENSION_POINT_LOCALE_SUBSET) != 0);
      g_key_file_set_boolean (key_file,
                              extension_group_name,
                              "autodelete",
                              (extension_info->flags & FLATPAK_EXTENSION_POINT_AUTODELETE) != 0);
    }

  if (!g_key_file_save_to_file (key_file, metadata_file_path, error))
    return FALSE;

  return TRUE;
}

gboolean
flatpak_populate_app (GFile        *updater_dir,
                      GFile        *app_directory_path,
                      const gchar  *app_name,
                      const gchar  *runtime_name,
                      const gchar  *branch,
                      GPtrArray    *extension_infos,
                      const gchar  *repo_directory,
                      const gchar  *repo_collection_id,
                      GFile        *gpg_homedir,
                      const gchar  *keyid,
                      GError      **error)
{
  g_autofree gchar *app_directory_path_str = g_file_get_path (app_directory_path);
  g_autofree gchar *app_bin_dir = g_build_filename (app_directory_path_str,
                                                    "files",
                                                    "bin",
                                                    NULL);
  g_autofree gchar *app_executable = g_build_filename (app_bin_dir, "test", NULL);
  g_autoptr(GFile) app_bin_path = g_file_new_for_path (app_bin_dir);

  if (!flatpak_build_init (updater_dir,
                           app_directory_path_str,
                           app_name,
                           runtime_name,
                           branch,
                           error))
    return FALSE;

  if (!g_file_make_directory_with_parents (app_bin_path, NULL, error))
    return FALSE;

  if (!g_file_set_contents (app_executable, "#!/bin/bash\nexit 0\n", -1, error))
    return FALSE;

  if (!customize_extensions (app_directory_path_str, extension_infos, error))
    return FALSE;

  if (!flatpak_build_finish (updater_dir,
                             app_directory_path_str,
                             "test",
                             error))
    return FALSE;

  if (!flatpak_build_export (updater_dir,
                             app_directory_path_str,
                             repo_directory,
                             branch,
                             repo_collection_id,
                             FALSE,
                             gpg_homedir,
                             keyid,
                             error))
    return FALSE;

  return TRUE;
}

gboolean
flatpak_populate_runtime (GFile        *updater_dir,
                          GFile        *runtime_directory_path,
                          const gchar  *repo_directory,
                          const gchar  *name,
                          const gchar  *runtime_name,
                          const gchar  *branch,
                          GPtrArray    *extension_infos,
                          const gchar  *repo_collection_id,
                          GFile        *gpg_homedir,
                          const gchar  *keyid,
                          GError      **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

  g_autofree gchar *runtime_directory_path_str = g_file_get_path (runtime_directory_path);
  g_autofree gchar *metadata_path = g_build_filename (runtime_directory_path_str,
                                                      "metadata",
                                                      NULL);
  g_autofree gchar *files_dir = g_build_filename (runtime_directory_path_str,
                                                  "files",
                                                  NULL);
  g_autoptr(GFile) files_dir_path = g_file_new_for_path (files_dir);
  g_autofree gchar *usr_dir = g_build_filename (runtime_directory_path_str,
                                                "usr",
                                                NULL);
  g_autoptr(GFile) usr_dir_path = g_file_new_for_path (usr_dir);
  g_autoptr(GKeyFile) metadata = g_key_file_new ();

  g_autoptr(GFile) repo_directory_path = g_file_new_for_path (repo_directory);

  g_key_file_set_string (metadata, "Runtime", "name", name);
  g_key_file_set_string (metadata, "Runtime", "runtime", runtime_name);

  if (!g_file_make_directory_with_parents (runtime_directory_path, NULL, error))
    return FALSE;

  if (!g_file_make_directory_with_parents (files_dir_path, NULL, error))
    return FALSE;

  if (!g_file_make_directory_with_parents (usr_dir_path, NULL, error))
    return FALSE;

  if (!g_key_file_save_to_file (metadata, metadata_path, error))
    return FALSE;

  if (!customize_extensions (runtime_directory_path_str, extension_infos, error))
    return FALSE;

  if (!ostree_init (repo_directory_path,
                    REPO_ARCHIVE_Z2,
                    repo_collection_id,
                    &cmd,
                    error))
    return FALSE;

  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  if (!flatpak_build_export (updater_dir,
                             runtime_directory_path_str,
                             repo_directory,
                             branch,
                             repo_collection_id,
                             TRUE,
                             gpg_homedir,
                             keyid,
                             error))
    return FALSE;

  return TRUE;
}

gboolean
flatpak_populate_extension (GFile        *updater_dir,
                            GFile        *extension_directory,
                            const gchar  *repo_directory,
                            const gchar  *name,
                            const gchar  *runtime_name,
                            const gchar  *branch,
                            const gchar  *extension_of_ref,
                            const gchar  *repo_collection_id,
                            GFile        *gpg_homedir,
                            const gchar  *keyid,
                            GError      **error)
{
  g_autofree gchar *extension_directory_path_str = g_file_get_path (extension_directory);
  g_autoptr(GKeyFile) key_file = g_key_file_new ();
  g_autoptr(GFile) metadata_file = g_file_get_child (extension_directory, "metadata");
  g_autofree gchar *metadata_file_path = g_file_get_path (metadata_file);
  g_autoptr(GPtrArray) extension_infos = g_ptr_array_new ();

  /* This structure causes us to build-export twice, but that's probably
   * not a huge problem */
  if (!flatpak_populate_runtime (updater_dir,
                                 extension_directory,
                                 repo_directory,
                                 name,
                                 runtime_name,
                                 branch,
                                 extension_infos,
                                 repo_collection_id,
                                 gpg_homedir,
                                 keyid,
                                 error))
    return FALSE;

  if (!g_key_file_load_from_file (key_file, metadata_file_path, G_KEY_FILE_NONE, error))
    return FALSE;

  if (extension_of_ref != NULL)
    g_key_file_set_string (key_file, "ExtensionOf", "ref", extension_of_ref);

  if (!g_key_file_save_to_file (key_file, metadata_file_path, error))
    return FALSE;

  if (!flatpak_build_export (updater_dir,
                             extension_directory_path_str,
                             repo_directory,
                             branch,
                             repo_collection_id,
                             TRUE,
                             gpg_homedir,
                             keyid,
                             error))
    return FALSE;

  return TRUE;
}
