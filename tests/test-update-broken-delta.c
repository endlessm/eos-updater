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

#include <test-common/misc-utils.h>
#include <test-common/spawn-utils.h>
#include <test-common/ostree-spawn.h>
#include <test-common/eos-test-utils.h>
#include <test-common/eos-test-convenience.h>

#include <gio/gio.h>
#include <locale.h>
#include <string.h>

/* Finds the big file in the deployment to figure out its checksum, so
 * it can then remove it from client's repository.
 */
static void
delete_big_file_object_from_client_repo (EtcData *data)
{
  const gchar *bigfile_path = eos_test_client_get_big_file_path ();
  const gchar *paths[] =
    {
      bigfile_path,
      NULL
    };
  g_autoptr(GFile) client_repo = NULL;
  OstreeLsFlags flags = OSTREE_LS_CHECKSUM;
  g_auto(CmdResult) listed = CMD_RESULT_CLEARED;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *escaped_bigfile_path = NULL;
  g_autofree gchar *regex = NULL;
  g_autoptr(GRegex) gregex = NULL;
  gboolean matched;
  g_autoptr(GMatchInfo) match_info = NULL;
  g_autofree gchar *checksum = NULL;
  g_autofree gchar *object = NULL;

  g_assert_nonnull (data);
  g_assert_nonnull (data->client);

  client_repo = eos_test_client_get_repo (data->client);
  ostree_ls (client_repo,
             flags,
             default_ref,
             paths,
             &listed,
             &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (&listed));

  escaped_bigfile_path = g_regex_escape_string (bigfile_path, -1);
  regex = g_strdup_printf ("\\s+([0-9a-zA-Z]{64})\\s+%s", escaped_bigfile_path);
  gregex = g_regex_new (regex,
                        /* compile flags: */ 0,
                        /* match flags: */ 0,
                        &error);
  g_assert_no_error (error);
  matched = g_regex_match (gregex,
                           listed.standard_output,
                           /* match flags: */ 0,
                           &match_info);
  g_assert_true (matched);
  checksum = g_match_info_fetch (match_info,
                                 /* match group: */ 1);
  object = g_strdup_printf ("%s.file", checksum);
  etc_delete_object (client_repo, object);
}

/* Corrupt a repository on the client side, so that using static delta
 * files is impossible and make sure that eos-updater can cope with
 * it.
 */
static void
test_update_broken_delta (EosUpdaterFixture *fixture,
                          gconstpointer user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;

  g_test_bug ("T17183");

  /* We could get OSTree working by setting OSTREE_BOOTID, but shortly
   * afterwards we hit unsupported syscalls in qemu-user when running in an
   * ARM chroot (for example), so just bail. */
  if (!eos_test_has_ostree_boot_id ())
    {
      g_test_skip ("OSTree will not work without a boot ID");
      return;
    }

  etc_data_init (data, fixture);
  /* Create and set up the server with the commit 0.
   */
  etc_set_up_server (data);
  /* Create and set up the client, that pulls the update from the
   * server, so it should have also a commit 0 and a deployment based
   * on this commit.
   */
  etc_set_up_client_synced_to_server (data);
  /* Update the server, so it has a new commit (1), and the delta
   * files between commits 0 and 1.
   */
  etc_update_server (data, 1);
  /* Delete an object in repository objects directory which serves as
   * a base for generating a new version of the object with the static
   * delta files.
   */
  delete_big_file_object_from_client_repo (data);
  /* Try to update the client - during the "fetch" step, it should
   * fallback to fetching objects instead of using delta files.
   */
  etc_update_client (data);
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("https://phabricator.endlessm.com/");

  eos_test_add ("/updater/update-cleanup-broken-delta", NULL, test_update_broken_delta);

  return g_test_run ();
}
