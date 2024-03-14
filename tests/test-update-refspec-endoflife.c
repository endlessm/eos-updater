/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
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
 *  - Matthew Leeds <matthew.leeds@endlessm.com>
 */

#include <test-common/gpg.h>
#include <test-common/misc-utils.h>
#include <test-common/spawn-utils.h>
#include <test-common/utils.h>

#include <gio/gio.h>
#include <locale.h>

const gchar *next_ref = "REFv2";
const OstreeCollectionRef _next_collection_ref = { (gchar *) "com.endlessm.CollectionId", (gchar *) "REFv2" };
const OstreeCollectionRef *next_collection_ref = &_next_collection_ref;

const OstreeCollectionRef _default_collection_ref_no_id = { NULL, (gchar *) "REF" };
const OstreeCollectionRef *default_collection_ref_no_id = &_default_collection_ref_no_id;

static GHashTable *
create_eol_rebase_metadata (const gchar *ref_to_upgrade)
{
  GHashTable *ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert (ht,
                       g_strdup (OSTREE_COMMIT_META_KEY_ENDOFLIFE_REBASE),
                       g_strdup (ref_to_upgrade));

  return ht;
}

/* Add some metadata to add to the given commit, which tells the updater to
 * upgrade to a different ref than the currently booted one. Unlike with
 * checkpoints, the redirect doesn't have to be the booted commit to be
 * followed. */
static void
insert_update_refspec_metadata_for_commit (guint         commit,
                                           const gchar  *new_ref,
                                           GHashTable  **out_metadata_hashtable)
{
  g_return_if_fail (out_metadata_hashtable != NULL);

  if (*out_metadata_hashtable == NULL)
    *out_metadata_hashtable = g_hash_table_new_full (g_direct_hash,
                                                     g_direct_equal,
                                                     NULL,
                                                     (GDestroyNotify) g_hash_table_unref);

  g_hash_table_insert (*out_metadata_hashtable,
                       GUINT_TO_POINTER (commit),
                       create_eol_rebase_metadata (new_ref));
}

/* @expected_updater_warnings should typically be set to %NULL. Set it to a
 * non-%NULL glob string for tests where the updater is expected to emit a
 * warning. FIXME: Currently we have no way to programmatically verify that the
 * warning matches the glob. */
static void
update_client (EosUpdaterFixture *fixture,
               EosTestClient     *client,
               const gchar       *expected_updater_warnings)
{
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_autoptr(GPtrArray) cmds = NULL;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(CmdResult) reaped = CMD_RESULT_CLEARED;
  g_autoptr(GError) error = NULL;

  if (expected_updater_warnings == NULL)
    eos_test_client_run_updater (client,
                                 &main_source,
                                 1,
                                 NULL,
                                 &updater_cmd,
                                 &error);
  else
    eos_test_client_run_updater_ignore_warnings (client,
                                                 &main_source,
                                                 1,
                                                 NULL,
                                                 &updater_cmd,
                                                 &error);
  g_assert_no_error (error);

  autoupdater_root = g_file_get_child (fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,  /* interval (days) */
                                          0, /* user visible delay (days) */
                                          TRUE,  /* force update */
                                          &error);
  g_assert_no_error (error);

  eos_test_client_reap_updater (client,
                                &updater_cmd,
                                &reaped,
                                &error);
  g_assert_no_error (error);

  cmds = g_ptr_array_new ();
  g_ptr_array_add (cmds, &reaped);
  g_ptr_array_add (cmds, autoupdater->cmd);
  g_assert_true (cmd_result_ensure_all_ok_verbose (cmds));
}

/* Start with a commit, and then make a final commit on the first refspec which
 * adds a redirect, which should change the upgrade ref. */
static void
_test_update_refspec_endoflife (EosUpdaterFixture *fixture,
                                gconstpointer user_data,
                                const OstreeCollectionRef *collection_ref)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GFile) repo_path = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;
  g_autofree gchar *branches_option = NULL;
  g_autofree gchar *expected_branches = NULL;

  insert_update_refspec_metadata_for_commit (1,
                                             next_ref,
                                             &additional_metadata_for_commit);

  server_root = g_file_get_child (fixture->tmpdir, "main");
  server = eos_test_server_new_quick (server_root,
                                      default_vendor,
                                      default_product,
                                      default_collection_ref,
                                      0,
                                      fixture->gpg_home,
                                      keyid,
                                      default_ostree_path,
                                      NULL,
                                      NULL,
                                      additional_metadata_for_commit,
                                      &error);
  g_assert_no_error (error);
  g_assert_cmpuint (server->subservers->len, ==, 1u);

  subserver = g_object_ref (EOS_TEST_SUBSERVER (g_ptr_array_index (server->subservers, 0)));
  client_root = g_file_get_child (fixture->tmpdir, "client");
  client = eos_test_client_new (client_root,
                                default_remote_name,
                                subserver,
                                collection_ref,
                                default_vendor,
                                default_product,
                                default_auto_bootloader,
                                &error);
  g_assert_no_error (error);

  repo_path = eos_test_client_get_repo (client);
  repo = ostree_repo_new (repo_path);
  ostree_repo_open (repo, NULL, &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (collection_ref),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2" which we should
   * update to. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  /* Check that the remote branches option is set to the next ref */
  ostree_repo_reload_config (repo, NULL, &error);
  g_assert_no_error (error);
  g_free (branches_option);
  g_free (expected_branches);
  expected_branches = g_strdup_printf ("%s;", next_ref);
  ostree_repo_get_remote_option (repo, default_remote_name,
                                 "branches", NULL,
                                 &branches_option, &error);
  g_assert_no_error (error);
  g_assert_cmpstr (branches_option, ==, expected_branches);
}

/* Test following a redirect with a collection ID configured */
static void
test_update_refspec_endoflife (EosUpdaterFixture *fixture,
                               gconstpointer user_data)
{
  _test_update_refspec_endoflife (fixture, user_data, default_collection_ref);
}

/* Test following a redirect without a collection ID configured */
static void
test_update_refspec_endoflife_no_collection_ref (EosUpdaterFixture *fixture,
                                                 gconstpointer user_data)
{
  _test_update_refspec_endoflife (fixture, user_data, default_collection_ref_no_id);
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  eos_test_add ("/updater/update-refspec-endoflife",
                NULL,
                test_update_refspec_endoflife);
  eos_test_add ("/updater/update-refspec-endoflife-no-collection-ref",
                NULL,
                test_update_refspec_endoflife_no_collection_ref);

  return g_test_run ();
}
