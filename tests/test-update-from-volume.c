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

#include <test-common/misc-utils.h>
#include <test-common/spawn-utils.h>
#include <test-common/utils.h>

#include <gio/gio.h>

static void
test_update_from_volume (EosUpdaterFixture *fixture,
                         gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client1_root = NULL;
  g_autoptr(GFile) client2_root = NULL;
  g_autoptr(EosTestClient) client1 = NULL;
  g_autoptr(EosTestClient) client2 = NULL;
  g_autoptr(GFile) volume_path = NULL;
  g_autoptr(GFile) volume_ostree_path = NULL;
  g_autoptr(GFile) volume_repo_path = NULL;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(CmdResult) reaped = CMD_RESULT_CLEARED;
  g_autoptr(GPtrArray) cmds = NULL;
  gboolean has_commit;
  DownloadSource volume_source = DOWNLOAD_VOLUME;
  g_autoptr(GPtrArray) override_uris = NULL;

  /* We could get OSTree working by setting OSTREE_BOOTID, but shortly
   * afterwards we hit unsupported syscalls in qemu-user when running in an
   * ARM chroot (for example), so just bail. */
  if (!eos_test_has_ostree_boot_id ())
    {
      g_test_skip ("OSTree will not work without a boot ID");
      return;
    }

  server_root = g_file_get_child (fixture->tmpdir, "main");
  server = eos_test_server_new_quick (server_root,
                                      default_vendor,
                                      default_product,
                                      default_collection_ref,
                                      0,
                                      fixture->gpg_home,
                                      keyid,
                                      default_ostree_path,
                                      &error);
  g_assert_no_error (error);
  g_assert_cmpuint (server->subservers->len, ==, 1u);

  subserver = g_object_ref (EOS_TEST_SUBSERVER (g_ptr_array_index (server->subservers, 0)));
  client1_root = g_file_get_child (fixture->tmpdir, "client1");
  client1 = eos_test_client_new (client1_root,
                                 default_remote_name,
                                 subserver,
                                 default_collection_ref,
                                 default_vendor,
                                 default_product,
                                 &error);
  g_assert_no_error (error);

  g_hash_table_insert (subserver->ref_to_commit,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));
  eos_test_subserver_update (subserver,
                             &error);
  g_assert_no_error (error);

  client2_root = g_file_get_child (fixture->tmpdir, "client2");
  client2 = eos_test_client_new (client2_root,
                                 default_remote_name,
                                 subserver,
                                 default_collection_ref,
                                 default_vendor,
                                 default_product,
                                 &error);
  g_assert_no_error (error);

  volume_path = g_file_get_child (fixture->tmpdir, "volume");
  eos_test_client_prepare_volume (client2,
                                  volume_path,
                                  &error);
  g_assert_no_error (error);

  override_uris = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
  volume_ostree_path = g_file_get_child (volume_path, ".ostree");
  volume_repo_path = g_file_get_child (volume_ostree_path, "repo");
  g_ptr_array_add (override_uris, g_file_get_uri (volume_repo_path));
  eos_test_client_run_updater (client1,
                               &volume_source,
                               1,
                               override_uris,
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

  eos_test_client_reap_updater (client1,
                                &updater_cmd,
                                &reaped,
                                &error);
  g_assert_no_error (error);

  cmds = g_ptr_array_new ();
  g_ptr_array_add (cmds, &reaped);
  g_ptr_array_add (cmds, autoupdater->cmd);
  g_assert_true (cmd_result_ensure_all_ok_verbose (cmds));

  eos_test_client_has_commit (client1,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  eos_test_add ("/updater/update-from-volume", NULL, test_update_from_volume);

  return g_test_run ();
}
