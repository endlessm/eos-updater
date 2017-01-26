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

#include "eos-updater-poll-common.h"

#include "eos-util.h"

#ifdef HAS_EOSMETRICS_0

#include <eosmetrics/eosmetrics.h>

#endif /* HAS_EOSMETRICS_0 */

#include <ostree.h>

#include <libsoup/soup.h>

#include <string.h>

static const gchar *const DEFAULT_GROUP = "Default";
static const gchar *const OSTREE_REF_KEY = "OstreeRef";
static const gchar *const ON_HOLD_KEY = "OnHold";

static const gchar *const VENDOR_KEY = "sys_vendor";
static const gchar *const PRODUCT_KEY = "product_name";
static const gchar *const DT_COMPATIBLE = "/proc/device-tree/compatible";
static const gchar *const DMI_PATH = "/sys/class/dmi/id/";
static const gchar *const dmi_attributes[] =
  {
    "bios_date",
    "bios_vendor",
    "bios_version",
    "board_name",
    "board_vendor",
    "board_version",
    "chassis_vendor",
    "chassis_version",
    "product_name",
    "product_version",
    "sys_vendor",
    NULL,
  };

static const gchar *const order_key_str[] = {
  "main",
  "lan",
  "volume"
};

G_STATIC_ASSERT (G_N_ELEMENTS (order_key_str) == EOS_UPDATER_DOWNLOAD_N_SOURCES);

#ifdef HAS_EOSMETRICS_0
/*
 * Records which branch will be used by the updater. The payload is a 4-tuple
 * of 3 strings and boolean: vendor name, product ID, selected OStree ref, and
 * whether the machine is on hold
 */
static const gchar *const EOS_UPDATER_BRANCH_SELECTED = "99f48aac-b5a0-426d-95f4-18af7d081c4e";
#endif

