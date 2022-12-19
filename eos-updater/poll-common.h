/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

#include <eos-updater/data.h>
#include <glib.h>
#include <gio/gio.h>
#include <ostree.h>

G_BEGIN_DECLS

gboolean
is_checksum_an_update (OstreeRepo *repo,
                       const gchar *update_checksum,
                       const gchar *booted_ref,
                       const gchar *update_ref,
                       GVariant **out_commit,
                       gboolean *out_is_update_user_visible,
                       gchar **out_booted_version,
                       gchar **out_update_version,
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
  gchar *version;
  gchar **urls;
  gboolean offline_results_only;
  gboolean is_user_visible;
  gchar *release_notes_uri;

  OstreeRepoFinderResult **results;  /* (owned) (array zero-terminated=1) */
};

EosUpdateInfo *
eos_update_info_new (const gchar *csum,
                     GVariant *commit,
                     const gchar *new_refspec,
                     const gchar *old_refspec,
                     const gchar *version,
                     gboolean is_user_visible,
                     const gchar *release_notes_uri,
                     const gchar * const *urls,
                     gboolean offline_results_only,
                     OstreeRepoFinderResult **results);

GDateTime *
eos_update_info_get_commit_timestamp (EosUpdateInfo *info);

typedef gboolean (*MetadataFetcher) (OstreeRepo     *repo,
                                     GMainContext   *context,
                                     EosUpdateInfo **out_info,
                                     GCancellable   *cancellable,
                                     GError        **error);

gboolean get_booted_refspec (OstreeDeployment     *booted_deployment,
                             gchar               **booted_refspec,
                             gchar               **booted_remote,
                             gchar               **booted_ref,
                             OstreeCollectionRef **booted_collection_ref,
                             GError              **error);

gboolean get_refspec_to_upgrade_on (gchar               **refspec_to_upgrade_on,
                                    gchar               **remote_to_upgrade_on,
                                    gchar               **ref_to_upgrade_on,
                                    OstreeCollectionRef **booted_collection_ref,
                                    GError              **error);

gboolean fetch_latest_commit (OstreeRepo *repo,
                              GCancellable *cancellable,
                              GMainContext *context,
                              const gchar *refspec,
                              const gchar *url_override,
                              GPtrArray *finders,
                              OstreeCollectionRef *collection_ref,
                              OstreeRepoFinderResult ***out_results,
                              gchar **out_checksum,
                              gchar **out_new_refspec,
                              gchar **out_version,
                              gchar **out_release_notes_uri_template,
                              GError **error);

gboolean parse_latest_commit (OstreeRepo           *repo,
                              const gchar          *refspec,
                              gboolean             *out_redirect_followed,
                              gchar               **out_checksum,
                              gchar               **out_new_refspec,
                              OstreeCollectionRef **out_new_collection_ref,
                              gchar               **out_version,
                              gchar               **out_release_notes_uri_template,
                              GCancellable         *cancellable,
                              GError              **error);

GHashTable *get_hw_descriptors (void);

void metrics_report_successful_poll (EosUpdateInfo *update);
gchar *eos_update_info_to_string (EosUpdateInfo *update);

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

EosUpdateInfo *run_fetchers (OstreeRepo   *repo,
                             GMainContext *context,
                             GCancellable *cancellable,
                             GPtrArray    *fetchers,
                             GArray       *sources,
                             GError      **error);

void metadata_fetch_finished (GObject *object,
                              GAsyncResult *res,
                              gpointer user_data);
G_END_DECLS
