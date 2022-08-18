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

#include <test-common/gpg.h>
#include <test-common/misc-utils.h>
#include <test-common/spawn-utils.h>
#include <test-common/utils.h>

#include <gio/gio.h>
#include <locale.h>
#include <sys/utsname.h>

const gchar *next_ref = "REFv2";
const OstreeCollectionRef _next_collection_ref = { (gchar *) "com.endlessm.CollectionId", (gchar *) "REFv2" };
const OstreeCollectionRef *next_collection_ref = &_next_collection_ref;

const OstreeCollectionRef _default_collection_ref_no_id = { NULL, (gchar *) "REF" };
const OstreeCollectionRef *default_collection_ref_no_id = &_default_collection_ref_no_id;

static GHashTable *
create_checkpoint_target_metadata (const gchar *ref_to_upgrade)
{
  GHashTable *ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert (ht,
                       g_strdup ("eos.checkpoint-target"),
                       g_strdup (ref_to_upgrade));

  return ht;
}

/* Add some metadata to add to the given commit, which when running as the
 * deployed commit, tells the updater which ref to pull from (as opposed to
 * the currently booted one) */
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
                       create_checkpoint_target_metadata (new_ref));
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

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new marker, such that when that commit is deployed, the updater
 * will know to use a new refspec to upgrade with. Then upgrade again on
 * that deployed commit and ensure that the new refspec is used. */