gboolean
is_checksum_an_update (OstreeRepo *repo,
                       const gchar *checksum,
                       GVariant **commit,
                       GError **error)
{
  g_autofree gchar *cur = NULL;
  g_autoptr(GVariant) current_commit = NULL;
  g_autoptr(GVariant) update_commit = NULL;
  gboolean is_newer;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (checksum != NULL, FALSE);
  g_return_val_if_fail (commit != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  cur = eos_updater_get_booted_checksum (error);
  if (cur == NULL)
    return FALSE;

  if (!ostree_repo_load_commit (repo, cur, &current_commit, NULL, error))
    return FALSE;

  if (!ostree_repo_load_commit (repo, checksum, &update_commit, NULL, error))
    return FALSE;

  /* Determine if the new commit is newer than the old commit to prevent
   * inadvertent (or malicious) attempts to downgrade the system.
   */
  is_newer = ostree_commit_get_timestamp (update_commit) > ostree_commit_get_timestamp (current_commit);
  /* if we have a checksum for the remote upgrade candidate
   * and it's ≠ what we're currently booted into, advertise it as such.
   */
  if (is_newer && g_strcmp0 (cur, checksum) != 0)
    *commit = g_steal_pointer (&update_commit);
  return TRUE;
}

static void
eos_metrics_info_finalize_impl (EosMetricsInfo *info)
{
  g_free (info->vendor);
  g_free (info->product);
  g_free (info->ref);
}

EOS_DEFINE_REFCOUNTED (EOS_METRICS_INFO,
                       EosMetricsInfo,
                       eos_metrics_info,
                       NULL,
                       eos_metrics_info_finalize_impl)

static void
eos_update_info_dispose_impl (EosUpdateInfo *info)
{
  g_clear_pointer (&info->commit, g_variant_unref);
  g_clear_object (&info->extensions);
}

static void
eos_update_info_finalize_impl (EosUpdateInfo *info)
{
  g_free (info->checksum);
  g_free (info->refspec);
  g_free (info->original_refspec);
  g_strfreev (info->urls);
}

EOS_DEFINE_REFCOUNTED (EOS_UPDATE_INFO,
                       EosUpdateInfo,
                       eos_update_info,
                       eos_update_info_dispose_impl,
                       eos_update_info_finalize_impl)

EosUpdateInfo *
eos_update_info_new (const gchar *checksum,
                     GVariant *commit,
                     const gchar *refspec,
                     const gchar *original_refspec,
                     const gchar * const *urls,
                     EosExtensions *extensions)
{
  EosUpdateInfo *info;

  g_return_val_if_fail (checksum != NULL, NULL);
  g_return_val_if_fail (commit != NULL, NULL);
  g_return_val_if_fail (refspec != NULL, NULL);
  g_return_val_if_fail (original_refspec != NULL, NULL);
  g_return_val_if_fail (EOS_IS_EXTENSIONS (extensions), NULL);

  info = g_object_new (EOS_TYPE_UPDATE_INFO, NULL);
  info->checksum = g_strdup (checksum);
  info->commit = g_variant_ref (commit);
  info->refspec = g_strdup (refspec);
  info->original_refspec = g_strdup (original_refspec);
  info->urls = g_strdupv ((gchar **) urls);
  info->extensions = g_object_ref (extensions);

  return info;
}

static void
eos_metadata_fetch_data_dispose_impl (EosMetadataFetchData *fetch_data)
{
  g_main_context_pop_thread_default (fetch_data->context);
  g_clear_pointer (&fetch_data->context, g_main_context_unref);
  g_clear_object (&fetch_data->task);
}

EOS_DEFINE_REFCOUNTED (EOS_METADATA_FETCH_DATA,
                       EosMetadataFetchData,
                       eos_metadata_fetch_data,
                       eos_metadata_fetch_data_dispose_impl,
                       NULL)

EosMetadataFetchData *
eos_metadata_fetch_data_new (GTask *task,
                             EosUpdaterData *data,
                             GMainContext *context)
{
  EosMetadataFetchData *fetch_data;

  g_return_val_if_fail (G_IS_TASK (task), NULL);
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (context != NULL, NULL);

  fetch_data = g_object_new (EOS_TYPE_METADATA_FETCH_DATA, NULL);
  fetch_data->task = g_object_ref (task);
  fetch_data->data = data;
  fetch_data->context = g_main_context_ref (context);

  g_main_context_push_thread_default (context);
  return fetch_data;
}

static gchar *
cleanstr (gchar *s)
{
  gchar *read;
  gchar *write;

  if (s == NULL)
    return s;

  for (read = write = s; *read != '\0'; ++read)
    {
      /* only allow printable */
      if (*read < 32 || *read > 126)
        continue;
      *write = *read;
      ++write;
    }
  *write = '\0';

  return s;
}

static gboolean
process_single_group (GKeyFile *bkf,
                      const gchar *group_name,
                      gboolean *out_on_hold,
                      gchar **out_ref,
                      GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *ref = NULL;
  gboolean on_hold;

  ref = g_key_file_get_string (bkf, group_name, OSTREE_REF_KEY, error);
  if (ref == NULL)
    return FALSE;

  on_hold = g_key_file_get_boolean (bkf, group_name, ON_HOLD_KEY, &local_error);
  /* The "OnHold" key is optional. */
  if (local_error != NULL &&
      !g_error_matches (local_error,
                        G_KEY_FILE_ERROR,
                        G_KEY_FILE_ERROR_KEY_NOT_FOUND))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  *out_on_hold = on_hold;
  *out_ref = g_steal_pointer (&ref);
  return TRUE;
}

static gboolean
process_branch_file (GKeyFile *bkf,
                     const gchar *group_name,
                     gboolean *out_on_hold,
                     gchar **out_ref,
                     GError **error)
{
  /* Check for product-specific entry */
  if (g_key_file_has_group (bkf, group_name))
    {
      message ("Product-specific branch configuration found");
      if (!process_single_group (bkf, group_name, out_on_hold, out_ref, error))
        return FALSE;
      if (*out_on_hold)
        message ("Product is on hold, nothing to upgrade here");
      return TRUE;
    }
  /* Check for a DEFAULT_GROUP entry */
  if (g_key_file_has_group (bkf, DEFAULT_GROUP))
    {
      message ("No product-specific branch configuration found, following %s",
               DEFAULT_GROUP);
      if (!process_single_group (bkf, DEFAULT_GROUP, out_on_hold, out_ref, error))
        return FALSE;
      if (*out_on_hold)
        message ("No product-specific configuration and %s is on hold, "
                 "nothing to upgrade here", DEFAULT_GROUP);
      return TRUE;
    }

  *out_on_hold = FALSE;
  *out_ref = NULL;
  return TRUE;
}

gboolean
get_upgrade_info_from_branch_file (EosBranchFile *branch_file,
                                   gchar **upgrade_refspec,
                                   gchar **original_refspec,
                                   EosMetricsInfo **metrics,
                                   GError **error)
{
  g_autoptr(OstreeDeployment) booted_deployment = NULL;
  g_autofree gchar *booted_refspec = NULL;
  g_autofree gchar *booted_remote = NULL;
  g_autofree gchar *booted_ref = NULL;
  g_autoptr(GHashTable) hw_descriptors = NULL;
  g_autofree gchar *vendor = NULL;
  g_autofree gchar *product = NULL;
  g_autofree gchar *product_group = NULL;
  g_autofree gchar *upgrade_ref = NULL;
  gboolean on_hold = FALSE;

  g_return_val_if_fail (EOS_IS_BRANCH_FILE (branch_file), FALSE);
  g_return_val_if_fail (upgrade_refspec != NULL, FALSE);
  g_return_val_if_fail (original_refspec != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  booted_deployment = eos_updater_get_booted_deployment (error);

  if (!get_origin_refspec (booted_deployment, &booted_refspec, error))
    return FALSE;

  if (!ostree_parse_refspec (booted_refspec, &booted_remote, &booted_ref, error))
    return FALSE;

  hw_descriptors = get_hw_descriptors ();
  vendor = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, VENDOR_KEY)));
  product = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, PRODUCT_KEY)));

  product_group = g_strdup_printf ("%s %s", vendor, product);
  if (!process_branch_file (branch_file->branch_file, product_group, &on_hold, &upgrade_ref, error))
    return FALSE;

  if (on_hold)
    upgrade_ref = g_strdup (booted_ref);
  else
    {
      if (upgrade_ref == NULL)
        {
          message ("No product-specific branch configuration or %s found, "
                   "following the origin file", DEFAULT_GROUP);
          upgrade_ref = g_strdup (booted_ref);
        }

      message ("Using product branch %s", upgrade_ref);
      *upgrade_refspec = g_strdup_printf ("%s:%s", booted_remote, upgrade_ref);
      *original_refspec = g_strdup (booted_refspec);
    }

  if (metrics != NULL)
    {
      g_autoptr(EosMetricsInfo) info = NULL;

      info = g_object_new (EOS_TYPE_METRICS_INFO, NULL);
      info->vendor = g_steal_pointer (&vendor);
      info->product = g_steal_pointer (&product);
      info->ref = g_steal_pointer (&upgrade_ref);
      info->on_hold = on_hold;
      info->branch_file = g_object_ref (branch_file);
      *metrics = g_steal_pointer (&info);
    }

  return TRUE;
}

