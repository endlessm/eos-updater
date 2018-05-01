/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include "eos-updater-object.h"
#include "eos-updater-poll-common.h"

#include <libeos-updater-util/util.h>

#ifdef HAS_EOSMETRICS_0

#include <eosmetrics/eosmetrics.h>

#endif /* HAS_EOSMETRICS_0 */

#include <ostree.h>

#include <libsoup/soup.h>

#include <string.h>

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

G_STATIC_ASSERT (G_N_ELEMENTS (order_key_str) == EOS_UPDATER_DOWNLOAD_LAST + 1);

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
  guint64 update_timestamp, current_timestamp;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (checksum != NULL, FALSE);
  g_return_val_if_fail (commit != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  cur = eos_updater_get_booted_checksum (error);
  if (cur == NULL)
    return FALSE;

  g_debug ("%s: current: %s, update: %s", G_STRFUNC, cur, checksum);

  if (!ostree_repo_load_commit (repo, cur, &current_commit, NULL, error))
    return FALSE;

  if (!ostree_repo_load_commit (repo, checksum, &update_commit, NULL, error))
    return FALSE;

  /* Determine if the new commit is newer than the old commit to prevent
   * inadvertent (or malicious) attempts to downgrade the system.
   */
  update_timestamp = ostree_commit_get_timestamp (update_commit);
  current_timestamp = ostree_commit_get_timestamp (current_commit);

  g_debug ("%s: current_timestamp: %" G_GUINT64_FORMAT ", "
           "update_timestamp: %" G_GUINT64_FORMAT,
           G_STRFUNC, update_timestamp, current_timestamp);

  is_newer = update_timestamp > current_timestamp;
  /* if we have a checksum for the remote upgrade candidate
   * and it's ≠ what we're currently booted into, advertise it as such.
   */
  if (is_newer && g_strcmp0 (cur, checksum) != 0)
    *commit = g_steal_pointer (&update_commit);
  else
    *commit = NULL;

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
}

static void
eos_update_info_finalize_impl (EosUpdateInfo *info)
{
  g_free (info->checksum);
  g_free (info->new_refspec);
  g_free (info->old_refspec);
  g_free (info->version);
  g_strfreev (info->urls);
  g_clear_pointer (&info->results, ostree_repo_finder_result_freev);
}

EOS_DEFINE_REFCOUNTED (EOS_UPDATE_INFO,
                       EosUpdateInfo,
                       eos_update_info,
                       eos_update_info_dispose_impl,
                       eos_update_info_finalize_impl)

/* Steals @results. */
EosUpdateInfo *
eos_update_info_new (const gchar *checksum,
                     GVariant *commit,
                     const gchar *new_refspec,
                     const gchar *old_refspec,
                     const gchar *version,
                     const gchar * const *urls,
                     OstreeRepoFinderResult **results)
{
  EosUpdateInfo *info;

  g_return_val_if_fail (checksum != NULL, NULL);
  g_return_val_if_fail (commit != NULL, NULL);
  g_return_val_if_fail (new_refspec != NULL, NULL);
  g_return_val_if_fail (old_refspec != NULL, NULL);

  info = g_object_new (EOS_TYPE_UPDATE_INFO, NULL);
  info->checksum = g_strdup (checksum);
  info->commit = g_variant_ref (commit);
  info->new_refspec = g_strdup (new_refspec);
  info->old_refspec = g_strdup (old_refspec);
  info->version = g_strdup (version);
  info->urls = g_strdupv ((gchar **) urls);
  info->results = g_steal_pointer (&results);

  return info;
}