static void
test_update_refspec_checkpoint (EosUpdaterFixture *fixture,
                                gconstpointer user_data)
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

  if (eos_test_skip_chroot ())
    return;

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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  repo_path = eos_test_client_get_repo (client);
  repo = ostree_repo_new (repo_path);
  ostree_repo_open (repo, NULL, &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, but when we switch over
   * the ref we pull from, we should have commit 2. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Check that the remote branches option is set to the default ref */
  ostree_repo_reload_config (repo, NULL, &error);
  g_assert_no_error (error);
  g_free (branches_option);
  g_free (expected_branches);
  expected_branches = g_strdup_printf ("%s;", default_ref);
  ostree_repo_get_remote_option (repo, default_remote_name,
                                 "branches", NULL,
                                 &branches_option, &error);
  g_assert_no_error (error);
  g_assert_cmpstr (branches_option, ==, expected_branches);

  /* Update the client again. Because we had deployed the
   * checkpoint, we should now have the new ref to update on and should
   * have pulled the new commit. */
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

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new marker, such that when that commit is deployed, the updater
 * will know to use a new refspec to upgrade with. Then upgrade again on
 * that deployed commit and ensure that the new refspec is used. */
static void
test_update_refspec_checkpoint_old_ref_deleted (EosUpdaterFixture *fixture,
                                                gconstpointer user_data)
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
  g_autoptr(GHashTable) refs = NULL;
  gboolean has_original_ref, has_new_ref;
  g_autofree gchar *branches_option = NULL;
  g_autofree gchar *expected_branches = NULL;

  if (eos_test_skip_chroot ())
    return;

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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  repo_path = eos_test_client_get_repo (client);
  repo = ostree_repo_new (repo_path);
  ostree_repo_open (repo, NULL, &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, but when we switch over
   * the ref we pull from, we should have commit 2. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  /* Update the client again. Because we had deployed the
   * checkpoint, we should have the new ref and should have
   * dropped the old one */
  update_client (fixture, client, NULL);

  ostree_repo_list_refs (repo, NULL, &refs, NULL, &error);
  g_assert_no_error (error);

  has_original_ref = g_hash_table_contains (refs, "REMOTE:REF");
  has_new_ref = g_hash_table_contains (refs, "REMOTE:REFv2");

  g_assert_true (has_new_ref);
  g_assert_false (has_original_ref);
}

/* Start with a commit, then make a new commit (2) on a new branch. Finally,
 * make a "checkpoint" commit on the old branch (3) which points to the new
 * branch. Even though (2) is older than (3), the checkpoint should still be
 * followed and we should "upgrade" to the older commit on the newer branch. */
static void
test_update_refspec_checkpoint_even_if_downgrade (EosUpdaterFixture *fixture,
                                                  gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;

  if (eos_test_skip_chroot ())
    return;

  insert_update_refspec_metadata_for_commit (2,
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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  /* Insert a commit on "REMOTE:REFv2". The first time we
   * update, we should update to commit 2, but when we switch over
   * the ref we pull from, we should have commit 1. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (1));
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should have
   * the second commit (we will also have the first, but only
   * because the tests don't have a mechanism to remove old
   * commit files). */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  /* Update the client again. Because we had deployed the
   * checkpoint, we should now have the new ref to update on and should
   * have pulled the new commit (we can't assert on anything here, but
   * we can do the next step to figure out what branch we're on). */
  update_client (fixture, client, NULL);

  /* Now that we should be on the new branch, make a commit there
   * and update again. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (3));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              3,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new marker, such that when that commit is deployed, the updater
 * will know to use a new refspec to upgrade with. However, no collection ref
 * is set on the commit on the server. In that case, we should still use the
 * checkpoint commit if we can. */
static void
test_update_refspec_checkpoint_no_collection_ref_server (EosUpdaterFixture *fixture,
                                                         gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;

  if (eos_test_skip_chroot ())
    return;

  insert_update_refspec_metadata_for_commit (1,
                                             next_ref,
                                             &additional_metadata_for_commit);

  server_root = g_file_get_child (fixture->tmpdir, "main");
  server = eos_test_server_new_quick (server_root,
                                      default_vendor,
                                      default_product,
                                      default_collection_ref_no_id,
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
                                default_collection_ref_no_id,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref_no_id),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, but when we switch over
   * the ref we pull from, we should have commit 2. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Update the client again. We deployed the checkpoint but
   * it has no collection-ref set on the remote, so fail to use it. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new checkpoint, however the checkpoint is malformed. Attempting
 * to use it should fail, but not crash. */
static void
test_update_refspec_checkpoint_malformed_checkpoint (EosUpdaterFixture *fixture,
                                                     gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;

  if (eos_test_skip_chroot ())
    return;

  insert_update_refspec_metadata_for_commit (1,
                                             "$^^@*invalid",
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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1 */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Update the client again. The checkpoint was invalid, so fail to use it. We
   * expect the updater to warn about this. */
  update_client (fixture, client,
                 "*Failed to parse eos.checkpoint-target ref '$^^@*invalid', ignoring it");

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);
}

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new checkpoint, however the checkpoint is malformed. Attempting
 * to use it should fail, but not crash. Afterwards, we recover by making
 * a new commit on the non-checkpointed branch with a new checkpoint that is
 * valid. Rebooting into that commit should allow us to upgrade further.
 *
 *  REFv2                   (4)
 *                         /
 *                        /
 *  REF (0)--(1)--(2*)--(3+)
 *
 * (2*) is a malformed checkpoint. (3+) is a maintenance commit on the original
 * "REF" refspec with a new checkpoint.
 */
static void
test_update_refspec_checkpoint_malformed_checkpoint_recovery (EosUpdaterFixture *fixture,
                                                              gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;

  if (eos_test_skip_chroot ())
    return;

  insert_update_refspec_metadata_for_commit (1,
                                             "$^^@*invalid",
                                             &additional_metadata_for_commit);
  insert_update_refspec_metadata_for_commit (3,
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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, and when we switch over
   * we won't have commit (2) as there was no way to get to it. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Update the client again. The checkpoint was invalid, so fail to use it. We
   * expect the updater to warn about this. */
  update_client (fixture, client,
                 "*Failed to parse eos.checkpoint-target ref '$^^@*invalid', ignoring it");

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Insert a new commit (3) on the original branch. This should fix up
   * the checkpoint. Also add a new commit on the checkpoint
   * branch (this is needed only for the tests, as the test infrastructure
   * adds files one commit after the other) */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (3));
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (4));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Update client. This was a checkpoint so we should not
   * have commit 4 (but should have commit 3) */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              3,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              4,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Update client again. Now that we rebooted after
   * updating, we should have commit 4. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              4,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new marker, such that when that commit is deployed, the updater
 * will know to use a new refspec to upgrade with. However, no collection ref
 * is set on the client side remote config. In that case, we should still use the
 * checkpoint commit if we can. */
static void
test_update_refspec_checkpoint_no_collection_ref_client (EosUpdaterFixture *fixture,
                                                         gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;

  if (eos_test_skip_chroot ())
    return;

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
                                default_collection_ref_no_id,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref_no_id),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, but when we switch over
   * the ref we pull from, we should have commit 2. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Update the client again. We deployed the checkpoint but
   * it has no collection-ref set on the remote, so fail to use it. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new marker, such that when that commit is deployed, the updater
 * will know to use a new refspec to upgrade with. However, say we screwed
 * up and need to do a maintenance fix on the old branch. The commit from the
 * old branch should be preferred on the next update such that the
 * old refspec is still in use on reboot.
 *
 *  REFv2              (4)
 *                    /
 *                   /
 *  REF (0)--(1)--(2)--(3)
 *
 * (2) is a checkpoint. (3) is a maintenance commit on the original
 * "REF" refspec.
 */
static void
test_update_refspec_checkpoint_continue_old_branch (EosUpdaterFixture *fixture,
                                                    gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  gboolean has_commit;

  if (eos_test_skip_chroot ())
    return;

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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (1,
                                                                               0,
                                                                               default_collection_ref));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, but when we switch over
   * the ref we pull from, we should have commit 2. */
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (2,
                                                                               1,
                                                                               next_collection_ref));
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Now, let's say we screw up something and need to do an update on
   * the old branch. Insert another commit, but this time without the
   * metadata. */
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (3,
                                                                               1,
                                                                               default_collection_ref));
  /* For completeness insert a new commit on the checkpoint branch */
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (4,
                                                                               2,
                                                                               next_collection_ref));
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Update the client again. Even though we deployed
   * the checkpoint, we should not have the new commit that
   * came from the checkpoint branch. Instead we should
   * have the newest commit on the non-checkpoint branch */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              4,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              3,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Start with a commit, and then make a final commit on the first refspec
 * which adds a new marker, such that when that commit is deployed, the updater
 * will know to use a new refspec to upgrade with. However, say we screwed
 * up and need to do a maintenance fix on the old branch. The commit from the
 * old branch should be preferred on the next update such that the
 * old refspec is still in use on reboot. However, later on we create
 * another checkpoint commit on the newest commit in the old branch. That
 * should take us to our new branch.
 *
 *  REFv2             (4)      (6)
 *                   /         /
 *                  /         /
 *  REF (0)--(1)--(2)--(3)--(5)
 *
 * (2) is a checkpoint. (3) is a maintenance commit on the original
 * "REF" refspec. (5) is another checkpoint. Note that (2) is the parent
 * of (4) and (5) is the parent of (6) in the sense that static deltas
 * will be generated between those two.
 *
 */
static void
test_update_refspec_checkpoint_continue_old_branch_then_new_branch (EosUpdaterFixture *fixture,
                                                                    gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  gboolean has_commit;

  if (eos_test_skip_chroot ())
    return;

  insert_update_refspec_metadata_for_commit (1,
                                             next_ref,
                                             &additional_metadata_for_commit);
  insert_update_refspec_metadata_for_commit (5,
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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (1,
                                                                               0,
                                                                               default_collection_ref));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, but when we switch over
   * the ref we pull from, we should have commit 2. */
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (2,
                                                                               1,
                                                                               next_collection_ref));
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Now, let's say we screw up something and need to do an update on
   * the old branch. Insert another commit, but this time without the
   * metadata. */
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (3,
                                                                               1,
                                                                               default_collection_ref));
  /* For completeness insert a new commit on the checkpoint branch */
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (4,
                                                                               2,
                                                                               next_collection_ref));
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Update the client again. Even though we deployed
   * the checkpoint, we should not have the new commit that
   * came from the checkpoint branch. Instead we should
   * have the newest commit on the non-checkpoint branch */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              4,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              3,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  /* Finally, we create another commit on the old branch
   * which is a checkpoint and a new commit on the new branch
   * which continues off from the old branch */
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (5,
                                                                               3,
                                                                               default_collection_ref));
  eos_test_updater_insert_commit_steal_info (subserver->commit_graph,
                                             eos_test_updater_commit_info_new (6,
                                                                               5,
                                                                               next_collection_ref));
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Update the client. We should stop
   * at the checkpoint commit again. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              6,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              5,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  /* Update one more time. We should now have the
   * commit on the post-checkpoint branch. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              6,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Make sure the checkpoint is followed when it has a full refspec with
 * remote. */
