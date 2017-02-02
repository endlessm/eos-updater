/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 * Authors: Vivek Dasmohapatra <vivek@etla.org>
 *          Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#pragma once

#include "eos-updater-generated.h"
#include "eos-updater-types.h"

#include "eos-refcounted.h"

#include <ostree.h>

#include <libsoup/soup.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

static inline GPtrArray *
object_array_new (void)
{
  return g_ptr_array_new_with_free_func (g_object_unref);
}

#define message(_f, ...) \
  g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, _f, ## __VA_ARGS__)

/* id returned by g_bus_own_name */
typedef guint EosBusNameID;
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(EosBusNameID, g_bus_unown_name, 0)

#define EOS_UPDATER_ERROR (eos_updater_error_quark ())
GQuark eos_updater_error_quark (void);

const gchar *eos_updater_state_to_string (EosUpdaterState state);

OstreeRepo *eos_updater_local_repo (void);

gboolean eos_updater_save_or_delete  (GBytes *contents,
                                      GFile *dir,
                                      const gchar *filename,
                                      GCancellable *cancellable,
                                      GError **error);

gboolean eos_updater_create_extensions_dir (OstreeRepo *repo,
                                            GFile **dir,
                                            GError **error);

void eos_updater_set_error (EosUpdater *updater,
                            const GError *error);
void eos_updater_clear_error (EosUpdater *updater,
                              EosUpdaterState state);

OstreeDeployment *eos_updater_get_booted_deployment_from_loaded_sysroot (OstreeSysroot *sysroot,
                                                                         GError **error);

OstreeDeployment *eos_updater_get_booted_deployment (GError **error);

gchar *eos_updater_get_booted_checksum (GError **error);

gchar *eos_updater_get_baseurl (OstreeDeployment *booted_deployment,
                                OstreeRepo *repo,
                                GError **error);

gboolean eos_updater_get_ostree_path (OstreeRepo *repo,
                                      gchar **ostree_path,
                                      GError **error);

guint eos_updater_queue_callback (GMainContext *context,
                                  GSourceFunc function,
                                  gpointer user_data,
                                  const gchar *name);

gchar *eos_updater_dup_envvar_or (const gchar *envvar,
                                  const gchar *default_value);

GFile *eos_updater_get_eos_extensions_dir (OstreeRepo *repo);

gboolean get_first_uri_from_server (SoupServer *server,
                                    SoupURI **out_uri,
                                    GError **error);

gboolean eos_updater_read_file_to_bytes (GFile *file,
                                         GCancellable *cancellable,
                                         GBytes **out_bytes,
                                         GError **error);

#define EOS_TYPE_QUIT_FILE eos_quit_file_get_type ()
EOS_DECLARE_REFCOUNTED (EosQuitFile, eos_quit_file, EOS, QUIT_FILE)

typedef enum
{
  EOS_QUIT_FILE_QUIT,
  EOS_QUIT_FILE_KEEP_CHECKING
} EosQuitFileCheckResult;

typedef EosQuitFileCheckResult (* EosQuitFileCheckCallback) (gpointer user_data);

EosQuitFile *eos_updater_setup_quit_file (const gchar *path,
                                          EosQuitFileCheckCallback check_callback,
                                          gpointer user_data,
                                          GDestroyNotify notify,
                                          guint timeout_seconds,
                                          GError **error);

G_END_DECLS