static GVariant *
get_repo_pull_options (const gchar *url_override,
                       const gchar *ref)
{
  g_auto(GVariantBuilder) builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (url_override != NULL)
    g_variant_builder_add (&builder, "{s@v}", "override-url",
                           g_variant_new_variant (g_variant_new_string (url_override)));
  g_variant_builder_add (&builder, "{s@v}", "flags",
                         g_variant_new_variant (g_variant_new_int32 (OSTREE_REPO_PULL_FLAGS_COMMIT_ONLY)));
  g_variant_builder_add (&builder, "{s@v}", "refs",
                         g_variant_new_variant (g_variant_new_strv (&ref, 1)));

  return g_variant_ref_sink (g_variant_builder_end (&builder));
};

static gboolean
get_extensions_url (OstreeRepo *repo,
                    const gchar *remote_name,
                    const gchar *url_override,
                    gchar **extensions_url,
                    GError **error)
{
  g_autofree gchar *url = g_strdup (url_override);

  if (url == NULL &&
      !ostree_repo_remote_get_url (repo, remote_name, &url, error))
    return FALSE;

  *extensions_url = g_build_path ("/", url, "extensions", "eos", NULL);
  return TRUE;
}

static gboolean
must_download_file_and_signature (const gchar *url,
                                  GBytes **contents,
                                  GBytes **signature,
                                  GError **error)
{
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GBytes) sig_bytes = NULL;

  if (!download_file_and_signature (url, &bytes, &sig_bytes, error))
    return FALSE;

  if (bytes == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to download the file at %s", url);
      return FALSE;
    }

  if (sig_bytes == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to download the signature for the file at %s", url);
      return FALSE;
    }

  *contents = g_steal_pointer (&bytes);
  *signature = g_steal_pointer (&sig_bytes);
  return TRUE;
}