static void
test_update_refspec_checkpoint_ignore_remote (EosUpdaterFixture *fixture,
                                              gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;
  g_autofree gchar *next_refspec = g_strdup_printf ("BADREMOTE:%s", next_ref);

  if (eos_test_skip_chroot ())
    return;

  /* Set checkpoint with full refspec */
  insert_update_refspec_metadata_for_commit (1,
                                             next_refspec,
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
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));

  /* Also insert a commit (2) for the refspec "REMOTE:REFv2". The first time we
   * update, we should only update to commit 1, but when we switch over
   * the ref we pull from, we should have commit 2. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (next_collection_ref),
                       GUINT_TO_POINTER (2));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  /* Now update the client. We stopped making commits on this
   * ref, so it is effectively a "checkpoint" and we should only have
   * the first commit. */
  update_client (fixture, client, NULL);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_false (has_commit);

  /* Update the client again. Because we had deployed the
   * checkpoint, we should now have the new ref to update on and should
   * have pulled the new commit. The updater should warn us about the ignored
   * remote. */
  update_client (fixture, client,
                 "*Ignoring remote 'BADREMOTE' in eos.checkpoint-target metadata 'BADREMOTE:REFv2'");

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Specifically test the checkpoint at the upgrade from the eos3a branch (EOS 3.9)
 * to the eos4 branch (EOS 4). With the release of EOS 4, various features and
 * systems are no longer supported, so we need to make sure they *don’t* get
 * migrated to the eos4 branch.
 *
 * Conversely, test that machines which don’t match any of the
 * no-longer-supported machines *do* get migrated to the eos4 branch.
 *
 * https://phabricator.endlessm.com/T31918 */
