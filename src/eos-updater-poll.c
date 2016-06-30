/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 * Authors: Vivek Dasmohapatra <vivek@etla.org>
 *          Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include "config.h"

#include "eos-updater-data.h"
#include "eos-updater-poll-common.h"
#include "eos-updater-poll-lan.h"
#include "eos-updater-poll-main.h"
#include "eos-updater-poll.h"

#include "eos-util.h"

#ifdef HAS_EOSMETRICS_0

#include <eosmetrics/eosmetrics.h>

#endif /* HAS_EOSMETRICS_0 */

/*
 * Records which branch will be used by the updater. The payload is a 4-tuple
 * of 3 strings and boolean: vendor name, product ID, selected OStree ref, and
 * whether the machine is on hold
 */
static const gchar *const EOS_UPDATER_BRANCH_SELECTED = "99f48aac-b5a0-426d-95f4-18af7d081c4e";
static const gchar *const CONFIG_FILE_PATH = "/etc/eos-updater-daemon.conf";
static const gchar *const DOWNLOAD_GROUP = "Download";
static const gchar *const ORDER_KEY = "Order";
static const gchar *const order_key_str[] = {
  "main",
  "lan"
};

G_STATIC_ASSERT (G_N_ELEMENTS (order_key_str) == EOS_UPDATER_DOWNLOAD_N_SOURCES);

static gboolean
string_to_download_source (const gchar *str,
                           EosUpdaterDownloadSource *source,
                           GError **error)
{
  EosUpdaterDownloadSource idx;

  g_return_val_if_fail (str != NULL, FALSE);
  g_return_val_if_fail (source != NULL, FALSE);

  for (idx = EOS_UPDATER_DOWNLOAD_FIRST;
       idx < EOS_UPDATER_DOWNLOAD_N_SOURCES;
       ++idx)
    if (g_str_equal (str, order_key_str[idx]))
      break;

  if (idx >= EOS_UPDATER_DOWNLOAD_N_SOURCES)
    {
      g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_CONFIGURATION, "Unknown download source %s", str);
      return FALSE;
    }
  *source = idx;
  return TRUE;
}

static gboolean
strv_to_download_order (gchar **sources,
                        EosUpdaterDownloadSource **order,
                        gsize *n_download_sources,
                        GError **error)
{
  g_autoptr(GArray) array = g_array_new (FALSE, FALSE, sizeof (EosUpdaterDownloadSource));
  g_autoptr(GHashTable) found_sources = g_hash_table_new (NULL, NULL);
  gchar **iter;

  for (iter = sources; *iter != NULL; ++iter)
    {
      EosUpdaterDownloadSource idx;
      const gchar *key = g_strstrip (*iter);

      if (!string_to_download_source (key, &idx, error))
        return FALSE;

      if (!g_hash_table_add (found_sources, GINT_TO_POINTER (idx)))
        {
          g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_CONFIGURATION, "Duplicated download source %s", key);
          return FALSE;
        }
      g_array_append_val (array, idx);
    }

  *n_download_sources = array->len;
  *order = (EosUpdaterDownloadSource*)g_array_free (g_steal_pointer (&array), FALSE);
  return TRUE;
}

static gboolean
string_to_download_order (const gchar *str,
                          EosUpdaterDownloadSource **order,
                          gsize *n_download_sources,
                          GError **error)
{
  g_autofree gchar *dup = g_strstrip (g_strdup (str));
  g_auto(GStrv) sources = NULL;

  g_return_val_if_fail (order != NULL, FALSE);

  if (dup[0] == '\0')
    {
      *order = g_new (EosUpdaterDownloadSource, 1);
      (*order)[0] = EOS_UPDATER_DOWNLOAD_MAIN;
      *n_download_sources = 1;
      return TRUE;
    }

  sources = g_strsplit (dup, ",", -1);
  return strv_to_download_order (sources, order, n_download_sources, error);
}

static gchar *
get_config_file_path (void)
{
  return eos_updater_dup_envvar_or ("EOS_UPDATER_TEST_UPDATER_CONFIG_FILE_PATH",
                                    CONFIG_FILE_PATH);
}

