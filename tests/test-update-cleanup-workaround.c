/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Kinvolk GmbH
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

#include <test-common/convenience.h>
#include <test-common/misc-utils.h>
#include <test-common/ostree-spawn.h>
#include <test-common/spawn-utils.h>
#include <test-common/utils.h>

#include <gio/gio.h>
#include <locale.h>
#include <string.h>

/* Finds an old deployment to figure out the commit id on which the
 * deployment was based on and creates a ref that references the
 * commit id.
 */
static void
save_old_deployment_commit_in_ref (EtcData *data,
                                   const gchar *ref)
{
  g_autoptr(GFile) sysroot_dir = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autoptr(GPtrArray) deployments = NULL;
  g_autoptr(GError) error = NULL;
  OstreeDeployment *old_deployment = NULL;
  g_autoptr(GFile) client_repo = NULL;
  g_auto(CmdResult) ref_created = CMD_RESULT_CLEARED;

  g_assert_nonnull (data);
  g_assert_nonnull (data->client);
  g_assert_nonnull (ref);

  sysroot_dir = g_file_get_child (data->client->root, "sysroot");
  sysroot = ostree_sysroot_new (sysroot_dir);
  ostree_sysroot_load (sysroot, NULL, &error);
  g_assert_no_error (error);

  deployments = ostree_sysroot_get_deployments (sysroot);
  g_assert_cmpint (deployments->len, ==, 2);

  /* Index 1 is always guaranteed to be the old deployment.
   */
  old_deployment = g_ptr_array_index (deployments, 1);

  client_repo = eos_test_client_get_repo (data->client);
  ostree_ref_create (client_repo,
                     ref,
                     ostree_deployment_get_csum (old_deployment),
                     &ref_created,
                     &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (&ref_created));
}

static void
undeploy_old_deployment (EtcData *data)
{
  g_auto(CmdResult) undeployed = CMD_RESULT_CLEARED;
  g_autoptr(GFile) client_sysroot = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_nonnull (data);
  g_assert_nonnull (data->client);

  client_sysroot = eos_test_client_get_sysroot (data->client);
  ostree_undeploy (client_sysroot,
                   1,
                   &undeployed,
                   &error);

  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (&undeployed));
}

static void
delete_ref_and_some_old_dirtree_objects (EtcData     *data,
                                         const gchar *ref)
{
  g_autoptr(GFile) client_repo = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_nonnull (data);
  g_assert_nonnull (ref);
  g_assert_nonnull (data->client);

  client_repo = eos_test_client_get_repo (data->client);

  g_autoptr(OstreeRepo) repo = ostree_repo_new (client_repo);
  ostree_repo_open (repo, NULL, &error);
  g_assert_no_error (error);

  g_autoptr(GFile) root = NULL;
  ostree_repo_read_commit (repo, ref, &root, NULL /* out_commit */, (GCancellable *) NULL, &error);
  g_assert_no_error (error);

  /* Delete a few dirtree objects that are unique to the ref we are about to
   * delete and so will become eligible for pruning as a result of deleting the
   * ref.
   *
   * This relies on the way the test trees are constructed: for each commit N,
   * files at for-all-commits/commit{0..N}.dir/{a,b,c}/{x,y,z}.N are created,
   * meaning those directories' contents differ and so the dirtree checksum is
   * unique to each commit.
   */
  const char *changed_paths[] = {
    /* TODO: why don't these dirtrees exist when I go to delete them? */
    /* "for-all-commits/commit0.dir/a", */
    /* "for-all-commits/commit0.dir/b", */
    "for-all-commits/commit0.dir",
  };
  for (size_t i = 0; i < G_N_ELEMENTS (changed_paths); i++)
    {
      g_autoptr(GFile) dirtree_file = g_file_resolve_relative_path (root, changed_paths[i]);
      ostree_repo_file_ensure_resolved (OSTREE_REPO_FILE (dirtree_file), &error);
      g_assert_no_error (error);

      const gchar *dirtree_checksum = ostree_repo_file_tree_get_contents_checksum (OSTREE_REPO_FILE (dirtree_file));
      g_autofree gchar *dirtree_object_name = g_strconcat (dirtree_checksum, ".dirtree", NULL);
      g_message ("deleting %s %s\n", changed_paths[i], dirtree_object_name);
      etc_delete_object (client_repo, dirtree_object_name);
    }

  /* Now delete the ref. This doesn't prune the repository. */
  ostree_repo_set_ref_immediate (repo, NULL /* remote */, ref, NULL /* checksum */, (GCancellable *) NULL, &error);
  g_assert_no_error (error);
}

/* Corrupt a repository on the client side, so that pruning may fail
 * and make sure that eos-updater can cope with it.
 */
static void
test_update_cleanup_workaround (EosUpdaterFixture *fixture,
                                gconstpointer user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;
  const gchar *save_ref_name = "save-old-commit";

  g_test_bug ("T16958");

  etc_data_init (data, fixture);
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
  /* We save the commit used for the old deployment in some temporary
   * ref, so the contents of the commit will not be pruned, when we
   * undeploy the old deployment in the followup step, because the
   * commit will still be referenced by our temporary ref.
   */
  save_old_deployment_commit_in_ref (data, save_ref_name);
  /* Remove old deployment, so the only thing that references the old
   * commit is our temporary ref. This also performs pruning, but here
   * it should be a noop.
   */
  undeploy_old_deployment (data);
  /* Remove the temporary ref, so the commit becomes unreferenced and
   * thus a candidate for pruning; and remove some dirtree objects referenced
   * by the old commit, so we can trigger an error during pruning.
   */
  delete_ref_and_some_old_dirtree_objects (data, save_ref_name);
  /* Let's advertise another update on the server.
   */
  etc_update_server (data, 2);
  /* Try to update the client - the final, "apply", step should emit a
   * warning about an error that happened during pruning, but
   * otherwise the operation should be successful.
   */
  etc_update_client (data);
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
  g_test_bug_base ("https://phabricator.endlessm.com/");

  eos_test_add ("/updater/update-cleanup-workaround", NULL, test_update_cleanup_workaround);

  return g_test_run ();
}
