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

#include "eos-updater-poll-main.h"

#include "eos-util.h"

#include <libsoup/soup.h>

static const gchar *const BRANCHES_CONFIG_PATH = "extensions/eos/branch_file";
static const gchar *const BRANCHES_CONFIG_PATH_OLD = "eos-branch";

static gboolean
try_all_branch_file_sources (OstreeRepo *repo,
                             const gchar *baseurl,
                             const gchar *query,
                             GBytes **out_branch_file_contents,
                             GBytes **out_signature_contents,
                             GError **error)
{
  const gchar *const paths [] = {
    BRANCHES_CONFIG_PATH,
    BRANCHES_CONFIG_PATH_OLD
  };
  guint idx;

  for (idx = 0; idx < G_N_ELEMENTS (paths); ++idx)
    {
      g_autofree gchar *uri = NULL;
      g_autoptr(GError) local_error = NULL;
      g_autoptr(GBytes) branch_file_contents = NULL;
      g_autoptr(GBytes) signature_contents = NULL;

      uri = g_strconcat (baseurl, "/", paths[idx], "?", query, NULL);
      if (!download_file_and_signature (uri,
                                        &branch_file_contents,
                                        &signature_contents,
                                        &local_error))
        {
          message ("Failed to download branch config data and the signature from %s: %s",
                   uri,
                   local_error->message);
          continue;
        }

      if (branch_file_contents == NULL)
        {
          message ("No branch config data available under %s", uri);
          continue;
        }

      if (signature_contents == NULL)
        message ("No signature for the branch config data available");
      else
        {
          g_autoptr(OstreeGpgVerifyResult) gpg_result = NULL;

          gpg_result = ostree_repo_gpg_verify_data (repo,
                                                    NULL,
                                                    branch_file_contents,
                                                    signature_contents,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    &local_error);
          if (!ostree_gpg_verify_result_require_valid_signature (gpg_result,
                                                                 &local_error))
            {
              message ("GPG validation of the branch config data signature failed: %s",
                       local_error->message);
              continue;
            }
        }

      *out_branch_file_contents = g_steal_pointer (&branch_file_contents);
      *out_signature_contents = g_steal_pointer (&signature_contents);
      return TRUE;
    }

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Failed to download branch config data, cannot upgrade");
  return FALSE;
}

static gboolean
download_branch_file (OstreeRepo *repo,
                      EosBranchFile **out_branch_file,
                      GError **error)
{
  g_autofree gchar *booted_refspec = NULL;
  g_autofree gchar *booted_remote = NULL;
  g_autofree gchar *booted_ref = NULL;
  g_autofree gchar *baseurl = NULL;
  g_autoptr(GHashTable) hw_descriptors = NULL;
  g_autoptr(OstreeDeployment) booted_deployment = NULL;
  g_autoptr(GBytes) branch_file_contents = NULL;
  g_autoptr(GBytes) signature_contents = NULL;
  g_autofree gchar *query = NULL;
  g_autoptr(GDateTime) now = NULL;
  g_autoptr(EosBranchFile) branch_file = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (out_branch_file != NULL, FALSE);

  booted_deployment = eos_updater_get_booted_deployment (error);
  if (booted_deployment == NULL)
    return FALSE;

  if (!get_origin_refspec (booted_deployment, &booted_refspec, error))
    return FALSE;

  baseurl = eos_updater_get_baseurl (booted_deployment, repo, error);
  if (baseurl == NULL)
    return FALSE;

  if (!ostree_parse_refspec (booted_refspec, &booted_remote, &booted_ref, error))
    return FALSE;

  hw_descriptors = get_hw_descriptors ();
  g_hash_table_insert (hw_descriptors, g_strdup ("ref"), g_strdup (booted_ref));
  g_hash_table_insert (hw_descriptors, g_strdup ("commit"), g_strdup (ostree_deployment_get_csum (booted_deployment)));
  query = soup_form_encode_hash (hw_descriptors);
  if (!try_all_branch_file_sources (repo,
                                    baseurl,
                                    query,
                                    &branch_file_contents,
                                    &signature_contents,
                                    error))
    return FALSE;

  now = g_date_time_new_now_utc ();
  branch_file = eos_branch_file_new_from_raw (branch_file_contents,
                                              signature_contents,
                                              now,
                                              error);
  if (branch_file == NULL)
    return FALSE;

  *out_branch_file = g_steal_pointer (&branch_file);
  return TRUE;
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

static gboolean
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

gboolean
metadata_fetch_from_main (EosMetadataFetchData *fetch_data,
                          EosUpdateInfo **out_info,
                          EosMetricsInfo **out_metrics,
                          GError **error)
{
  OstreeRepo *repo = fetch_data->data->repo;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *orig_refspec = NULL;
  g_autoptr(EosBranchFile) branch_file = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_autoptr(EosMetricsInfo) metrics = NULL;
  gboolean valid;

  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (out_metrics != NULL, FALSE);

  if (!download_branch_file (repo,
                             &branch_file,
                             error))
    return FALSE;

  if (!check_branch_file_validity (repo,
                                   fetch_data->data->branch_file,
                                   branch_file,
                                   &valid,
                                   error))
    return FALSE;

  if (!valid)
    g_set_object (&branch_file, fetch_data->data->branch_file);

  if (!get_upgrade_info_from_branch_file (branch_file,
                                          &refspec,
                                          &orig_refspec,
                                          &metrics,
                                          error))
    return FALSE;

  if (!metrics->on_hold)
    {
      g_autofree gchar *checksum = NULL;
      g_autoptr(GVariant) commit = NULL;
      g_autofree gchar *remote = NULL;
      g_autofree gchar *ref = NULL;
      g_autoptr(EosExtensions) extensions = NULL;

      if (!ostree_parse_refspec (refspec, &remote, &ref, error))
        return FALSE;

      if (!fetch_latest_commit (repo,
                                g_task_get_cancellable (fetch_data->task),
                                remote,
                                ref,
                                NULL,
                                &checksum,
                                &extensions,
                                error))
        return FALSE;

      if (!is_checksum_an_update (repo, checksum, &commit, error))
        return FALSE;

      g_set_object (&extensions->branch_file, branch_file);
      if (commit != NULL)
        info = eos_update_info_new (checksum,
                                    commit,
                                    refspec,
                                    orig_refspec,
                                    NULL,
                                    extensions);
    }

  *out_info = g_steal_pointer (&info);
  *out_metrics = g_steal_pointer (&metrics);
  return TRUE;
}