static gboolean
commit_checksum_from_extensions_ref (OstreeRepo *repo,
                                     GCancellable *cancellable,
                                     const gchar *remote_name,
                                     const gchar *ref,
                                     const gchar *url_override,
                                     gchar **out_checksum,
                                     EosExtensions **out_extensions,
                                     GError **error)
{
  g_autofree gchar *extensions_url = NULL;
  g_autofree gchar *eos_ref_url = NULL;
  g_autoptr(GBytes) contents = NULL;
  g_autoptr(GBytes) signature = NULL;
  g_autoptr(OstreeGpgVerifyResult) gpg_result = NULL;
  g_autofree gchar *checksum = NULL;
  gconstpointer raw_data;
  gsize raw_len;
  g_autoptr(EosExtensions) extensions = NULL;
  g_autoptr(EosRef) ext_ref = NULL;
  g_autoptr(GKeyFile) ref_keyfile = NULL;
  g_autofree gchar *actual_ref = NULL;

  if (!get_extensions_url (repo, remote_name, url_override, &extensions_url, error))
    return FALSE;

  eos_ref_url = g_build_path ("/", extensions_url, "refs.d", ref, NULL);
  if (!must_download_file_and_signature (eos_ref_url, &contents, &signature, error))
    return FALSE;

  gpg_result = ostree_repo_gpg_verify_data (repo,
                                            remote_name,
                                            contents,
                                            signature,
                                            NULL,
                                            NULL,
                                            cancellable,
                                            error);
  if (!ostree_gpg_verify_result_require_valid_signature (gpg_result, error))
    return FALSE;

  ref_keyfile = g_key_file_new ();
  raw_data = g_bytes_get_data (contents, &raw_len);
  if (!g_key_file_load_from_data (ref_keyfile,
                                  raw_data,
                                  raw_len,
                                  G_KEY_FILE_NONE,
                                  error))
    return FALSE;

  actual_ref = g_key_file_get_string (ref_keyfile,
                                      "mapping",
                                      "ref",
                                      error);
  if (actual_ref == NULL)
    return FALSE;

  if (g_strcmp0 (actual_ref, ref) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The file under %s contains data about ref %s, instead of %s",
                   eos_ref_url, actual_ref, ref);
      return FALSE;
    }

  checksum = g_key_file_get_string (ref_keyfile,
                                    "mapping",
                                    "commit",
                                    error);
  if (checksum == NULL)
    return FALSE;
  g_strstrip (checksum);

  if (!ostree_validate_structureof_checksum_string (checksum, error))
    return FALSE;

  ext_ref = eos_ref_new_empty ();
  ext_ref->contents = g_steal_pointer (&contents);
  ext_ref->signature = g_steal_pointer (&signature);
  ext_ref->name = g_strdup (ref);

  extensions = eos_extensions_new_empty ();
  g_ptr_array_add (extensions->refs, g_steal_pointer (&ext_ref));

  *out_checksum = g_steal_pointer (&checksum);
  *out_extensions = g_steal_pointer (&extensions);
  return TRUE;
}

/* stolen from ostree (ot_variant_bsearch_str) and slightly modified */
static gboolean
bsearch_variant (GVariant *array,
                 const gchar *str,
                 gsize *out_pos)
{
  gsize imax, imin;
  gsize imid = -1;
  gsize n;

  n = g_variant_n_children (array);
  if (n == 0)
    return FALSE;

  imax = n - 1;
  imin = 0;
  while (imax >= imin)
    {
      g_autoptr(GVariant) child = NULL;
      const char *cur;
      int cmp;

      imid = (imin + imax) / 2;

      child = g_variant_get_child_value (array, imid);
      g_variant_get_child (child, 0, "&s", &cur, NULL);

      cmp = strcmp (cur, str);
      if (cmp < 0)
        imin = imid + 1;
      else if (cmp > 0)
        {
          if (imid == 0)
            break;
          imax = imid - 1;
        }
      else
        {
          *out_pos = imid;
          return TRUE;
        }
    }

  *out_pos = imid;
  return FALSE;
}

static gchar *
get_commit_checksum_from_summary (GVariant *summary,
                                  const gchar *ref,
                                  GError **error)
{
  g_autoptr(GVariant) refs_v = NULL;
  g_autoptr(GVariant) ref_v = NULL;
  g_autoptr(GVariant) ref_data_v = NULL;
  g_autoptr(GVariant) checksum_v = NULL;
  gsize ref_idx;

  /* summary variant is (a(s(taya{sv}))a{sv}) */
  /* this gets the a(s(taya{sv})) variant */
  refs_v = g_variant_get_child_value (summary, 0);
  if (!bsearch_variant (refs_v, ref, &ref_idx))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "No ref '%s' in summary",
                   ref);
      return NULL;
    }

  /* this gets the (s(taya{sv})) variant */
  ref_v = g_variant_get_child_value (refs_v, ref_idx);
  /* this gets the (taya{sv}) variant */
  ref_data_v = g_variant_get_child_value (ref_v, 1);
  g_variant_get (ref_data_v, "(t@ay@a{sv})", NULL, &checksum_v, NULL);

  if (!ostree_validate_structureof_csum_v (checksum_v, error))
    return NULL;

  return ostree_checksum_from_bytes_v (checksum_v);
};

static gboolean
commit_checksum_from_any_summary (OstreeRepo *repo,
                                  const gchar *remote_name,
                                  const gchar *ref,
                                  const gchar *summary_url,
                                  GCancellable *cancellable,
                                  gchar **out_checksum,
                                  EosExtensions **out_extensions,
                                  GError **error)
{
  g_autoptr(GBytes) contents = NULL;
  g_autoptr(GBytes) signature = NULL;
  g_autoptr(OstreeGpgVerifyResult) gpg_result = NULL;
  g_autoptr(GVariant) summary = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(EosExtensions) extensions = NULL;

  if (!must_download_file_and_signature (summary_url, &contents, &signature, error))
    return FALSE;

  gpg_result = ostree_repo_verify_summary (repo,
                                           remote_name,
                                           contents,
                                           signature,
                                           cancellable,
                                           error);
  if (!ostree_gpg_verify_result_require_valid_signature (gpg_result, error))
    return FALSE;

  summary = g_variant_ref_sink (g_variant_new_from_bytes (OSTREE_SUMMARY_GVARIANT_FORMAT,
                                                          contents,
                                                          FALSE));
  checksum = get_commit_checksum_from_summary (summary, ref, error);
  if (checksum == NULL)
    return FALSE;

  extensions = eos_extensions_new_empty ();
  extensions->summary = g_steal_pointer (&contents);
  extensions->summary_sig = g_steal_pointer (&signature);

  *out_checksum = g_steal_pointer (&checksum);
  *out_extensions = g_steal_pointer (&extensions);
  return TRUE;
}

