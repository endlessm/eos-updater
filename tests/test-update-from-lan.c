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


#include "misc-utils.h"
#include "spawn-utils.h"
#include "eos-test-utils.h"

#include <gio/gio.h>
#include <locale.h>

static void
test_update_from_lan (EosUpdaterFixture *fixture,
                      gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GPtrArray) lan_servers = NULL;
  g_autoptr(GPtrArray) lan_server_cmds = NULL;
  guint idx;
  guint lan_server_count = 4;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(CmdResult) reaped = CMD_RESULT_CLEARED;
  g_autoptr(GPtrArray) cmds = NULL;
  g_autoptr(GPtrArray) cmds_to_free = NULL;
  gboolean has_commit;
  DownloadSource lan_source = DOWNLOAD_LAN;
  g_autoptr(GVariant) lan_source_variant = NULL;

  g_test_message ("Setting up server");

  server_root = g_file_get_child (fixture->tmpdir, "main");
  server = eos_test_server_new_quick (server_root,
                                      lan_server_count + 1,
                                      default_vendor,
                                      default_product,
                                      FALSE,
                                      default_ref,
                                      0,
                                      fixture->gpg_home,
                                      keyid,
                                      default_ostree_path,
                                      &error);
  g_assert_no_error (error);
  g_assert_cmpuint (server->subservers->len, ==, 1u);

  g_test_message ("Setting up client");

  subserver = g_object_ref (EOS_TEST_SUBSERVER (g_ptr_array_index (server->subservers, 0)));
  client_root = g_file_get_child (fixture->tmpdir, "client");
  client = eos_test_client_new (client_root,
                                default_remote_name,
                                subserver,
                                default_ref,
                                default_vendor,
                                default_product,
                                &error);
  g_assert_no_error (error);

  lan_servers = object_array_new ();
  lan_server_cmds = g_ptr_array_new_with_free_func ((GDestroyNotify)cmd_async_result_free);
  for (idx = 0; idx < lan_server_count; ++idx)
    {
      g_autofree gchar *dir_name = g_strdup_printf ("lan_server_%u", idx);
      g_autoptr(GFile) lan_server_root = g_file_get_child (fixture->tmpdir, dir_name);
      g_autoptr(EosTestClient) lan_server = NULL;
      g_autoptr(GKeyFile) definition = NULL;
      g_autoptr(CmdAsyncResult) server_cmd = NULL;

      g_test_message ("Updating subserver %u", idx);

      g_date_time_unref (subserver->head_commit_timestamp);
      subserver->head_commit_timestamp = days_ago (lan_server_count - idx);
      g_hash_table_insert (subserver->ref_to_commit,
                           g_strdup (default_ref),
                           GUINT_TO_POINTER (1 + idx));
      eos_test_subserver_update (subserver,
                                 &error);
      g_assert_no_error (error);

      g_test_message ("Setting up subserver client %u", idx);

      lan_server = eos_test_client_new (lan_server_root,
                                        default_remote_name,
                                        subserver,
                                        default_ref,
                                        default_vendor,
                                        default_product,
                                        &error);
      g_assert_no_error (error);

      g_test_message ("Updating eos-update-server for client %u", idx);

      server_cmd = g_new0 (CmdAsyncResult, 1);
      eos_test_client_run_update_server (lan_server,
                                         server_cmd,
                                         &definition,
                                         &error);
      g_assert_no_error (error);

      eos_test_client_store_definition (client,
                                        dir_name,
                                        definition,
                                        &error);
      g_assert_no_error (error);

      g_ptr_array_add (lan_servers, g_steal_pointer (&lan_server));
      g_ptr_array_add (lan_server_cmds, g_steal_pointer (&server_cmd));
    }

  g_test_message ("Running updater");

  eos_test_client_run_updater (client,
                               &lan_source,
                               &lan_source_variant,
                               1,
                               &updater_cmd,
                               &error);
  g_assert_no_error (error);

  g_test_message ("Running autoupdater apply step");

  autoupdater_root = g_file_get_child (fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  g_test_message ("Reaping autoupdater");

  eos_test_client_reap_updater (client,
                                &updater_cmd,
                                &reaped,
                                &error);
  g_assert_no_error (error);

  g_test_message ("Removing update server quit files");

  for (idx = 0; idx < lan_server_cmds->len; ++idx)
    {
      EosTestClient *lan_server = g_ptr_array_index (lan_servers, idx);

      eos_test_client_remove_update_server_quit_file (lan_server, &error);
      g_assert_no_error (error);
    }

  g_test_message ("Reaping servers");

  cmds = g_ptr_array_new ();
  cmds_to_free = g_ptr_array_new_with_free_func ((GDestroyNotify)cmd_result_free);
  for (idx = 0; idx < lan_server_cmds->len; ++idx)
    {
      g_autoptr(CmdResult) reaped_server = g_new0(CmdResult, 1);
      EosTestClient *lan_server = g_ptr_array_index (lan_servers, idx);
      CmdAsyncResult *lan_server_cmd = g_ptr_array_index (lan_server_cmds, idx);

      eos_test_client_wait_for_update_server (lan_server,
                                              lan_server_cmd,
                                              reaped_server,
                                              &error);
      g_assert_no_error (error);
      g_ptr_array_add (cmds_to_free, reaped_server);
      g_ptr_array_add (cmds, g_steal_pointer (&reaped_server));
    }

  g_ptr_array_add (cmds, &reaped);
  g_ptr_array_add (cmds, autoupdater->cmd);
  g_assert_true (cmd_result_ensure_all_ok_verbose (cmds));

  eos_test_client_has_commit (client,
                              default_remote_name,
                              lan_server_count,
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

  eos_test_add ("/updater/update-from-lan", NULL, test_update_from_lan);

  return g_test_run ();
}
