/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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
 *  - Sam Spilsbury <sam@endlessm.com>
 */

#include <test-common/flatpak-spawn.h>
#include <test-common/gpg.h>
#include <test-common/misc-utils.h>
#include <test-common/spawn-utils.h>
#include <test-common/ostree-spawn.h>
#include <test-common/utils.h>
#include <test-common/convenience.h>

#include <ostree.h>
#include <flatpak.h>
#include <json-glib/json-glib.h>

#include <gio/gio.h>
#include <locale.h>
#include <string.h>

typedef enum {
  FLATPAK_TO_INSTALL_FLAGS_NONE = 0,
  FLATPAK_TO_INSTALL_FLAGS_SKIP_TESTING_ARCHITECTURE = 1 << 0,
  FLATPAK_TO_INSTALL_FLAGS_ONLY_NOT_TESTING_ARCHITECTURE = 1 << 1,
  FLATPAK_TO_INSTALL_FLAGS_SKIP_TESTING_LOCALE = 1 << 2,
  FLATPAK_TO_INSTALL_FLAGS_ONLY_NOT_TESTING_LOCALE = 1 << 3,
} FlatpakToInstallFlags;

typedef struct {
  const gchar *action;
  const gchar *collection_id;
  const gchar *remote;
  const gchar *app_id;
  const gchar *branch;
  const gchar *ref_kind;
  FlatpakToInstallFlags flags;
} FlatpakToInstall;

static void
install_json_detail (const FlatpakToInstall *flatpak_to_install,
                     JsonBuilder            *builder)
{
  json_builder_set_member_name (builder, "ref-kind");
  json_builder_add_string_value (builder, flatpak_to_install->ref_kind);

  if (flatpak_to_install->collection_id)
    {
      json_builder_set_member_name (builder, "collection-id");
      json_builder_add_string_value (builder, flatpak_to_install->collection_id);
    }

  if (flatpak_to_install->remote)
    {
      json_builder_set_member_name (builder, "remote");
      json_builder_add_string_value (builder, flatpak_to_install->remote);
    }

  if (flatpak_to_install->branch)
    {
      json_builder_set_member_name (builder, "branch");
      json_builder_add_string_value (builder, flatpak_to_install->branch);
    }

  json_builder_set_member_name (builder, "name");
  json_builder_add_string_value (builder, flatpak_to_install->app_id);
}

static void
uninstall_json_detail (const FlatpakToInstall *flatpak_to_install,
                       JsonBuilder            *builder)
{
  json_builder_set_member_name (builder, "ref-kind");
  json_builder_add_string_value (builder, flatpak_to_install->ref_kind);

  json_builder_set_member_name (builder, "name");
  json_builder_add_string_value (builder, flatpak_to_install->app_id);

  if (flatpak_to_install->branch)
    {
      json_builder_set_member_name (builder, "branch");
      json_builder_add_string_value (builder, flatpak_to_install->branch);
    }
}

static void
update_json_detail (const FlatpakToInstall *flatpak_to_install,
                    JsonBuilder            *builder)
{
  json_builder_set_member_name (builder, "ref-kind");
  json_builder_add_string_value (builder, flatpak_to_install->ref_kind);

  json_builder_set_member_name (builder, "name");
  json_builder_add_string_value (builder, flatpak_to_install->app_id);

  if (flatpak_to_install->branch)
    {
      json_builder_set_member_name (builder, "branch");
      json_builder_add_string_value (builder, flatpak_to_install->branch);
    }
}

static void
add_detail_for_action_type (const FlatpakToInstall *flatpak_to_install,
                            JsonBuilder            *builder)
{
  if (g_strcmp0 (flatpak_to_install->action, "install") == 0)
    install_json_detail (flatpak_to_install, builder);
  else if (g_strcmp0 (flatpak_to_install->action, "uninstall") == 0)
    uninstall_json_detail (flatpak_to_install, builder);
  else if (g_strcmp0 (flatpak_to_install->action, "update") == 0)
    update_json_detail (flatpak_to_install, builder);
  else
    g_assert_not_reached ();
}

static JsonNode *
filters_for_action (const FlatpakToInstall *flatpak_to_install)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_object (builder);

  if (flatpak_to_install->flags & FLATPAK_TO_INSTALL_FLAGS_SKIP_TESTING_ARCHITECTURE)
    {
      g_autoptr(JsonBuilder) skip_architectures_array_builder = json_builder_new ();

      json_builder_begin_array (skip_architectures_array_builder);
      json_builder_add_string_value (skip_architectures_array_builder, "arch");
      json_builder_end_array (skip_architectures_array_builder);

      json_builder_set_member_name (builder, "~architecture");
      json_builder_add_value (builder,
                              json_builder_get_root (skip_architectures_array_builder));
    }

  if (flatpak_to_install->flags & FLATPAK_TO_INSTALL_FLAGS_ONLY_NOT_TESTING_ARCHITECTURE)
    {
      g_autoptr(JsonBuilder) skip_architectures_array_builder = json_builder_new ();

      json_builder_begin_array (skip_architectures_array_builder);
      json_builder_add_string_value (skip_architectures_array_builder, "differentarch");
      json_builder_end_array (skip_architectures_array_builder);

      json_builder_set_member_name (builder, "architecture");
      json_builder_add_value (builder,
                              json_builder_get_root (skip_architectures_array_builder));
    }

  if (flatpak_to_install->flags & FLATPAK_TO_INSTALL_FLAGS_SKIP_TESTING_LOCALE)
    {
      g_autoptr(JsonBuilder) skip_architectures_array_builder = json_builder_new ();

      json_builder_begin_array (skip_architectures_array_builder);
      json_builder_add_string_value (skip_architectures_array_builder, "locale");
      json_builder_end_array (skip_architectures_array_builder);

      json_builder_set_member_name (builder, "~locale");
      json_builder_add_value (builder,
                              json_builder_get_root (skip_architectures_array_builder));
    }

  if (flatpak_to_install->flags & FLATPAK_TO_INSTALL_FLAGS_ONLY_NOT_TESTING_LOCALE)
    {
      g_autoptr(JsonBuilder) skip_architectures_array_builder = json_builder_new ();

      json_builder_begin_array (skip_architectures_array_builder);
      json_builder_add_string_value (skip_architectures_array_builder, "differentlocale");
      json_builder_end_array (skip_architectures_array_builder);

      json_builder_set_member_name (builder, "locale");
      json_builder_add_value (builder,
                              json_builder_get_root (skip_architectures_array_builder));
    }

  json_builder_end_object (builder);

  return json_builder_get_root (builder);
}

static JsonNode *
flatpak_to_install_to_json_entry (const FlatpakToInstall *flatpak_to_install,
                                  guint                   serial)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "action");
  json_builder_add_string_value (builder, flatpak_to_install->action);

  json_builder_set_member_name (builder, "serial");
  json_builder_add_int_value (builder, serial);

  add_detail_for_action_type (flatpak_to_install, builder);

  json_builder_set_member_name (builder, "filters");
  json_builder_add_value (builder, filters_for_action (flatpak_to_install));

  json_builder_end_object (builder);

  return json_builder_get_root (builder);
}

static JsonNode *
flatpaks_to_install_to_json (const FlatpakToInstall *flatpaks,
                             gsize                   n_flatpaks)
{
  gsize i;
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_array (builder);

  /* Zero is not a valid serial, we pass i + 1 */
  for (i = 0; i < n_flatpaks; ++i)
    json_builder_add_value (builder,
                            flatpak_to_install_to_json_entry (&flatpaks[i],
                                                              (guint) i + 1));

  json_builder_end_array (builder);

  return json_builder_get_root (builder);
}

static gchar *
flatpaks_to_install_to_string (const FlatpakToInstall *flatpaks,
                               gsize                   n_flatpaks)
{
  g_autoptr(JsonGenerator) gen = json_generator_new ();
  g_autoptr(JsonNode) node = flatpaks_to_install_to_json (flatpaks, n_flatpaks);

  json_generator_set_root (gen, node);
  return json_generator_to_data (gen, NULL);
}

static GStrv
flatpaks_to_install_app_ids_strv (FlatpakToInstall *flatpaks_to_install,
                                  gsize             n_flatpaks_to_install)
{
  GStrv strv = g_new0 (gchar *, n_flatpaks_to_install + 1);
  gsize i = 0;

  for (; i < n_flatpaks_to_install; ++i)
    strv[i] = g_strdup (flatpaks_to_install[i].app_id);

  return strv;
}

static void
autoinstall_flatpaks_files_name (guint                    commit,
                                 const gchar             *name,
                                 const FlatpakToInstall  *flatpaks,
                                 gsize                    n_flatpaks,
                                 GHashTable             **out_directories_hashtable,
                                 GHashTable             **out_files_hashtable)
{
  g_autofree gchar *autoinstall_flatpaks_contents = flatpaks_to_install_to_string (flatpaks, n_flatpaks);
  GStrv directories_to_create_strv = g_new0 (gchar *, 2);
  GPtrArray *files_to_create = g_ptr_array_new_full (1, simple_file_free);

  g_return_if_fail (out_directories_hashtable != NULL);
  g_return_if_fail (out_files_hashtable != NULL);

  if (*out_directories_hashtable == NULL)
    *out_directories_hashtable = g_hash_table_new_full (g_direct_hash,
                                                        g_direct_equal,
                                                        NULL,
                                                        (GDestroyNotify) g_strfreev);
  if (*out_files_hashtable == NULL)
    *out_files_hashtable = g_hash_table_new_full (g_direct_hash,
                                                  g_direct_equal,
                                                  NULL,
                                                  (GDestroyNotify) g_ptr_array_free);

  directories_to_create_strv[0] = g_build_filename ("usr", "share", "eos-application-tools", "flatpak-autoinstall.d", NULL);
  g_ptr_array_add (files_to_create,
                   simple_file_new_steal (g_build_filename ("usr", "share", "eos-application-tools", "flatpak-autoinstall.d", name, NULL),
                                          g_steal_pointer (&autoinstall_flatpaks_contents)));

  g_hash_table_insert (*out_directories_hashtable,
                       GUINT_TO_POINTER (commit),
                       directories_to_create_strv);
  g_hash_table_insert (*out_files_hashtable,
                       GUINT_TO_POINTER (commit),
                       files_to_create);
}

static void
autoinstall_flatpaks_files (guint                    commit,
                            const FlatpakToInstall  *flatpaks,
                            gsize                    n_flatpaks,
                            GHashTable             **out_directories_hashtable,
                            GHashTable             **out_files_hashtable)
{
  autoinstall_flatpaks_files_name (commit,
                                   "autoinstall",
                                   flatpaks,
                                   n_flatpaks,
                                   out_directories_hashtable,
                                   out_files_hashtable);
}