static gboolean
commit_checksum_from_extensions_summary (OstreeRepo *repo,
                                         GCancellable *cancellable,
                                         const gchar *remote_name,
                                         const gchar *ref,
                                         const gchar *url_override,
                                         gchar **out_checksum,
                                         EosExtensions **out_extensions,
                                         GError **error)
{
  g_autofree gchar *eos_summary_url = NULL;
  g_autofree gchar *extensions_url = NULL;

  if (!get_extensions_url (repo, remote_name, url_override, &extensions_url, error))
    return FALSE;

  eos_summary_url = g_build_path ("/", extensions_url, "eos-summary", NULL);
  return commit_checksum_from_any_summary (repo,
                                           remote_name,
                                           ref,
                                           eos_summary_url,
                                           cancellable,
                                           out_checksum,
                                           out_extensions,
                                           error);
}

static gboolean
commit_checksum_from_summary (OstreeRepo *repo,
                              GCancellable *cancellable,
                              const gchar *remote_name,
                              const gchar *ref,
                              const gchar *url_override,
                              gchar **out_checksum,
                              EosExtensions **out_extensions,
                              GError **error)
{
  g_autofree gchar *url = NULL;
  g_autofree gchar *summary_url = NULL;

  if (url_override != NULL)
    url = g_strdup (url_override);
  else if (!ostree_repo_remote_get_url (repo, remote_name, &url, error))
    return FALSE;

  summary_url = g_build_path ("/", url, "summary", NULL);
  return commit_checksum_from_any_summary (repo,
                                           remote_name,
                                           ref,
                                           summary_url,
                                           cancellable,
                                           out_checksum,
                                           out_extensions,
                                           error);
}

static gboolean
fetch_commit_checksum (OstreeRepo *repo,
                       GCancellable *cancellable,
                       const gchar *remote_name,
                       const gchar *ref,
                       const gchar *url_override,
                       gchar **out_checksum,
                       EosExtensions **out_extensions,
                       GError **error)
{
  g_autoptr(GPtrArray) failures = NULL;
  g_autofree gchar *failures_str = NULL;
  g_autoptr(GError) local_error = NULL;

  if (commit_checksum_from_extensions_ref (repo,
                                           cancellable,
                                           remote_name,
                                           ref,
                                           url_override,
                                           out_checksum,
                                           out_extensions,
                                           &local_error))
    return TRUE;

  failures = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (failures, g_strdup_printf ("Failed to get extensions refs: %s", local_error->message));
  g_clear_error (&local_error);
  if (commit_checksum_from_extensions_summary (repo,
                                               cancellable,
                                               remote_name,
                                               ref,
                                               url_override,
                                               out_checksum,
                                               out_extensions,
                                               &local_error))
    return TRUE;

  g_ptr_array_add (failures, g_strdup_printf ("Failed to get extensions summary: %s", local_error->message));
  g_clear_error (&local_error);
  if (commit_checksum_from_summary (repo,
                                    cancellable,
                                    remote_name,
                                    ref,
                                    url_override,
                                    out_checksum,
                                    out_extensions,
                                    &local_error))
    return TRUE;

  g_ptr_array_add (failures, g_strdup_printf ("Failed to get ostree summary: %s", local_error->message));
  g_ptr_array_add (failures, NULL);
  failures_str = g_strjoinv ("; ", (gchar **)failures->pdata);
  g_clear_error (&local_error);
  if (url_override != NULL)
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Failed to get the checksum of the latest commit in ref %s from remote %s with URL %s, reasons: %s",
                 ref,
                 remote_name,
                 url_override,
                 failures_str);
  else
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Failed to get the checksum of the latest commit in ref %s from remote %s, reasons: %s",
                 ref,
                 remote_name,
                 failures_str);
  return FALSE;
}