GDateTime *
eos_update_info_get_commit_timestamp (EosUpdateInfo *info)
{
  g_return_val_if_fail (EOS_IS_UPDATE_INFO (info), NULL);

  return g_date_time_new_from_unix_utc ((gint64) ostree_commit_get_timestamp (info->commit));
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

EosMetricsInfo *
eos_metrics_info_new (const gchar *booted_ref)
{
  g_autoptr(GHashTable) hw_descriptors = NULL;
  g_autoptr(EosMetricsInfo) info = NULL;

  hw_descriptors = get_hw_descriptors ();

  info = g_object_new (EOS_TYPE_METRICS_INFO, NULL);
  info->vendor = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, VENDOR_KEY)));
  info->product = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, PRODUCT_KEY)));
  info->ref = g_strdup (booted_ref);

  return g_steal_pointer (&info);
}

gboolean
get_booted_refspec (OstreeDeployment     *booted_deployment,
                    gchar               **booted_refspec,
                    gchar               **booted_remote,
                    gchar               **booted_ref,
                    OstreeCollectionRef **booted_collection_ref,
                    GError              **error)
{
  GKeyFile *origin;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *collection_id = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (OSTREE_IS_DEPLOYMENT (booted_deployment), FALSE);
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

  if (!ostree_parse_refspec (refspec, &remote, &ref, error))
    return FALSE;
  if (remote == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid refspec ‘%s’ in origin: did not contain a remote name",
                   refspec);
      return FALSE;
    }

  repo = eos_updater_local_repo (&local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  if (!ostree_repo_get_remote_option (repo, remote, "collection-id", NULL, &collection_id, error))
    return FALSE;

  g_message ("Using product branch %s", ref);

  if (booted_collection_ref != NULL)
    *booted_collection_ref = (collection_id != NULL) ? ostree_collection_ref_new (collection_id, ref) : NULL;
  if (booted_refspec != NULL)
    *booted_refspec = g_steal_pointer (&refspec);
  if (booted_remote != NULL)
    *booted_remote = g_steal_pointer (&remote);
  if (booted_ref != NULL)
    *booted_ref = g_steal_pointer (&ref);

  return TRUE;
}

static gboolean
get_ref_to_upgrade_on_from_deployment (OstreeSysroot     *sysroot,
                                       OstreeDeployment  *booted_deployment,
                                       gchar            **out_ref_to_upgrade_from_deployment,
                                       GError           **error)
{
  const gchar *checksum = ostree_deployment_get_csum (booted_deployment);
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) metadata = NULL;
  g_autoptr(GVariant) ref_for_deployment_variant = NULL;
  const gchar *refspec_for_deployment = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (out_ref_to_upgrade_from_deployment != NULL, FALSE);

  if (!ostree_sysroot_get_repo (sysroot, &repo, NULL, error))
   return FALSE;

  if (!ostree_repo_load_variant (repo,
                                 OSTREE_OBJECT_TYPE_COMMIT,
                                 checksum,
                                 &commit,
                                 error))
    return FALSE;

  /* Look up the checkpoint target to see if there is one on this commit. */
  metadata = g_variant_get_child_value (commit, 0);
  ref_for_deployment_variant = g_variant_lookup_value (metadata,
                                                       "eos.checkpoint-target",
                                                       G_VARIANT_TYPE_STRING);

  /* No metadata tag on this commit, just return TRUE with no value */
  if (ref_for_deployment_variant == NULL)
    {
      *out_ref_to_upgrade_from_deployment = NULL;
      return TRUE;
    }

  refspec_for_deployment = g_variant_get_string (ref_for_deployment_variant, NULL);

  if (!ostree_parse_refspec (refspec_for_deployment, &remote, &ref, &local_error))
    {
      g_warning ("Failed to parse eos.checkpoint-target ref '%s', ignoring it",
                 refspec_for_deployment);
      *out_ref_to_upgrade_from_deployment = NULL;
      return TRUE;
    }

  if (remote != NULL)
    g_warning ("Ignoring remote '%s' in eos.checkpoint-target metadata '%s'",
               remote, refspec_for_deployment);

  *out_ref_to_upgrade_from_deployment = g_steal_pointer (&ref);
  return TRUE;
}