static gboolean
read_config (EosUpdaterData *data,
             GError **error)
{
  g_autoptr(GKeyFile) config = g_key_file_new ();
  g_autofree gchar *config_file_path = get_config_file_path ();
  g_autofree gchar *download_order_str = NULL;
  g_autoptr(GError) local_error = NULL;

  if (!g_key_file_load_from_file (config, config_file_path, G_KEY_FILE_NONE, &local_error))
    {
      /* The documentation is not very clear about which error is
       * returned when the file is not found.
       */
      if (!g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT) &&
          !g_error_matches (local_error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
      /* config file was not found, fall back to the defaults */
      download_order_str = g_strdup ("main");
      g_clear_error (&local_error);
    }
  else
    {
      download_order_str = g_key_file_get_string (config, DOWNLOAD_GROUP, ORDER_KEY, error);
      if (download_order_str == NULL)
        return FALSE;
    }

  g_clear_pointer (&data->download_order, g_free);
  data->n_download_sources = 0;
  return string_to_download_order (download_order_str,
                                   &data->download_order,
                                   &data->n_download_sources,
                                   error);
}

/* This is to make sure that the the function we pass is of the
   correct prototype. g_ptr_array_add will not tell that to us,
   because it takes a gpointer.
 */
static void
add_fetcher (GPtrArray *fetchers,
             MetadataFetcher fetcher)
{
  g_ptr_array_add (fetchers, fetcher);
}

static GPtrArray *
get_fetchers (EosUpdaterData *data)
{
  GPtrArray *fetchers = g_ptr_array_new ();
  gsize idx;

  for (idx = 0; idx < data->n_download_sources; ++idx)
    switch (data->download_order[idx])
      {
      case EOS_UPDATER_DOWNLOAD_MAIN:
        add_fetcher (fetchers, metadata_fetch_from_main);
        break;

      case EOS_UPDATER_DOWNLOAD_LAN:
        add_fetcher (fetchers, metadata_fetch_from_lan);
        break;

      case EOS_UPDATER_DOWNLOAD_N_SOURCES:
        g_assert_not_reached ();
      }

  return fetchers;
}

static void
metadata_fetch_finished (GObject *object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask *task;
  GError *error = NULL;
  EosUpdaterData *data = user_data;
  OstreeRepo *repo = data->repo;
  g_autoptr(EosUpdateInfo) info = NULL;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  /* get the info about the fetched update */
  task = G_TASK (res);
  info = g_task_propagate_pointer (task, &error);

  if (info != NULL)
    {
      gint64 archived = -1;
      gint64 unpacked = -1;
      gint64 new_archived = 0;
      gint64 new_unpacked = 0;
      const gchar *label;
      const gchar *message;

      g_set_object (&data->extensions, info->extensions);
      g_strfreev (data->overridden_urls);
      data->overridden_urls = g_steal_pointer (&info->urls);

      /* Everything is happy thusfar */
      /* if we have a checksum for the remote upgrade candidate
       * and it's ≠ what we're currently booted into, advertise it as such.
       */
      eos_updater_clear_error (updater, EOS_UPDATER_STATE_UPDATE_AVAILABLE);
      eos_updater_set_update_id (updater, info->checksum);
      eos_updater_set_update_refspec (updater, info->refspec);
      eos_updater_set_original_refspec (updater, info->original_refspec);

      g_variant_get_child (info->commit, 3, "&s", &label);
      g_variant_get_child (info->commit, 4, "&s", &message);
      eos_updater_set_update_label (updater, label ? label : "");
      eos_updater_set_update_message (updater, message ? message : "");

      if (ostree_repo_get_commit_sizes (repo, info->checksum,
                                        &new_archived, &new_unpacked,
                                        NULL,
                                        &archived, &unpacked,
                                        NULL,
                                        g_task_get_cancellable (task),
                                        &error))
        {
          eos_updater_set_full_download_size (updater, archived);
          eos_updater_set_full_unpacked_size (updater, unpacked);
          eos_updater_set_download_size (updater, new_archived);
          eos_updater_set_unpacked_size (updater, new_unpacked);
          eos_updater_set_downloaded_bytes (updater, 0);
        }
      else /* no size data available (may or may not be an error) */
        {
          eos_updater_set_full_download_size (updater, -1);
          eos_updater_set_full_unpacked_size (updater, -1);
          eos_updater_set_download_size (updater, -1);
          eos_updater_set_unpacked_size (updater, -1);
          eos_updater_set_downloaded_bytes (updater, -1);

          /* shouldn't actually stop us offering an update, as long
           * as the branch itself is resolvable in the next step,
           * but log it anyway.
           */
          if (error)
            {
              message ("No size summary data: %s", error->message);
              g_clear_error (&error);
            }
        }
    }
  else /* info == NULL means OnHold=true, nothing to do here */
    eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_READY);

  if (error)
    {
      eos_updater_set_error (updater, error);
      g_clear_error (&error);
    }
  return;

 invalid_task:
  /* Either the threading or the memory management is shafted. Or both.
   * We're boned. Log an error and activate the self destruct mechanism.
   */
  g_error ("Invalid async task object when returning from Poll() thread!");
  g_assert_not_reached ();
}

static void
maybe_send_metric (EosMetricsInfo *metrics)
{
#ifdef HAS_EOSMETRICS_0
  static gboolean metric_sent = FALSE;

  if (metric_sent)
    return;

  message ("Recording metric event %s: (%s, %s, %s, %d)",
           EOS_UPDATER_BRANCH_SELECTED, metrics->vendor, metrics->product,
           metrics->ref, metrics->on_hold);
  emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                         EOS_UPDATER_BRANCH_SELECTED,
                                         g_variant_new ("(sssb)", metrics->vendor,
                                                        metrics->product,
                                                        metrics->ref,
                                                        metrics->on_hold));
  metric_sent = TRUE;