gboolean
fetch_latest_commit (OstreeRepo *repo,
                     GCancellable *cancellable,
                     const gchar *remote_name,
                     const gchar *ref,
                     const gchar *url_override,
                     gchar **out_checksum,
                     EosExtensions **out_extensions,
                     GError **error)
{
  g_autoptr(GBytes) summary_bytes = NULL;
  g_autoptr(GVariant) summary = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) options = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (remote_name != NULL, FALSE);
  g_return_val_if_fail (ref != NULL, FALSE);
  g_return_val_if_fail (out_checksum != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  options = get_repo_pull_options (url_override, ref);
  if (!ostree_repo_pull_with_options (repo,
                                      remote_name,
                                      options,
                                      NULL,
                                      cancellable,
                                      error))
    return FALSE;

  return fetch_commit_checksum (repo,
                                cancellable,
                                remote_name,
                                ref,
                                url_override,
                                out_checksum,
                                out_extensions,
                                error);
}

static SoupURI *
get_uri_to_sig (SoupURI *uri)
{
  g_autofree gchar *sig_path = NULL;
  SoupURI *sig_uri = soup_uri_copy (uri);

  sig_path = g_strconcat (soup_uri_get_path (uri), ".sig", NULL);
  soup_uri_set_path (sig_uri, sig_path);

  return sig_uri;
}

static GBytes *
download_file (SoupURI *uri)
{
  g_autoptr(GBytes) contents = NULL;

  if (soup_uri_get_scheme (uri) == SOUP_URI_SCHEME_FILE)
    {
      g_autoptr(GFile) file = g_file_new_for_path (soup_uri_get_path (uri));

      eos_updater_read_file_to_bytes (file, NULL, &contents, NULL);
    }
  else
    {
      g_autoptr(SoupSession) soup = soup_session_new ();
      g_autoptr(SoupMessage) msg = soup_message_new_from_uri ("GET", uri);
      guint status = soup_session_send_message (soup, msg);

      if (SOUP_STATUS_IS_SUCCESSFUL (status))
        g_object_get (msg,
                      SOUP_MESSAGE_RESPONSE_BODY_DATA, &contents,
                      NULL);
    }

  return g_steal_pointer (&contents);
}

gboolean
download_file_and_signature (const gchar *url,
                             GBytes **contents,
                             GBytes **signature,
                             GError **error)
{
  g_autoptr(SoupURI) uri = soup_uri_new (url);
  g_autoptr(SoupURI) sig_uri = NULL;

  if (uri == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid URL %s", url);
      return FALSE;
    }

  sig_uri = get_uri_to_sig (uri);
  *contents = download_file (uri);
  *signature = download_file (sig_uri);
  return TRUE;
}

gboolean
get_origin_refspec (OstreeDeployment *booted_deployment,
                    gchar **out_refspec,
                    GError **error)
{
  GKeyFile *origin;
  g_autofree gchar *refspec = NULL;

  g_return_val_if_fail (OSTREE_IS_DEPLOYMENT (booted_deployment), FALSE);
  g_return_val_if_fail (out_refspec != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  origin = ostree_deployment_get_origin (booted_deployment);
  if (origin == NULL)
    {
      const gchar *osname = ostree_deployment_get_osname (booted_deployment);
      const gchar *booted = ostree_deployment_get_csum (booted_deployment);

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No origin found for %s (%s), cannot upgrade",
                   osname, booted);
      return FALSE;
    }

  refspec = g_key_file_get_string (origin, "origin", "refspec", error);
  if (refspec == NULL)
    return FALSE;

  *out_refspec = g_steal_pointer (&refspec);
  return TRUE;
}

static void
get_custom_hw_descriptors (GHashTable *hw_descriptors,
                           const gchar *path)
{
  g_autoptr(GKeyFile) keyfile = NULL;
  g_auto(GStrv) keys = NULL;
  gchar **iter;
  const gchar *group = "descriptors";

  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_file (keyfile,
                                  path,
                                  G_KEY_FILE_NONE,
                                  NULL))
    return;

  keys = g_key_file_get_keys (keyfile,
                              group,
                              NULL,
                              NULL);
  if (keys == NULL)
    return;

  for (iter = keys; *iter != NULL; ++iter)
    {
      const gchar *key = *iter;
      gchar *value = g_key_file_get_string (keyfile,
                                            group,
                                            key,
                                            NULL);

      if (value == NULL)
        continue;

      g_hash_table_insert (hw_descriptors, g_strdup (key), value);
    }
}

static void
get_arm_hw_descriptors (GHashTable *hw_descriptors)
{
  g_autoptr(GFile) fp = g_file_new_for_path (DT_COMPATIBLE);
  g_autofree gchar *fc = NULL;

  if (g_file_load_contents (fp, NULL, &fc, NULL, NULL, NULL))
    {
      g_auto(GStrv) sv = g_strsplit (fc, ",", -1);

      if (sv && sv[0])
        g_hash_table_insert (hw_descriptors, g_strdup (VENDOR_KEY),
                             g_strdup (g_strstrip (sv[0])));
      if (sv && sv[1])
        g_hash_table_insert (hw_descriptors, g_strdup (PRODUCT_KEY),
                             g_strdup (g_strstrip (sv[1])));
    }
}

