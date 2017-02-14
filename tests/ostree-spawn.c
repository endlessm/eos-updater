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

#include "eos-test-utils.h"
#include "misc-utils.h"
#include "ostree-spawn.h"

#ifndef OSTREE_BINARY
#error OSTREE_BINARY is not defined
#endif

static void
copy_strv_to_ptr_array (const gchar * const *strv,
                        GPtrArray *array)
{
  const gchar * const *iter;

  if (strv != NULL)
    for (iter = strv; *iter != NULL; ++iter)
      g_ptr_array_add (array, g_strdup (*iter));
  g_ptr_array_add (array, NULL);
}

static gboolean
spawn_ostree_in_repo (GFile *repo,
                      const gchar * const *args,
                      CmdResult *cmd,
                      GError **error)
{
  g_autoptr(GPtrArray) argv = string_array_new ();
  g_autofree gchar *raw_repo_path = g_file_get_path (repo);
  CmdEnvVar envv[] =
    {
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { NULL, NULL, NULL },
    };
  g_auto(GStrv) envp = build_cmd_env (envv);

  g_ptr_array_add (argv, g_strdup (OSTREE_BINARY));
  g_ptr_array_add (argv, flag ("repo", raw_repo_path));
  copy_strv_to_ptr_array (args, argv);

  return test_spawn ((const gchar * const *) argv->pdata,
                     (const gchar * const *) envp, cmd, error);
}

static gboolean
spawn_ostree_in_repo_args (GFile *repo,
                           CmdArg *args,
                           CmdResult *cmd,
                           GError **error)
{
  g_auto(GStrv) raw_args = build_cmd_args (args);

  return spawn_ostree_in_repo (repo,
                               (const gchar * const *) raw_args,
                               cmd,
                               error);
}

static const gchar *
repo_mode_to_string (RepoMode mode)
{
  switch (mode)
    {
    case REPO_ARCHIVE_Z2:
      return "archive-z2";

    case REPO_BARE:
      return "bare";

    default:
      g_assert_not_reached ();
    }
}

gboolean
ostree_init (GFile *repo,
             RepoMode mode,
             CmdResult *cmd,
             GError **error)
{
  CmdArg args[] =
    {
      { NULL, "init" },
      { "mode", repo_mode_to_string (mode) },
      { NULL, NULL }
    };

  return spawn_ostree_in_repo_args (repo,
                                    args,
                                    cmd,
                                    error);
}

gboolean
ostree_commit (GFile *repo,
               GFile *tree_root,
               const gchar *subject,
               const gchar *ref,
               GFile *gpg_home,
               const gchar *keyid,
               GDateTime *timestamp,
               CmdResult *cmd,
               GError **error)
{
  g_autofree gchar *gpg_home_path = g_file_get_path (gpg_home);
  g_autofree gchar *formatted_timestamp = g_date_time_format (timestamp, "%F");
  g_autofree gchar *raw_tree_path = g_file_get_path (tree_root);
  CmdArg args[] =
    {
      { NULL, "commit" },
      { "subject", subject },
      { "branch", ref },
      { "gpg-sign", keyid },
      { "gpg-homedir", gpg_home_path },
      { "timestamp", formatted_timestamp },
      { NULL, raw_tree_path },
      { NULL, NULL }
    };

  return spawn_ostree_in_repo_args (repo,
                                    args,
                                    cmd,
                                    error);
}

gboolean
ostree_summary (GFile *repo,
                GFile *gpg_home,
                const gchar *keyid,
                CmdResult *cmd,
                GError **error)
{
  g_autofree gchar *gpg_home_path = g_file_get_path (gpg_home);
  CmdArg args[] =
    {
      { NULL, "summary" },
      { "update", NULL },
      { "gpg-sign", keyid },
      { "gpg-homedir", gpg_home_path },
      { NULL, NULL }
    };

  return spawn_ostree_in_repo_args (repo,
                                    args,
                                    cmd,
                                    error);
}

gboolean
ostree_pull (GFile *repo,
             const gchar *remote_name,
             const gchar *ref,
             CmdResult *cmd,
             GError **error)
{
  const gchar *args[] =
    {
      "pull",
      remote_name,
      ref,
      NULL
    };

  return spawn_ostree_in_repo (repo,
                               args,
                               cmd,
                               error);
}

