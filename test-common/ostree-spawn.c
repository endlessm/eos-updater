/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

#include <test-common/misc-utils.h>
#include <test-common/ostree-spawn.h>
#include <test-common/utils.h>

#ifndef OSTREE_BINARY
#error OSTREE_BINARY is not defined
#endif

#ifndef OSTREE_TRIVIAL_HTTPD_BINARY
#error OSTREE_TRIVIAL_HTTPD_BINARY is not defined
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
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
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
             const gchar *collection_id,
             CmdResult *cmd,
             GError **error)
{
  CmdArg args[] =
    {
      { NULL, "init" },
      { "mode", repo_mode_to_string (mode) },
      { (collection_id != NULL) ? "collection-id" : NULL, collection_id },
      { NULL, NULL }
    };

  return spawn_ostree_in_repo_args (repo,
                                    args,
                                    cmd,
                                    error);
}

static void
copy_additional_metadata_args_from_hashtable (GArray      *cmd_args,
                                              GHashTable  *metadata,
                                              GPtrArray  **out_cmd_args_membuf)
{
  gpointer key;
  gpointer value;
  GHashTableIter iter;

  g_return_if_fail (out_cmd_args_membuf != NULL);

  if (metadata == NULL)
    return;

  *out_cmd_args_membuf = g_ptr_array_new_with_free_func (g_free);

  g_hash_table_iter_init (&iter, metadata);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_autofree gchar *formatted_metadata_string = g_strdup_printf ("%s=%s",
                                                                     (const char *) key,
                                                                     (const gchar *) value);
      CmdArg arg =
        {
          "add-metadata-string",
          formatted_metadata_string
        };

      /* Note that in most cases, we are passing values to CmdArg which
       * are owned by g_autoptrs in the scope of the function where CmdArg
       * is used itself, however in this case, we are dynamically populating
       * CmdArg based on the values in the hashtable. Making matters worse is
       * the fact that the values we append to the CmdArg array
       * needs to be heap allocated strings. Those strings must have an
       * owner, but that owner cannot be CmdArg because we cannot know which
       * strings were stack allocated (or already owned) and that owner
       * is cmd_args_membuf here, which is expected to be free'd by the
       * caller. */
      g_ptr_array_add (*out_cmd_args_membuf,
                       g_steal_pointer (&formatted_metadata_string));
      g_array_append_val (cmd_args, arg);
    }
}