static gboolean
autoinstall_flatpaks_files_override_name (GFile                   *updater_directory,
                                          const gchar             *filename,
                                          const FlatpakToInstall  *flatpaks,
                                          gsize                    n_flatpaks,
                                          GError                 **error)
{
  g_autofree gchar *autoinstall_flatpaks_contents = flatpaks_to_install_to_string (flatpaks, n_flatpaks);
  g_autofree gchar *updater_directory_path = g_file_get_path (updater_directory);
  g_autofree gchar *override_autoinstall_path = g_build_filename (updater_directory_path, "flatpak-autoinstall-override", filename, NULL);
  g_autoptr(GFile) override_autoinstall_file = g_file_new_for_path (override_autoinstall_path);
  g_autoptr(GFile) override_autoinstall_file_parent = g_file_get_parent (override_autoinstall_file);
  g_autoptr(GError) local_error = NULL;

  if (!g_file_make_directory_with_parents (override_autoinstall_file_parent, NULL, &local_error))
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
    }

  if (!g_file_set_contents (override_autoinstall_path,
                            autoinstall_flatpaks_contents,
                            -1,
                            error))
    return FALSE;

  return TRUE;
}

static gboolean
autoinstall_flatpaks_files_override (GFile                   *updater_directory,
                                     const FlatpakToInstall  *flatpaks,
                                     gsize                    n_flatpaks,
                                     GError                 **error)
{
  return autoinstall_flatpaks_files_override_name (updater_directory,
                                                   "install.override",
                                                   flatpaks,
                                                   n_flatpaks,
                                                   error);
}

static GStrv
drop_empty_lines (const gchar  *paragraph,
                  GError      **error)
{
  g_auto(GStrv) lines = g_strsplit (paragraph, "\n", -1);
  g_auto(GStrv) constructed_lines = g_new0 (gchar *, g_strv_length (lines));
  guint i = 0;

  for (const gchar **iter = (const gchar **) lines; *iter; ++iter)
    {
      if (g_strcmp0 (*iter, "") == 0)
        continue;

      constructed_lines[i++] = g_strdup (*iter);
    }

  return g_steal_pointer (&constructed_lines);
}

static GStrv
parse_ostree_refs_for_flatpaks (const gchar  *ostree_refs_stdout,
                                GError      **error)
{
  g_auto(GStrv) ostree_refs_stdout_lines = g_strsplit (ostree_refs_stdout, "\n", -1);
  g_auto(GStrv) parsed_out_flatpak_refs = g_new0 (gchar *, g_strv_length (ostree_refs_stdout_lines));
  GStrv ostree_refs_stdout_lines_iter = ostree_refs_stdout_lines;
  g_autoptr(GRegex) flatpak_refs_parser = g_regex_new (".*:.*?\\/(.*?)\\/.*", 0, 0, error);
  guint i = 0;

  if (!flatpak_refs_parser)
    return NULL;

  for (; *ostree_refs_stdout_lines_iter; ++ostree_refs_stdout_lines_iter)
    {
      g_autoptr(GMatchInfo) match_info = NULL;
      gchar *matched_flatpak_name = NULL;

      if (g_strcmp0 (*ostree_refs_stdout_lines_iter, "") == 0)
        continue;

      /* If the regex does not match this is probably a refspec for some
       * other ostree internal ref such as the metadata. Ignore it */
      if (!g_regex_match (flatpak_refs_parser,
                          *ostree_refs_stdout_lines_iter,
                          0,
                          &match_info))
        continue;

      matched_flatpak_name = g_match_info_fetch (match_info, 1);

      if (!matched_flatpak_name)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to parse output of OSTree refs: %s", ostree_refs_stdout);
          return FALSE;
        }

      parsed_out_flatpak_refs[i++] = g_steal_pointer (&matched_flatpak_name);
    }

  return g_steal_pointer (&parsed_out_flatpak_refs);
}

static gchar *
parse_ostree_checksum_from_stdout (const gchar  *ostree_show_stdout,
                                   GError      **error)
{
  gchar *first_newline = strchr (ostree_show_stdout, '\n');
  g_autoptr(GRegex) checksum_parser = g_regex_new ("commit (.*)", 0, 0, error);
  g_autoptr(GMatchInfo) match_info = NULL;
  g_autofree gchar *matched_checksum = NULL;

  if (!checksum_parser)
    return NULL;

  /* Only care about the first line */
  if (first_newline)
    *first_newline = '\0';

  if (!g_regex_match (checksum_parser,
                      ostree_show_stdout,
                      0,
                      &match_info))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to parse output of OSTree commit: %s", ostree_show_stdout);
      return FALSE;
    }

  matched_checksum = g_match_info_fetch (match_info, 1);

  if (!matched_checksum)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to parse output of OSTree commit: %s", ostree_show_stdout);
      return FALSE;
    }

  return g_steal_pointer (&matched_checksum);
}

static const gchar *
find_matching_ref_for_listed_refs (GStrv         all_refs_in_repo,
                                   const gchar  *partial_refspec,
                                   GError      **error)
{
  GStrv iter;
  gsize partial_len = strlen (partial_refspec);

  for (iter = all_refs_in_repo; *iter; ++iter)
    if (strncmp (*iter, partial_refspec, partial_len) == 0)
      return *iter;

  g_set_error (error,
               G_IO_ERROR,
               G_IO_ERROR_FAILED,
               "Couldn't find matching refspec for %s",
               partial_refspec);

  return NULL;
}

static gchar *
get_checksum_for_flatpak_in_installation_dir (GFile        *flatpak_installation_dir,
                                              const gchar  *partial_refspec,
                                              GError      **error)
{
  g_auto(CmdResult) refs_cmd = CMD_RESULT_CLEARED;
  g_auto(CmdResult) show_cmd = CMD_RESULT_CLEARED;
  g_autoptr(GFile) flatpak_repo = g_file_get_child (flatpak_installation_dir, "repo");
  g_auto(GStrv) all_refs_in_repo = NULL;
  const gchar *matching_refspec = NULL;

  if (!ostree_list_refs_in_repo (flatpak_repo, &refs_cmd, error))
    return FALSE;

  if (!cmd_result_ensure_ok (&refs_cmd, error))
    return FALSE;

  all_refs_in_repo = g_strsplit (refs_cmd.standard_output, "\n", -1);
  matching_refspec = find_matching_ref_for_listed_refs (all_refs_in_repo,
                                                        partial_refspec,
                                                        error);

  if (matching_refspec == NULL)
    return FALSE;

  if (!ostree_show (flatpak_repo,
                    matching_refspec,
                    &show_cmd,
                    error))
    return FALSE;

  if (!cmd_result_ensure_ok (&show_cmd, error))
    return FALSE;

  return parse_ostree_checksum_from_stdout (show_cmd.standard_output, error);
}

/* Inspect the underlying OSTree repo for flatpak refs that are
 * in the repository but not necessarily installed. We regex out the names
 * of the flatpaks and return them. */
static GStrv
flatpaks_in_installation_repo (GFile   *flatpak_installation_dir,
                               GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GFile) flatpak_repo = g_file_get_child (flatpak_installation_dir, "repo");

  if (!ostree_list_refs_in_repo (flatpak_repo, &cmd, error))
    return FALSE;

  return parse_ostree_refs_for_flatpaks (cmd.standard_output, error);
}

static GStrv
ostree_refspecs_in_installation_repo (GFile   *flatpak_installation_dir,
                                      GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GFile) flatpak_repo = g_file_get_child (flatpak_installation_dir, "repo");

  if (!ostree_list_refs_in_repo (flatpak_repo, &cmd, error))
    return FALSE;

  return drop_empty_lines (cmd.standard_output, error);
}

static gchar *
concat_refspec (const gchar *remote_name, const gchar *ref)
{
  return g_strjoin (":", remote_name, ref, NULL);
}

static gchar *
get_checksum_for_deploy_repo_dir (GFile        *deployment_repo_dir,
                                  const gchar  *refspec,
                                  GError      **error)
{
  g_autoptr(OstreeRepo) repo = ostree_repo_new (deployment_repo_dir);
  g_autoptr(GHashTable) refs = NULL;
  const gchar *ret_checksum = NULL;

  if (!ostree_repo_open (repo, NULL, error))
    return NULL;

  if (!ostree_repo_list_refs (repo, NULL, &refs, NULL, error))
    return NULL;

  ret_checksum = g_hash_table_lookup (refs, refspec);

  if (!ret_checksum)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to lookup ref %s", refspec);
      return NULL;
    }

  return g_strdup (ret_checksum);
}

/* Insert an empty list of flatpaks to automatically install on the commit
 * and ensure that the update still succeeds */
static void
test_update_install_no_flatpaks (EosUpdaterFixture *fixture,
                                 gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
  };
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install no flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);
}

/* Insert a list of flatpaks to automatically install on the commit
 * and ensure that they are pulled into the local repo once the
 * system update has completed. */
