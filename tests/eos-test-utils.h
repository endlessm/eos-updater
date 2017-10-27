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

#include <gio/gio.h>
#include <libeos-updater-util/refcounted.h>
#include <ostree.h>

#include "spawn-utils.h"

G_BEGIN_DECLS

typedef struct
{
  GTestDBus *dbus;
  GFile *tmpdir;
  GFile *gpg_home;
} EosUpdaterFixture;

void eos_updater_fixture_setup (EosUpdaterFixture *fixture,
                                gconstpointer user_data);

void eos_updater_fixture_teardown (EosUpdaterFixture *fixture,
                                   gconstpointer user_data);

#define eos_test_add(testpath, tdata, ftest) g_test_add (testpath, EosUpdaterFixture, tdata, eos_updater_fixture_setup, ftest, eos_updater_fixture_teardown)

gchar *get_keyid (GFile *gpg_home);

extern const gchar *const default_vendor;
extern const gchar *const default_product;
extern const gchar *const default_collection_id;
extern const gchar *const default_ref;
extern const OstreeCollectionRef *default_collection_ref;
extern const gchar *const default_ostree_path;
extern const gchar *const default_remote_name;

#define EOS_TEST_TYPE_SUBSERVER eos_test_subserver_get_type ()
G_DECLARE_FINAL_TYPE (EosTestSubserver,
                      eos_test_subserver,
                      EOS_TEST,
                      SUBSERVER,
                      GObject)

struct _EosTestSubserver
{
  GObject parent_instance;

  gchar *collection_id;
  gchar *keyid;
  gchar *ostree_path;
  GHashTable *ref_to_commit;

  GFile *repo;
  GFile *tree;
  gchar *url;
  GFile *gpg_home;
};

static inline GHashTable *
eos_test_subserver_ref_to_commit_new (void)
{
  return g_hash_table_new_full (ostree_collection_ref_hash, ostree_collection_ref_equal,
                                (GDestroyNotify) ostree_collection_ref_free, NULL);
}

EosTestSubserver *eos_test_subserver_new (const gchar *collection_id,
                                          GFile *gpg_home,
                                          const gchar *keyid,
                                          const gchar *ostree_path,
                                          GHashTable *ref_to_commit);

gboolean eos_test_subserver_update (EosTestSubserver *subserver,
                                    GError **error);

#define EOS_TEST_TYPE_SERVER eos_test_server_get_type ()
G_DECLARE_FINAL_TYPE (EosTestServer,
                      eos_test_server,
                      EOS_TEST,
                      SERVER,
                      GObject)

struct _EosTestServer
{
  GObject parent_instance;

  GFile *root;
  gchar *url;
  GPtrArray *subservers;
};

EosTestServer *eos_test_server_new (GFile *server_root,
                                    GPtrArray *subservers,
                                    GError **error);

EosTestServer *eos_test_server_new_quick (GFile *server_root,
                                          const gchar *vendor,
                                          const gchar *product,
                                          const OstreeCollectionRef *collection_ref,
                                          guint commit_number,
                                          GFile *gpg_home,
                                          const gchar *keyid,
                                          const gchar *ostree_path,
                                          GError **error);

#define EOS_TEST_TYPE_CLIENT eos_test_client_get_type ()
G_DECLARE_FINAL_TYPE (EosTestClient,
                      eos_test_client,
                      EOS_TEST,
                      CLIENT,
                      GObject)

struct _EosTestClient
{
  GObject parent_instance;

  GFile *root;
  gchar *vendor;
  gchar *product;
  gchar *remote_name;
  gchar *ostree_path;
};

typedef enum
  {
    DOWNLOAD_MAIN,
    DOWNLOAD_LAN,
    DOWNLOAD_VOLUME
  } DownloadSource;

EosTestClient *eos_test_client_new (GFile *client_root,
                                    const gchar *remote_name,
                                    EosTestSubserver *subserver,
                                    const OstreeCollectionRef *collection_ref,
                                    const gchar *vendor,
                                    const gchar *product,
                                    GError **error);

gboolean eos_test_client_run_updater (EosTestClient *client,
                                      DownloadSource *order,
                                      gsize n_sources,
                                      GPtrArray *override_uris,
                                      CmdAsyncResult *cmd,
                                      GError **error);

gboolean eos_test_client_reap_updater (EosTestClient *client,
                                       CmdAsyncResult *cmd,
                                       CmdResult *reaped,
                                       GError **error);

gboolean eos_test_client_run_update_server (EosTestClient *client,
                                            CmdAsyncResult *cmd,
                                            guint16 *out_port,
                                            GError **error);


gboolean eos_test_client_remove_update_server_quit_file (EosTestClient *client,
                                                         GError **error);

gboolean eos_test_client_wait_for_update_server (EosTestClient *client,
                                                 CmdAsyncResult *cmd,
                                                 CmdResult *reaped,
                                                 GError **error);

gboolean eos_test_client_reap_update_server (EosTestClient *client,
                                             CmdAsyncResult *cmd,
                                             CmdResult *reaped,
                                             GError **error);

gboolean eos_test_client_has_commit (EosTestClient *client,
                                     const gchar *osname,
                                     guint commit_number,
                                     gboolean *out_has_commit,
                                     GError **error);

gboolean eos_test_client_get_deployments (EosTestClient *client,
                                          const gchar *osname,
                                          gchar ***out_ids,
                                          GError **error);

gboolean eos_test_client_prepare_volume (EosTestClient *client,
                                         GFile *volume_path,
                                         GError **error);

GFile *eos_test_client_get_repo (EosTestClient *client);

GFile *eos_test_client_get_sysroot (EosTestClient *client);

const gchar *eos_test_client_get_big_file_path (void);

typedef enum _UpdateStep {
  UPDATE_STEP_NONE,
  UPDATE_STEP_POLL,
  UPDATE_STEP_FETCH,
  UPDATE_STEP_APPLY
} UpdateStep;

#define EOS_TEST_TYPE_AUTOUPDATER eos_test_autoupdater_get_type ()
G_DECLARE_FINAL_TYPE (EosTestAutoupdater,
                      eos_test_autoupdater,
                      EOS_TEST,
                      AUTOUPDATER,
                      GObject)

struct _EosTestAutoupdater
{
  GObject parent_instance;

  GFile *root;
  CmdResult *cmd;
};

EosTestAutoupdater *eos_test_autoupdater_new (GFile *autoupdater_root,
                                              UpdateStep final_auto_step,
                                              guint interval_in_days,
                                              gboolean update_on_mobile,
                                              GError **error);

gboolean eos_test_has_ostree_boot_id (void);

G_END_DECLS
