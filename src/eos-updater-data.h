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

#pragma once

#include "eos-updater-branch-file.h"
#include "eos-updater-extensions.h"

#include <ostree.h>

G_BEGIN_DECLS

typedef enum
{
  EOS_UPDATER_DOWNLOAD_FIRST,

  EOS_UPDATER_DOWNLOAD_MAIN = EOS_UPDATER_DOWNLOAD_FIRST,
  EOS_UPDATER_DOWNLOAD_LAN,

  EOS_UPDATER_DOWNLOAD_N_SOURCES,
} EosUpdaterDownloadSource;

typedef struct EosUpdaterData EosUpdaterData;

struct EosUpdaterData
{
  OstreeRepo *repo;
  EosUpdaterDownloadSource *download_order;
  gsize n_download_sources;
  EosBranchFile *branch_file;
  EosExtensions *extensions;
  gchar **overridden_urls;
};

#define EOS_UPDATER_DATA_CLEARED { NULL, NULL, 0, NULL, NULL, NULL }

gboolean eos_updater_data_init (EosUpdaterData *data,
                                OstreeRepo *repo,
                                GError **error);

void eos_updater_data_clear (EosUpdaterData *data);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (EosUpdaterData, eos_updater_data_clear)

G_END_DECLS
