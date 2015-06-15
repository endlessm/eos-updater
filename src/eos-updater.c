/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 * Author: Vivek Dasmohapatra <vivek@etla.org>
 */

#include "eos-updater-generated.h"
#include "eos-updater-util.h"
#include "eos-updater-poll.h"
#include "eos-updater-fetch.h"
#include "eos-updater-apply.h"
#include <ostree.h>
#include <gio/gio.h>
#include <glib.h>

static GDBusObjectManagerServer *manager = NULL;

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  EosObjectSkeleton *object = NULL;
  EosUpdater *updater = NULL;
  OstreeRepo *repo = OSTREE_REPO (user_data);
  GError *error = NULL;
  EosState state;

  gs_free gchar *src = NULL;
  gs_free gchar *ref = NULL;
  gs_free gchar *sum = NULL;

  message ("Acquired a message bus connection\n");

  /* Create a new org.freedesktop.DBus.ObjectManager rooted at /com/endlessm */
  manager = g_dbus_object_manager_server_new ("/com/endlessm");
  object  = eos_object_skeleton_new ("/com/endlessm/Updater");

  /* Make the newly created object export the interface com.endlessm.Updater
     (note that @skeleton takes its own reference to @updater). */
  updater = eos_updater_skeleton_new ();
  eos_object_skeleton_set_updater (object, updater);
  g_object_unref (updater);

  /* Handle the various DBus methods: */
  g_signal_connect (updater, "handle-fetch", G_CALLBACK (handle_fetch), repo);
  g_signal_connect (updater, "handle-poll",  G_CALLBACK (handle_poll), repo);
  g_signal_connect (updater, "handle-apply", G_CALLBACK (handle_apply), repo);

  if (eos_updater_resolve_upgrade (updater, repo, NULL, NULL, &sum, &error))
    {
      eos_updater_set_current_id (updater, sum);
      eos_updater_set_download_size (updater, 0);
      eos_updater_set_downloaded_bytes (updater, 0);
      eos_updater_set_unpacked_size (updater, 0);
      eos_updater_set_error_code (updater, 0);
      eos_updater_set_error_message (updater, "");
      eos_updater_set_update_id (updater, "");
      state = EOS_STATE_READY;
    }
  else
    {
      eos_updater_set_error_code (updater, error->code);
      eos_updater_set_error_message (updater, error->message);
      state = EOS_STATE_ERROR;
    }

  /* We are deliberately not emitting a signal here. This
   * isn't a state change, it's our initial state.
   */
  eos_updater_set_state_changed (updater, state);

  /* Export the object (@manager takes its own reference to @object) */
  g_dbus_object_manager_server_export (manager, G_DBUS_OBJECT_SKELETON (object));
  g_object_unref (object);

  /* Export all objects */
  message ("Exporting objects");
  g_dbus_object_manager_server_set_connection (manager, connection);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  message ("Acquired the name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  message ("Lost the name %s\n", name);
}

gint
main (gint argc, gchar *argv[])
{
  GMainLoop *loop = NULL;
  OstreeRepo *repo = NULL;
  guint id = 0;

  g_set_prgname (argv[0]);

  repo = eos_updater_local_repo ();
  loop = g_main_loop_new (NULL, FALSE);
  id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                       "com.endlessm.Updater",
                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                       on_bus_acquired,
                       on_name_acquired,
                       on_name_lost,
                       repo,
                       NULL);

  g_main_loop_run (loop);

  g_bus_unown_name (id);
  g_main_loop_unref (loop);

  g_object_unref (repo);

  return 0;
}