static void
test_update_install_flatpaks_in_repo (EosUpdaterFixture *fixture,
                                      gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * and ensure that they are pulled into the local repo once the
 * system update has completed, using a branch name other than "stable" */
static void
test_update_install_flatpaks_custom_branch_name (EosUpdaterFixture *fixture,
                                                 gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "custom_branch", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "custom_branch",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit, this
 * time with a collection-id specified, but no collection-id is
 * configured in the ostree config. The pulling of refs should continue
 * from the remote name as a fallback. */
static void
test_update_install_flatpaks_in_repo_fallback_if_collection_not_in_repo_config (EosUpdaterFixture *fixture,
                                                                                gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T20812");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection", /* repo config */
                                      NULL, /* remote config on local repo  */
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit, this
 * time with a collection-id specified, a collection-id is not configured
 * on either the repo or in the remote config. Fall back to using the remote
 * name, as the collection-id could not be looked up. */
static void
test_update_install_flatpaks_in_repo_fallback_if_collection_not_in_remote_or_repo (EosUpdaterFixture *fixture,
                                                                                   gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T20812");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      NULL, /* repo collection-id */
                                      NULL, /* remote config */
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit, specifying
 * remote name instead of a collection-id, which is considered an error. Nothing
 * should happen. */
static void
test_update_install_flatpaks_in_repo_error_using_remote_name (EosUpdaterFixture *fixture,
                                                              gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", NULL, "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               NULL);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Done with update, reap updater server */
  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit, specifying
 * no branch name. This is an error and nothing should happen */
static void
test_update_install_flatpaks_in_repo_error_no_branch_name (EosUpdaterFixture *fixture,
                                                           gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", NULL, "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               NULL);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Done with update, reap updater server */
  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit, this
 * time with a collection-id specified, a collection-id is configured
 * on the remote config, but the collection-id is not set up on the remote
 * end. This is an invalid configuration and should fail. */
static void
test_update_install_flatpaks_in_repo_error_if_collection_invalid (EosUpdaterFixture *fixture,
                                                                  gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T20812");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      NULL, /* repo collection-id */
                                      "com.endlessm.TestInstallFlatpaksCollection", /* remote config */
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);

  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               NULL);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Done with update, reap updater server */
  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit, specifying
 * neither a remote name or a collection-id. This should be treated as an
 * error and the deployment aborted */
static void
test_update_install_flatpaks_no_location_error (EosUpdaterFixture *fixture,
                                                gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", NULL, NULL, "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               NULL);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Done with update, reap updater server */
  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit, specifying
 * both a collection ID and a remote name, though the remote name should
 * should differ to the remote that the collection ID would resolve to. It should
 * not succeed and flatpaks should not be installed.
 */
static void
test_update_install_flatpaks_conflicting_location_error (EosUpdaterFixture *fixture,
                                                         gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "other-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               NULL);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Done with update, reap updater server */
  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  /* Should have been an error on the autoupdater, since the update would
   * have failed. */
  g_assert_false (g_spawn_check_exit_status (autoupdater->cmd->exit_status, NULL));

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * with one of the flatpaks using a runtime that we do not have installed.
 * Ensure that when we only specify the flatpak itself that the runtime
 * is also pulled automatically */
static void
test_update_install_flatpaks_in_repo_also_pull_runtimes (EosUpdaterFixture *fixture,
                                                         gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_autoptr(GPtrArray) flatpak_install_infos = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Set up a runtime and an app, neither of which should be installed by
   * default */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo",
                                             FALSE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_APP,
                                             "org.test.Test",
                                             "stable",
                                             "org.test.Runtime",
                                             "stable",
                                             "test-repo",
                                             FALSE));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our runtime was pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo,
                                  "org.test.Runtime"));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * with one of the flatpaks using a runtime that we do not have installed.
 * That runtime will only be available in a different remote.
 * Ensure that when we only specify the flatpak itself that the runtime
 * is also pulled automatically */
static void
test_update_install_flatpaks_in_repo_also_pull_runtimes_different_remote (EosUpdaterFixture *fixture,
                                                                         gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_autoptr(GPtrArray) flatpak_install_infos = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Set up a runtime and an app, neither of which should be installed by
   * default */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "other-repo",
                                             FALSE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_APP,
                                             "org.test.Test",
                                             "stable",
                                             "org.test.Runtime",
                                             "stable",
                                             "test-repo",
                                             FALSE));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));

  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("other-repo"),
                       flatpak_repo_info_new ("other-repo",
                                              "com.endlessm.TestInstallOtherFlatpaksCollection",
                                              "com.endlessm.TestInstallOtherFlatpaksCollection"));

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our runtime was pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo,
                                  "org.test.Runtime"));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * with one of the flatpaks having an extension marked autodownload that
 * is not already pulled. After pulled the flatpak, the autodownload
 * extension should also have been pulled. */
static void
test_update_install_flatpaks_in_repo_also_pull_autodownload_extension (EosUpdaterFixture *fixture,
                                                                       gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_autoptr(GPtrArray) app_extension_points = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_extension_point_info_free);
  g_autoptr(GPtrArray) flatpak_install_infos = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Set up extension point for the app */
  g_ptr_array_add (app_extension_points,
                   flatpak_extension_point_info_new_single_version ("org.test.Test.Extension",
                                                                    "extension_point_directory",
                                                                    "stable",
                                                                    FLATPAK_EXTENSION_POINT_NONE));

  /* Set up a runtime, an app and an extension, of which the extension should
   * be installed by default. */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo",
                                             TRUE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_APP,
                                                                 "org.test.Test",
                                                                 "stable",
                                                                 "org.test.Runtime",
                                                                 "stable",
                                                                 "test-repo",
                                                                 FALSE,
                                                                 NULL,
                                                                 app_extension_points));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_EXTENSION,
                                                                 "org.test.Test.Extension",
                                                                 "stable",
                                                                 NULL,
                                                                 NULL,
                                                                 "test-repo",
                                                                 FALSE,
                                                                 "app/org.test.Test/arch/stable",
                                                                 NULL));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our runtime was pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo,
                                  "org.test.Test.Extension"));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * with one of the flatpaks having an extension marked autodownload that
 * is not already pulled. The extension is in a different remote.
 * Flatpak should not trust that extension and it will not be auto downloaded. */
static void
test_update_install_flatpaks_in_repo_dont_also_pull_autodownload_extension_different_remote (EosUpdaterFixture *fixture,
                                                                                             gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_autoptr(GPtrArray) app_extension_points = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_extension_point_info_free);
  g_autoptr(GPtrArray) flatpak_install_infos = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Set up extension point for the app */
  g_ptr_array_add (app_extension_points,
                   flatpak_extension_point_info_new_single_version ("org.test.Test.Extension",
                                                                    "extension_point_directory",
                                                                    "stable",
                                                                    FLATPAK_EXTENSION_POINT_NONE));

  /* Set up a runtime, an app and an extension, of which the extension should
   * be installed by default. */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo",
                                             TRUE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_APP,
                                                                 "org.test.Test",
                                                                 "stable",
                                                                 "org.test.Runtime",
                                                                 "stable",
                                                                 "test-repo",
                                                                 FALSE,
                                                                 NULL,
                                                                 app_extension_points));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_EXTENSION,
                                                                 "org.test.Test.Extension",
                                                                 "stable",
                                                                 NULL,
                                                                 NULL,
                                                                 "other-repo",
                                                                 FALSE,
                                                                 "app/org.test.Test/arch/stable",
                                                                 NULL));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("other-repo"),
                       flatpak_repo_info_new ("other-repo",
                                              "com.endlessm.TestInstallOtherFlatpaksCollection",
                                              "com.endlessm.TestInstallOtherFlatpaksCollection"));

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our runtime was pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo,
                                   "org.test.Test.Extension"));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * with one of the flatpaks having a runtime that was not also installed.
 * However, the runtime is in a repository that does not have a collection-id
 * set. The runtime should not be pulled along with the flatpak. */
static void
test_update_install_flatpaks_in_repo_dont_also_pull_dep_no_collection_id (EosUpdaterFixture *fixture,
                                                                          gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_autoptr(GPtrArray) flatpak_install_infos = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Set up a runtime and an app, neither of which should be installed by
   * default */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo-no-collection-id",
                                             FALSE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_APP,
                                             "org.test.Test",
                                             "stable",
                                             "org.test.Runtime",
                                             "stable",
                                             "test-repo",
                                             FALSE));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo-no-collection-id"),
                       flatpak_repo_info_new ("test-repo-no-collection-id",
                                              NULL,
                                              NULL));

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our runtime was pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo,
                                  "org.test.Test"));
  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo,
                                   "org.test.Runtime"));
}

/* Install a flatpak in the user repository without the use of the
 * updater's installer code. Then add an action to update the flatpak
 * on a new commit. The flatpak should be updated. */
static void
test_update_flatpaks_updated_in_repo (EosUpdaterFixture *fixture,
                                      gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "update", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_path = NULL;
  g_autoptr(GFile) flatpak_build_dir = NULL;
  g_autofree gchar *flatpak_apps_dir_path = NULL;
  g_autoptr(GFile) flatpak_apps_dir = NULL;
  g_autoptr(GFile) flatpak_repos_dir = NULL;
  g_autoptr(GFile) flatpak_repo_dir = NULL;
  g_autofree gchar *flatpak_repo_path = NULL;
  g_autoptr(GFile) app_dir = NULL;
  g_autofree gchar *app_dir_path = NULL;
  g_autofree gchar *app_executable_path = NULL;
  g_autofree gchar *initially_installed_flatpak_checksum = NULL;
  g_autofree gchar *updated_flatpak_checksum = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_path = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_path,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);

  /* Set up the flatpak repo and also preinstall the apps */
  eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (updater_directory,
                                                             "stable",
                                                             "test-repo",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             (const gchar **) wanted_flatpaks,
                                                             (const gchar **) wanted_flatpaks,
                                                             gpg_key_file,
                                                             keyid,
                                                             &error);

  flatpak_build_dir = eos_test_get_flatpak_build_dir_for_updater_dir (updater_directory);
  flatpak_repos_dir = g_file_get_child (flatpak_build_dir, "repos");
  flatpak_repo_dir = g_file_get_child (flatpak_repos_dir, "test-repo");
  flatpak_repo_path = g_file_get_path (flatpak_repo_dir);
  flatpak_apps_dir = g_file_get_child (flatpak_build_dir, "apps");
  flatpak_apps_dir_path = g_file_get_path (flatpak_apps_dir);
  app_dir_path = g_build_filename (flatpak_apps_dir_path,
                                   "test-repo",
                                   flatpaks_to_install[0].app_id,
                                   "stable",
                                   NULL);
  app_executable_path = g_build_filename (app_dir_path,
                                          "files",
                                          "bin",
                                          "test",
                                          NULL);
  app_dir = g_file_new_for_path (app_dir_path);

  /* Get checksum for first installed flatpak */
  initially_installed_flatpak_checksum =
    get_checksum_for_flatpak_in_installation_dir (flatpak_user_installation_dir,
                                                  "test-repo:app/org.test.Test",
                                                  &error);

  g_assert_no_error (error);

  /* Slightly different contents so that the checksum will change */
  g_file_set_contents (app_executable_path, "#!/bin/bash\nexit 1\n", -1, &error);
  g_assert_no_error (error);

  flatpak_build_export (updater_directory,
                        app_dir_path,
                        flatpak_repo_path,
                        "stable",
                        fixture->gpg_home,
                        keyid,
                        &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1). */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1) */
  etc_update_client (data);

  updated_flatpak_checksum =
    get_checksum_for_flatpak_in_installation_dir (flatpak_user_installation_dir,
                                                  "test-repo:app/org.test.Test",
                                                  &error);

  g_assert_cmpstr (initially_installed_flatpak_checksum, !=, updated_flatpak_checksum);
}

/* Insert a list of flatpaks to automatically install on the first commit,
 * and run the flatpak installer,  then on the second commit, update the
 * flatpak to the newest revision. The checksum for the flatpak pulled into the
 * repo should differ on the second commit */