#endif
}

typedef struct
{
  EosUpdateInfo *update;
  EosMetricsInfo *metrics;
} UpdateAndMetrics;

static UpdateAndMetrics *
update_and_metrics_new (EosUpdateInfo *update,
                        EosMetricsInfo *metrics)
{
  UpdateAndMetrics *uam = g_new0 (UpdateAndMetrics, 1);

  if (update != NULL)
    uam->update = g_object_ref (update);

  if (metrics != NULL)
    uam->metrics = g_object_ref (metrics);

  return uam;
}

static void
update_and_metrics_free (UpdateAndMetrics *uam)
{
  if (uam == NULL)
    return;

  g_clear_object (&uam->update);
  g_clear_object (&uam->metrics);
  g_free (uam);
}

static UpdateAndMetrics *
get_latest_uam (EosUpdaterData *data,
                GHashTable *source_to_uam,
                gboolean with_updates)
{
  g_autoptr(GHashTable) latest = g_hash_table_new (NULL, NULL);
  GHashTableIter iter;
  gpointer name_ptr;
  gpointer uam_ptr;
  GDateTime *latest_timestamp = NULL;
  gsize idx;

  g_hash_table_iter_init (&iter, source_to_uam);
  while (g_hash_table_iter_next (&iter, &name_ptr, &uam_ptr))
    {
      UpdateAndMetrics *uam = uam_ptr;
      EosBranchFile *branch_file = uam->metrics->branch_file;
      gint compare_value = 1;

      if (with_updates && uam->update == NULL)
        continue;

      if (latest_timestamp != NULL)
        compare_value = g_date_time_compare (branch_file->download_time,
                                             latest_timestamp);
      if (compare_value > 0)
        {
          latest_timestamp = branch_file->download_time;
          g_hash_table_remove_all (latest);
          compare_value = 0;
        }

      if (compare_value == 0)
        g_hash_table_insert (latest, name_ptr, uam_ptr);
    }

  for (idx = 0; idx < data->n_download_sources; ++idx)
    {
      const gchar *name = order_key_str[data->download_order[idx]];
      UpdateAndMetrics *uam = g_hash_table_lookup (latest, name);

      if (uam != NULL)
        return uam;
    }

  return NULL;
}

static void
metadata_fetch (GTask *task,
                gpointer object,
                gpointer task_data,
                GCancellable *cancel)
{
  EosUpdaterData *data = task_data;
  GError *error = NULL;
  GMainContext *task_context = g_main_context_new ();
  g_autoptr(EosMetadataFetchData) fetch_data = NULL;
  g_autoptr(GPtrArray) fetchers = NULL;
  guint idx;
  UpdateAndMetrics *latest_uam = NULL;
  g_autoptr(GHashTable) source_to_uam = NULL;

  fetch_data = eos_metadata_fetch_data_new (task, data, task_context);
  g_main_context_unref (task_context);

  if (!read_config (data, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  fetchers = get_fetchers (data);
  source_to_uam = g_hash_table_new_full (NULL,
                                         NULL,
                                         NULL,
                                         (GDestroyNotify)update_and_metrics_free);
  for (idx = 0; idx < fetchers->len; ++idx)
    {
      MetadataFetcher fetcher = g_ptr_array_index (fetchers, idx);
      g_autoptr(EosUpdateInfo) info = NULL;
      g_autoptr(EosMetricsInfo) metrics = NULL;
      const gchar *name = order_key_str[data->download_order[idx]];
      UpdateAndMetrics *uam;

      if (!fetcher (fetch_data, &info, &metrics, &error))
        {
          message ("Failed to poll metadata from source %s: %s",
                   name, error->message);
          g_clear_error (&error);
          continue;
        }
      if (metrics == NULL)
        {
          message ("No metadata available from source %s", name);
          continue;
        }

      uam = update_and_metrics_new (info, metrics);

      g_hash_table_insert (source_to_uam, (gpointer)name, uam);
    }

  if (g_hash_table_size (source_to_uam) > 0)
    {
      latest_uam = get_latest_uam (data, source_to_uam, FALSE);
      maybe_send_metric (latest_uam->metrics);
      latest_uam = get_latest_uam (data, source_to_uam, TRUE);
      if (latest_uam != NULL)
        {
          g_task_return_pointer (task,
                                 g_object_ref (latest_uam->update),
                                 g_object_unref);
          return;
        }
    }
  /* no update found */
  g_task_return_pointer (task, NULL, NULL);
}

gboolean
handle_poll (EosUpdater            *updater,
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
      default:
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call Poll() while in state %s",
          eos_updater_state_to_string (state));
        goto bail;
    }

  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_POLLING);
  task = g_task_new (updater, NULL, metadata_fetch_finished, user_data);
  g_task_set_task_data (task, user_data, NULL);
  g_task_run_in_thread (task, metadata_fetch);

  eos_updater_complete_poll (updater, call);

bail:
  return TRUE;
}
