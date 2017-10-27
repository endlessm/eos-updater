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

#pragma once

#include <ostree.h>

G_BEGIN_DECLS

typedef struct EosUpdaterData EosUpdaterData;

struct EosUpdaterData
{
  OstreeRepo *repo;

  /* fields below are meant to be shared between some update stages;
   * when adding a new one, document it.
   */

  /* overridden_urls field is filled with some of the results of the
   * polling stage and it is used during fetch stage to select a
   * server to download the data from.
   */
  gchar **overridden_urls;

  /* The results from ostree_repo_find_remotes_async(), which contain all the
   * possible sources of the given refs, including internet, LAN and USB sources
   * (depending on what OstreeRepoFinders were enabled in the poll stage).
   * This needs to be passed from poll() to fetch().
   * May be NULL if using the fallback code in poll(). */
  OstreeRepoFinderResult **results;
};

#define EOS_UPDATER_DATA_CLEARED { NULL, NULL }

void eos_updater_data_init (EosUpdaterData *data,
                            OstreeRepo *repo);

void eos_updater_data_clear (EosUpdaterData *data);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (EosUpdaterData, eos_updater_data_clear)

G_END_DECLS