/* @refspec_to_upgrade_on is guaranteed to include a remote and a ref name. */
gboolean
get_refspec_to_upgrade_on (gchar               **refspec_to_upgrade_on,
                           gchar               **remote_to_upgrade_on,
                           gchar               **ref_to_upgrade_on,
                           OstreeCollectionRef **collection_ref_to_upgrade_on,
                           GError              **error)
{
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autoptr(OstreeCollectionRef) collection_ref = NULL;
  g_autofree gchar *ref_for_deployment = NULL;
  g_autoptr(OstreeSysroot) sysroot = ostree_sysroot_new_default ();
  g_autoptr(OstreeDeployment) booted_deployment = NULL;

  if (!ostree_sysroot_load (sysroot, NULL, error))
    return FALSE;

  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);

  if (booted_deployment == NULL)
    return FALSE;

  if (!get_booted_refspec (booted_deployment,
                           &refspec,
                           &remote,
                           &ref,
                           &collection_ref,
                           error))
    return FALSE;

  if (!get_ref_to_upgrade_on_from_deployment (sysroot,
                                              booted_deployment,
                                              &ref_for_deployment,
                                              error))
    return FALSE;

  /* Handle the ref from the commit's metadata */
  if (ref_for_deployment != NULL)
    {
      OstreeCollectionRef *orig_collection_ref = collection_ref;

      /* Replace the ref, refspec and collection_ref */
      g_free (ref);
      ref = g_steal_pointer (&ref_for_deployment);
      g_free (refspec);
      refspec = g_strdup_printf ("%s:%s", remote, ref);
      collection_ref = ostree_collection_ref_new (orig_collection_ref->collection_id,
                                                  ref);
      ostree_collection_ref_free (orig_collection_ref);
    }

  if (collection_ref_to_upgrade_on != NULL)
    *collection_ref_to_upgrade_on = g_steal_pointer (&collection_ref);
  if (refspec_to_upgrade_on != NULL)
    *refspec_to_upgrade_on = g_steal_pointer (&refspec);
  if (remote_to_upgrade_on != NULL)
    *remote_to_upgrade_on = g_steal_pointer (&remote);
  if (ref_to_upgrade_on != NULL)
    *ref_to_upgrade_on = g_steal_pointer (&ref);

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

/* @refspec *must* contain a remote and ref name (not just a ref name).
 * @out_new_refspec is guaranteed to include a remote and a ref name. */
gboolean
fetch_latest_commit (OstreeRepo *repo,
                     GCancellable *cancellable,
                     const gchar *refspec,
                     const gchar *url_override,
                     gchar **out_checksum,
                     gchar **out_new_refspec,
                     gchar **out_version,
                     GError **error)
{
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) options = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) rebase = NULL;
  g_autoptr(GVariant) metadata = NULL;
  g_autofree gchar *remote_name = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *new_refspec = NULL;
  gboolean redirect_followed = FALSE;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (refspec != NULL, FALSE);
  g_return_val_if_fail (out_checksum != NULL, FALSE);
  g_return_val_if_fail (out_new_refspec != NULL, FALSE);
  g_return_val_if_fail (out_version != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Check whether the commit is a redirection; if so, fetch the new ref and
   * check again. */
  do
    {
      g_clear_pointer (&remote_name, g_free);
      g_clear_pointer (&ref, g_free);
      g_clear_pointer (&new_refspec, g_free);
      g_clear_pointer (&checksum, g_free);

      if (!ostree_parse_refspec (refspec, &remote_name, &ref, error))
        return FALSE;
      g_assert (remote_name != NULL);  /* caller must guarantee this */

      options = get_repo_pull_options (url_override, ref);
      if (!ostree_repo_pull_with_options (repo,
                                          remote_name,
                                          options,
                                          NULL,
                                          cancellable,
                                          error))
        return FALSE;

      if (!parse_latest_commit (repo, refspec, &redirect_followed, &checksum,
                                &new_refspec, NULL, out_version, cancellable,
                                error))
        return FALSE;

      if (new_refspec != NULL)
        refspec = new_refspec;
    }
  while (redirect_followed);

  *out_checksum = g_steal_pointer (&checksum);
  if (new_refspec != NULL)
    *out_new_refspec = g_steal_pointer (&new_refspec);
  else
    *out_new_refspec = g_strdup (refspec);

  return TRUE;
}

