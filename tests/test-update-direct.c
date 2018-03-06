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
 *  - Joaquim Rocha <jrocha@endlessm.com>
 */

#include <test-common/misc-utils.h>
#include <test-common/spawn-utils.h>
#include <test-common/utils.h>
#include <libeos-updater-util/types.h>

#include <gio/gio.h>
#include <locale.h>

#include "eos-updater-generated.h"

typedef struct
{
  GMainLoop *loop;
  gboolean *cancelled_states;
  guint cancelled_error_count;
  guint cancel_calls_count;
} TestCancelHelper;

static gboolean
cancel_update (EosUpdater *updater,
               TestCancelHelper *helper)
{
  gboolean should_succeed;
  EosUpdaterState state = eos_updater_get_state (updater);
  g_autoptr(GError) local_error = NULL;
  const gchar *state_str = eos_updater_state_to_string (state);

  switch (state)
    {
      case EOS_UPDATER_STATE_NONE:
      case EOS_UPDATER_STATE_ERROR:
      case EOS_UPDATER_STATE_READY:
      case EOS_UPDATER_STATE_UPDATE_AVAILABLE:
      case EOS_UPDATER_STATE_UPDATE_READY:
      case EOS_UPDATER_STATE_UPDATE_APPLIED:
        should_succeed = FALSE;
        break;
      case EOS_UPDATER_STATE_POLLING:
      case EOS_UPDATER_STATE_FETCHING:
      case EOS_UPDATER_STATE_APPLYING_UPDATE:
      default:
        should_succeed = TRUE;
        break;
    }

  g_debug ("Trying to cancel state %s", state_str);

  helper->cancelled_states[state] = TRUE;
  eos_updater_call_cancel_sync (updater, NULL, &local_error);

  if (should_succeed)
    {
      g_assert_no_error (local_error);
      ++helper->cancel_calls_count;
      g_debug ("Cancelled state %s", state_str);
    }
  else
    {
      g_assert_nonnull (local_error);
      g_debug ("Error cancelling %s: %s", state_str, local_error->message);
    }

  return should_succeed;
}

static void
updater_state_changed_cb (EosUpdater *updater,
                          GParamSpec *pspec,
                          gpointer data)
{
  EosUpdaterState state = eos_updater_get_state (updater);
  TestCancelHelper *helper = (TestCancelHelper *) data;
  const gchar *state_str = eos_updater_state_to_string (state);
  const gchar *error_name = NULL;
  const gchar *error_message = NULL;

  /* we call the Cancel() method from the EOS Updater on every
   * state once (it will either perform the cancel or return an error
   * depending on the state); when a cancel has been called on a state already
   * (or it gets an error), we call the next step in the update logic */
  if (!helper->cancelled_states[state] && cancel_update (updater, helper))
    return;

  g_debug ("State changed %s", state_str);
  switch (state)
    {
      case EOS_UPDATER_STATE_ERROR:
        error_name = eos_updater_get_error_name (updater);
        error_message = eos_updater_get_error_message (updater);
        g_debug ("Error name: %s", error_name);
        g_debug ("Error message: %s", error_message);

        if (g_strcmp0 (error_name,
                       "com.endlessm.Updater.Error.Cancelled") == 0)
          ++helper->cancelled_error_count;
        /* fall through */
      case EOS_UPDATER_STATE_NONE:
      case EOS_UPDATER_STATE_READY:
        eos_updater_call_poll (updater, NULL, NULL, NULL);
        break;
      case EOS_UPDATER_STATE_UPDATE_AVAILABLE:
        {
          g_auto(GVariantDict) options_dict = G_VARIANT_DICT_INIT (NULL);
          g_variant_dict_insert (&options_dict, "force", "b", TRUE);

          eos_updater_call_fetch_full (updater, g_variant_dict_end (&options_dict),
                                       NULL, NULL, NULL);
          break;
        }
      case EOS_UPDATER_STATE_UPDATE_READY:
        eos_updater_call_apply (updater, NULL, NULL, NULL);
        break;
      case EOS_UPDATER_STATE_UPDATE_APPLIED:
        g_main_loop_quit (helper->loop);
        break;
      case EOS_UPDATER_STATE_POLLING:
      case EOS_UPDATER_STATE_FETCHING:
      case EOS_UPDATER_STATE_APPLYING_UPDATE:
      default:
        /* let it go until the next state change occurs */
        break;
    }
}

/* Tests calling Cancel() on every EOS updater state; when the states can be
 * indeed cancelled, the update is run again without being cancelled this time
 * so the update proceeds. */
static void
test_cancel_update (EosUpdaterFixture *fixture,
                    gconstpointer user_data)
{
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(EosUpdater) updater = NULL;
  g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  DownloadSource main_source = DOWNLOAD_MAIN;
  gboolean has_commit;
  gulong state_change_handler = 0;
  gboolean cancelled_states[EOS_UPDATER_STATE_LAST + 1] = { FALSE };
  TestCancelHelper helper = { loop, cancelled_states, 0, 0 };

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
                                      NULL, NULL, NULL,
                                      &local_error);
  g_assert_no_error (local_error);
  g_assert_cmpuint (server->subservers->len, ==, 1u);

  subserver = g_object_ref (EOS_TEST_SUBSERVER (g_ptr_array_index (server->subservers, 0)));
  client_root = g_file_get_child (fixture->tmpdir, "client");
  client = eos_test_client_new (client_root,
                                default_remote_name,
                                subserver,
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                &local_error);
  g_assert_no_error (local_error);

  g_hash_table_insert (subserver->ref_to_commit,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));
  eos_test_subserver_update (subserver,
                             &local_error);
  g_assert_no_error (local_error);

  eos_test_client_run_updater (client,
                               &main_source,
                               1,
                               NULL,
                               NULL,
                               &local_error);
  g_assert_no_error (local_error);

  /* the proxy will use the DBus connection set up by the test */
  updater = eos_updater_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                "com.endlessm.Updater",
                                                "/com/endlessm/Updater",
                                                NULL,
                                                &local_error);
  g_assert_no_error (local_error);

  state_change_handler = g_signal_connect (updater, "notify::state",
                                           G_CALLBACK (updater_state_changed_cb),
                                           &helper);

  /* start the state changes */
  updater_state_changed_cb (updater, NULL, &helper);

  g_main_loop_run (loop);

  g_signal_handler_disconnect (updater, state_change_handler);

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &local_error);
  g_assert_no_error (local_error);
  g_assert_true (has_commit);

  g_assert_cmpuint (helper.cancelled_error_count, ==, helper.cancel_calls_count);
}

int
main (int argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  eos_test_add ("/updater/cancel-update", NULL, test_cancel_update);

  return g_test_run ();
}
