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

#include "spawn-utils.h"

#include <eos-refcounted.h>

#include <gio/gio.h>

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
extern const gchar *const default_ref;
extern const gchar *const default_ostree_path;
extern const gchar *const default_remote_name;

#define EOS_TEST_TYPE_DEVICE eos_test_device_get_type ()
EOS_DECLARE_REFCOUNTED (EosTestDevice,
                        eos_test_device,
                        EOS_TEST,
                        DEVICE)

struct _EosTestDevice
{
  GObject parent_instance;

  gchar *vendor;
  gchar *product;
  gchar *ref;
};

EosTestDevice *eos_test_device_new (const gchar *vendor,
                                    const gchar *product,
                                    const gchar *ref);

#define EOS_TEST_TYPE_SUBSERVER eos_test_subserver_get_type ()
EOS_DECLARE_REFCOUNTED (EosTestSubserver,
                        eos_test_subserver,
                        EOS_TEST,
                        SUBSERVER)

struct _EosTestSubserver
{
  GObject parent_instance;

  gchar *keyid;
  gchar *ostree_path;
  GPtrArray *devices;
  GHashTable *ref_to_commit;

  GFile *repo;
  GFile *tree;
  gchar *url;
  GFile *gpg_home;
};

static inline GHashTable *
eos_test_subserver_ref_to_commit_new (void)
{
  return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

EosTestSubserver *eos_test_subserver_new (GFile *gpg_home,
                                          const gchar *keyid,
                                          const gchar *ostree_path,
                                          GPtrArray *devices,
                                          GHashTable *ref_to_commit);

gboolean eos_test_subserver_update (EosTestSubserver *subserver,
                                    GError **error);

#define EOS_TEST_TYPE_SERVER eos_test_server_get_type ()
EOS_DECLARE_REFCOUNTED (EosTestServer,
                        eos_test_server,
                        EOS_TEST,
                        SERVER)

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
                                          const gchar *ref,
                                          guint commit,
                                          GFile *gpg_home,
                                          const gchar *keyid,
                                          const gchar *ostree_path,
                                          GError **error);

#define EOS_TEST_TYPE_CLIENT eos_test_client_get_type ()
EOS_DECLARE_REFCOUNTED (EosTestClient,
                        eos_test_client,
                        EOS_TEST,
                        CLIENT)

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
                                    const gchar *ref,
                                    const gchar *vendor,
                                    const gchar *product,
                                    GError **error);

gboolean eos_test_client_run_updater (EosTestClient *client,
                                      DownloadSource *order,
                                      GVariant **source_variants,
                                      gsize n_sources,
                                      CmdAsyncResult *cmd,
                                      GError **error);

gboolean eos_test_client_reap_updater (EosTestClient *client,
                                       CmdAsyncResult *cmd,
                                       CmdResult *reaped,
                                       GError **error);

gboolean eos_test_client_run_update_server (EosTestClient *client,
                                            CmdAsyncResult *cmd,
                                            GKeyFile **out_avahi_definition,
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

gboolean eos_test_client_store_definition (EosTestClient *client,
                                           const gchar *name,
                                           GKeyFile *avahi_definition,
                                           GError **error);

gboolean eos_test_client_has_commit (EosTestClient *client,
                                     const gchar *osname,
                                     guint commit_no,
                                     gboolean *out_has_commit,
                                     GError **error);

gboolean eos_test_client_prepare_volume (EosTestClient *client,
                                         GFile *volume_path,
                                         GError **error);

typedef enum _UpdateStep {
  UPDATE_STEP_NONE,
  UPDATE_STEP_POLL,
  UPDATE_STEP_FETCH,
  UPDATE_STEP_APPLY
} UpdateStep;

#define EOS_TEST_TYPE_AUTOUPDATER eos_test_autoupdater_get_type ()
EOS_DECLARE_REFCOUNTED (EosTestAutoupdater,
                        eos_test_autoupdater,
                        EOS_TEST,
                        AUTOUPDATER)

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

G_END_DECLS
