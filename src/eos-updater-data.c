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

#include "eos-updater-data.h"

#include <libeos-updater-util/util.h>

#include <string.h>

void
eos_updater_data_init (EosUpdaterData *data,
                       OstreeRepo *repo)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (OSTREE_IS_REPO (repo));

  memset (data, 0, sizeof *data);
  data->repo = g_object_ref (repo);
}

void
eos_updater_data_clear (EosUpdaterData *data)
{
  g_return_if_fail (data != NULL);

  g_clear_pointer (&data->results, ostree_repo_finder_result_freev);
  g_clear_pointer (&data->overridden_urls, g_strfreev);
  g_clear_object (&data->repo);
}
