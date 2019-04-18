/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Kinvolk GmbH
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
  g_auto(GStrv) deployment_ids = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *old_commit_id = NULL;
  gchar *dot_ptr;
  g_autoptr(GFile) client_repo = NULL;
  g_auto(CmdResult) ref_created = CMD_RESULT_CLEARED;

  g_assert_nonnull (data);
  g_assert_nonnull (data->client);
  g_assert_nonnull (ref);

  eos_test_client_get_deployments (data->client,
                                   default_remote_name,
                                   &deployment_ids,
                                   &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_strv_length (deployment_ids), ==, 2);

  /* Index 1 is always guaranteed to be the old deployment.
   */
  old_commit_id = g_strdup (deployment_ids[1]);
  dot_ptr = strchr (old_commit_id, '.');
  g_assert_nonnull (dot_ptr);
  *dot_ptr = '\0';

  client_repo = eos_test_client_get_repo (data->client);
  ostree_ref_create (client_repo,
                     ref,
                     old_commit_id,
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
delete_ref (EtcData *data,
            const gchar *ref)
{
  g_autoptr(GFile) client_repo = NULL;
  g_auto(CmdResult) ref_deleted = CMD_RESULT_CLEARED;
  g_autoptr(GError) error = NULL;

  g_assert_nonnull (data);
  g_assert_nonnull (ref);
  g_assert_nonnull (data->client);

  client_repo = eos_test_client_get_repo (data->client);
  ostree_ref_delete (client_repo, ref, &ref_deleted, &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (&ref_deleted));
}

/* Runs the "ostree prune" with a mode that does no pruning, but
 * prints what items would be pruned if it could do it. Then remove
 * some of the dirtree objects from that list (at most 3).
 */
static void
delete_some_old_dirtree_objects (EtcData *data)
{
  g_autoptr(GFile) client_repo = NULL;
  OstreePruneFlags prune_flags = OSTREE_PRUNE_REFS_ONLY | OSTREE_PRUNE_NO_PRUNE | OSTREE_PRUNE_VERBOSE;
  g_auto(CmdResult) listed = CMD_RESULT_CLEARED;
  g_autoptr(GError) error = NULL;
  g_autoptr(GRegex) gregex = NULL;
  g_autoptr(GMatchInfo) match_info = NULL;
  guint removed;

  g_assert_nonnull (data);
  g_assert_nonnull (data->client);

  client_repo = eos_test_client_get_repo (data->client);
  ostree_prune (client_repo, prune_flags, 0, &listed, &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (&listed));

  gregex = g_regex_new ("Pruning\\s+unneeded\\s+object\\s+([0-9a-zA-Z]{64}.dirtree)",
                        /* compile flags: */ 0,
                        /* match flags: */ 0,
                        &error);
  g_assert_no_error (error);
  g_regex_match (gregex,
                 listed.standard_error,
                 /* match flags: */ 0,
                 &match_info);
  for (removed = 0;
       g_match_info_matches (match_info) && (removed < 3);
       ++removed)
    {
      g_autofree gchar *object = g_match_info_fetch (match_info,
                                                     /* match group: */ 1);
      etc_delete_object (client_repo, object);
      g_match_info_next (match_info, &error);
      g_assert_no_error (error);
    }
  g_assert_cmpint (removed, >, 0);
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

  if (eos_test_skip_chroot ())
    return;

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
   * thus a candidate for pruning. This step does not invoke pruning.
   */
  delete_ref (data, save_ref_name);
  /* We remove some dirtree objects referenced by the old commit, so
   * we can trigger an error during pruning.
   */
  delete_some_old_dirtree_objects (data);
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