gboolean
ostree_remote_add (GFile *repo,
                   const gchar *remote_name,
                   const gchar *remote_url,
                   const gchar *ref,
                   GFile *gpg_key,
                   CmdResult *cmd,
                   GError **error)
{
  g_autofree gchar *raw_key_path = g_file_get_path (gpg_key);
  CmdArg args[] =
    {
      { NULL, "remote" },
      { NULL, "add" },
      { "gpg-import", raw_key_path },
      { NULL, remote_name },
      { NULL, remote_url },
      { NULL, ref },
      { NULL, NULL }
    };

  return spawn_ostree_in_repo_args (repo,
                                    args,
                                    cmd,
                                    error);
}

static gboolean
ostree_admin_spawn_in_sysroot (GFile *sysroot,
                               const gchar *admin_subcommand,
                               const gchar * const *args,
                               CmdResult *cmd,
                               GError **error)
{
  g_autofree gchar *raw_sysroot_path = g_file_get_path (sysroot);
  g_autoptr(GPtrArray) argv = string_array_new ();
  CmdEnvVar envv[] =
    {
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { NULL, NULL, NULL },
    };
  g_auto(GStrv) envp = build_cmd_env (envv);

  g_ptr_array_add (argv, g_strdup (OSTREE_BINARY));
  g_ptr_array_add (argv, g_strdup ("admin"));
  g_ptr_array_add (argv, g_strdup (admin_subcommand));
  g_ptr_array_add (argv, flag ("sysroot", raw_sysroot_path));
  copy_strv_to_ptr_array (args, argv);

  return test_spawn ((const gchar * const *) argv->pdata,
                     (const gchar * const *) envp, cmd, error);
}

static gboolean
ostree_admin_spawn_in_sysroot_args (GFile *sysroot,
                                    const gchar *admin_subcommand,
                                    CmdArg *args,
                                    CmdResult *cmd,
                                    GError **error)
{
  g_auto(GStrv) raw_args = build_cmd_args (args);

  return ostree_admin_spawn_in_sysroot (sysroot, admin_subcommand,
                                        (const gchar * const *) raw_args,
                                        cmd, error);
}

gboolean
ostree_deploy (GFile *sysroot,
               const gchar *osname,
               const gchar *refspec,
               CmdResult *cmd,
               GError **error)
{
  CmdArg args[] =
    {
      { "os", osname },
      { "retain", NULL },
      { NULL, refspec },
      { NULL, NULL }
    };

  return ostree_admin_spawn_in_sysroot_args (sysroot,
                                             "deploy",
                                             args,
                                             cmd,
                                             error);
}

gboolean
ostree_init_fs (GFile *sysroot,
                CmdResult *cmd,
                GError **error)
{
  g_autofree gchar *raw_sysroot_path = g_file_get_path (sysroot);
  const gchar *args[] =
    {
      raw_sysroot_path,
      NULL
    };

  return ostree_admin_spawn_in_sysroot (sysroot,
                                        "init-fs",
                                        args,
                                        cmd,
                                        error);
}

gboolean
ostree_os_init (GFile *sysroot,
                const gchar *remote_name,
                CmdResult *cmd,
                GError **error)
{
  const gchar *args[] =
    {
      remote_name,
      NULL
    };

  return ostree_admin_spawn_in_sysroot (sysroot,
                                        "os-init",
                                        args,
                                        cmd,
                                        error);
}

gboolean
ostree_status (GFile *sysroot,
               CmdResult *cmd,
               GError **error)
{
  return ostree_admin_spawn_in_sysroot (sysroot,
                                        "status",
                                        NULL,
                                        cmd,
                                        error);
}

gboolean
ostree_httpd (GFile *served_dir,
              GFile *port_file,
              CmdResult *cmd,
              GError **error)
{
  g_autofree gchar *raw_port_file = g_file_get_path (port_file);
  g_autofree gchar *raw_served_dir = g_file_get_path (served_dir);
  CmdArg args[] =
    {
      { NULL, OSTREE_BINARY },
      { NULL, "trivial-httpd" },
      { "autoexit", NULL },
      { "daemonize", NULL },
      { "port-file", raw_port_file },
      { NULL, raw_served_dir },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);
  CmdEnvVar envv[] =
    {
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { NULL, NULL, NULL },
    };
  g_auto(GStrv) envp = build_cmd_env (envv);

  if (!test_spawn_cwd_full (NULL,
                            (const gchar * const *) argv,
                            (const gchar * const *) envp,
                            TRUE,
                            cmd,
                            error))
    return FALSE;

  return TRUE;
}
