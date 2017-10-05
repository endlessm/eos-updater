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

#include "eos-updater-avahi.h"
#include "eos-updater-poll-common.h"
#include "eos-updater-poll-lan.h"

#include <glib.h>
#include <libeos-updater-util/avahi-service-file.h>
#include <libeos-updater-util/util.h>
#include <libsoup/soup.h>
#include <string.h>

typedef enum
{
  TXT_RECORD_OK,
  TXT_RECORD_NOT_UNIQUE,
  TXT_RECORD_NOT_FOUND
} TxtRecordError;

typedef struct
{
  EosMetadataFetchData *fetch_data;
  GMainLoop *main_loop;
  GError *error;
  EosUpdateInfo *info;
  gchar *cached_ostree_path;
} LanData;

#define LAN_DATA_CLEARED { NULL, NULL, NULL, NULL, NULL }

static gboolean
lan_data_init (LanData *lan_data,
               EosMetadataFetchData *fetch_data,
               GError **error)
{
  g_autoptr(OstreeDeployment) deployment = NULL;

  g_return_val_if_fail (lan_data != NULL, FALSE);
  g_return_val_if_fail (EOS_IS_METADATA_FETCH_DATA (fetch_data), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  memset (lan_data, 0, sizeof (*lan_data));
  lan_data->fetch_data = g_object_ref (fetch_data);
  lan_data->main_loop = g_main_loop_new (fetch_data->context, FALSE);

  deployment = eos_updater_get_booted_deployment (error);
  if (deployment == NULL)
    return FALSE;

  if (!eos_updater_get_ostree_path (fetch_data->data->repo,
                                    ostree_deployment_get_osname (deployment),
                                    &lan_data->cached_ostree_path,
                                    error))
    return FALSE;

  return TRUE;
}

static void
lan_data_clear (LanData *lan_data)
{
  g_clear_pointer (&lan_data->cached_ostree_path, g_free);
  g_clear_object (&lan_data->info);
  g_clear_error (&lan_data->error);
  g_clear_pointer (&lan_data->main_loop, g_main_loop_unref);
  g_clear_object (&lan_data->fetch_data);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (LanData, lan_data_clear)

static void
place_fail_key_to_hash_table (GHashTable *keys,
                              const gchar *fail_key)
{
  g_hash_table_steal_all (keys);
  g_hash_table_insert (keys, (gchar *)fail_key, NULL);
}

static TxtRecordError
fill_txt_records (gchar **txt_records,
                  GHashTable *keys)
{
  gchar **iter;
  g_autoptr(GPtrArray) keys_to_find = g_ptr_array_new ();
  GHashTableIter key_iter;
  const gchar *key;

  g_hash_table_iter_init (&key_iter, keys);
  while (g_hash_table_iter_next (&key_iter, (gpointer *)&key, NULL))
    g_ptr_array_add (keys_to_find, (gpointer)key);

  for (iter = txt_records; *iter != NULL; ++iter)
    {
      const gchar *txt_record = *iter;

      g_hash_table_iter_init (&key_iter, keys);
      while (g_hash_table_iter_next (&key_iter, (gpointer *)&key, NULL))
        {
          gsize key_len;

          if (!g_str_has_prefix (txt_record, key))
            continue;
          key_len = strlen (key);
          if (txt_record[key_len] != '=')
            continue;
          if (g_hash_table_lookup (keys, key) != NULL)
            {
              place_fail_key_to_hash_table (keys, key);
              return TXT_RECORD_NOT_UNIQUE;
            }
          g_hash_table_insert (keys, (gchar *)key, (gchar *)txt_record + key_len + 1);
          g_ptr_array_remove_fast (keys_to_find, (gchar *)key);
          break;
        }
    }

  if (keys_to_find->len > 0)
    {
      place_fail_key_to_hash_table (keys,
                                    g_ptr_array_index (keys_to_find, 0));
      return TXT_RECORD_NOT_FOUND;
    }

  return TXT_RECORD_OK;
}

static TxtRecordError
get_unique_txt_records (gchar **txt_records,
                        GHashTable **records,
                        ...)
{
  gchar *key = NULL;
  GHashTable *unique_keys = g_hash_table_new (g_str_hash, g_str_equal);
  va_list args;

  va_start (args, records);
  for (;;)
    {
      key = va_arg (args, gchar*);

      if (key == NULL)
        break;
      g_hash_table_insert (unique_keys, key, NULL);
    }
  va_end (args);

  *records = unique_keys;
  return fill_txt_records (txt_records, unique_keys);
}

static const gchar*
get_unique_txt_record (gchar **txt_records,
                       const gchar *key,
                       TxtRecordError *txt_error)
{
  g_autoptr(GHashTable) records = NULL;
  TxtRecordError error;

  error = get_unique_txt_records (txt_records, &records, key, NULL);
  if (txt_error != NULL)
    *txt_error = error;
  if (error != TXT_RECORD_OK)
    return NULL;

  /* the hash table does not manage the memory of its values, so we
     don't have to steal them */
  return g_hash_table_lookup (records, key);
}

#define EOS_TYPE_SERVICE_WITH_METADATA eos_service_with_metadata_get_type ()
G_DECLARE_FINAL_TYPE (EosServiceWithMetadata,
                      eos_service_with_metadata,
                      EOS,
                      SERVICE_WITH_METADATA,
                      GObject)

struct _EosServiceWithMetadata
{
  GObject parent_instance;

  EosAvahiService *service;
  GDateTime *declared_head_commit_timestamp;
};

static void
eos_service_with_metadata_dispose_impl (EosServiceWithMetadata *swm)
{
  g_clear_object (&swm->service);
  g_clear_pointer (&swm->declared_head_commit_timestamp, g_date_time_unref);
}

static void
eos_service_with_metadata_finalize_impl (EosServiceWithMetadata *swm)
{
}

EOS_DEFINE_REFCOUNTED (EOS_SERVICE_WITH_METADATA,
                       EosServiceWithMetadata,
                       eos_service_with_metadata,
                       eos_service_with_metadata_dispose_impl,
                       eos_service_with_metadata_finalize_impl)

static EosServiceWithMetadata *
eos_service_with_metadata_new (EosAvahiService *service)
{
  EosServiceWithMetadata *swm = g_object_new (EOS_TYPE_SERVICE_WITH_METADATA, NULL);

  swm->service = g_object_ref (service);

  return swm;
}

static gboolean
check_ostree_path (LanData *lan_data,
                   const gchar *ostree_path)
{
  return g_strcmp0 (ostree_path, lan_data->cached_ostree_path) == 0;
}

static gboolean
check_dl_time (LanData *lan_data,
               const gchar *dl_time,
               GDateTime **utc_out)
{
  gint64 utc_time;
  g_autoptr(GDateTime) utc = NULL;

  g_assert (dl_time != NULL);

  if (dl_time[0] == '\0')
    return FALSE;
  if (!eos_string_to_signed (dl_time, 10, G_MININT64, G_MAXINT64, &utc_time, NULL))
    return FALSE;
  utc = g_date_time_new_from_unix_utc (utc_time);
  if (utc == NULL)
    return FALSE;
  *utc_out = g_steal_pointer (&utc);
  return TRUE;
}

static gboolean
time_check (LanData *lan_data,
            EosServiceWithMetadata *swm,
            const gchar *dl_time)
{
  g_autoptr(GDateTime) txt_utc = NULL;

  g_return_val_if_fail (EOS_IS_SERVICE_WITH_METADATA (swm), FALSE);

  if (check_dl_time (lan_data, dl_time, &txt_utc))
    {
      swm->declared_head_commit_timestamp = g_steal_pointer (&txt_utc);
      return TRUE;
    }

  return FALSE;
}

static gboolean
txt_v1_handler (LanData *lan_data,
                EosServiceWithMetadata *swm,
                gboolean *valid,
                GError **error)
{
  g_autoptr(GHashTable) records = NULL;
  const gchar *ostree_path, *dl_time;
  TxtRecordError txt_error = get_unique_txt_records (swm->service->txt,
                                                     &records,
                                                     eos_avahi_v1_ostree_path,
                                                     eos_avahi_v1_head_commit_timestamp,
                                                     NULL);
  if (txt_error != TXT_RECORD_OK)
    {
      // TODO: message
      *valid = FALSE;
      return TRUE;
    }

  ostree_path = g_hash_table_lookup (records, eos_avahi_v1_ostree_path);
  dl_time = g_hash_table_lookup (records, eos_avahi_v1_head_commit_timestamp);

  *valid = (check_ostree_path (lan_data, ostree_path) &&
            time_check (lan_data, swm, dl_time));

  return TRUE;
}

/* Puts services with newer head commit timestamps in front of services with
 * older ones. */
static gint
g_compare_func_swm_by_timestamp (gconstpointer swm1_ptr_ptr,
                                 gconstpointer swm2_ptr_ptr)
{
  EosServiceWithMetadata *swm1 = *((EosServiceWithMetadata **)swm1_ptr_ptr);
  EosServiceWithMetadata *swm2 = *((EosServiceWithMetadata **)swm2_ptr_ptr);

  return g_date_time_compare (swm2->declared_head_commit_timestamp,
                              swm1->declared_head_commit_timestamp);
}

/* Valid version numbers start from 1. Return 0 on error. */
static guint
parse_txt_version (const gchar *txt_version)
{
  guint64 v;

  if (!eos_string_to_unsigned (txt_version, 10, 1, G_MAXUINT, &v, NULL))
    return 0;

  return (guint) v;
}

static gboolean
filter_services (LanData *lan_data,
                 GPtrArray *found_services,
                 GPtrArray **out_valid_services,
                 GError **error)
{
  guint idx;
  g_autoptr(GPtrArray) valid_services = object_array_new ();

  for (idx = 0; idx < found_services->len; ++idx)
    {
      gpointer service_ptr = g_ptr_array_index (found_services, idx);
      EosAvahiService *service = EOS_AVAHI_SERVICE (service_ptr);
      TxtRecordError txt_error = TXT_RECORD_OK;
      guint version_number;
      const gchar *txt_version = get_unique_txt_record (service->txt,
                                                        "eos_txt_version",
                                                        &txt_error);
      gboolean valid = FALSE;
      g_autoptr(EosServiceWithMetadata) swm = NULL;

      if (txt_version == NULL)
        {
          g_message ("service at %s has no txt records version, ignoring it",
                     service->address);
          continue;
        }

      version_number = parse_txt_version (txt_version);
      swm = eos_service_with_metadata_new (service);

      if (version_number == 1)
        {
          if (!txt_v1_handler (lan_data, swm, &valid, error))
            return FALSE;
        }
      else
        {
          g_message ("unknown txt records version %s from service at %s, ignoring it",
                     txt_version,
                     service->address);
          continue;
        }

      if (!valid)
        continue;
      g_ptr_array_add (valid_services, g_steal_pointer (&swm));
    }

  g_ptr_array_sort (valid_services, g_compare_func_swm_by_timestamp);
  *out_valid_services = g_steal_pointer (&valid_services);
  return TRUE;
}

/*
 * TODO: async fetch_ref_file ([](){
 *   checksum = get_from_ref_file
 *   commit = NULL;
 *   if (checksum_is_old (checksum))
 *     return
 *   if (checksum_unknown (checksum))
 *     commit = fetch_the_commit(checksum)
 *     if commit_is_an_update (commit)
 *       is_commit_valid (commit)
 *         make_checksum_old (latest_checksum)
 *         latest_checksum = checksum
 *         latest_services = service
 *     else
 *       make_checksum_old (checksum)
 */
static gboolean
get_update_info_from_swms (LanData *lan_data,
                           GPtrArray *swms,
                           EosUpdateInfo **out_info,
                           GError **error)
{
  guint idx;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *new_refspec = NULL;
  g_autofree gchar *latest_checksum = NULL;
  guint64 latest_timestamp = 0;
  g_autoptr(GPtrArray) swms_with_latest_commit = NULL;
  g_autoptr(GVariant) latest_commit = NULL;
  g_autoptr(GPtrArray) urls = NULL;
  OstreeRepo *repo = lan_data->fetch_data->data->repo;

  if (!get_booted_refspec (&refspec, NULL, NULL, error))
    return FALSE;

  swms_with_latest_commit = object_array_new ();
  urls = g_ptr_array_new_with_free_func (g_free);
  for (idx = 0; idx < swms->len; ++idx)
    {
      gpointer swm_ptr = g_ptr_array_index (swms, idx);
      EosServiceWithMetadata *swm = EOS_SERVICE_WITH_METADATA (swm_ptr);
      EosAvahiService *service = swm->service;
      g_autoptr(SoupURI) _url_override = NULL;
      g_autofree gchar *url_override = NULL;
      g_autoptr(GError) local_error = NULL;
      g_autofree gchar *checksum = NULL;
      g_autoptr(GVariant) commit = NULL;
      guint64 timestamp;
      GCancellable *cancellable = g_task_get_cancellable (lan_data->fetch_data->task);

      /* Build the URI. */
      _url_override = soup_uri_new (NULL);
      soup_uri_set_scheme (_url_override, "http");
      soup_uri_set_host (_url_override, service->address);
      soup_uri_set_port (_url_override, service->port);
      soup_uri_set_path (_url_override, "");
      url_override = soup_uri_to_string (_url_override, FALSE);

      if (!fetch_latest_commit (repo,
                                cancellable,
                                refspec,
                                url_override,
                                &checksum,
                                &new_refspec,
                                &local_error))
        {
          g_message ("Failed to fetch latest commit from %s: %s",
                     url_override,
                     local_error->message);
          continue;
        }

      if (!is_checksum_an_update (repo, checksum, &commit, &local_error))
        {
          g_message ("Failed to fetch metadata for commit %s from %s: %s",
                     checksum, url_override, local_error->message);
          continue;
        }
      else if (commit == NULL)
        {
          g_message ("Commit %s from %s is not an update; ignoring",
                     checksum, url_override);
          continue;
        }

      timestamp = ostree_commit_get_timestamp (commit);

      /* Sanity check that the commit has the declared timestamp. */
      if (g_date_time_to_unix (swm->declared_head_commit_timestamp) != (gint64) timestamp)
        {
          g_autofree gchar *declared_str = NULL;
          g_autofree gchar *actual_str = NULL;
          g_autoptr(GDateTime) actual_time = NULL;

          declared_str = g_date_time_format (swm->declared_head_commit_timestamp,
                                             "%FT%T%:z");
          actual_time = g_date_time_new_from_unix_utc ((gint64) timestamp);
          actual_str = g_date_time_format (actual_time, "%FT%T%:z");

          g_message ("The commit timestamp (%s) from %s does not match the "
                     "timestamp declared by the host (%s). Ignoring.",
                     declared_str, url_override, actual_str);
          continue;
        }

      if (latest_checksum != NULL)
        {
          if (timestamp < latest_timestamp)
            continue;

          if (timestamp == latest_timestamp && g_strcmp0(checksum, latest_checksum) == 0)
            {
              g_ptr_array_add (swms_with_latest_commit, g_object_ref (swm));
              g_ptr_array_add (urls, g_steal_pointer (&url_override));
            }
          else if (timestamp > latest_timestamp && g_strcmp0(checksum, latest_checksum) != 0)
            {
              g_clear_pointer (&latest_checksum, g_free);
              g_clear_pointer (&latest_commit, g_variant_unref);
              latest_timestamp = 0;
              g_ptr_array_set_size (swms_with_latest_commit, 0);
              g_ptr_array_set_size (urls, 0);
            }
          else
            {
              g_message ("The commit from %s has either only timestamp the same as the timestamp from latest commit"
                         " or only checksum the same as the checksum from latest commit."
                         " This should not happen. Ignoring.",
                         url_override);
              continue;
            }
        }
      if (latest_checksum == NULL)
        {
          latest_checksum = g_steal_pointer (&checksum);
          latest_commit = g_steal_pointer (&commit);
          latest_timestamp = timestamp;
          g_ptr_array_add (swms_with_latest_commit, g_object_ref (swm));
          g_ptr_array_add (urls, g_steal_pointer (&url_override));
        }
    }

  /* NULL-terminate the urls array. */
  g_ptr_array_add (urls, NULL);

  if (latest_checksum != NULL)
    {
      *out_info = eos_update_info_new (latest_checksum,
                                       latest_commit,
                                       new_refspec,
                                       refspec,
                                       (const gchar * const *)g_ptr_array_free (g_steal_pointer (&urls),
                                                                                FALSE));
    }
  else
    *out_info = NULL;

  return TRUE;
}

static void
check_lan_updates (LanData *lan_data,
                   GPtrArray *found_services,
                   GError **error)
{
  g_autoptr(GPtrArray) valid_services = NULL;
  g_autoptr(GError) child_error = NULL;

  if (!filter_services (lan_data,
                        found_services,
                        &valid_services,
                        &child_error))
    {
      g_message ("Failed to filter services: %s", child_error->message);
      g_propagate_error (error, g_steal_pointer (&child_error));
      return;
    }

  if (valid_services->len == 0)
    {
      g_message ("No valid LAN servers found");
      return;
    }

  if (!get_update_info_from_swms (lan_data,
                                   valid_services,
                                   &lan_data->info,
                                   &child_error))
    {
      g_message ("Failed to get the latest update info: %s",
                 child_error->message);
      g_propagate_error (error, g_steal_pointer (&child_error));
      return;
    }
}

static void
discoverer_callback (EosAvahiDiscoverer *discoverer,
                     GPtrArray *found_services,
                     gpointer lan_data_ptr,
                     GError *error)
{
  LanData *lan_data = lan_data_ptr;

  lan_data->error = g_steal_pointer (&error);
  if (lan_data->error == NULL)
    check_lan_updates (lan_data, found_services, &lan_data->error);

  g_main_loop_quit (lan_data->main_loop);
}

gboolean
metadata_fetch_from_lan (EosMetadataFetchData *fetch_data,
                         GVariant *source_variant,
                         EosUpdateInfo **out_info,
                         GError **error)
{
  g_autoptr(EosAvahiDiscoverer) discoverer = NULL;
  g_auto(LanData) lan_data = LAN_DATA_CLEARED;

  g_return_val_if_fail (EOS_IS_METADATA_FETCH_DATA (fetch_data), FALSE);
  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!lan_data_init (&lan_data, fetch_data, error))
    return FALSE;

  discoverer = eos_avahi_discoverer_new (fetch_data->context,
                                         discoverer_callback,
                                         &lan_data,
                                         NULL,
                                         error);

  if (discoverer == NULL)
    return FALSE;

  g_main_loop_run (lan_data.main_loop);
  if (lan_data.error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&lan_data.error));
      return FALSE;
    }
  *out_info = g_steal_pointer (&lan_data.info);

  return TRUE;
}
