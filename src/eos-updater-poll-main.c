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

#include <libeos-updater-util/util.h>

gboolean
metadata_fetch_from_main (EosMetadataFetchData *fetch_data,
                          GVariant *source_variant,
                          EosUpdateInfo **out_info,
                          GError **error)
{
  OstreeRepo *repo = fetch_data->data->repo;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *new_refspec = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(EosExtensions) extensions = NULL;

  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!get_booted_refspec (&refspec, NULL, NULL, error))
    return FALSE;

  if (!fetch_latest_commit (repo,
                            g_task_get_cancellable (fetch_data->task),
                            refspec,
                            NULL,
                            &checksum,
                            &new_refspec,
                            &extensions,
                            error))
    return FALSE;

  if (!is_checksum_an_update (repo, checksum, &commit, error))
    return FALSE;

  if (commit != NULL)
    info = eos_update_info_new (checksum,
                                commit,
                                new_refspec,
                                refspec,
                                NULL,
                                extensions);

  *out_info = g_steal_pointer (&info);

  return TRUE;
}