static void
test_update_flatpaks_updated_in_repo_after_install (EosUpdaterFixture *fixture,
                                                    gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  FlatpakToInstall flatpaks_to_install_on_second_commit[] = {
    /* List starts with @flatpaks_to_install due to being append-only. */
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
    { "update", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_path = NULL;
  g_autoptr(GFile) flatpak_build_dir = NULL;
  g_autoptr(GFile) flatpak_apps_dir = NULL;
  g_autofree gchar *flatpak_apps_dir_path = NULL;
  g_autoptr(GFile) flatpak_repos_dir = NULL;
  g_autoptr(GFile) flatpak_repo_dir = NULL;
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autofree gchar *flatpak_repo_path = NULL;
  g_autoptr(GFile) app_dir = NULL;
  g_autofree gchar *app_dir_path = NULL;
  g_autofree gchar *app_executable_path = NULL;
  g_autofree gchar *initially_installed_flatpak_checksum = NULL;
  g_autofree gchar *updated_flatpak_checksum = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 2 will update those flatpaks
   */
  autoinstall_flatpaks_files (2,
                              flatpaks_to_install_on_second_commit,
                              G_N_ELEMENTS (flatpaks_to_install_on_second_commit),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_path = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_path,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);

  /* Get checksum for first installed flatpak */
  initially_installed_flatpak_checksum =
    get_checksum_for_flatpak_in_installation_dir (flatpak_user_installation_dir,
                                                  "test-repo:app/org.test.Test",
                                                  &error);

  g_assert_no_error (error);

  flatpak_build_dir = eos_test_get_flatpak_build_dir_for_updater_dir (updater_directory);
  flatpak_repos_dir = g_file_get_child (flatpak_build_dir, "repos");
  flatpak_repo_dir = g_file_get_child (flatpak_repos_dir, "test-repo");
  flatpak_repo_path = g_file_get_path (flatpak_repo_dir);
  flatpak_apps_dir = g_file_get_child (flatpak_build_dir, "apps");
  flatpak_apps_dir_path = g_file_get_path (flatpak_apps_dir);
  app_dir_path = g_build_filename (flatpak_apps_dir_path,
                                   "test-repo",
                                   flatpaks_to_install[0].app_id,
                                   "stable",
                                   NULL);
  app_executable_path = g_build_filename (app_dir_path,
                                          "files",
                                          "bin",
                                          "test",
                                          NULL);
  app_dir = g_file_new_for_path (app_dir_path);

  /* Slightly different contents so that the checksum will change */
  g_file_set_contents (app_executable_path, "#!/bin/bash\nexit 1\n", -1, &error);
  g_assert_no_error (error);

  flatpak_build_export (updater_directory,
                        app_dir_path,
                        flatpak_repo_path,
                        "stable",
                        fixture->gpg_home,
                        keyid,
                        &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (2). */
  etc_update_server (data, 2);
  /* Update the client, so it also has a new commit (2) */
  etc_update_client (data);

  updated_flatpak_checksum =
    get_checksum_for_flatpak_in_installation_dir (flatpak_user_installation_dir,
                                                  "test-repo:app/org.test.Test",
                                                  &error);

  g_assert_cmpstr (initially_installed_flatpak_checksum, !=, updated_flatpak_checksum);
}

/* Insert a list of flatpaks to automatically install on the first commit,
 * but don't run the flatpak installer, and keep the autoinstall list constant
 * between commits. The checksum for the flatpak pulled into the
 * repo should differ on the second commit. The reason for this is that
 * we would have never updated the state file if we only fetched last
 * time, so the actions would have been "prepared" again. */
static void
test_update_flatpaks_updated_in_repo_on_subsequent_fetch (EosUpdaterFixture *fixture,
                                                          gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_path = NULL;
  g_autoptr(GFile) flatpak_build_dir = NULL;
  g_autoptr(GFile) flatpak_apps_dir = NULL;
  g_autofree gchar *flatpak_apps_dir_path = NULL;
  g_autoptr(GFile) flatpak_repos_dir = NULL;
  g_autoptr(GFile) flatpak_repo_dir = NULL;
  g_autofree gchar *flatpak_repo_path = NULL;
  g_autoptr(GFile) app_dir = NULL;
  g_autofree gchar *app_dir_path = NULL;
  g_autofree gchar *app_executable_path = NULL;
  g_autofree gchar *initially_installed_flatpak_checksum = NULL;
  g_autofree gchar *updated_flatpak_checksum = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 2 will use the same list
   */
  autoinstall_flatpaks_files (2,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_path = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_path,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Get checksum for first installed flatpak */
  initially_installed_flatpak_checksum =
    get_checksum_for_flatpak_in_installation_dir (flatpak_user_installation_dir,
                                                  "test-repo:app/org.test.Test",
                                                  &error);

  g_assert_no_error (error);

  flatpak_build_dir = eos_test_get_flatpak_build_dir_for_updater_dir (updater_directory);
  flatpak_repos_dir = g_file_get_child (flatpak_build_dir, "repos");
  flatpak_repo_dir = g_file_get_child (flatpak_repos_dir, "test-repo");
  flatpak_repo_path = g_file_get_path (flatpak_repo_dir);
  flatpak_apps_dir = g_file_get_child (flatpak_build_dir, "apps");
  flatpak_apps_dir_path = g_file_get_path (flatpak_apps_dir);
  app_dir_path = g_build_filename (flatpak_apps_dir_path,
                                   "test-repo",
                                   flatpaks_to_install[0].app_id,
                                   "stable",
                                   NULL);
  app_executable_path = g_build_filename (app_dir_path,
                                          "files",
                                          "bin",
                                          "test",
                                          NULL);
  app_dir = g_file_new_for_path (app_dir_path);

  /* Slightly different contents so that the checksum will change */
  g_file_set_contents (app_executable_path, "#!/bin/bash\nexit 1\n", -1, &error);
  g_assert_no_error (error);

  flatpak_build_export (updater_directory,
                        app_dir_path,
                        flatpak_repo_path,
                        "stable",
                        fixture->gpg_home,
                        keyid,
                        &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (2). */
  etc_update_server (data, 2);
  /* Update the client, so it also has a new commit (2) */
  etc_update_client (data);

  updated_flatpak_checksum =
    get_checksum_for_flatpak_in_installation_dir (flatpak_user_installation_dir,
                                                  "test-repo:app/org.test.Test",
                                                  &error);

  g_assert_cmpstr (initially_installed_flatpak_checksum, !=, updated_flatpak_checksum);
}

/* Insert a list of flatpaks to automatically install on the commit
 * but mark them as skipped for "arch" (the override architecture) such that
 * they will not be pulled into the repo */
static void
test_update_skip_install_flatpaks_on_architecture (EosUpdaterFixture *fixture,
                                                   gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    /* Indicate that we should skip the testing architecture */
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_SKIP_TESTING_ARCHITECTURE }
  };
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) undesired_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                       G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *to_export_flatpaks_contents = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * but mark them as only for "differentarch" (not the override architecture) such that
 * they will not be pulled into the repo */
static void
test_update_only_install_flatpaks_on_architecture (EosUpdaterFixture *fixture,
                                                   gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    /* Indicate that we should skip the testing architecture */
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_ONLY_NOT_TESTING_ARCHITECTURE }
  };
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) undesired_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                       G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * but mark them as skipped for "locale" (the override locale) such that
 * they will not be pulled into the repo */
static void
test_update_skip_install_flatpaks_on_locale (EosUpdaterFixture *fixture,
                                             gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    /* Indicate that we should skip the testing architecture */
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_SKIP_TESTING_LOCALE }
  };
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) undesired_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                       G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * but mark them as only for "differentlocale" (not the override architecture) such that
 * they will not be pulled into the repo */
static void
test_update_only_install_flatpaks_on_locale (EosUpdaterFixture *fixture,
                                             gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    /* Indicate that we should skip the testing architecture */
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_ONLY_NOT_TESTING_LOCALE }
  };
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) undesired_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                       G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Assert that our flatpaks were not pulled into the local repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Have flatpaks that are pending deployment but induce a failure in
 * the sysroot deployment. It should be the case that the flatpak refs
 * stay on the local system repo. */
static void
test_update_deploy_fail_flatpaks_stay_in_repo (EosUpdaterFixture *fixture,
                                              gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) flatpaks_in_repo = NULL;
  g_autofree gchar *remote_repo_directory_relative_path = g_build_filename ("main",
                                                                            "served",
                                                                            default_ostree_path,
                                                                            NULL);
  g_autoptr(GFile) remote_repo_directory = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *expected_directory_relative_path = NULL;
  g_autoptr(GFile) expected_directory = NULL;
  g_autoptr(GFile) expected_directory_child = NULL;
  g_autofree gchar *expected_directory_child_str = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *deployment_id = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);

  /* Before updating the client, write a directory to a location of one of the
   * files that ostree_sysroot_deploy_tree will want to write to. This relies on
   * implementation details of ostree_sysroot_deploy_tree, but essentially it
   * puts a nonempty directory where the origin file should be.
   *
   * ostree_sysroot_deploy_tree will call glnx_file_replace_contents_at
   * which will only replace the contents of the file if it is a file
   * or a nonempty directory and return an error otherwise.
   *
   * When the error occurs, the updater should catch it and revert the
   * operations done to pre-install flatpaks. */  
  remote_repo_directory = g_file_get_child (data->fixture->tmpdir,
                                            remote_repo_directory_relative_path);
  deployment_csum = get_checksum_for_deploy_repo_dir (remote_repo_directory,
                                                      default_ref,
                                                      &error);
  deployment_id = g_strjoin (".", deployment_csum, "0", "origin", NULL);

  expected_directory_relative_path = g_build_filename ("sysroot",
                                                       "ostree",
                                                       "deploy",
                                                       default_remote_name,
                                                       "deploy",
                                                       deployment_id,
                                                       NULL);
  expected_directory = g_file_get_child (data->client->root,
                                         expected_directory_relative_path);
  expected_directory_child = g_file_get_child (expected_directory, "child");

  g_file_make_directory_with_parents (expected_directory, NULL, &error);
  g_assert_no_error (error);

  expected_directory_child_str = g_file_get_path (expected_directory_child);
  g_file_set_contents (expected_directory_child_str, "", -1, &error);
  g_assert_no_error (error);

  /* Attempt to update client - run updater daemon */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               NULL);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Done with update, reap updater server */
  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  /* Should have been an error on the autoupdater, since the update would
   * have failed. */
  g_assert_false (g_spawn_check_exit_status (autoupdater->cmd->exit_status, NULL));

  /* Assert that our flatpaks are in the installation repo */
  flatpaks_in_repo = flatpaks_in_installation_repo (flatpak_user_installation_dir,
                                                    &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) flatpaks_in_repo, flatpaks_to_install[0].app_id));
}

/* Have flatpaks that are pending deployment but induce a failure in
 * the sysroot deployment. It should be the case that the flatpaks are
 * not deployed on reboot */
