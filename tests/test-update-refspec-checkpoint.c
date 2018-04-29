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

const gchar *next_ref = "REFv2";
const gchar *next_refspec = "REMOTE:REFv2";
const OstreeCollectionRef _next_collection_ref = { (gchar *) "com.endlessm.CollectionId", (gchar *) "REFv2" };
const OstreeCollectionRef *next_collection_ref = &_next_collection_ref;

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
 * deployed commit, tells the updater which refspec to pull from (as opposed
 * to the currently booted one */
static void
insert_update_refspec_metadata_for_commit (guint         commit,
                                           const gchar  *new_refspec,
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
                       create_checkpoint_target_metadata (new_refspec));
}

static void
update_client (EosUpdaterFixture *fixture,
               EosTestClient     *client)
{
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_autoptr(GPtrArray) cmds = NULL;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(CmdResult) reaped = CMD_RESULT_CLEARED;
  g_autoptr(GError) error = NULL;

  eos_test_client_run_updater (client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               &error);
  g_assert_no_error (error);

  autoupdater_root = g_file_get_child (fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
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
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  gboolean has_commit;

  /* We could get OSTree working by setting OSTREE_BOOTID, but shortly
   * afterwards we hit unsupported syscalls in qemu-user when running in an
   * ARM chroot (for example), so just bail. */
  if (!eos_test_has_ostree_boot_id ())
    {
      g_test_skip ("OSTree will not work without a boot ID");
      return;
    }

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
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

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
  update_client (fixture, client);

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

  /* Update the client client again. Because we had deployed the
   * checkpoint, we should now have the new ref to update on and should
   * have pulled the new commit. */
  update_client (fixture, client);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              2,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  eos_test_add ("/updater/update-refspec-checkpoint",
                NULL,
                test_update_refspec_checkpoint);

  return g_test_run ();
}