/* @refspec *must* contain a remote and ref name (not just a ref name).
 * @out_new_refspec is guaranteed to include a remote and a ref name. */
gboolean
parse_latest_commit (OstreeRepo           *repo,
                     const gchar          *refspec,
                     gboolean             *out_redirect_followed,
                     gchar               **out_checksum,
                     gchar               **out_new_refspec,
                     OstreeCollectionRef **out_new_collection_ref,
                     gchar               **out_version,
                     GCancellable         *cancellable,
                     GError              **error)
{
  g_autofree gchar *ref = NULL;
  g_autofree gchar *remote_name = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) rebase = NULL;
  g_autoptr(GVariant) version = NULL;
  g_autoptr(GVariant) metadata = NULL;
  g_autofree gchar *collection_id = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (refspec != NULL, FALSE);
  g_return_val_if_fail (out_redirect_followed != NULL, FALSE);
  g_return_val_if_fail (out_checksum != NULL, FALSE);
  g_return_val_if_fail (out_new_refspec != NULL, FALSE);
  g_return_val_if_fail (out_version != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!ostree_parse_refspec (refspec, &remote_name, &ref, error))
    return FALSE;
  g_assert (remote_name != NULL);  /* caller must guarantee this */

  if (!ostree_repo_resolve_rev (repo, refspec, FALSE, &checksum, error))
    return FALSE;
  if (!ostree_repo_get_remote_option (repo, remote_name, "collection-id", NULL, &collection_id, error))
    return FALSE;

  if (!ostree_repo_load_variant (repo,
                                 OSTREE_OBJECT_TYPE_COMMIT,
                                 checksum,
                                 &commit,
                                 error))
    return FALSE;

  /* If this is a redirect commit, follow it and fetch the new ref instead
   * (unless the rebase is a loop; ignore that). */
  metadata = g_variant_get_child_value (commit, 0);
  rebase = g_variant_lookup_value (metadata, "ostree.endoflife-rebase", G_VARIANT_TYPE_STRING);

  if (rebase != NULL &&
      g_strcmp0 (g_variant_get_string (rebase, NULL), ref) != 0)
    {
      g_clear_pointer (&ref, g_free);
      ref = g_variant_dup_string (rebase, NULL);

      *out_redirect_followed = TRUE;
    }
  else
    *out_redirect_followed = FALSE;

  version = g_variant_lookup_value (metadata, "version", G_VARIANT_TYPE_STRING);
  if (version == NULL)
    *out_version = NULL;
  else
    *out_version = g_variant_dup_string (version, NULL);

  *out_checksum = g_steal_pointer (&checksum);
  *out_new_refspec = g_strconcat (remote_name, ":", ref, NULL);
  if (out_new_collection_ref != NULL && collection_id != NULL)
    *out_new_collection_ref = ostree_collection_ref_new (collection_id, ref);
  else if (out_new_collection_ref != NULL)
    *out_new_collection_ref = NULL;

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

static const gchar *
get_custom_descriptors_path (void)
{
  return g_getenv ("EOS_UPDATER_TEST_UPDATER_CUSTOM_DESCRIPTORS_PATH");
}

GHashTable *
get_hw_descriptors (void)
{
  GHashTable *hw_descriptors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, g_free);
  const gchar *custom_descriptors = get_custom_descriptors_path ();

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

