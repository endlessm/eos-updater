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

#include "eos-updater-data.h"

#include <libeos-updater-util/refcounted.h>

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
G_DECLARE_FINAL_TYPE (EosMetricsInfo,
                      eos_metrics_info,
                      EOS,
                      METRICS_INFO,
                      GObject)

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
G_DECLARE_FINAL_TYPE (EosUpdateInfo,
                      eos_update_info,
                      EOS,
                      UPDATE_INFO,
                      GObject)

struct _EosUpdateInfo
{
  GObject parent_instance;

  gchar *checksum;
  GVariant *commit;
  gchar *new_refspec;
  gchar *old_refspec;
  gchar **urls;
};

EosUpdateInfo *
eos_update_info_new (const gchar *csum,
                     GVariant *commit,
                     const gchar *new_refspec,
                     const gchar *old_refspec,
                     const gchar * const *urls);

GDateTime *
eos_update_info_get_commit_timestamp (EosUpdateInfo *info);

#define EOS_TYPE_METADATA_FETCH_DATA eos_metadata_fetch_data_get_type ()
G_DECLARE_FINAL_TYPE (EosMetadataFetchData,
                      eos_metadata_fetch_data,
                      EOS,
                      METADATA_FETCH_DATA,
                      GObject)

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
                                     EosUpdateInfo **info,
                                     GError **error);

gboolean get_booted_refspec (gchar               **booted_refspec,
                             gchar               **booted_remote,
                             gchar               **booted_ref,
                             OstreeCollectionRef **booted_collection_ref,
                             GError              **error);

gboolean fetch_latest_commit (OstreeRepo *repo,
                              GCancellable *cancellable,
                              const gchar *refspec,
                              const gchar *url_override,
                              gchar **out_checksum,
                              gchar **out_new_refspec,
                              GError **error);

gboolean parse_latest_commit (OstreeRepo           *repo,
                              const gchar          *refspec,
                              gchar               **out_checksum,
                              gchar               **out_new_refspec,
                              OstreeCollectionRef **out_new_collection_ref,
                              GCancellable         *cancellable,
                              GError              **error);

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
                             GArray *sources);

void metadata_fetch_finished (GObject *object,
                              GAsyncResult *res,
                              gpointer user_data);
G_END_DECLS
