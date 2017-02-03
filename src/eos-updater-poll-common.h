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

#include "eos-updater-data.h"

#include "eos-extensions.h"
#include "eos-refcounted.h"

#include <ostree.h>

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

gboolean
is_checksum_an_update (OstreeRepo *repo,
                       const gchar *checksum,
                       GVariant **commit,
                       GError **error);

#define EOS_TYPE_METRICS_INFO eos_metrics_info_get_type ()
EOS_DECLARE_REFCOUNTED (EosMetricsInfo,
                        eos_metrics_info,
                        EOS,
                        METRICS_INFO)

struct _EosMetricsInfo
{
  GObject parent_instance;

  gchar *vendor;
  gchar *product;
  gchar *ref;
};

EosMetricsInfo *
eos_metrics_info_new (const gchar *booted_ref);

#define EOS_TYPE_UPDATE_INFO eos_update_info_get_type ()
EOS_DECLARE_REFCOUNTED (EosUpdateInfo,
                        eos_update_info,
                        EOS,
                        UPDATE_INFO)

struct _EosUpdateInfo
{
  GObject parent_instance;

  gchar *checksum;
  GVariant *commit;
  gchar *refspec;
  gchar *original_refspec;
  gchar **urls;
  EosExtensions *extensions;
};

EosUpdateInfo *
eos_update_info_new (const gchar *csum,
                     GVariant *commit,
                     const gchar *refspec,
                     const gchar *original_refspec,
                     const gchar * const *urls,
                     EosExtensions *extensions);

GDateTime *
eos_update_info_get_commit_timestamp (EosUpdateInfo *info);

#define EOS_TYPE_METADATA_FETCH_DATA eos_metadata_fetch_data_get_type ()
EOS_DECLARE_REFCOUNTED (EosMetadataFetchData,
                        eos_metadata_fetch_data,
                        EOS,
                        METADATA_FETCH_DATA)

struct _EosMetadataFetchData
{
  GObject parent_instance;

  GTask *task;
  EosUpdaterData *data;
  GMainContext *context;
};

EosMetadataFetchData *
eos_metadata_fetch_data_new (GTask *task,
                             EosUpdaterData *data,
                             GMainContext *context);

typedef gboolean (*MetadataFetcher) (EosMetadataFetchData *fetch_data,
                                     GVariant *source_variant,
                                     EosUpdateInfo **info,
                                     GError **error);

gboolean get_booted_refspec (gchar **booted_refspec,
                             gchar **booted_remote,
                             gchar **booted_ref,
                             GError **error);

gboolean fetch_latest_commit (OstreeRepo *repo,
                              GCancellable *cancellable,
                              const gchar *remote_name,
                              const gchar *ref,
                              const gchar *url_override,
                              gchar **out_checksum,
                              EosExtensions **out_extensions,
                              GError **error);

gboolean download_file_and_signature (const gchar *url,
                                      GBytes **contents,
                                      GBytes **signature,
                                      GError **error);

gboolean get_origin_refspec (OstreeDeployment *booted_deployment,
                             gchar **out_refspec,
                             GError **error);

GHashTable *get_hw_descriptors (void);

typedef enum
{
  EOS_UPDATER_DOWNLOAD_MAIN = 0,
  EOS_UPDATER_DOWNLOAD_LAN,
  EOS_UPDATER_DOWNLOAD_VOLUME,

  EOS_UPDATER_DOWNLOAD_FIRST = EOS_UPDATER_DOWNLOAD_MAIN,
  EOS_UPDATER_DOWNLOAD_LAST = EOS_UPDATER_DOWNLOAD_VOLUME,
} EosUpdaterDownloadSource;

const gchar *download_source_to_string (EosUpdaterDownloadSource source);

gboolean string_to_download_source (const gchar *str,
                                    EosUpdaterDownloadSource *source,
                                    GError **error);

EosUpdateInfo *run_fetchers (EosMetadataFetchData *fetch_data,
                             GPtrArray *fetchers,
                             GPtrArray *source_variants,
                             GArray *sources);

void metadata_fetch_finished (GObject *object,
                              GAsyncResult *res,
                              gpointer user_data);
G_END_DECLS