static void
get_x86_hw_descriptors (GHashTable *hw_descriptors)
{
  guint i;

  for (i = 0; dmi_attributes[i]; i++)
    {
      g_autofree gchar *path = NULL;
      g_autoptr(GFile) fp = NULL;
      g_autofree gchar *fc = NULL;
      gsize len;

      path = g_build_filename (DMI_PATH, dmi_attributes[i], NULL);
      fp = g_file_new_for_path (path);
      if (g_file_load_contents (fp, NULL, &fc, &len, NULL, NULL))
        {
          if (len > 128)
            fc[128] = '\0';
          g_hash_table_insert (hw_descriptors, g_strdup (dmi_attributes[i]),
                               g_strdup (g_strstrip (fc)));
        }
    }
}

static gchar *
get_custom_descriptors_path (void)
{
  return eos_updater_dup_envvar_or ("EOS_UPDATER_TEST_UPDATER_CUSTOM_DESCRIPTORS_PATH",
                                    NULL);
}

GHashTable *
get_hw_descriptors (void)
{
  GHashTable *hw_descriptors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, g_free);
  g_autofree gchar *custom_descriptors = get_custom_descriptors_path ();

  if (custom_descriptors != NULL)
    get_custom_hw_descriptors (hw_descriptors,
                               custom_descriptors);
  else if (g_file_test (DT_COMPATIBLE, G_FILE_TEST_EXISTS))
    get_arm_hw_descriptors (hw_descriptors);
  else
    get_x86_hw_descriptors (hw_descriptors);

  if (!g_hash_table_lookup (hw_descriptors, VENDOR_KEY))
    g_hash_table_insert (hw_descriptors, g_strdup (VENDOR_KEY),
                         g_strdup ("EOSUNKNOWN"));

  if (!g_hash_table_lookup (hw_descriptors, PRODUCT_KEY))
    g_hash_table_insert (hw_descriptors, g_strdup (PRODUCT_KEY),
                         g_strdup ("EOSUNKNOWN"));

  return hw_descriptors;
}

static gboolean
get_timestamp_from_branch_file (EosBranchFile *branch_file,
                                GDateTime **out_timestamp,
                                GError **error)
{
  if (branch_file->raw_signature != NULL)
    return eos_updater_get_timestamp_from_branch_file_keyfile (branch_file->branch_file,
                                                               out_timestamp,
                                                               error);

  if (branch_file->download_time != NULL)
    {
      *out_timestamp = g_date_time_ref (branch_file->download_time);
      return TRUE;
    }

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "No timestamp found in the branch file");

  return FALSE;
}

static gboolean
timestamps_check (EosBranchFile *cached_branch_file,
                  EosBranchFile *branch_file,
                  gboolean *valid,
                  GError **error)
{
  g_autoptr(GDateTime) cached_stamp = NULL;
  g_autoptr(GDateTime) stamp = NULL;

  if (!get_timestamp_from_branch_file (cached_branch_file,
                                       &cached_stamp,
                                       error))
    return FALSE;

  if (!get_timestamp_from_branch_file (branch_file,
                                       &stamp,
                                       NULL))
    {
      *valid = FALSE;
      return TRUE;
    }

  *valid = (g_date_time_compare (stamp, cached_stamp) >= 0);
  return TRUE;
}

static gboolean
ostree_paths_check (OstreeRepo *repo,
                    EosBranchFile *branch_file,
                    gboolean *valid,
                    GError **error)
{
  g_auto(GStrv) ostree_paths = NULL;
  g_autofree gchar *ostree_path = NULL;

  if (!eos_updater_get_ostree_paths_from_branch_file_keyfile (branch_file->branch_file,
                                                              &ostree_paths,
                                                              NULL))
    {
      *valid = FALSE;
      return TRUE;
    }

  if (!eos_updater_get_ostree_path (repo,
                                    &ostree_path,
                                    error))
    return FALSE;

  *valid = g_strv_contains ((const gchar *const *)ostree_paths,
                            ostree_path);
  return TRUE;
}