static void
test_update_deploy_fail_flatpaks_not_deployed (EosUpdaterFixture *fixture,
                                              gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *remote_repo_directory_relative_path = g_build_filename ("main",
                                                                            "served",
                                                                            default_ostree_path,
                                                                            NULL);
  g_autoptr(GFile) remote_repo_directory = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *expected_directory_relative_path = NULL;
  g_autoptr(GFile) expected_directory = NULL;
  g_autoptr(GFile) expected_directory_child = NULL;
  g_autofree gchar *expected_directory_child_str = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *anticipated_deployment_csum = NULL;
  g_autofree gchar *deployment_id = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);

  /* Before updating the client, write a directory to a location of one of the
   * files that ostree_sysroot_deploy_tree will want to write to. This relies on
   * implementation details of ostree_sysroot_deploy_tree, but essentially it
   * puts a nonempty directory where the origin file should be.
   *
   * ostree_sysroot_deploy_tree will call glnx_file_replace_contents_at
   * which will only replace the contents of the file if it is a file
   * or a nonempty directory and return an error otherwise.
   *
   * When the error occurs, the updater should catch it and revert the
   * operations done to pre-install flatpaks. */
  remote_repo_directory = g_file_get_child (data->fixture->tmpdir,
                                            remote_repo_directory_relative_path);
  anticipated_deployment_csum = get_checksum_for_deploy_repo_dir (remote_repo_directory,
                                                                  default_ref,
                                                                  &error);
  deployment_id = g_strjoin (".", anticipated_deployment_csum, "0", "origin", NULL);

  expected_directory_relative_path = g_build_filename ("sysroot",
                                                       "ostree",
                                                       "deploy",
                                                       default_remote_name,
                                                       "deploy",
                                                       deployment_id,
                                                       NULL);
  expected_directory = g_file_get_child (data->client->root,
                                         expected_directory_relative_path);
  expected_directory_child = g_file_get_child (expected_directory, "child");

  g_file_make_directory_with_parents (expected_directory, NULL, &error);
  g_assert_no_error (error);

  expected_directory_child_str = g_file_get_path (expected_directory_child);
  g_file_set_contents (expected_directory_child_str, "", -1, &error);
  g_assert_no_error (error);

  /* Attempt to update client - run updater daemon */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               NULL);
  g_assert_no_error (error);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Done with update, reap updater server */
  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was not installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
}

/* Have flatpaks that are pending deployment but induce a failure in
 * the flatpak pulling. The new OSTree should not be deployed. */
static void
test_update_flatpak_pull_fail_system_not_deployed (EosUpdaterFixture *fixture,
                                                   gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_autofree gchar *flatpak_remote_path = NULL;
  g_autoptr(GFile) flatpak_remote_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *expected_directory_relative_path = NULL;
  g_autoptr(GFile) expected_directory = NULL;
  g_autoptr(GFile) expected_directory_child = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(GStrv) initial_deployment_ids = NULL;
  g_autofree gchar *deployment_id = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_auto(GStrv) after_update_deployment_ids = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  eos_test_client_get_deployments (data->client,
                                   default_remote_name,
                                   &initial_deployment_ids,
                                   &error);
  g_assert_no_error (error);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_remote_path = g_build_filename (updater_directory_str,
                                          "flatpak",
                                          NULL);
  flatpak_remote_dir = g_file_new_for_path (flatpak_remote_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);

  /* Before updating the client, nuke the flatpak remote directory. This
   * will make the pull operation fail, which should make the entire
   * deployment fail */
  rm_rf (flatpak_remote_dir, &error);
  g_assert_no_error (error);

  /* Attempt to update client - run updater daemon */
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               &error);
  g_assert_no_error (error);

  /* Trigger update */
  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  /* Update should have failed */
  g_assert_false (g_spawn_check_exit_status (autoupdater->cmd->exit_status, NULL));

  /* Assert that the deployment checksum is the same as earlier */
  eos_test_client_get_deployments (data->client,
                                   default_remote_name,
                                   &after_update_deployment_ids,
                                   &error);
  g_assert_no_error (error);

  g_assert_cmpstr (initial_deployment_ids[0], ==, after_update_deployment_ids[0]);
}


/* Insert a list of flatpaks to automatically install on the commit
 * and ensure that they are not installed before reboot */
static void
test_update_install_flatpaks_not_deployed (EosUpdaterFixture *fixture,
                                           gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Get the currently deployed flatpaks and ensure we are not one of them */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpaks to install and
 * install them from the local repo into the installation. Verify that
 * the flatpaks are installed and deployed once this has completed. */
static void
test_update_deploy_flatpaks_on_reboot (EosUpdaterFixture *fixture,
                                       gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * including a flatpak that has a dependency on a runtime that is not
 * yet installed. Pull the update and then simulate a reboot by running
 * eos-updater-flatpak-installer installer. Both the incoming package
 * and the dependency should be installed. */
static void
test_update_deploy_dependency_runtime_flatpaks_on_reboot (EosUpdaterFixture *fixture,
                                                          gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autoptr(GPtrArray) flatpak_install_infos =
    g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Set up a runtime and an app, neither of which should be installed by
   * default */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo",
                                             FALSE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_APP,
                                             "org.test.Test",
                                             "stable",
                                             "org.test.Runtime",
                                             "stable",
                                             "test-repo",
                                             FALSE));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our runtime was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, "org.test.Runtime"));
}

/* See `man 8 eos-updater-flatpak-installer`. */
#define EUFI_EXIT_CODE_APPLY_FAILED 4

/* Insert a list of flatpaks to automatically install on the commit,
 * including a flatpak that has a dependency on a runtime that is not
 * yet installed. Pull the update and then simulate a reboot by running
 * eos-updater-flatpak-installer installer. Installing the app
 * will fail due to a file being in the way. The counter should not
 * be written to even though the dependency got installed. */
static void
test_update_deploy_dependency_runtime_fail_no_update_counter (EosUpdaterFixture *fixture,
                                                              gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autoptr(GPtrArray) flatpak_install_infos =
    g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autoptr(GFile) updater_state_directory = NULL;
  g_autoptr(GFile) updater_state_file = NULL;
  g_autofree gchar *updater_state_file_path = NULL;
  const gchar *test_broken_flatpak_relative_path = "app/org.test.Test/current/active";
  const gchar *app_executable_relative_path = "files/bin/test";
  g_autofree gchar *app_executable_path = NULL;
  g_autoptr(GFile) installed_app_executable = NULL;
  g_autofree gchar *installed_app_executable_path = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Set up a runtime and an app, neither of which should be installed by
   * default */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo",
                                             FALSE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_APP,
                                             "org.test.Test",
                                             "stable",
                                             "org.test.Runtime",
                                             "stable",
                                             "test-repo",
                                             FALSE));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_state_directory = get_flatpak_upgrade_state_dir_for_updater_dir (updater_directory);
  updater_state_file = g_file_get_child (updater_state_directory, "flatpak-autoinstall.progress");
  updater_state_file_path = g_file_get_path (updater_state_file);
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Before rebooting and running the installer, put a directory
   * in place of where one of the deployed files needs to be */
  installed_app_executable_path = g_build_filename (flatpak_user_installation,
                                                    test_broken_flatpak_relative_path,
                                                    app_executable_relative_path,
                                                    NULL);
  installed_app_executable = g_file_new_for_path (installed_app_executable_path);

  g_file_make_directory_with_parents (installed_app_executable, NULL, &error);
  g_assert_no_error (error);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_error (error, G_SPAWN_EXIT_ERROR, EUFI_EXIT_CODE_APPLY_FAILED);
  g_clear_error (&error);

  /* Assert that our runtime was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, "org.test.Runtime"));

  /* Assert that the counter was not touched */
  g_assert_false (g_file_test (updater_state_file_path, G_FILE_TEST_EXISTS));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * including a flatpak that has an extension marked autodownload. Pull the update
 * and then simulate a reboot by running eos-updater-flatpak-installer installer.
 * Both the incoming package and the extension marked autodownload should be installed. */
static void
test_update_deploy_dependency_autodownload_extension_flatpaks_on_reboot (EosUpdaterFixture *fixture,
                                                                         gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autoptr(GPtrArray) app_extension_points =
    g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_extension_point_info_free);
  g_autoptr(GPtrArray) flatpak_install_infos =
    g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Set up extension point for the app */
  g_ptr_array_add (app_extension_points,
                   flatpak_extension_point_info_new_single_version ("org.test.Test.Extension",
                                                                    "extension_point_directory",
                                                                    "stable",
                                                                    FLATPAK_EXTENSION_POINT_NONE));

  /* Set up a runtime, an app and an extension, of which the extension should
   * be installed by default. */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo",
                                             TRUE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_APP,
                                                                 "org.test.Test",
                                                                 "stable",
                                                                 "org.test.Runtime",
                                                                 "stable",
                                                                 "test-repo",
                                                                 FALSE,
                                                                 NULL,
                                                                 app_extension_points));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_EXTENSION,
                                                                 "org.test.Test.Extension",
                                                                 "stable",
                                                                 NULL,
                                                                 NULL,
                                                                 "test-repo",
                                                                 FALSE,
                                                                 "app/org.test.Test/arch/stable",
                                                                 NULL));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our runtime was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, "org.test.Test.Extension"));
}

/* Insert a list of flatpaks to automatically install on the commit,
 * including a flatpak that has an extension marked no-autodownload. Pull the update
 * and then simulate a reboot by running eos-updater-flatpak-installer installer.
 * The incoming package should be installed, but not its extension. */
static void
test_update_no_deploy_dependency_non_autodownload_extension_flatpaks_on_reboot (EosUpdaterFixture *fixture,
                                                                                gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autoptr(GPtrArray) app_extension_points =
    g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_extension_point_info_free);
  g_autoptr(GPtrArray) flatpak_install_infos =
    g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_install_info_free);
  g_autoptr(GHashTable) flatpak_repo_infos = g_hash_table_new_full (g_str_hash,
                                                                    g_str_equal,
                                                                    g_free,
                                                                    (GDestroyNotify) flatpak_repo_info_free);
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T22054");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Set up extension point for the app */
  g_ptr_array_add (app_extension_points,
                   flatpak_extension_point_info_new_single_version ("org.test.Test.Extension",
                                                                    "extension_point_directory",
                                                                    "stable",
                                                                    FLATPAK_EXTENSION_POINT_NO_AUTODOWNLOAD));

  /* Set up a runtime, an app and an extension, of which the extension should
   * be installed by default. */
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             "stable",
                                             NULL,
                                             NULL,
                                             "test-repo",
                                             TRUE));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_APP,
                                                                 "org.test.Test",
                                                                 "stable",
                                                                 "org.test.Runtime",
                                                                 "stable",
                                                                 "test-repo",
                                                                 FALSE,
                                                                 NULL,
                                                                 app_extension_points));
  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new_with_extension_info (FLATPAK_INSTALL_INFO_TYPE_EXTENSION,
                                                                 "org.test.Test.Extension",
                                                                 "stable",
                                                                 NULL,
                                                                 NULL,
                                                                 "test-repo",
                                                                 FALSE,
                                                                 "app/org.test.Test/arch/stable",
                                                                 NULL));
  g_hash_table_insert (flatpak_repo_infos,
                       g_strdup ("test-repo"),
                       flatpak_repo_info_new ("test-repo",
                                              "com.endlessm.TestInstallFlatpaksCollection",
                                              "com.endlessm.TestInstallFlatpaksCollection"));

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo (updater_directory,
                               flatpak_install_infos,
                               flatpak_repo_infos,
                               gpg_key_file,
                               keyid,
                               &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our runtime was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks, "org.test.Test.Extension"));
}