static void
maybe_send_metric (EosMetricsInfo *metrics)
{
#ifdef HAS_EOSMETRICS_0
  static gboolean metric_sent = FALSE;

  if (metric_sent)
    return;

  g_message ("Recording metric event %s: (%s, %s, %s)",
             EOS_UPDATER_BRANCH_SELECTED, metrics->vendor, metrics->product,
             metrics->ref);
  emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                         EOS_UPDATER_BRANCH_SELECTED,
                                         g_variant_new ("(sssb)", metrics->vendor,
                                                        metrics->product,
                                                        metrics->ref,
                                                        (gboolean) FALSE  /* on-hold */));
  metric_sent = TRUE;
#endif
}

void
metrics_report_successful_poll (EosUpdateInfo *update)
{
  g_autofree gchar *new_ref = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(EosMetricsInfo) metrics = NULL;

  if (!ostree_parse_refspec (update->new_refspec, NULL, &new_ref, &error))
    {
      g_message ("Failed to get metrics: %s", error->message);
      return;
    }

  /* Send metrics about our ref: this is the ref we’re going to upgrade to,
   * and that’s not always the same as the one we’re currently on. */
  metrics = eos_metrics_info_new (new_ref);
  maybe_send_metric (metrics);
}

gchar *
eos_update_info_to_string (EosUpdateInfo *update)
{
  g_autofree gchar *update_urls = NULL;
  g_autoptr(GDateTime) timestamp = NULL;
  g_autofree gchar *timestamp_str = NULL;
  g_autoptr(GString) results_string = NULL;
  g_autofree gchar *results = NULL;
  const gchar *version = update->version;
  gsize i;

  if (update->urls != NULL)
    update_urls = g_strjoinv ("\n   ", update->urls);
  else
    update_urls = g_strdup ("");

  timestamp = eos_update_info_get_commit_timestamp (update);
  timestamp_str = g_date_time_format (timestamp, "%FT%T%:z");

  results_string = g_string_new ("");
  for (i = 0; update->results != NULL && update->results[i] != NULL; i++)
    {
      const OstreeRepoFinderResult *result = update->results[i];

      g_string_append_printf (results_string, "\n   %s, priority %d, %u refs",
                              ostree_remote_get_name (result->remote),
                              result->priority,
                              g_hash_table_size (result->ref_to_checksum));
    }
  if (update->results == NULL)
    g_string_append (results_string, "(no repo finder results)");

  results = g_string_free (g_steal_pointer (&results_string), FALSE);

  if (version == NULL)
    version = "(no version information)";

  return g_strdup_printf ("%s, %s, %s, %s, %s\n   %s%s",
                          update->checksum,
                          update->new_refspec,
                          update->old_refspec,
                          version,
                          timestamp_str,
                          update_urls,
                          results);
}

static EosUpdateInfo *
get_latest_update (GArray *sources,
                   GHashTable *source_to_update)
{
  g_autoptr(GHashTable) latest = g_hash_table_new (NULL, NULL);
  GHashTableIter iter;
  gpointer name_ptr;
  gpointer update_ptr;
  g_autoptr(GDateTime) latest_timestamp = NULL;
  gsize idx;

  g_debug ("%s: source_to_update mapping:", G_STRFUNC);

  g_hash_table_iter_init (&iter, source_to_update);
  while (g_hash_table_iter_next (&iter, &name_ptr, &update_ptr))
    {
      EosUpdateInfo *update = update_ptr;
      g_autofree gchar *update_string = eos_update_info_to_string (update);
      gint compare_value = 1;
      g_autoptr(GDateTime) update_timestamp = NULL;

      g_debug ("%s: - %s: %s", G_STRFUNC, (const gchar *) name_ptr, update_string);

      update_timestamp = eos_update_info_get_commit_timestamp (update);

      if (latest_timestamp != NULL)
        compare_value = g_date_time_compare (update_timestamp,
                                             latest_timestamp);
      if (compare_value > 0)
        {
          g_clear_pointer (&latest_timestamp, g_date_time_unref);
          latest_timestamp = g_date_time_ref (update_timestamp);
          g_hash_table_remove_all (latest);
          compare_value = 0;
        }

      if (compare_value == 0)
        g_hash_table_insert (latest, name_ptr, update_ptr);
    }

  g_debug ("%s: sources list:", G_STRFUNC);

  for (idx = 0; idx < sources->len; ++idx)
    {
      EosUpdaterDownloadSource source = g_array_index (sources,
                                                       EosUpdaterDownloadSource,
                                                       idx);
      const gchar *name = download_source_to_string (source);
      EosUpdateInfo *update = g_hash_table_lookup (latest, name);

      if (update != NULL)
        {
          g_debug ("%s: - %s (matched)", G_STRFUNC, name);
          return update;
        }
      else
        {
          g_debug ("%s: - %s", G_STRFUNC, name);
        }
    }

  return NULL;
}