gboolean
check_branch_file_validity (OstreeRepo *repo,
                            EosBranchFile *cached_branch_file,
                            EosBranchFile *branch_file,
                            gboolean *out_valid,
                            GError **error)
{
  gboolean do_timestamps_check = TRUE;
  gboolean do_ostree_paths_check = TRUE;
  gboolean timestamps_valid = TRUE;
  gboolean ostree_paths_valid = TRUE;

  if (cached_branch_file->raw_signature != NULL &&
      branch_file->raw_signature == NULL)
    {
      /* main server reverted to unsigned branch files? fishy.
       */
      *out_valid = FALSE;
      return TRUE;
    }

  if (cached_branch_file->raw_signature == NULL &&
      branch_file->raw_signature != NULL)
    {
      /* main server switched to signed branch files, skip timestamp
       * comparison, but check if the field exists.
       */
      timestamps_valid = eos_updater_get_timestamp_from_branch_file_keyfile (branch_file->branch_file,
                                                                             NULL,
                                                                             error);
      do_timestamps_check = FALSE;
    }

  if (branch_file->raw_signature == NULL)
    /* old and unsigned branch file format, skip ostree paths check
     */
    do_ostree_paths_check = FALSE;

  if (do_timestamps_check &&
      !timestamps_check (cached_branch_file,
                         branch_file,
                         &timestamps_valid,
                         error))
    return FALSE;

  if (do_ostree_paths_check &&
      !ostree_paths_check (repo,
                           branch_file,
                           &ostree_paths_valid,
                           error))
    return FALSE;

  *out_valid = timestamps_valid && ostree_paths_valid;
  return TRUE;
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
get_latest_uam (GArray *sources,
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

  for (idx = 0; idx < sources->len; ++idx)
    {
      EosUpdaterDownloadSource source = g_array_index (sources,
                                                       EosUpdaterDownloadSource,
                                                       idx);
      const gchar *name = download_source_to_string (source);
      UpdateAndMetrics *uam = g_hash_table_lookup (latest, name);

      if (uam != NULL)
        return uam;
    }

  return NULL;
}

EosUpdateInfo *
run_fetchers (EosMetadataFetchData *fetch_data,
              GPtrArray *fetchers,
              GPtrArray *source_variants,
              GArray *sources)
{
  guint idx;
  g_autoptr(GHashTable) source_to_uam = g_hash_table_new_full (NULL,
                                                               NULL,
                                                               NULL,
                                                               (GDestroyNotify)update_and_metrics_free);

  g_return_val_if_fail (EOS_IS_METADATA_FETCH_DATA (fetch_data), NULL);
  g_return_val_if_fail (fetchers != NULL, NULL);
  g_return_val_if_fail (source_variants != NULL, NULL);
  g_return_val_if_fail (sources != NULL, NULL);
  g_return_val_if_fail (fetchers->len == source_variants->len, NULL);
  g_return_val_if_fail (source_variants->len == sources->len, NULL);

  for (idx = 0; idx < fetchers->len; ++idx)
    {
      MetadataFetcher fetcher = g_ptr_array_index (fetchers, idx);
      GVariant *source_variant = g_ptr_array_index (source_variants, idx);
      g_autoptr(EosUpdateInfo) info = NULL;
      g_autoptr(EosMetricsInfo) metrics = NULL;
      EosUpdaterDownloadSource source = g_array_index (sources,
                                                       EosUpdaterDownloadSource,
                                                       idx);
      const gchar *name = download_source_to_string (source);
      g_autoptr(GError) local_error = NULL;
      UpdateAndMetrics *uam;
      const GVariantType *source_variant_type = g_variant_get_type (source_variant);

      if (!g_variant_type_equal (source_variant_type, G_VARIANT_TYPE_VARDICT))
        {
          g_autofree gchar *expected = g_variant_type_dup_string (G_VARIANT_TYPE_VARDICT);
          g_autofree gchar *got = g_variant_type_dup_string (source_variant_type);

          message ("Wrong type of %s fetcher configuration, expected %s, got %s",
                   name,
                   expected,
                   got);
          continue;
        }

      if (!fetcher (fetch_data, source_variant, &info, &metrics, &local_error))
        {
          message ("Failed to poll metadata from source %s: %s",
                   name, local_error->message);
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
      UpdateAndMetrics *latest_uam = NULL;

      latest_uam = get_latest_uam (sources, source_to_uam, FALSE);
      maybe_send_metric (latest_uam->metrics);
      latest_uam = get_latest_uam (sources, source_to_uam, TRUE);
      if (latest_uam != NULL)
        return g_object_ref (latest_uam->update);
    }

  return NULL;
}

const gchar *
download_source_to_string (EosUpdaterDownloadSource source)
{
  switch (source)
    {
    case EOS_UPDATER_DOWNLOAD_MAIN:
    case EOS_UPDATER_DOWNLOAD_LAN:
    case EOS_UPDATER_DOWNLOAD_VOLUME:
      return order_key_str[source];

    case EOS_UPDATER_DOWNLOAD_N_SOURCES:
      break;
    }

  g_assert_not_reached ();
}

gboolean
string_to_download_source (const gchar *str,
                           EosUpdaterDownloadSource *source,
                           GError **error)
{
  EosUpdaterDownloadSource idx;

  g_return_val_if_fail (str != NULL, FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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

void
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