static void
test_update_refspec_checkpoint_eos3a_eos4 (EosUpdaterFixture *fixture,
                                           gconstpointer      user_data)
{
  struct utsname uts;
  gboolean host_is_aarch64;
  const gchar *cpuinfo_i8565u =
      "processor	: 0\n"
      "vendor_id	: GenuineIntel\n"
      "cpu family	: 6\n"
      "model		: 142\n"
      "model name	: Intel(R) Core(TM) i7-8565U CPU @ 1.80GHz\n"
      "stepping	: 12\n";
      /* etc */
  const gchar *cpuinfo_not_i8565u =
      "processor	: 0\n"
      "vendor_id	: NotIntel\n"
      "cpu family	: 6\n"
      "model		: 123\n"
      "model name	: Some collection of ASICs\n"
      "stepping	: 12\n";
      /* etc */
  const gchar *cmdline_ro_end = "BOOT_IMAGE=(hd0,gpt3)/boot/ostree/eos-c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/vmlinuz-5.11.0-12-generic root=UUID=11356111-ea76-4f63-9d7e-1d6b9d10a065 rw splash plymouth.ignore-serial-consoles quiet loglevel=0 ostree=/ostree/boot.0/eos/c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/0 ro";
  const gchar *cmdline_ro_middle = "BOOT_IMAGE=(hd0,gpt3)/boot/ostree/eos-c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/vmlinuz-5.11.0-12-generic root=UUID=11356111-ea76-4f63-9d7e-1d6b9d10a065 rw splash plymouth.ignore-serial-consoles quiet ro loglevel=0 ostree=/ostree/boot.0/eos/c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/0";
  const gchar *cmdline_not_ro = "BOOT_IMAGE=(hd0,gpt3)/boot/ostree/eos-c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/vmlinuz-5.11.0-12-generic root=UUID=11356111-ea76-4f63-9d7e-1d6b9d10a065 rw splash plymouth.ignore-serial-consoles quiet loglevel=0 ostree=/ostree/boot.0/eos/c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/0";
  const struct
    {
      /* Setup */
      const gchar *src_ref;  /* (nullable) for default */
      const gchar *tgt_ref;  /* (nullable) for default */
      const gchar *sys_vendor;  /* (nullable) for default */
      const gchar *product_name;  /* (nullable) for default */
      gboolean is_split_disk;
      const gchar *uname_machine;  /* (nullable) for default */
      const gchar *cpuinfo;  /* (nullable) for default */
      const gchar *cmdline;  /* (nullable) for default */
      gboolean force_follow_checkpoint;

      /* Results */
      gboolean expect_checkpoint_followed;
    }
  tests[] =
    {
      /* Normal system */
      { NULL, NULL, NULL, NULL, FALSE, NULL, NULL, NULL, FALSE, TRUE },
      { NULL, NULL, NULL, NULL, FALSE, NULL, NULL, NULL, TRUE, TRUE },

      /* Split disk */
      { NULL, NULL, NULL, NULL, TRUE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, NULL, NULL, TRUE, NULL, NULL, NULL, TRUE, TRUE },

      /* aarch64 */
      { NULL, NULL, NULL, NULL, FALSE, "x86_64", NULL, NULL, FALSE, TRUE },
      { NULL, NULL, NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, FALSE },
      { NULL, NULL, NULL, NULL, FALSE, "aarch64", NULL, NULL, TRUE, TRUE },

      /* Ref matching. When the ref does not match the expected "eos3a"
       * and "eos4" patterns, the checkpoint is followed. Since that
       * would be indistinguishable from a normal system with matching
       * refs, the machine is set to aarch64. The result being that the
       * checkpoint is skipped for ref matches and followed for ref
       * mismatches. */
      { "eos3", NULL, NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, TRUE },
      { "eos3a2", NULL, NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, TRUE },
      { NULL, "eos3b", NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, FALSE },
      { NULL, "eos4a", NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, FALSE },
      { "os/eos/arm64/eos3a", NULL, NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, FALSE },
      { NULL, "os/eos/arm64/eos4", NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, FALSE },
      { "os/eos/arm64/eos3a", "os/eos/arm64/eos4", NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, FALSE },
      { "os/eos/arm64/eos3a", "os/eos/arm64/latest1", NULL, NULL, FALSE, "aarch64", NULL, NULL, FALSE, FALSE },
      { "os/eos/arm64/eos3a", "os/eos/arm64/eos4", NULL, NULL, FALSE, "aarch64", NULL, NULL, TRUE, TRUE },

      /* Ref matching. When the ref matches the "nexthw/eos3.9", the checkpoint
       * is followed. It should allow updating to "eos4" directly.
       * https://phabricator.endlessm.com/T32542 */
      { "nexthw/eos3.9", NULL, NULL, NULL, FALSE, NULL, NULL, NULL, FALSE, TRUE },

      /* Asus with i-8565U CPU */
      { NULL, NULL, NULL, NULL, FALSE, NULL, cpuinfo_i8565u, NULL, FALSE, TRUE },
      { NULL, NULL, "Asus", NULL, FALSE, NULL, cpuinfo_not_i8565u, NULL, FALSE, TRUE },
      { NULL, NULL, "Asus", NULL, FALSE, NULL, cpuinfo_i8565u, NULL, FALSE, FALSE },
      { NULL, NULL, "Asus", NULL, FALSE, NULL, cpuinfo_i8565u, NULL, TRUE, TRUE },

      /* Various systems unsupported by the new kernel */
      { NULL, NULL, "Acer", "Aspire ES1-533", FALSE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, "Acer", "Aspire ES1-732", FALSE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, "Acer", "Veriton Z4660G", FALSE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, "Acer", "Veriton Z4860G", FALSE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, "Acer", "Veriton Z6860G", FALSE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, "ASUSTeK COMPUTER INC.", "Z550MA", FALSE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, "Endless", "ELT-JWM", FALSE, NULL, NULL, NULL, FALSE, FALSE },
      { NULL, NULL, "Endless", "ELT-JWM", FALSE, NULL, NULL, NULL, TRUE, TRUE },

      /* Read-only in kernel command line args */
      { NULL, NULL, NULL, NULL, FALSE, NULL, NULL, cmdline_not_ro, FALSE, TRUE },
      { NULL, NULL, NULL, NULL, FALSE, NULL, NULL, cmdline_ro_end, FALSE, FALSE },
      { NULL, NULL, NULL, NULL, FALSE, NULL, NULL, cmdline_ro_middle, FALSE, FALSE },
      { NULL, NULL, NULL, NULL, FALSE, NULL, NULL, cmdline_ro_end, TRUE, TRUE },
    };

  if (eos_test_skip_chroot ())
    return;

  /* If the host running the tests is actually aarch64, it will refuse to
   * follow the checkpoint unless @force_follow_checkpoint is set (or there are
   * bugs), so the test has to adapt. */
  g_assert (uname (&uts) == 0);
  host_is_aarch64 = (g_strcmp0 (uts.machine, "aarch64") == 0);

  for (gsize i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const gchar *src_ref = tests[i].src_ref ? tests[i].src_ref : "eos3a";
      const OstreeCollectionRef _src_collection_ref = { (gchar *) "com.endlessm.CollectionId", (gchar *) src_ref };
      const OstreeCollectionRef *src_collection_ref = &_src_collection_ref;
      const gchar *tgt_ref = tests[i].tgt_ref ? tests[i].tgt_ref : "eos4";
      const OstreeCollectionRef _tgt_collection_ref = { (gchar *) "com.endlessm.CollectionId", (gchar *) tgt_ref };
      const OstreeCollectionRef *tgt_collection_ref = &_tgt_collection_ref;
      g_autoptr(GFile) server_root = NULL;
      g_autoptr(EosTestServer) server = NULL;
      g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
      g_autoptr(GError) error = NULL;
      g_autoptr(EosTestSubserver) subserver = NULL;
      g_autoptr(GFile) client_root = NULL;
      g_autoptr(EosTestClient) client = NULL;
      g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
      g_autoptr(GHashTable) leaf_commit_nodes =
        eos_test_subserver_ref_to_commit_new ();
      gboolean has_commit;

      g_test_message ("Test %" G_GSIZE_FORMAT, i);

      /* Create the checkpoint */
      insert_update_refspec_metadata_for_commit (1,
                                                 tgt_ref,
                                                 &additional_metadata_for_commit);

      server_root = g_file_get_child (fixture->tmpdir, "main");
      server = eos_test_server_new_quick (server_root,
                                          default_vendor,
                                          default_product,
                                          src_collection_ref,
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
                                    src_collection_ref,
                                    tests[i].sys_vendor ? tests[i].sys_vendor : default_vendor,
                                    tests[i].product_name ? tests[i].product_name : default_product,
                                    &error);
      g_assert_no_error (error);

      /* Set the client to imitate a machine which may or may not follow the
       * checkpoint. */
      eos_test_client_set_is_split_disk (client, tests[i].is_split_disk);
      eos_test_client_set_uname_machine (client, tests[i].uname_machine);
      eos_test_client_set_cpuinfo (client, tests[i].cpuinfo);
      eos_test_client_set_cmdline (client, tests[i].cmdline);
      eos_test_client_set_force_follow_checkpoint (client, tests[i].force_follow_checkpoint);

      g_hash_table_insert (leaf_commit_nodes,
                           ostree_collection_ref_dup (src_collection_ref),
                           GUINT_TO_POINTER (1));

      /* Also insert a commit (2) for the refspec "REMOTE:TGT_REF". The first
       * time we update, we should only update to commit 1 */
      g_hash_table_insert (leaf_commit_nodes,
                           ostree_collection_ref_dup (tgt_collection_ref),
                           GUINT_TO_POINTER (2));
      eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                                leaf_commit_nodes);
      eos_test_subserver_update (subserver,
                                 &error);
      g_assert_no_error (error);

      /* Now update the client. We stopped making commits on this
       * ref, so it is effectively a "checkpoint" and we should only have
       * the first commit. */
      update_client (fixture, client, NULL);

      eos_test_client_has_commit (client,
                                  default_remote_name,
                                  1,
                                  &has_commit,
                                  &error);
      g_assert_no_error (error);
      g_assert_true (has_commit);

      eos_test_client_has_commit (client,
                                  default_remote_name,
                                  2,
                                  &has_commit,
                                  &error);
      g_assert_no_error (error);
      g_assert_false (has_commit);

      /* Update the client again. Because we had deployed the
       * checkpoint, *if the machine is going to cross the checkpoint*, we
       * should now have the new ref to update on and should
       * have pulled the new commit (we can't assert on anything here, but
       * we can do the next step to figure out what branch we're on). */
      update_client (fixture, client, NULL);

      eos_test_client_has_commit (client,
                                  default_remote_name,
                                  2,
                                  &has_commit,
                                  &error);
      g_assert_no_error (error);

      if (tests[i].expect_checkpoint_followed &&
          (!host_is_aarch64 ||
           tests[i].force_follow_checkpoint ||
           tests[i].uname_machine != NULL))
        g_assert_true (has_commit);
      else
        g_assert_false (has_commit);

      /* Prepare for the next iteration */
      eos_updater_fixture_teardown (fixture, user_data);
      eos_updater_fixture_setup (fixture, user_data);
    }
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  eos_test_add ("/updater/update-refspec-checkpoint",
                NULL,
                test_update_refspec_checkpoint);
  eos_test_add ("/updater/update-refspec-checkpoint-old-ref-deleted",
                NULL,
                test_update_refspec_checkpoint_old_ref_deleted);
  eos_test_add ("/updater/update-refspec-checkpoint-even-if-downgrade",
                NULL,
                test_update_refspec_checkpoint_even_if_downgrade);
  eos_test_add ("/updater/update-refspec-checkpoint-no-collection-ref-server",
                NULL,
                test_update_refspec_checkpoint_no_collection_ref_server);
  eos_test_add ("/updater/update-refspec-checkpoint-no-collection-ref-client",
                NULL,
                test_update_refspec_checkpoint_no_collection_ref_client);
  eos_test_add ("/updater/update-refspec-checkpoint-malformed-checkpoint",
                NULL,
                test_update_refspec_checkpoint_malformed_checkpoint);
  eos_test_add ("/updater/update-refspec-checkpoint-malformed-checkpoint-recovery",
                NULL,
                test_update_refspec_checkpoint_malformed_checkpoint_recovery);
  eos_test_add ("/updater/update-refspec-checkpoint-continue-old-branch",
                NULL,
                test_update_refspec_checkpoint_continue_old_branch);
  eos_test_add ("/updater/update-refspec-checkpoint-continue-old-branch-then-new-branch",
                NULL,
                test_update_refspec_checkpoint_continue_old_branch_then_new_branch);
  eos_test_add ("/updater/update-refspec-checkpoint-ignore-remote",
                NULL,
                test_update_refspec_checkpoint_ignore_remote);
  eos_test_add ("/updater/update-refspec-checkpoint-eos3a-eos4",
                NULL,
                test_update_refspec_checkpoint_eos3a_eos4);

  return g_test_run ();
}