/* Insert a list of flatpaks to automatically install on the commit, but the
 * flatpak listed should already be installed. Run the updater, this should
 * have no effect and the flatpak should remain installed without errors. */
static void
test_update_no_op_flatpak_already_installed (EosUpdaterFixture *fixture,
                                             gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *installed_app_executable_contents_before_install = NULL;
  g_autofree gchar *installed_app_executable_contents_after_install = NULL;
  g_autofree gchar *installed_app_executable_path = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  const gchar *test_flatpak_relative_path = "app/org.test.Test/current/active";
  const gchar *app_executable_relative_path = "files/bin/test";
  g_autoptr(GError) error = NULL;

  g_test_bug ("T20881");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (updater_directory,
                                                             "stable",
                                                             "test-repo",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             (const gchar **) wanted_flatpaks,
                                                             (const gchar **) wanted_flatpaks,
                                                             gpg_key_file,
                                                             keyid,
                                                             &error);


  /* Get the contents of the executable before the update */
  installed_app_executable_path = g_build_filename (flatpak_user_installation,
                                                    test_flatpak_relative_path,
                                                    app_executable_relative_path,
                                                    NULL);
  g_file_get_contents (installed_app_executable_path,
                       &installed_app_executable_contents_before_install,
                       NULL,
                       &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that nothing changed in our flatpak */
  g_file_get_contents (installed_app_executable_path,
                       &installed_app_executable_contents_after_install,
                       NULL,
                       &error);
  g_assert_no_error (error);

  g_assert_cmpstr (installed_app_executable_contents_after_install,
                   ==,
                   installed_app_executable_contents_before_install);
}

/* Insert a list of flatpaks to automatically install on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpaks to install and
 * install them from the local repo into the installation.
 *
 * In this scenario, we attempt to install two flatpaks, but the second one
 * will fail to install due to a file being in the way. Verify that
 * an error was set from the flatpak installer and that the first one
 * is installed but the second one isn't. */
static void
test_update_deploy_flatpaks_on_reboot_partially_on_failure (EosUpdaterFixture *fixture,
                                                            gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test2", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  const gchar *test_broken_flatpak_relative_path = "app/org.test.Test2/current/active";
  const gchar *app_executable_relative_path = "files/bin/test";
  g_autofree gchar *app_executable_path = NULL;
  g_autofree gchar *installed_app_executable_path = NULL;
  g_autoptr(GFile) installed_app_executable = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Before rebooting and running the installer, put a directory
   * in place of where one of the deployed files needs to be */
  installed_app_executable_path = g_build_filename (flatpak_user_installation,
                                                    test_broken_flatpak_relative_path,
                                                    app_executable_relative_path,
                                                    NULL);
  installed_app_executable = g_file_new_for_path (installed_app_executable_path);

  g_file_make_directory_with_parents (installed_app_executable, NULL, &error);
  g_assert_no_error (error);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_error (error, G_SPAWN_EXIT_ERROR, EUFI_EXIT_CODE_APPLY_FAILED);
  g_clear_error (&error);

  /* Assert that our frist flatpak was installed, but the second
   * one was not */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[1].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpaks to install and
 * install them from the local repo into the installation.
 *
 * In this scenario, we attempt to install two flatpaks, but the second one
 * will fail to install due to a file being in the way. We then fix
 * the probelm by removing the file that's in the way and run
 * the installer again. Verify that although an error was set the first
 * time, the second time around the installer successfully completes
 * and both flatpaks are installed. */
static void
test_update_deploy_flatpaks_on_reboot_resume_on_failure_resolved (EosUpdaterFixture *fixture,
                                                                  gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test2", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  const gchar *test_broken_flatpak_relative_path = "app/org.test.Test2";
  const gchar *test_broken_flatpak_relative_contents_path = "app/org.test.Test2/current/active";
  const gchar *app_executable_relative_path = "files/bin/test";
  g_autofree gchar *app_executable_path = NULL;
  g_autofree gchar *installed_app_executable_path = NULL;
  g_autoptr(GFile) installed_app_executable = NULL;
  g_autoptr(GFile) failed_flatpak_installation_directory = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Before rebooting and running the installer, put a directory
   * in place of where one of the deployed files needs to be */
  installed_app_executable_path = g_build_filename (flatpak_user_installation,
                                                    test_broken_flatpak_relative_contents_path,
                                                    app_executable_relative_path,
                                                    NULL);
  installed_app_executable = g_file_new_for_path (installed_app_executable_path);

  g_file_make_directory_with_parents (installed_app_executable, NULL, &error);
  g_assert_no_error (error);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  /* Should not have worked */
  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_error (error, G_SPAWN_EXIT_ERROR, EUFI_EXIT_CODE_APPLY_FAILED);
  g_clear_error (&error);

  /* Remove the offending partial installation and try again */
  failed_flatpak_installation_directory = g_file_get_child (flatpak_user_installation_dir,
                                                            test_broken_flatpak_relative_path);
  rm_rf (failed_flatpak_installation_directory, &error);
  g_assert_no_error (error);

  /* Should now work again */
  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our frist flatpak was installed, but the second
   * one was not */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[1].app_id));
}

/* Insert a list of flatpaks to automatically uninstall on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpak actions and
 * uninstall them in the system installation */
