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

#include "eos-updater-object.h"
#include "eos-updater-poll-common.h"
#include "eos-updater-poll-volume-dbus.h"
#include "eos-updater-poll-volume.h"

#include <libeos-updater-util/util.h>

typedef struct
{
  EosUpdaterData *data;

  gchar *volume_path;
} VolumeMetadataFetchData;

static VolumeMetadataFetchData *
volume_metadata_fetch_data_new (EosUpdaterData *data,
                                GDBusMethodInvocation *call)
{
  GVariant *parameters = g_dbus_method_invocation_get_parameters (call);
  const gchar *path;
  VolumeMetadataFetchData *volume_fetch_data;

  g_variant_get (parameters, "(s)", &path);
  volume_fetch_data = g_new (VolumeMetadataFetchData, 1);
  volume_fetch_data->data = data;
  volume_fetch_data->volume_path = g_strdup (path);

  return volume_fetch_data;
}

static void
volume_metadata_fetch_data_free (gpointer volume_fetch_data_ptr)
{
  VolumeMetadataFetchData *volume_fetch_data = volume_fetch_data_ptr;

  g_free (volume_fetch_data->volume_path);
  g_free (volume_fetch_data);
}

static void
volume_metadata_fetch (GTask *task,
                       gpointer object,
                       gpointer task_data,
                       GCancellable *cancel)
{
  VolumeMetadataFetchData *volume_fetch_data = task_data;
  g_autoptr(GMainContext) task_context = NULL;
  g_autoptr(EosMetadataFetchData) fetch_data = NULL;
  g_autoptr(GPtrArray) fetchers = NULL;
  g_autoptr(GPtrArray) source_variants = NULL;
  g_autoptr(GArray) download_order = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_autoptr(GVariant) volume_variant = NULL;
  EosUpdaterDownloadSource volume_source = EOS_UPDATER_DOWNLOAD_VOLUME;

  task_context = g_main_context_new ();
  fetch_data = eos_metadata_fetch_data_new (task, volume_fetch_data->data,
                                            task_context);
  fetchers = g_ptr_array_sized_new (1);
  source_variants = g_ptr_array_sized_new (1);
  download_order = g_array_sized_new (FALSE, /* not null-terminated */
                                      FALSE, /* no clearing */
                                      sizeof (EosUpdaterDownloadSource),
                                      1);
  volume_variant = g_variant_ref_sink (g_variant_new_string (volume_fetch_data->volume_path));

  g_ptr_array_add (fetchers, metadata_fetch_from_volume);
  g_ptr_array_add (source_variants, volume_variant);
  g_array_append_val (download_order, volume_source);
  info = run_fetchers (fetch_data,
                       fetchers,
                       source_variants,
                       download_order);

  g_task_return_pointer (task,
                         (info != NULL) ? g_object_ref (info) : NULL,
                         g_object_unref);
}

gboolean
handle_poll_volume (EosUpdater            *updater,
                    GDBusMethodInvocation *call,
                    gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  EosUpdaterState state = eos_updater_get_state (updater);

  switch (state)
    {
      case EOS_UPDATER_STATE_READY:
      case EOS_UPDATER_STATE_UPDATE_AVAILABLE:
      case EOS_UPDATER_STATE_UPDATE_READY:
      case EOS_UPDATER_STATE_ERROR:
        break;
      case EOS_UPDATER_STATE_NONE:
      case EOS_UPDATER_STATE_POLLING:
      case EOS_UPDATER_STATE_FETCHING:
      case EOS_UPDATER_STATE_APPLYING_UPDATE:
      case EOS_UPDATER_STATE_UPDATE_APPLIED:
      default:
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call PollVolume() while in state %s",
          eos_updater_state_to_string (state));
        return TRUE;
    }

  eos_updater_clear_error (updater, EOS_UPDATER_STATE_POLLING);
  task = g_task_new (updater, NULL, metadata_fetch_finished, user_data);
  g_task_set_task_data (task,
                        volume_metadata_fetch_data_new (user_data, call),
                        volume_metadata_fetch_data_free);
  g_task_run_in_thread (task, volume_metadata_fetch);

  eos_updater_complete_poll_volume (updater, call);
  return TRUE;
}
