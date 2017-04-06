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

#include "eos-updater-poll-main.h"
#include "eos-updater-poll-volume.h"

#include <libeos-updater-util/util.h>

#include <libsoup/soup.h>

const gchar *const VOLUME_FETCHER_PATH_KEY = "volume-path";

static gboolean
get_repo_from_volume (const gchar *raw_volume_path,
                      GCancellable *cancellable,
                      OstreeRepo **out_volume_repo,
                      gchar **out_repo_url,
                      GError **error)
{
  g_autoptr(GFile) volume_path = g_file_new_for_path (raw_volume_path);
  g_autoptr(GFile) repo_path = g_file_get_child (volume_path, "eos-update");
  g_autoptr(OstreeRepo) volume_repo = ostree_repo_new (repo_path);

  if (!ostree_repo_open (volume_repo, NULL, error))
    return FALSE;

  *out_repo_url = g_file_get_uri (ostree_repo_get_path (volume_repo));
  *out_volume_repo = g_steal_pointer (&volume_repo);
  return TRUE;
}

static gboolean
get_volume_options_from_variant (GVariant *source_variant,
                                 gchar **raw_volume_path,
                                 GError **error)
{
  g_auto(GVariantDict) dict;

  g_variant_dict_init (&dict, source_variant);

  if (!g_variant_dict_lookup (&dict,
                              VOLUME_FETCHER_PATH_KEY,
                              "s",
                              raw_volume_path))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No %s option specified or it has wrong type",
                   VOLUME_FETCHER_PATH_KEY);
      return FALSE;
    }

  return TRUE;
}

gboolean
metadata_fetch_from_volume (EosMetadataFetchData *fetch_data,
                            GVariant *source_variant,
                            EosUpdateInfo **out_info,
                            GError **error)
{
  OstreeRepo *repo = fetch_data->data->repo;
  GCancellable *cancellable = g_task_get_cancellable (fetch_data->task);
  g_autoptr(OstreeRepo) volume_repo = NULL;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *raw_volume_path = NULL;
  g_autofree gchar *repo_url = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autoptr(EosExtensions) extensions = NULL;
  const gchar *urls[] = { repo_url, NULL };

  g_return_val_if_fail (source_variant != NULL, FALSE);
  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!get_volume_options_from_variant (source_variant,
                                        &raw_volume_path,
                                        error))
    return FALSE;

  if (!get_repo_from_volume (raw_volume_path,
                             cancellable,
                             &volume_repo,
                             &repo_url,
                             error))
    return FALSE;

  if (!get_booted_refspec (&refspec, &remote, &ref, error))
    return FALSE;

  if (!fetch_latest_commit (repo,
                            cancellable,
                            remote,
                            ref,
                            repo_url,
                            &checksum,
                            &extensions,
                            error))
    return FALSE;

  if (!is_checksum_an_update (repo, checksum, &commit, error))
    return FALSE;

  if (commit != NULL)
    *out_info = eos_update_info_new (checksum,
                                     commit,
                                     refspec,  /* for upgrade */
                                     refspec,  /* original */
                                     urls,
                                     extensions);

  return TRUE;
}
