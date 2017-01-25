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

#include "eos-updater-data.h"

#include "eos-util.h"

#include <string.h>

gboolean
eos_updater_data_init (EosUpdaterData *data,
                       OstreeRepo *repo,
                       GError **error)
{
  g_autoptr (GError) child_error = NULL;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  memset (data, 0, sizeof *data);
  data->repo = g_object_ref (repo);

  data->branch_file = eos_branch_file_new_from_repo (repo, NULL, &child_error);

  /* If no branch file exists, we are probably upgrading from a system with an
   * older version of eos-updater. Assume it exists and is zero-sized. */
  if (g_error_matches (child_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      g_autoptr (GBytes) contents = NULL;
      g_autoptr (GDateTime) download_time = NULL;

      g_message ("Using blank branch file");

      contents = g_bytes_new (NULL, 0);
      download_time = g_date_time_new_from_unix_utc (0);

      g_clear_error (&child_error);
      data->branch_file = eos_branch_file_new_from_raw (contents, NULL,
                                                        download_time,
                                                        &child_error);
    }

  if (child_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&child_error));
      return FALSE;
    }

  return TRUE;
}

void
eos_updater_data_clear (EosUpdaterData *data)
{
  g_return_if_fail (data != NULL);

  g_clear_pointer (&data->overridden_urls, g_strfreev);
  g_clear_object (&data->branch_file);
  g_clear_object (&data->repo);
}
