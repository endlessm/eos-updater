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

/* Functions here implement the common actions the test implementer
 * may want to execute (like set up a server or a client or update
 * either one). These are of a higher level than the functions in
 * `utils.h`.
 */

#include <string.h>
#include <test-common/convenience.h>

/* This initializes the EtcData structure. In the beginning, nothing
 * is available. Use etc_set_up_server() and
 * etc_set_up_client_synced_to_server() to get the server and client
 * fields filled.
 */
void
etc_data_init (EtcData *data,
               EosUpdaterFixture *fixture)
{
  g_assert_nonnull (data);
  g_assert_nonnull (fixture);

  data->fixture = fixture;
  data->server = NULL;
  data->subserver = NULL;
  data->client = NULL;
}

/* Clear all the fields, but do not free the passed data pointer.
 */
void
etc_data_clear (EtcData *data)
{
  if (data == NULL)
    return;

  data->fixture = NULL;
  g_clear_object (&data->server);
  g_clear_object (&data->subserver);
  g_clear_object (&data->client);
}

/* Set up a server with a single subserver with the default vendor,
 * product and ostree path. The subserver will contain one commit
 * (0). This sets the server and subserver fields in data.
 */
void
etc_set_up_server (EtcData *data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autofree gchar *keyid = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_nonnull (data);
  g_assert_nonnull (data->fixture);
  g_assert_null (data->server);
  g_assert_null (data->subserver);

  server_root = g_file_get_child (data->fixture->tmpdir, "main");
  keyid = get_keyid (data->fixture->gpg_home);
  data->server = eos_test_server_new_quick (server_root,
                                            default_vendor,
                                            default_product,
                                            default_collection_ref,
                                            0,
                                            data->fixture->gpg_home,
                                            keyid,
                                            default_ostree_path,
                                            &error);
  g_assert_no_error (error);
  g_assert_cmpuint (data->server->subservers->len, ==, 1u);

  data->subserver = g_object_ref (EOS_TEST_SUBSERVER (g_ptr_array_index (data->server->subservers, 0)));
}

/* Set up a client, that is in sync with the server. Needs to be
 * called after the server is set up. This sets the client field in
 * data.
 */
void
etc_set_up_client_synced_to_server (EtcData *data)
{
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_nonnull (data);
  g_assert_nonnull (data->fixture);
  g_assert_nonnull (data->server);
  g_assert_nonnull (data->subserver);
  g_assert_null (data->client);

  client_root = g_file_get_child (data->fixture->tmpdir, "client");
  data->client = eos_test_client_new (client_root,
                                      default_remote_name,
                                      data->subserver,
                                      default_collection_ref,
                                      default_vendor,
                                      default_product,
                                      &error);
  g_assert_no_error (error);
}

/* Update the server to the new commit.
 */
void
etc_update_server (EtcData *data,
                   guint commit)
{
  g_autoptr(GError) error = NULL;
  guint current_commit;

  g_assert_nonnull (data);
  g_assert_nonnull (data->subserver);

  current_commit = GPOINTER_TO_UINT (g_hash_table_lookup (data->subserver->ref_to_commit, default_collection_ref));
  g_assert_cmpint (current_commit, <, commit);

  g_hash_table_insert (data->subserver->ref_to_commit,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (commit));
  eos_test_subserver_update (data->subserver,
                             &error);
  g_assert_no_error (error);
}

/* Pulls the updates from the server using eos-updater and autoupdater.
 */
void
etc_update_client (EtcData *data)
{
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) autoupdater_root = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(CmdResult) reaped_updater = CMD_RESULT_CLEARED;
  g_autoptr(GPtrArray) cmds = NULL;
  gboolean has_commit = FALSE;

  g_assert_nonnull (data);
  g_assert_nonnull (data->client);
  g_assert_nonnull (data->fixture);

  // update the client
  eos_test_client_run_updater (data->client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               &error);
  g_assert_no_error (error);

  autoupdater_root = g_file_get_child (data->fixture->tmpdir, "autoupdater");
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          1,
                                          TRUE,
                                          &error);
  g_assert_no_error (error);

  eos_test_client_reap_updater (data->client,
                                &updater_cmd,
                                &reaped_updater,
                                &error);
  g_assert_no_error (error);

  cmds = g_ptr_array_sized_new (2);
  g_ptr_array_add (cmds, &reaped_updater);
  g_ptr_array_add (cmds, autoupdater->cmd);
  g_assert_true (cmd_result_ensure_all_ok_verbose (cmds));

  eos_test_client_has_commit (data->client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  g_assert_true (has_commit);
}

/* Deletes an object from a repositories' objects directory. The repo
 * should be a GFile pointing to the ostree repository, and the object
 * should describe what will be removed.  The object string should
 * look like a filename formatted as <HASH>.<TYPE>, where <HASH> is 64
 * characters of the object checksum and <TYPE> should describe an
 * ostree object type (dirtree, dirmeta, commit, file, …).
 */
void
etc_delete_object (GFile *repo,
                   const gchar *object)
{
  g_autoptr(GFile) objects_dir = NULL;
  gchar prefix[3] = { '\0', '\0', '\0' };
  g_autofree gchar *rest = NULL;
  g_autoptr(GFile) prefix_dir = NULL;
  g_autoptr(GFile) object_file = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_nonnull (repo);
  g_assert_nonnull (object);
  g_assert_true (strlen (object) > 64);

  objects_dir = g_file_get_child (repo, "objects");
  prefix[0] = object[0];
  prefix[1] = object[1];
  rest = g_strdup (object + 2);
  prefix_dir = g_file_get_child (objects_dir, prefix);
  object_file = g_file_get_child (prefix_dir, rest);
  g_file_delete (object_file, NULL, &error);
  g_assert_no_error (error);
}