static void
test_update_uninstall_flatpaks_on_reboot (EosUpdaterFixture *fixture,
                                          gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[][2] = {
    {
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    },
    {
      /* First element is identical to @flatpaks_to_install, since it’s an append-only list. */
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
      { "uninstall", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install[0], 1);
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *second_deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install[0],
                              1,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 2 will remove those flatpaks
   */
  autoinstall_flatpaks_files (2,
                              flatpaks_to_install[1],
                              2,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Update the server again with commit 2, then update the client. This will
   * pull in a new ref-action list with the uninstall action. */
  etc_update_server (data, 2);
  etc_update_client (data);

  /* Reboot again, run the flatapk installer over this new commit */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Flatpak should not be installed here */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks,
                                   flatpaks_to_install[1][1].app_id));
}

/* Insert a list of flatpaks to automatically uninstall on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpak actions and
 * uninstall them in the system installation. This uses a custom branch name */
static void
test_update_uninstall_flatpaks_on_reboot_custom_branch_name (EosUpdaterFixture *fixture,
                                                             gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[][2] = {
    {
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "custom_branch", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    },
    {
      /* First element is identical to @flatpaks_to_install, since it’s an append-only list. */
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "custom_branch", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
      { "uninstall", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "custom_branch", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install[0], 1);
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *second_deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install[0],
                              1,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 2 will remove those flatpaks
   */
  autoinstall_flatpaks_files (2,
                              flatpaks_to_install[1],
                              2,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "custom_branch",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Update the server again with commit 2, then update the client. This will
   * pull in a new ref-action list with the uninstall action. */
  etc_update_server (data, 2);
  etc_update_client (data);

  /* Reboot again, run the flatapk installer over this new commit */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Flatpak should not be installed here */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks,
                                   flatpaks_to_install[1][1].app_id));
}

/* Insert a list of flatpaks to automatically uninstall on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpak actions and
 * uninstall a flatpak with a different branch name on the system installation.
 * This should not result in our flatpak being uninstalled. */
static void
test_update_no_uninstall_flatpaks_on_reboot_different_branch_name (EosUpdaterFixture *fixture,
                                                                  gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[][2] = {
    {
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    },
    {
      /* First element is identical to @flatpaks_to_install, since it’s an append-only list. */
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
      { "uninstall", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "custom_branch", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install[0], 1);
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *second_deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install[0],
                              1,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 2 will remove those flatpaks
   */
  autoinstall_flatpaks_files (2,
                              flatpaks_to_install[1],
                              2,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Update the server again with commit 2, then update the client. This will
   * pull in a new ref-action list with the uninstall action. */
  etc_update_server (data, 2);
  etc_update_client (data);

  /* Reboot again, run the flatapk installer over this new commit */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Flatpak should be installed here */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks,
                                  flatpaks_to_install[1][1].app_id));
}

/* Insert a list of flatpaks to automatically update on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpaks to install, but
 * because the flatpaks are not already installed, it should have no effect. */
static void
test_update_flatpaks_no_op_if_not_installed (EosUpdaterFixture *fixture,
                                             gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "update", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was not installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically update on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpaks to install, but
 * because the flatpaks are already up to date installed, it should have no effect. */
static void
test_update_flatpaks_no_op_if_up_to_date (EosUpdaterFixture *fixture,
                                          gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "update", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *installed_app_executable_contents_before_update = NULL;
  g_autofree gchar *installed_app_executable_contents_after_update = NULL;
  g_autofree gchar *installed_app_executable_path = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  const gchar *test_flatpak_relative_path = "app/org.test.Test/current/active";
  const gchar *app_executable_relative_path = "files/bin/test";
  g_autoptr(GError) error = NULL;

  g_test_bug ("T20881");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (updater_directory,
                                                             "stable",
                                                             "test-repo",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             (const gchar **) wanted_flatpaks,
                                                             (const gchar **) wanted_flatpaks,
                                                             gpg_key_file,
                                                             keyid,
                                                             &error);

  /* Get the contents of the executable before the update */
  installed_app_executable_path = g_build_filename (flatpak_user_installation,
                                                    test_flatpak_relative_path,
                                                    app_executable_relative_path,
                                                    NULL);
  g_file_get_contents (installed_app_executable_path,
                       &installed_app_executable_contents_before_update,
                       NULL,
                       &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that nothing changed in our flatpak */
  g_file_get_contents (installed_app_executable_path,
                       &installed_app_executable_contents_after_update,
                       NULL,
                       &error);
  g_assert_no_error (error);

  g_assert_cmpstr (installed_app_executable_contents_after_update,
                   ==,
                   installed_app_executable_contents_before_update);
}

/* Insert a list of flatpaks to automatically update on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. Change
 * the contents of the flatpak in the remote repository and ensure that
 * after a reboot, the flatpak with newer content is deployed. */
static void
test_updated_flatpak_is_installed (EosUpdaterFixture *fixture,
                                   gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "update", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autoptr(GFile) flatpak_build_dir = NULL;
  g_autoptr(GFile) flatpak_apps_dir = NULL;
  g_autofree gchar *flatpak_apps_dir_path = NULL;
  g_autoptr(GFile) flatpak_repos_dir = NULL;
  g_autoptr(GFile) flatpak_repo_dir = NULL;
  g_autofree gchar *flatpak_repo_path = NULL;
  g_autoptr(GFile) app_dir = NULL;
  g_autofree gchar *app_dir_path = NULL;
  const gchar *test_flatpak_relative_path = "app/org.test.Test/current/active";
  const gchar *app_executable_relative_path = "files/bin/test";
  g_autofree gchar *app_executable_path = NULL;
  g_autofree gchar *installed_app_executable_path = NULL;
  g_autofree gchar *installed_app_executable_contents = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  const gchar *expected_app_executable_contents = "CONTENTS\n";
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (updater_directory,
                                                             "stable",
                                                             "test-repo",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             (const gchar **) wanted_flatpaks,
                                                             (const gchar **) wanted_flatpaks,
                                                             gpg_key_file,
                                                             keyid,
                                                             &error);
  flatpak_build_dir = eos_test_get_flatpak_build_dir_for_updater_dir (updater_directory);
  flatpak_repos_dir = g_file_get_child (flatpak_build_dir, "repos");
  flatpak_repo_dir = g_file_get_child (flatpak_repos_dir, "test-repo");
  flatpak_repo_path = g_file_get_path (flatpak_repo_dir);
  flatpak_apps_dir = g_file_get_child (flatpak_build_dir, "apps");
  flatpak_apps_dir_path = g_file_get_path (flatpak_apps_dir);
  app_dir_path = g_build_filename (flatpak_apps_dir_path,
                                   "test-repo",
                                   flatpaks_to_install[0].app_id,
                                   "stable",
                                  NULL);
  app_dir = g_file_new_for_path (app_dir_path);
  flatpak_repo_path = g_file_get_path (flatpak_repo_dir);
  app_executable_path = g_build_filename (app_dir_path,
                                          app_executable_relative_path,
                                          NULL);

  /* Set the contents to something that we expect */
  g_file_set_contents (app_executable_path, expected_app_executable_contents, -1, &error);
  g_assert_no_error (error);

  flatpak_build_export (updater_directory,
                        app_dir_path,
                        flatpak_repo_path,
                        "stable",
                        fixture->gpg_home,
                        keyid,
                        &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was updated with new contents */
  installed_app_executable_path = g_build_filename (flatpak_user_installation,
                                                    test_flatpak_relative_path,
                                                    app_executable_relative_path,
                                                    NULL);
  g_file_get_contents (installed_app_executable_path,
                       &installed_app_executable_contents,
                       NULL,
                       &error);
  g_assert_no_error (error);

  g_assert_cmpstr (installed_app_executable_contents, ==, expected_app_executable_contents);
}

/* Insert a list of flatpaks to automatically update on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. Change
 * the contents of the flatpak in the remote repository and ensure that
 * after a reboot, the flatpak with newer content is deployed.
 *
 * This is similar to test_updated_flatpak_is_installed above, but this
 * time we mark the flatpak as "install" and expect it to be updated
 * in a similar fashion. */
static void
test_updated_flatpak_is_installed_on_install_action (EosUpdaterFixture *fixture,
                                                     gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autoptr(GFile) flatpak_build_dir = NULL;
  g_autoptr(GFile) flatpak_apps_dir = NULL;
  g_autofree gchar *flatpak_apps_dir_path = NULL;
  g_autoptr(GFile) flatpak_repos_dir = NULL;
  g_autoptr(GFile) flatpak_repo_dir = NULL;
  g_autofree gchar *flatpak_repo_path = NULL;
  g_autoptr(GFile) app_dir = NULL;
  g_autofree gchar *app_dir_path = NULL;
  const gchar *test_flatpak_relative_path = "app/org.test.Test/current/active";
  const gchar *app_executable_relative_path = "files/bin/test";
  g_autofree gchar *app_executable_path = NULL;
  g_autofree gchar *installed_app_executable_path = NULL;
  g_autofree gchar *installed_app_executable_contents = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  const gchar *expected_app_executable_contents = "CONTENTS\n";
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (updater_directory,
                                                             "stable",
                                                             "test-repo",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             "com.endlessm.TestInstallFlatpaksCollection",
                                                             (const gchar **) wanted_flatpaks,
                                                             (const gchar **) wanted_flatpaks,
                                                             gpg_key_file,
                                                             keyid,
                                                             &error);
  flatpak_build_dir = eos_test_get_flatpak_build_dir_for_updater_dir (updater_directory);
  flatpak_repos_dir = g_file_get_child (flatpak_build_dir, "repos");
  flatpak_repo_dir = g_file_get_child (flatpak_repos_dir, "test-repo");
  flatpak_repo_path = g_file_get_path (flatpak_repo_dir);
  flatpak_apps_dir = g_file_get_child (flatpak_build_dir, "apps");
  flatpak_apps_dir_path = g_file_get_path (flatpak_apps_dir);
  app_dir_path = g_build_filename (flatpak_apps_dir_path,
                                   "test-repo",
                                   flatpaks_to_install[0].app_id,
                                   "stable",
                                  NULL);
  app_dir = g_file_new_for_path (app_dir_path);
  flatpak_repo_path = g_file_get_path (flatpak_repo_dir);
  app_executable_path = g_build_filename (app_dir_path,
                                          app_executable_relative_path,
                                          NULL);

  /* Set the contents to something that we expect */
  g_file_set_contents (app_executable_path, expected_app_executable_contents, -1, &error);
  g_assert_no_error (error);

  flatpak_build_export (updater_directory,
                        app_dir_path,
                        flatpak_repo_path,
                        "stable",
                        fixture->gpg_home,
                        keyid,
                        &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was updated with new contents */
  installed_app_executable_path = g_build_filename (flatpak_user_installation,
                                                    test_flatpak_relative_path,
                                                    app_executable_relative_path,
                                                    NULL);
  g_file_get_contents (installed_app_executable_path,
                       &installed_app_executable_contents,
                       NULL,
                       &error);
  g_assert_no_error (error);

  g_assert_cmpstr (installed_app_executable_contents, ==, expected_app_executable_contents);
}

/* Insert a list of flatpaks to automatically install in the override directory
 * and simulate a reboot by running eos-updater-flatpak-installer. This
 * should check the deployment for a list of flatpaks to install and
 * install them from the local repo into the installation. Verify that
 * the flatpaks are installed and deployed once this has completed. */
static void
test_update_deploy_flatpaks_on_reboot_in_override_dir (EosUpdaterFixture *fixture,
                                                       gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);

  /* Vendor requested to install some flatpaks on the next update
   */
  autoinstall_flatpaks_files_override (updater_directory,
                                       flatpaks_to_install,
                                       G_N_ELEMENTS (flatpaks_to_install),
                                       &error);
  g_assert_no_error (error);

  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install in the override directory
 * as well as the OSTree, ensuring that both files have the same name. Also
 * put another file in the commit directory with a higher priority. We should
 * apply actions from both the override directory first, then the commit
 * directory, with the higher priority file "winning" in case of a
 * conflict. */
static void
test_update_deploy_flatpaks_on_reboot_override_ostree (EosUpdaterFixture *fixture,
                                                       gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install_override_high_priority[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test2", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  /* Note that the low priority list will attempt to remove the file, but this
   * will always get "beaten" by the higher priority file */
  FlatpakToInstall flatpaks_to_install_in_ostree_low_priority[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
    { "uninstall", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  FlatpakToInstall flatpaks_to_install_in_ostree_high_priority[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install_override_high_priority,
                                                                    G_N_ELEMENTS (flatpaks_to_install_override_high_priority));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);

  /* Vendor requested to install some flatpaks on the next update
   */
  autoinstall_flatpaks_files_override_name (updater_directory,
                                            "10-autoinstall",
                                            flatpaks_to_install_override_high_priority,
                                            G_N_ELEMENTS (flatpaks_to_install_override_high_priority),
                                            &error);
  g_assert_no_error (error);

  /* Commit number 1 will install some flatpaks (low priority)
   */
  autoinstall_flatpaks_files_name (1,
                                   "10-autoinstall",
                                   flatpaks_to_install_in_ostree_low_priority,
                                   G_N_ELEMENTS (flatpaks_to_install_in_ostree_low_priority),
                                   &data->additional_directories_for_commit,
                                   &data->additional_files_for_commit);

  /* Commit number 1 will install some flatpaks (high priority)
   */
  autoinstall_flatpaks_files_name (1,
                                   "20-autoinstall",
                                   flatpaks_to_install_in_ostree_high_priority,
                                   G_N_ELEMENTS (flatpaks_to_install_in_ostree_high_priority),
                                   &data->additional_directories_for_commit,
                                   &data->additional_files_for_commit);

  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  /* Now simulate a reboot by running eos-updater-flatpak-installer */
  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install_override_high_priority[0].app_id));
  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install_override_high_priority[1].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. Then
 * uninstall the flatpak and update again with the same list of actions. This
 * should not reinstall the flatpak that was previously removed. */ 
static void
test_update_no_deploy_flatpaks_twice (EosUpdaterFixture *fixture,
                                      gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *second_deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 2 has the same list of actions to apply
   */
  autoinstall_flatpaks_files (2,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  /* First reboot, should install flatpaks */
  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Now, uninstall the flatpak */
  flatpak_uninstall (updater_directory,
                     "org.test.Test/arch/stable",
                     &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (2).
   */
  etc_update_server (data, 2);
  /* Update the client, so it also has a new commit (2); and, at this
   * point, three deployments.
   */
  etc_update_client (data);

  second_deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                             refspec,
                                                             &error);
  g_assert_no_error (error);

  /* Reboot #2. Should not reinstall the same flatpak */
  eos_test_run_flatpak_installer (data->client->root,
                                  second_deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was not installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_false (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
}

/* Insert a list of flatpaks to automatically install on the commit
 * and simulate a reboot by running eos-updater-flatpak-installer. Then
 * uninstall the flatpak and update again with a new list of actions containing
 * a new install command. This should reinstall the flatpak. */
static void
test_update_force_reinstall_flatpak (EosUpdaterFixture *fixture,
                                     gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[] = {
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  FlatpakToInstall next_flatpaks_to_install[] = {
    /* First element is identical to @flatpaks_to_install, since it’s an append-only list. */
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
    { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
  };
  g_autofree gchar *flatpak_user_installation = NULL;
  g_autoptr(GFile) flatpak_user_installation_dir = NULL;
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install,
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *second_deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *updater_directory_str = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Commit number 1 will install some flatpaks
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install,
                              G_N_ELEMENTS (flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 2 has an updated list of actions to apply
   */
  autoinstall_flatpaks_files (2,
                              next_flatpaks_to_install,
                              G_N_ELEMENTS (next_flatpaks_to_install),
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  updater_directory_str = g_file_get_path (updater_directory);
  flatpak_user_installation = g_build_filename (updater_directory_str,
                                                "flatpak-user",
                                                NULL);
  flatpak_user_installation_dir = g_file_new_for_path (flatpak_user_installation);
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (1).
   */
  etc_update_server (data, 1);
  /* Update the client, so it also has a new commit (1); and, at this
   * point, two deployments - old one pointing to commit 0 and a new
   * one pointing to commit 1.
   */
  etc_update_client (data);

  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  /* First reboot, should install flatpaks */
  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Now, uninstall the flatpak */
  flatpak_uninstall (updater_directory,
                     "org.test.Test/arch/stable",
                     &error);
  g_assert_no_error (error);

  /* Update the server, so it has a new commit (2).
   */
  etc_update_server (data, 2);
  /* Update the client, so it also has a new commit (2); and, at this
   * point, three deployments.
   */
  etc_update_client (data);

  second_deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                             refspec,
                                                             &error);
  g_assert_no_error (error);

  /* Reboot #2. Should reinstall the same flatpak */
  eos_test_run_flatpak_installer (data->client->root,
                                  second_deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[0].app_id));
}

/* Over the course of three revisions, install, remove, then install a flatpak.
 * The result should be that the flatpak is installed (overall) */
static void
test_update_install_through_squashed_list (EosUpdaterFixture *fixture,
                                           gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  FlatpakToInstall flatpaks_to_install[][3] = {
    {
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    },
    {
      /* List grows over time as it’s append-only. */
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
      { "uninstall", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    },
    {
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
      { "uninstall", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE },
      { "install", "com.endlessm.TestInstallFlatpaksCollection", "test-repo", "org.test.Test", "stable", "app", FLATPAK_TO_INSTALL_FLAGS_NONE }
    },
  };
  g_auto(GStrv) wanted_flatpaks = flatpaks_to_install_app_ids_strv (flatpaks_to_install[0],
                                                                    G_N_ELEMENTS (flatpaks_to_install));
  g_auto(GStrv) deployed_flatpaks = NULL;
  g_autofree gchar *deployment_repo_relative_path = g_build_filename ("sysroot", "ostree", "repo", NULL);
  g_autofree gchar *deployment_csum = NULL;
  g_autofree gchar *refspec = concat_refspec (default_remote_name, default_ref);
  g_autoptr(GFile) deployment_repo_dir = NULL;
  g_autoptr(GFile) updater_directory = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) gpg_key_file = get_gpg_key_file_for_keyid (fixture->gpg_home, keyid);
  g_autoptr(GError) error = NULL;

  g_test_bug ("T16682");

  etc_data_init (data, fixture);

  /* Note that since we had to hardcode the sub-array size above in the
   * flatpaks_to_install declaration in order to keep the compiler happy, we
   * cannot use G_N_ELEMENTS to work out the sub array sizes. Just use
   * hardcoded sizes instead. */

  /* Commit number 1 will install a flatpak
   */
  autoinstall_flatpaks_files (1,
                              flatpaks_to_install[0],
                              1,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 1 will remove that flatpak
   */
  autoinstall_flatpaks_files (2,
                              flatpaks_to_install[1],
                              2,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Commit number 3 will install it again
   */
  autoinstall_flatpaks_files (3,
                              flatpaks_to_install[2],
                              3,
                              &data->additional_directories_for_commit,
                              &data->additional_files_for_commit);

  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);

  updater_directory = g_file_get_child (data->client->root, "updater");
  deployment_repo_dir = g_file_get_child (data->client->root,
                                          deployment_repo_relative_path);
  eos_test_setup_flatpak_repo_simple (updater_directory,
                                      "stable",
                                      "test-repo",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      "com.endlessm.TestInstallFlatpaksCollection",
                                      (const gchar **) wanted_flatpaks,
                                      gpg_key_file,
                                      keyid,
                                      &error);

  /* Update the server, so it has a new commit (3).
   */
  etc_update_server (data, 3);
  /* Update the client to commit 3, skipping 2.
   */
  etc_update_client (data);

  deployment_csum = get_checksum_for_deploy_repo_dir (deployment_repo_dir,
                                                      refspec,
                                                      &error);
  g_assert_no_error (error);

  /* Reboot and install flatpaks */
  eos_test_run_flatpak_installer (data->client->root,
                                  deployment_csum,
                                  default_remote_name,
                                  &error);
  g_assert_no_error (error);

  /* Assert that our flatpak was not installed */
  deployed_flatpaks = eos_test_get_installed_flatpaks (updater_directory, &error);
  g_assert_no_error (error);

  g_assert_true (g_strv_contains ((const gchar * const *) deployed_flatpaks, flatpaks_to_install[2][2].app_id));
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("https://phabricator.endlessm.com/");

  eos_test_add ("/updater/install-no-flatpaks", NULL, test_update_install_no_flatpaks);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo", NULL, test_update_install_flatpaks_in_repo);
  eos_test_add ("/updater/install-flatpaks-custom-branch-name", NULL, test_update_install_flatpaks_custom_branch_name);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-if-collection-id-not-supported", NULL, test_update_install_flatpaks_in_repo_fallback_if_collection_not_in_repo_config);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-fallback-if-collection-id-not-configured-in-remote-or-repo", NULL, test_update_install_flatpaks_in_repo_fallback_if_collection_not_in_remote_or_repo);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-error-if-collection-id-invalid", NULL, test_update_install_flatpaks_in_repo_error_if_collection_invalid);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-error-using-only-remote-name", NULL, test_update_install_flatpaks_in_repo_error_using_remote_name);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-error-no-branch-name", NULL, test_update_install_flatpaks_in_repo_error_no_branch_name);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-error-no-remote-or-collection-name", NULL, test_update_install_flatpaks_no_location_error);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-error-conflicting-remote-collection-name", NULL, test_update_install_flatpaks_conflicting_location_error);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-also-pull-runtimes", NULL, test_update_install_flatpaks_in_repo_also_pull_runtimes);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-also-pull-runtimes-different-remote", NULL, test_update_install_flatpaks_in_repo_also_pull_runtimes_different_remote);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-also-pull-autodownload-extensions", NULL, test_update_install_flatpaks_in_repo_also_pull_autodownload_extension);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-also-pull-autodownload-extensions-different-remote", NULL, test_update_install_flatpaks_in_repo_dont_also_pull_autodownload_extension_different_remote);
  eos_test_add ("/updater/install-flatpaks-pull-to-repo-dont-also-pull-dep-no-collection-id", NULL, test_update_install_flatpaks_in_repo_dont_also_pull_dep_no_collection_id);
  eos_test_add ("/updater/update-flatpaks-pull-updated-to-repo-no-previous-install", NULL, test_update_flatpaks_updated_in_repo);
  eos_test_add ("/updater/update-flatpaks-updated-flatpak-is-installed", NULL, test_updated_flatpak_is_installed);
  eos_test_add ("/updater/update-flatpaks-updated-flatpak-is-installed-on-install-action", NULL, test_updated_flatpak_is_installed_on_install_action);
  eos_test_add ("/updater/update-flatpaks-pull-updated-to-repo-after-install", NULL, test_update_flatpaks_updated_in_repo_after_install);
  eos_test_add ("/updater/update-flatpaks-pull-updated-to-repo-on-subsequent-fetch", NULL, test_update_flatpaks_updated_in_repo_on_subsequent_fetch);
  eos_test_add ("/updater/skip-install-flatpaks-on-architecture", NULL, test_update_skip_install_flatpaks_on_architecture);
  eos_test_add ("/updater/only-install-flatpaks-on-architecture", NULL, test_update_only_install_flatpaks_on_architecture);
  eos_test_add ("/updater/skip-install-flatpaks-on-locale", NULL, test_update_skip_install_flatpaks_on_locale);
  eos_test_add ("/updater/only-install-flatpaks-on-locale", NULL, test_update_only_install_flatpaks_on_locale);
  eos_test_add ("/updater/install-flatpaks-not-deployed", NULL, test_update_install_flatpaks_not_deployed);
  eos_test_add ("/updater/install-flatpaks-deploy-on-reboot", NULL, test_update_deploy_flatpaks_on_reboot);
  eos_test_add ("/updater/install-flatpaks-deploy-dependency-runtime-on-reboot", NULL, test_update_deploy_dependency_runtime_flatpaks_on_reboot);
  eos_test_add ("/updater/install-flatpaks-deploy-dependency-autodownload-extension-on-reboot", NULL, test_update_deploy_dependency_autodownload_extension_flatpaks_on_reboot);
  eos_test_add ("/updater/install-flatpaks-deploy-dependency-fail-no-update-counter", NULL, test_update_deploy_dependency_runtime_fail_no_update_counter);
  eos_test_add ("/updater/install-flatpaks-no-deploy-dependency-non-autodownload-extension-on-reboot", NULL, test_update_no_deploy_dependency_non_autodownload_extension_flatpaks_on_reboot);
  eos_test_add ("/updater/install-flatpaks-deploy-no-op-already-installed", NULL, test_update_no_op_flatpak_already_installed);
  eos_test_add ("/updater/install-flatpaks-deploy-on-reboot-partially-on-failure", NULL, test_update_deploy_flatpaks_on_reboot_partially_on_failure);
  eos_test_add ("/updater/install-flatpaks-deploy-on-reboot-resume-on-failure-resolved", NULL, test_update_deploy_flatpaks_on_reboot_resume_on_failure_resolved);
  eos_test_add ("/updater/install-flatpaks-deploy-on-reboot-in-override", NULL, test_update_deploy_flatpaks_on_reboot_override_ostree);
  eos_test_add ("/updater/install-flatpaks-deploy-on-reboot-ostree-override", NULL, test_update_deploy_flatpaks_on_reboot_in_override_dir);
  eos_test_add ("/updater/update-flatpaks-no-op-if-not-installed", NULL, test_update_flatpaks_no_op_if_not_installed);
  eos_test_add ("/updater/update-flatpaks-no-op-if-up-to-date", NULL, test_update_flatpaks_no_op_if_up_to_date);
  eos_test_add ("/updater/uninstall-flatpaks-on-reboot", NULL, test_update_uninstall_flatpaks_on_reboot);
  eos_test_add ("/updater/uninstall-flatpaks-on-reboot-custom-branch-name", NULL, test_update_uninstall_flatpaks_on_reboot_custom_branch_name);
  eos_test_add ("/updater/no-uninstall-flatpaks-on-reboot-different-branch-name", NULL, test_update_no_uninstall_flatpaks_on_reboot_different_branch_name);
  eos_test_add ("/updater/no-deploy-same-action-twice", NULL, test_update_no_deploy_flatpaks_twice);
  eos_test_add ("/updater/reinstall-flatpak-if-counter-is-later", NULL, test_update_force_reinstall_flatpak);
  eos_test_add ("/updater/update-deploy-fail-flatpaks-stay-in-repo", NULL, test_update_deploy_fail_flatpaks_stay_in_repo);
  eos_test_add ("/updater/update-deploy-fail-flatpaks-not-deployed", NULL, test_update_deploy_fail_flatpaks_not_deployed);
  eos_test_add ("/updater/update-flatpaks-pull-fail-system-not-deployed", NULL, test_update_flatpak_pull_fail_system_not_deployed);
  eos_test_add ("/updater/update-install-through-squashed-list", NULL, test_update_install_through_squashed_list);

  return g_test_run ();
}
