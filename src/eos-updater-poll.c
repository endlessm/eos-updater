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
#include "eos-updater-poll-volume.h"
#include "eos-updater-poll.h"

#include "eos-util.h"

/*
 * Records which branch will be used by the updater. The payload is a 4-tuple
 * of 3 strings and boolean: vendor name, product ID, selected OStree ref, and
 * whether the machine is on hold
 */
static const gchar *const EOS_UPDATER_BRANCH_SELECTED = "99f48aac-b5a0-426d-95f4-18af7d081c4e";
static const gchar *const CONFIG_FILE_PATH = "/etc/eos-updater-daemon.conf";
static const gchar *const DOWNLOAD_GROUP = "Download";
static const gchar *const ORDER_KEY = "Order";

static gboolean
strv_to_download_order (gchar **sources,
                        GArray **out_download_order,
                        GError **error)
{
  g_autoptr(GArray) array = g_array_new (FALSE, /* not null terminated */
                                         FALSE, /* no clearing */
                                         sizeof (EosUpdaterDownloadSource));
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
          g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_CONFIGURATION,
                       "Duplicated download source %s",
                       key);
          return FALSE;
        }
      g_array_append_val (array, idx);
    }

  if (array->len == 0)
    {
      g_set_error_literal (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_CONFIGURATION,
                           "No download sources");
      return FALSE;
    }

  *out_download_order = g_steal_pointer (&array);
  return TRUE;
}

static gchar *
get_config_file_path (void)
{
  return eos_updater_dup_envvar_or ("EOS_UPDATER_TEST_UPDATER_CONFIG_FILE_PATH",
                                    CONFIG_FILE_PATH);
}

typedef struct
{
  GArray *download_order;

  gchar *volume_path;
} SourcesConfig;

#define SOURCES_CONFIG_CLEARED { NULL, NULL }

static void
sources_config_clear (SourcesConfig *config)
{
  g_clear_pointer (&config->download_order, g_array_unref);
  g_clear_pointer (&config->volume_path, g_free);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (SourcesConfig, sources_config_clear)

static gboolean
sources_config_has_source (SourcesConfig *config,
                           EosUpdaterDownloadSource source,
                           gchar **out_group_name)
{
  gsize idx;

  for (idx = 0; idx < config->download_order->len; ++idx)
    {
      EosUpdaterDownloadSource config_source = g_array_index (config->download_order,
                                                              EosUpdaterDownloadSource,
                                                              idx);

      if (config_source == source)
        {
          *out_group_name = g_strdup_printf ("Source \"%s\"",
                                             download_source_to_string (source));
          return TRUE;
        }
    }

  *out_group_name = NULL;
  return FALSE;
}

static gboolean
read_config (SourcesConfig *sources_config,
             GError **error)
{
  g_autoptr(GKeyFile) config = g_key_file_new ();
  g_autofree gchar *config_file_path = get_config_file_path ();
  g_auto(GStrv) download_order_strv = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *group_name = NULL;

  if (!g_key_file_load_from_file (config, config_file_path, G_KEY_FILE_NONE, &local_error))
    {
      EosUpdaterDownloadSource main_source = EOS_UPDATER_DOWNLOAD_MAIN;
      /* The documentation is not very clear about which error is
       * returned when the file is not found.
       */
      if (!g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT) &&
          !g_error_matches (local_error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      /* Config file was not found, fall back to the defaults
       */
      sources_config->download_order = g_array_sized_new (FALSE, /* not null terminated */
                                                          FALSE, /* no clearing */
                                                          sizeof (EosUpdaterDownloadSource),
                                                          1);
      g_array_append_val (sources_config->download_order, main_source);
      sources_config->volume_path = NULL;

      return TRUE;
    }

  download_order_strv = g_key_file_get_string_list (config,
                                                    DOWNLOAD_GROUP,
                                                    ORDER_KEY,
                                                    NULL,
                                                    error);
  if (download_order_strv == NULL)
    return FALSE;

  if (!strv_to_download_order (download_order_strv,
                               &sources_config->download_order,
                               error))
    return FALSE;

  if (sources_config_has_source (sources_config,
                                 EOS_UPDATER_DOWNLOAD_VOLUME,
                                 &group_name))
    {
      sources_config->volume_path = g_key_file_get_string (config,
                                                           group_name,
                                                           "Path",
                                                           error);

      if (sources_config->volume_path == NULL)
        return FALSE;
    }

  return TRUE;
}

/* This is to make sure that the function we pass is of the correct
 * prototype. g_ptr_array_add will not tell that to us, because it
 * takes a gpointer.
 */
static void
add_fetcher (GPtrArray *fetchers,
             MetadataFetcher fetcher)
{
  g_ptr_array_add (fetchers, fetcher);
}

static void
add_source_variant (GPtrArray *source_variants,
                    GVariant *variant)
{
  g_variant_ref_sink (variant);

  g_ptr_array_add (source_variants, variant);
}

static void
get_fetchers (SourcesConfig *config,
              GPtrArray **out_fetchers,
              GPtrArray **out_source_variants)
{
  g_autoptr(GPtrArray) fetchers = g_ptr_array_sized_new (config->download_order->len);
  g_autoptr(GPtrArray) source_variants = g_ptr_array_new_full (config->download_order->len,
                                                               (GDestroyNotify)g_variant_unref);
  gsize idx;

  for (idx = 0; idx < config->download_order->len; ++idx)
    {
      g_auto(GVariantDict) dict_builder;

      g_variant_dict_init (&dict_builder, NULL);
      switch (g_array_index (config->download_order,
                             EosUpdaterDownloadSource,
                             idx))
        {
        case EOS_UPDATER_DOWNLOAD_MAIN:
          add_fetcher (fetchers, metadata_fetch_from_main);
          break;

        case EOS_UPDATER_DOWNLOAD_LAN:
          add_fetcher (fetchers, metadata_fetch_from_lan);
          break;

        case EOS_UPDATER_DOWNLOAD_VOLUME:
          add_fetcher (fetchers, metadata_fetch_from_volume);
          g_variant_dict_insert_value (&dict_builder,
                                       VOLUME_FETCHER_PATH_KEY,
                                       g_variant_new_string (config->volume_path));
          break;

        case EOS_UPDATER_DOWNLOAD_N_SOURCES:
          g_assert_not_reached ();
        }

      add_source_variant (source_variants,
                          g_variant_dict_end (&dict_builder));
    }

  *out_fetchers = g_steal_pointer (&fetchers);
  *out_source_variants = g_steal_pointer (&source_variants);
}

static void
metadata_fetch (GTask *task,
                gpointer object,
                gpointer task_data,
                GCancellable *cancel)
{
  EosUpdaterData *data = task_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_new ();
  g_autoptr(EosMetadataFetchData) fetch_data = NULL;
  g_autoptr(GPtrArray) fetchers = NULL;
  g_autoptr(GPtrArray) source_variants = NULL;
  g_auto(SourcesConfig) config = SOURCES_CONFIG_CLEARED;
  g_autoptr(EosUpdateInfo) info = NULL;

  fetch_data = eos_metadata_fetch_data_new (task, data, task_context);

  if (!read_config (&config, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  get_fetchers (&config, &fetchers, &source_variants);
  info = run_fetchers (fetch_data,
                       fetchers,
                       source_variants,
                       config.download_order);

  g_task_return_pointer (task,
                         (info != NULL) ? g_object_ref (info) : NULL,
                         g_object_unref);
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
