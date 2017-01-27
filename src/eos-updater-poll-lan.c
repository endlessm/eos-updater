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

#include "eos-updater-avahi.h"
#include "eos-updater-poll-common.h"
#include "eos-updater-poll-lan.h"

#include "eos-util.h"

#include <string.h>
#include <errno.h>

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
  EosMetricsInfo *metrics;
  gchar *cached_ostree_path;
} LanData;

#define LAN_DATA_CLEARED { NULL, NULL, NULL, NULL, NULL }

static gboolean
lan_data_init (LanData *lan_data,
               EosMetadataFetchData *fetch_data,
               GError **error)
{
  g_return_val_if_fail (lan_data != NULL, FALSE);
  g_return_val_if_fail (EOS_IS_METADATA_FETCH_DATA (fetch_data), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  memset (lan_data, 0, sizeof (*lan_data));
  lan_data->fetch_data = g_object_ref (fetch_data);
  lan_data->main_loop = g_main_loop_new (fetch_data->context, FALSE);

  if (!eos_updater_get_ostree_path (fetch_data->data->repo,
                                    &lan_data->cached_ostree_path,
                                    error))
    return FALSE;

  return TRUE;
}

static void
lan_data_clear (LanData *lan_data)
{
  g_clear_pointer (&lan_data->cached_ostree_path, g_free);
  g_clear_object (&lan_data->metrics);
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

#define EOS_TYPE_SERVICE_WITH_BRANCH_FILE eos_service_with_branch_file_get_type ()
EOS_DECLARE_REFCOUNTED (EosServiceWithBranchFile,
                        eos_service_with_branch_file,
                        EOS,
                        SERVICE_WITH_BRANCH_FILE)

struct _EosServiceWithBranchFile
{
  GObject parent_instance;

  EosAvahiService *service;
  EosBranchFile *branch_file;

  GDateTime *declared_download_time;
  gchar *declared_sha512sum;
};

static void
eos_service_with_branch_file_dispose_impl (EosServiceWithBranchFile *swbf)
{
  g_clear_object (&swbf->service);
  g_clear_object (&swbf->branch_file);
  g_clear_pointer (&swbf->declared_download_time, g_date_time_unref);
}

static void
eos_service_with_branch_file_finalize_impl (EosServiceWithBranchFile *swbf)
{
  g_free (swbf->declared_sha512sum);
}

EOS_DEFINE_REFCOUNTED (EOS_SERVICE_WITH_BRANCH_FILE,
                       EosServiceWithBranchFile,
                       eos_service_with_branch_file,
                       eos_service_with_branch_file_dispose_impl,
                       eos_service_with_branch_file_finalize_impl)

static EosServiceWithBranchFile *
eos_service_with_branch_file_new (EosAvahiService *service)
{
  EosServiceWithBranchFile *swbf = g_object_new (EOS_TYPE_SERVICE_WITH_BRANCH_FILE, NULL);

  swbf->service = g_object_ref (service);
  swbf->branch_file = eos_branch_file_new_empty ();

  return swbf;
}

static void
eos_service_with_branch_file_set_branch_file_from_lan_data (EosServiceWithBranchFile *swbf,
                                                            LanData *lan_data)
{
  g_set_object (&swbf->branch_file, lan_data->fetch_data->data->branch_file);
}

typedef gboolean (*ServiceCheck) (LanData *lan_data,
                                  EosServiceWithBranchFile *swbf,
                                  gboolean *valid,
                                  GError **error);

typedef enum
  {
    SERVICE_INVALID,
    SERVICE_VALID_MORE_CHECKS,
    SERVICE_VALID
  } ServiceValidResult;

static gboolean
check_ostree_path (LanData *lan_data,
                   const gchar *ostree_path)
{
  return g_strcmp0 (ostree_path, lan_data->cached_ostree_path) == 0;
}

typedef enum
  {
    DL_TIME_NEW,
    DL_TIME_OLD,
    DL_TIME_INVALID,
    DL_TIME_OUT_OF_RANGE,
  } DlTimeCheckResult;

static DlTimeCheckResult
check_dl_time (LanData *lan_data,
               const gchar *dl_time,
               GDateTime **utc_out)
{
  gint64 utc_time;
  gchar *utc_str_end = NULL;
  g_autoptr(GDateTime) utc = NULL;

  g_assert (dl_time != NULL);

  if (dl_time[0] == '\0')
    return DL_TIME_INVALID;
  errno = 0;
  utc_time = g_ascii_strtoll (dl_time, &utc_str_end, 10);
  if (errno == EINVAL)
    return DL_TIME_INVALID;
  if (errno == ERANGE)
    return DL_TIME_OUT_OF_RANGE;
  if (*utc_str_end != '\0')
    return DL_TIME_INVALID;
  utc = g_date_time_new_from_unix_utc (utc_time);
  if (utc == NULL)
    return DL_TIME_OUT_OF_RANGE;
  *utc_out = g_steal_pointer (&utc);
  if (g_date_time_compare (lan_data->fetch_data->data->branch_file->download_time, *utc_out) >= 0)
    return DL_TIME_OLD;
  return DL_TIME_NEW;
}

static ServiceValidResult
time_check (LanData *lan_data,
            EosServiceWithBranchFile *swbf,
            const gchar *dl_time)
{
  g_autoptr(GDateTime) txt_utc = NULL;

  switch (check_dl_time (lan_data, dl_time, &txt_utc))
    {
    case DL_TIME_OLD:
      eos_service_with_branch_file_set_branch_file_from_lan_data (swbf, lan_data);
      return SERVICE_VALID;

    case DL_TIME_NEW:
      swbf->declared_download_time = g_steal_pointer (&txt_utc);
      return SERVICE_VALID_MORE_CHECKS;

    case DL_TIME_INVALID:
      // TODO: message
      return SERVICE_INVALID;

    case DL_TIME_OUT_OF_RANGE:
      // TODO: message
      return SERVICE_INVALID;

    default:
      // TODO: message
      g_assert_not_reached ();
      return SERVICE_INVALID;
    }
}

static gboolean
txt_v1_handler_checks (LanData *lan_data,
                       EosServiceWithBranchFile *swbf,
                       GHashTable *records)
{
  const gchar *ostree_path = g_hash_table_lookup (records,
                                                  eos_avahi_v1_ostree_path);
  const gchar *dl_time;
  ServiceValidResult result;

  if (!check_ostree_path (lan_data, ostree_path))
    return FALSE;

  dl_time = g_hash_table_lookup (records, eos_avahi_v1_branch_file_dl_time);
  result = time_check (lan_data, swbf, dl_time);
  if (result == SERVICE_VALID_MORE_CHECKS)
    {
      const gchar *sha512sum = g_hash_table_lookup (records, eos_avahi_v1_branch_file_sha512sum);
      const gchar *sha512sum_local = lan_data->fetch_data->data->branch_file->contents_sha512sum;

      if (g_strcmp0 (sha512sum_local, sha512sum) == 0)
        eos_service_with_branch_file_set_branch_file_from_lan_data (swbf, lan_data);
      else
        swbf->declared_sha512sum = g_strdup (sha512sum);
      result = SERVICE_VALID;
    }
  if (result == SERVICE_VALID)
    return TRUE;
  return FALSE;
}

static gboolean
txt_v1_handler (LanData *lan_data,
                EosServiceWithBranchFile *swbf,
                gboolean *valid,
                GError **error)
{
  g_autoptr(GHashTable) records = NULL;
  TxtRecordError txt_error = get_unique_txt_records (swbf->service->txt,
                                                     &records,
                                                     eos_avahi_v1_ostree_path,
                                                     eos_avahi_v1_branch_file_dl_time,
                                                     eos_avahi_v1_branch_file_sha512sum,
                                                     NULL);
  if (txt_error != TXT_RECORD_OK)
    {
      // TODO: message
      *valid = FALSE;
      return TRUE;
    }
  *valid = txt_v1_handler_checks (lan_data,
                                  swbf,
                                  records);
  return TRUE;
}

static gboolean
txt_v2_handler_checks (LanData *lan_data,
                       EosServiceWithBranchFile *swbf,
                       GHashTable *records)
{
  const gchar *ostree_path = g_hash_table_lookup (records,
                                                  eos_avahi_v2_ostree_path);
  const gchar *dl_time;

  if (!check_ostree_path (lan_data, ostree_path))
    return FALSE;

  dl_time = g_hash_table_lookup (records, eos_avahi_v2_branch_file_timestamp);

  return time_check (lan_data, swbf, dl_time) != SERVICE_INVALID;
}

static gboolean
txt_v2_handler (LanData *lan_data,
                EosServiceWithBranchFile *swbf,
                gboolean *valid,
                GError **error)
{
  g_autoptr(GHashTable) records = NULL;
  TxtRecordError txt_error = get_unique_txt_records (swbf->service->txt,
                                                     &records,
                                                     eos_avahi_v2_ostree_path,
                                                     eos_avahi_v2_branch_file_timestamp,
                                                     NULL);
  if (txt_error != TXT_RECORD_OK)
    {
      // TODO: message
      *valid = FALSE;
      return TRUE;
    }
  *valid = txt_v2_handler_checks (lan_data,
                                  swbf,
                                  records);
  return TRUE;
}

static ServiceCheck
get_txt_handler (const gchar *txt_version)
{
  guint64 v;
  const gchar *end = NULL;

  errno = 0;
  v = g_ascii_strtoull (txt_version, (gchar **)&end, 10);

  if (errno != 0 || end == NULL)
    return NULL;

  if (v == 1 && *end == '\0')
    return txt_v1_handler;
  if (v == 2 && *end == '\0')
    return txt_v2_handler;

  return NULL;
}

// Puts services with branch file signatures in front of services
// without them.  Puts services with newer branch file timestamps in
// front of services with older ones.
static gint
g_compare_func_swbf_by_timestamp (gconstpointer swbf1_ptr_ptr,
                                  gconstpointer swbf2_ptr_ptr)
{
  EosServiceWithBranchFile *swbf1 = *((EosServiceWithBranchFile **)swbf1_ptr_ptr);
  EosServiceWithBranchFile *swbf2 = *((EosServiceWithBranchFile **)swbf2_ptr_ptr);
  gboolean with_signature1 = swbf1->declared_sha512sum == NULL;
  gboolean with_signature2 = swbf2->declared_sha512sum == NULL;

  if ((with_signature1 ^ with_signature2) == 0)
    {
      // either both have signatures or neither
      GDateTime *timestamp1 = swbf1->declared_download_time ?
        swbf1->declared_download_time :
        swbf1->branch_file->download_time;
      GDateTime *timestamp2 = swbf2->declared_download_time ?
        swbf2->declared_download_time :
        swbf2->branch_file->download_time;

      return g_date_time_compare (timestamp2, timestamp1);
    }
  // one has signature, the other not
  if (with_signature1)
    return -1;
  return 1;
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
      const gchar *txt_version = get_unique_txt_record (service->txt,
                                                        "eos_txt_version",
                                                        &txt_error);
      ServiceCheck checker;
      gboolean valid = FALSE;
      g_autoptr(EosServiceWithBranchFile) swbf = NULL;

      if (txt_version == NULL)
        {
          message ("service at %s has no txt records version, ignoring it",
                   service->address);
          continue;
        }
      checker = get_txt_handler (txt_version);
      if (checker == NULL)
        {
          message ("unknown txt records version %s from service at %s, ignoring it",
                   txt_version,
                   service->address);
          continue;
        }
      swbf = eos_service_with_branch_file_new (service);
      if (!checker (lan_data, swbf, &valid, error))
        return FALSE;

      if (!valid)
        continue;
      g_ptr_array_add (valid_services, g_steal_pointer (&swbf));
    }

  g_ptr_array_sort (valid_services, g_compare_func_swbf_by_timestamp);
  *out_valid_services = g_steal_pointer (&valid_services);
  return TRUE;
}

static gchar *
get_branch_file_uri (EosAvahiService *service)
{
  g_autoptr (SoupURI) uri = NULL;

  uri = soup_uri_new (NULL);
  soup_uri_set_scheme (uri, "http");
  soup_uri_set_host (uri, service->address);
  soup_uri_set_port (uri, service->port);
  soup_uri_set_path (uri, "/extensions/eos/branch_file");

  return soup_uri_to_string (uri, FALSE);
}

static EosBranchFile *
get_newest_branch_file (LanData *lan_data,
                        GPtrArray *valid_services)
{
  guint idx;
  GCancellable *cancellable = g_task_get_cancellable (lan_data->fetch_data->task);

  for (idx = 0; idx < valid_services->len; ++idx)
    {
      gpointer swbf_ptr = g_ptr_array_index (valid_services, idx);
      EosServiceWithBranchFile* swbf = EOS_SERVICE_WITH_BRANCH_FILE (swbf_ptr);
      g_autoptr(GBytes) branch_file_contents = NULL;
      g_autoptr(GBytes) signature_contents = NULL;
      g_autofree gchar *uri = NULL;
      g_autoptr(GError) error = NULL;
      g_autoptr(EosBranchFile) branch_file = NULL;

      /* @valid_services is already ordered with newest first, so if a swbf
       * already has a branch file, we can use it straight away. */
      if (swbf->branch_file->branch_file != NULL)
        return g_object_ref (swbf->branch_file);

      uri = get_branch_file_uri (swbf->service);
      if (!download_file_and_signature (uri,
                                        &branch_file_contents,
                                        &signature_contents,
                                        &error))
        {
          message ("Failed to download branch file from %s: %s",
                   uri,
                   error->message);
          continue;
        }

      if (branch_file_contents == NULL)
        {
          message ("Branch file on %s not found", uri);
          continue;
        }

      branch_file = eos_branch_file_new_from_raw (branch_file_contents,
                                                  signature_contents,
                                                  swbf->declared_download_time,
                                                  &error);
      if (branch_file == NULL)
        {
          message ("Branch file from %s is wrong: %s",
                   uri, error->message);
          continue;
        }

      if (signature_contents != NULL)
        {
          g_autoptr(OstreeGpgVerifyResult) gpg_result = NULL;
          OstreeRepo *repo = lan_data->fetch_data->data->repo;
          g_auto(GStrv) ostree_paths = NULL;

          gpg_result = ostree_repo_gpg_verify_data (repo,
                                                    NULL,
                                                    branch_file_contents,
                                                    signature_contents,
                                                    NULL,
                                                    NULL,
                                                    cancellable,
                                                    &error);
          if (!ostree_gpg_verify_result_require_valid_signature (gpg_result,
                                                                 &error))
            {
              message ("Branch file from %s has invalid signature: %s",
                       uri,
                       error->message);
              continue;
            }

          if (g_date_time_difference (swbf->declared_download_time,
                                      branch_file->download_time) != 0)
            {
              gint64 declared = g_date_time_to_unix (swbf->declared_download_time);
              gint64 from_file = g_date_time_to_unix (branch_file->download_time);

              message ("The %s declared timestamp (%" G_GINT64_FORMAT ") is different from the timestamp in branch file (%" G_GINT64_FORMAT "), looks suspicious",
                       uri,
                       declared,
                       from_file);
              continue;
            }

          if (!eos_updater_get_ostree_paths_from_branch_file_keyfile (branch_file->branch_file,
                                                                      &ostree_paths,
                                                                      &error))
            {
              message ("Failed to get ostree paths from branch file from %s: %s",
                       uri,
                       error->message);
              continue;
            }

          if (!g_strv_contains ((const gchar *const *)ostree_paths,
                                lan_data->cached_ostree_path))
            {
              message ("The branch file from %s has no ostree path %s as it declared, looks suspicious",
                       uri,
                       lan_data->cached_ostree_path);
              continue;
            }
        }

      return g_steal_pointer (&branch_file);
    }

  /* no valid branch file found */
  return NULL;
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
get_update_info_from_swbfs (LanData *lan_data,
                            GPtrArray *swbfs,
                            EosBranchFile *newest,
                            EosUpdateInfo **out_info,
                            EosMetricsInfo **out_metrics,
                            GError **error)
{
  guint idx;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *original_refspec = NULL;
  g_autoptr(EosMetricsInfo) metrics = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *latest_checksum = NULL;
  guint64 latest_timestamp = 0;
  g_autoptr(GPtrArray) swbfs_with_latest_commit = NULL;
  g_autoptr(GVariant) latest_commit = NULL;
  g_autoptr(GPtrArray) urls = NULL;
  g_autoptr(EosExtensions) latest_extensions = NULL;
  OstreeRepo *repo = lan_data->fetch_data->data->repo;

  if (!get_upgrade_info_from_branch_file (newest,
                                          &refspec,
                                          &original_refspec,
                                          &metrics,
                                          error))
    return FALSE;

  if (!ostree_parse_refspec (refspec, &remote, &ref, error))
    return FALSE;

  swbfs_with_latest_commit = object_array_new ();
  urls = g_ptr_array_new_with_free_func (g_free);
  for (idx = 0; idx < swbfs->len; ++idx)
    {
      gpointer swbf_ptr = g_ptr_array_index (swbfs, idx);
      EosServiceWithBranchFile *swbf = EOS_SERVICE_WITH_BRANCH_FILE (swbf_ptr);
      EosAvahiService *service = swbf->service;
      g_autoptr(SoupURI) _url_override = NULL;
      g_autofree gchar *url_override = NULL;
      g_autoptr(GError) local_error = NULL;
      g_autofree gchar *checksum = NULL;
      g_autoptr(GVariant) commit = NULL;
      guint64 timestamp;
      g_autoptr(EosExtensions) extensions = NULL;
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
                                remote,
                                ref,
                                url_override,
                                &checksum,
                                &extensions,
                                &local_error))
        {
          message ("Failed to fetch latest commit from %s: %s",
                   url_override,
                   local_error->message);
          continue;
        }

      if (!is_checksum_an_update (repo, checksum, &commit, &local_error))
        {
          message ("Commit %s from %s is not an update",
                   checksum,
                   url_override);
          continue;
        }

      timestamp = ostree_commit_get_timestamp (commit);
      if (latest_checksum != NULL)
        {
          if (timestamp < latest_timestamp)
            continue;

          if (timestamp == latest_timestamp && g_strcmp0(checksum, latest_checksum) == 0)
            {
              g_ptr_array_add (swbfs_with_latest_commit, g_object_ref (swbf));
              g_ptr_array_add (urls, g_steal_pointer (&url_override));
            }
          else if (timestamp > latest_timestamp && g_strcmp0(checksum, latest_checksum) != 0)
            {
              g_clear_pointer (&latest_checksum, g_free);
              g_clear_pointer (&latest_commit, g_variant_unref);
              latest_timestamp = 0;
              g_ptr_array_set_size (swbfs_with_latest_commit, 0);
              g_ptr_array_set_size (urls, 0);
              g_clear_object (&latest_extensions);
            }
          else
            {
              message ("The commit from %s has either only timestamp the same as the timestamp from latest commit"
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
          g_ptr_array_add (swbfs_with_latest_commit, g_object_ref (swbf));
          g_ptr_array_add (urls, g_steal_pointer (&url_override));
          latest_extensions = g_steal_pointer (&extensions);
        }
    }

  /* NULL-terminate the urls array. */
  g_ptr_array_add (urls, NULL);

  if (latest_checksum != NULL)
    {
      g_set_object (&latest_extensions->branch_file, newest);
      *out_info = eos_update_info_new (latest_checksum,
                                       latest_commit,
                                       refspec,
                                       original_refspec,
                                       (const gchar * const *)g_ptr_array_free (g_steal_pointer (&urls),
                                                                                FALSE),
                                       latest_extensions);
    }
  else
    *out_info = NULL;
  *out_metrics = g_steal_pointer (&metrics);

  return TRUE;
}

static void
check_lan_updates (LanData *lan_data,
                   GPtrArray *found_services,
                   GError **error)
{
  g_autoptr(GPtrArray) valid_services = NULL;
  g_autoptr(EosBranchFile) branch_file = NULL;
  g_autoptr(GError) child_error = NULL;

  if (!filter_services (lan_data,
                        found_services,
                        &valid_services,
                        &child_error))
    {
      message ("Failed to filter services: %s", child_error->message);
      g_propagate_error (error, g_steal_pointer (&child_error));
      return;
    }

  if (valid_services->len == 0)
    {
      message ("No valid LAN servers found");
      return;
    }

  branch_file = get_newest_branch_file (lan_data, valid_services);
  if (branch_file == NULL)
    {
      message ("No valid branch file found");
      return;
    }

  if (!get_update_info_from_swbfs (lan_data,
                                   valid_services,
                                   branch_file,
                                   &lan_data->info,
                                   &lan_data->metrics,
                                   &child_error))
    {
      message ("Failed to get the latest update info: %s",
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
                         EosMetricsInfo **out_metrics,
                         GError **error)
{
  g_autoptr(EosAvahiDiscoverer) discoverer = NULL;
  g_auto(LanData) lan_data = LAN_DATA_CLEARED;

  g_return_val_if_fail (EOS_IS_METADATA_FETCH_DATA (fetch_data), FALSE);
  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (out_metrics != NULL, FALSE);
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
  *out_metrics = g_steal_pointer (&lan_data.metrics);
  return TRUE;
}
