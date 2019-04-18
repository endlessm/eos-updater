/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <gio/gio.h>
#include <libeos-updater-util/util.h>
#include <locale.h>
#include <test-common/convenience.h>


/* Delete the commit object representing the currently deployed commit, and try
 * to do an update. The update should succeed (but should warn about the missing
 * commit object). */
static void
test_update_missing_deployed_commit (EosUpdaterFixture *fixture,
                                     gconstpointer      user_data)
{
  g_auto(EtcData) real_data = { NULL, };
  EtcData *data = &real_data;

  g_test_bug ("T22805");

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
  /* Update the server, so it has a new commit (1), and the delta
   * files between commits 0 and 1.
   */
  etc_update_server (data, 1);
  /* Delete the currently deployed commit object (and all other commit objects)
   * from the client.
   */
  etc_delete_all_client_commits (data);
  /* Try to update the client. It should succeed, but should warn about the
   * currently deployed commit being missing.
   */
  etc_update_client_with_warnings (data,
                                   "Error loading current commit ‘*’ to check "
                                   "if ‘*’ is an update (assuming it is): No "
                                   "such metadata object *.commit");
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
  g_test_bug_base ("https://phabricator.endlessm.com/");

  eos_test_add ("/updater/update-missing-deployed-commit", NULL,
                test_update_missing_deployed_commit);

  return g_test_run ();
}