gboolean
ostree_cmd_remote_set_collection_id (GFile        *repo,
                                     const gchar  *remote_name,
                                     const gchar  *collection_id,
                                     CmdResult    *cmd,
                                     GError      **error)
{
  g_autofree gchar *section_name = g_strdup_printf ("remote \"%s\".collection-id",
                                                    remote_name);
  CmdArg args[] =
    {
      { NULL, "config" },
      { NULL, "set" },
      { NULL, section_name },
      { NULL, collection_id },
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
               GHashTable *metadata,
               CmdResult *cmd,
               GError **error)
{
  g_autofree gchar *gpg_home_path = g_file_get_path (gpg_home);
  g_autofree gchar *formatted_timestamp = g_date_time_format (timestamp, "%F");
  g_autofree gchar *raw_tree_path = g_file_get_path (tree_root);
  const CmdArg initial_args[] =
    {
      { NULL, "commit" },
      { "subject", subject },
      { "branch", ref },
      { "gpg-sign", keyid },
      { "gpg-homedir", gpg_home_path },
      { "timestamp", formatted_timestamp },
      { "generate-sizes", NULL },
      { NULL, raw_tree_path },
    };
  CmdArg empty = { NULL, NULL };
  g_autoptr(GArray) cmd_args = g_array_sized_new (FALSE, TRUE, sizeof (CmdArg),
                                                  (guint) G_N_ELEMENTS (initial_args) +
                                                  ((metadata != NULL) ? g_hash_table_size (metadata) : 0) +
                                                  1);
  g_autoptr(GPtrArray) formatted_cmd_args_membuf = NULL;

  g_array_append_vals (cmd_args, initial_args, G_N_ELEMENTS (initial_args));
  copy_additional_metadata_args_from_hashtable (cmd_args,
                                                metadata,
                                                &formatted_cmd_args_membuf);
  g_array_append_val (cmd_args, empty);

  return spawn_ostree_in_repo_args (repo,
                                    (CmdArg *) cmd_args->data,
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
  g_autoptr(GFile) summary_sig_file = NULL;
  g_autoptr(GFileInfo) summary_sig_info = NULL;
  g_autoptr(GDateTime) summary_sig_mtime = NULL;
  g_autoptr(GDateTime) now = NULL;
  CmdArg args[] =
    {
      { NULL, "summary" },
      { "update", NULL },
      { "gpg-sign", keyid },
      { "gpg-homedir", gpg_home_path },
      { NULL, NULL }
    };

  if (!spawn_ostree_in_repo_args (repo,
                                  args,
                                  cmd,
                                  error))
    return FALSE;

  /* To try to avoid downloading the summary file when it already has the
   * current version, the ostree client requests the summary and signature with
   * an If-Modified-Since HTTP header (when it can't use the preferable
   * If-None-Match header). The HTTP header only has second precision, so it
   * may not receive an updated summary if the request is sent within the same
   * second the summary is updated.
   *
   * To ensure the client will always receive the updated summary, sleep until
   * the next second if necessary. Note that only the signature file is checked
   * since it's always created after the summary.
   */
  summary_sig_file = g_file_get_child (repo, "summary.sig");
  summary_sig_info = g_file_query_info (summary_sig_file,
                                        G_FILE_ATTRIBUTE_TIME_MODIFIED ","
                                        G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL,
                                        error);
  if (summary_sig_info == NULL)
    return FALSE;
  summary_sig_mtime = g_file_info_get_modification_date_time (summary_sig_info);
  now = g_date_time_new_now_utc ();
  if ((g_date_time_difference (now, summary_sig_mtime) < G_USEC_PER_SEC) &&
      (g_date_time_get_second (now) == g_date_time_get_second (summary_sig_mtime)))
    g_usleep (G_USEC_PER_SEC - (gulong) g_date_time_get_microsecond (now));

  return TRUE;
}

gboolean
ostree_show (GFile *repo,
             const gchar *refspec,
             CmdResult *cmd,
             GError **error)
{
  CmdArg args[] =
    {
      { NULL, "show" },
      { NULL, refspec },
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
                   const OstreeCollectionRef *collection_ref,
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
      { NULL, collection_ref->ref_name },
      { (collection_ref->collection_id != NULL) ? "collection-id" : NULL, collection_ref->collection_id },
      { NULL, NULL }
    };

  return spawn_ostree_in_repo_args (repo,
                                    args,
                                    cmd,
                                    error);
}

gboolean
ostree_ref_create (GFile *repo,
                   const gchar *ref_name,
                   const gchar *commit_id,
                   CmdResult *cmd,
                   GError **error)
{
  CmdArg args[] =
    {
      { NULL, "refs" },
      { "create", ref_name },
      { NULL, commit_id },
      { NULL, NULL }
    };

    return spawn_ostree_in_repo_args (repo,
                                      args,
                                      cmd,
                                      error);
}

gboolean
ostree_ref_delete (GFile *repo,
                   const gchar *ref_name,
                   CmdResult *cmd,
                   GError **error)
{
  CmdArg args[] =
    {
      { NULL, "refs" },
      { "delete", NULL },
      { NULL, ref_name },
      { NULL, NULL }
    };

    return spawn_ostree_in_repo_args (repo,
                                      args,
                                      cmd,
                                      error);
}

gboolean
ostree_prune (GFile *repo,
              OstreePruneFlags flags,
              gint depth_opt,
              CmdResult *cmd,
              GError **error)
{
  g_autoptr(GArray) args = cmd_arg_array_new ();
  g_autofree gchar *depth_str = g_strdup_printf ("%d", depth_opt);
  CmdArg prune = { NULL, "prune" };
  CmdArg refs_only = { "refs-only", NULL };
  CmdArg no_prune = { "no-prune", NULL };
  CmdArg verbose = { "verbose", NULL };
  CmdArg depth = { "depth", depth_str };
  CmdArg terminator = { NULL, NULL };

  g_array_append_val (args, prune);
  if ((flags & OSTREE_PRUNE_REFS_ONLY) == OSTREE_PRUNE_REFS_ONLY)
    g_array_append_val (args, refs_only);
  if ((flags & OSTREE_PRUNE_NO_PRUNE) == OSTREE_PRUNE_NO_PRUNE)
    g_array_append_val (args, no_prune);
  if ((flags & OSTREE_PRUNE_VERBOSE) == OSTREE_PRUNE_VERBOSE)
    g_array_append_val (args, verbose);
  g_array_append_val (args, depth);
  g_array_append_val (args, terminator);

  return spawn_ostree_in_repo_args (repo,
                                    cmd_arg_array_raw (args),
                                    cmd,
                                    error);
}

gboolean
ostree_static_delta_generate (GFile *repo,
                              const gchar *from,
                              const gchar *to,
                              CmdResult *cmd,
                              GError **error)
{
  CmdArg args[] =
    {
      { NULL, "static-delta" },
      { NULL, "generate" },
      { "from", from },
      { "to", to },
      { NULL, NULL }
    };

  return spawn_ostree_in_repo_args (repo,
                                    args,
                                    cmd,
                                    error);
}

gboolean
ostree_ls (GFile *repo,
           OstreeLsFlags flags,
           const gchar *ref,
           const gchar * const *paths,
           CmdResult *cmd,
           GError **error)
{
  g_autoptr(GArray) args = cmd_arg_array_new ();
  CmdArg ls = { NULL, "ls" };
  CmdArg dir_only = { "dironly", NULL };
  CmdArg recursive = { "recursive", NULL };
  CmdArg checksum = { "checksum", NULL };
  CmdArg xattrs = { "xattrs", NULL };
  CmdArg nul_filenames_only = { "nul-filenames-only", NULL };
  CmdArg ref_arg = { NULL, ref };
  CmdArg terminator = { NULL, NULL };
  const gchar * const * iter;

  g_array_append_val (args, ls);
  if ((flags & OSTREE_LS_DIR_ONLY) == OSTREE_LS_DIR_ONLY)
    g_array_append_val (args, dir_only);
  if ((flags & OSTREE_LS_RECURSIVE) == OSTREE_LS_RECURSIVE)
    g_array_append_val (args, recursive);
  if ((flags & OSTREE_LS_CHECKSUM) == OSTREE_LS_CHECKSUM)
    g_array_append_val (args, checksum);
  if ((flags & OSTREE_LS_XATTRS) == OSTREE_LS_XATTRS)
    g_array_append_val (args, xattrs);
  if ((flags & OSTREE_LS_NUL_FILENAMES_ONLY) == OSTREE_LS_NUL_FILENAMES_ONLY)
    g_array_append_val (args, nul_filenames_only);
  g_array_append_val (args, ref_arg);
  for (iter = paths; *iter != NULL; ++iter)
    {
      CmdArg path_arg = { NULL, *iter };

      g_array_append_val (args, path_arg);
    }
  g_array_append_val (args, terminator);

  return spawn_ostree_in_repo_args (repo,
                                    cmd_arg_array_raw (args),
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
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
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
ostree_undeploy (GFile *sysroot,
                 int deployment_index,
                 CmdResult *cmd,
                 GError **error)
{
  g_autofree gchar *index_str = g_strdup_printf ("%d", deployment_index);
  const gchar *args[] = {
    index_str,
    NULL
  };

  return ostree_admin_spawn_in_sysroot (sysroot,
                                        "undeploy",
                                        args,
                                        cmd,
                                        error);
}

gboolean
ostree_list_refs_in_repo (GFile      *repo,
                          CmdResult  *cmd,
                          GError    **error)
{
  g_autoptr(GPtrArray) argv = string_array_new ();
  g_autofree gchar *repo_path = g_file_get_path (repo);
  CmdArg args[] =
    {
      { NULL, OSTREE_BINARY },
      { NULL, "refs" },
      { "repo", repo_path },
      { NULL, NULL }
    };
  g_auto(GStrv) raw_args = build_cmd_args (args);

  return test_spawn ((const gchar * const *) raw_args,
                     NULL,
                     cmd,
                     error);
}

gboolean
ostree_httpd (GFile *served_dir,
              GFile *port_file,
              GFile *log_file,
              CmdResult *cmd,
              GError **error)
{
  g_autofree gchar *raw_port_file = g_file_get_path (port_file);
  g_autofree gchar *raw_served_dir = g_file_get_path (served_dir);
  g_autofree gchar *raw_log_file = g_file_get_path (log_file);
  CmdArg args[] =
    {
      { NULL, OSTREE_TRIVIAL_HTTPD_BINARY },
      { "autoexit", NULL },
      { "daemonize", NULL },
      { "port-file", raw_port_file },
      { "log-file", raw_log_file },
      { NULL, raw_served_dir },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);
  CmdEnvVar envv[] =
    {
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
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