EosUpdateInfo *
run_fetchers (OstreeRepo   *repo,
              GMainContext *context,
              GCancellable *cancellable,
              GPtrArray    *fetchers,
              GArray       *sources,
              GError      **error)
{
  guint idx;
  g_autoptr(GHashTable) source_to_update = g_hash_table_new_full (NULL,
                                                                  NULL,
                                                                  NULL,
                                                                  (GDestroyNotify) g_object_unref);

  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);
  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (fetchers != NULL, NULL);
  g_return_val_if_fail (sources != NULL, NULL);
  g_return_val_if_fail (fetchers->len == sources->len, NULL);

  for (idx = 0; idx < fetchers->len; ++idx)
    {
      MetadataFetcher fetcher = g_ptr_array_index (fetchers, idx);
      g_autoptr(EosUpdateInfo) info = NULL;
      EosUpdaterDownloadSource source = g_array_index (sources,
                                                       EosUpdaterDownloadSource,
                                                       idx);
      const gchar *name = download_source_to_string (source);
      g_autoptr(GError) local_error = NULL;

      if (!fetcher (repo, context, &info, cancellable, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return NULL;
            }

          g_message ("Failed to poll metadata from source %s: %s",
                     name, local_error->message);
          continue;
        }

      if (info != NULL)
        {
          g_hash_table_insert (source_to_update,
                               (gpointer) name, g_object_ref (info));
        }
    }

  if (g_hash_table_size (source_to_update) > 0)
    {
      EosUpdateInfo *latest_update = NULL;

      latest_update = get_latest_update (sources, source_to_update);
      if (latest_update != NULL)
        {
          metrics_report_successful_poll (latest_update);
          return g_object_ref (latest_update);
        }
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

    default:
      g_assert_not_reached ();
    }
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
       idx <= EOS_UPDATER_DOWNLOAD_LAST;
       ++idx)
    if (g_str_equal (str, order_key_str[idx]))
      break;

  if (idx > EOS_UPDATER_DOWNLOAD_LAST)
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

      g_strfreev (data->overridden_urls);
      data->overridden_urls = g_steal_pointer (&info->urls);

      g_clear_pointer (&data->results, ostree_repo_finder_result_freev);
      data->results = g_steal_pointer (&info->results);

      /* Everything is happy thusfar */
      /* if we have a checksum for the remote upgrade candidate
       * and it's ≠ what we're currently booted into, advertise it as such.
       */
      eos_updater_clear_error (updater, EOS_UPDATER_STATE_UPDATE_AVAILABLE);
      eos_updater_set_update_id (updater, info->checksum);
      eos_updater_set_update_refspec (updater, info->new_refspec);
      eos_updater_set_original_refspec (updater, info->old_refspec);
      eos_updater_set_version (updater, info->version);

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
              g_message ("No size summary data: %s", error->message);
              g_clear_error (&error);
            }
        }
    }
  else /* info == NULL means OnHold=true, nothing to do here */
    eos_updater_clear_error (updater, EOS_UPDATER_STATE_READY);

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
