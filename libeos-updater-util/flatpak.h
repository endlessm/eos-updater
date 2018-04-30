/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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
 *  - Sam Spilsbury <sam@endlessm.com>
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>

#include <flatpak.h>
#include <ostree.h>

#include <libeos-updater-util/enums.h>

G_BEGIN_DECLS

typedef enum {
  EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL = 0,
  EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL = 1,
  EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE = 2
} EuuFlatpakRemoteRefActionType;

typedef struct {
  gint ref_count;
  FlatpakRef *ref;
  const gchar *remote;
  const gchar *collection_id;
} EuuFlatpakLocationRef;

/**
 * EuuFlatpakRemoteRefActionFlags:
 * @EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_NONE: No flags
 * @EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY: The ref action was added as a dependency
 *
 * Flags for euu_flatpak_remote_ref_action_new(). They affect
 * the behavior of remote ref actions when they are applied. For
 * instance, dependency ref actions are immediately deployed.
 *
 */
typedef enum {
  EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_NONE = 0,
  EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY = 1 << 0
} EuuFlatpakRemoteRefActionFlags;

typedef struct {
  gint ref_count;

  EuuFlatpakRemoteRefActionType type;
  EuuFlatpakLocationRef *ref;

  gchar *source;

  gint32 serial;

  EuuFlatpakRemoteRefActionFlags flags;
} EuuFlatpakRemoteRefAction;

typedef struct {
  GPtrArray *remote_ref_actions;  /* (element-type EuuFlatpakRemoteRefAction) */
  gint priority;
} EuuFlatpakRemoteRefActionsFile;

EuuFlatpakLocationRef *euu_flatpak_location_ref_new (FlatpakRef  *ref,
                                                     const gchar *remote,
                                                     const gchar *collection_id);
EuuFlatpakLocationRef *euu_flatpak_location_ref_ref (EuuFlatpakLocationRef *ref);
void euu_flatpak_location_ref_unref (EuuFlatpakLocationRef *ref);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EuuFlatpakLocationRef, euu_flatpak_location_ref_unref)

EuuFlatpakRemoteRefAction *euu_flatpak_remote_ref_action_new (EuuFlatpakRemoteRefActionType  type,
                                                              EuuFlatpakLocationRef         *ref,
                                                              const gchar                   *source,
                                                              gint32                         serial,
                                                              EuuFlatpakRemoteRefActionFlags flags);
EuuFlatpakRemoteRefAction *euu_flatpak_remote_ref_action_ref (EuuFlatpakRemoteRefAction *action);
void euu_flatpak_remote_ref_action_unref (EuuFlatpakRemoteRefAction *action);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EuuFlatpakRemoteRefAction, euu_flatpak_remote_ref_action_unref)

EuuFlatpakRemoteRefActionsFile *euu_flatpak_remote_ref_actions_file_new (GPtrArray *remote_ref_actions,
                                                                         gint       priority);
void euu_flatpak_remote_ref_actions_file_free (EuuFlatpakRemoteRefActionsFile *file);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EuuFlatpakRemoteRefActionsFile, euu_flatpak_remote_ref_actions_file_free)

GPtrArray *euu_flatpak_ref_actions_from_file (GFile         *file,
                                              GPtrArray    **out_skipped_actions,
                                              GCancellable  *cancellable,
                                              GError       **error);
GPtrArray *euu_flatpak_ref_actions_from_data (const gchar   *data,
                                              gssize         length,
                                              const gchar   *path,
                                              GPtrArray    **out_skipped_actions,
                                              GCancellable  *cancellable,
                                              GError       **error);
gboolean euu_flatpak_ref_actions_append_from_directory (GFile         *directory,
                                                        GHashTable    *ref_actions,
                                                        gint           priority,
                                                        gboolean       allow_noent,
                                                        GCancellable  *cancellable,
                                                        GError       **error);
GHashTable *euu_flatpak_ref_actions_from_directory (GFile         *directory,
                                                    gint           priority,
                                                    GCancellable  *cancellable,
                                                    GError       **error);
GHashTable *euu_hoist_flatpak_remote_ref_actions (GHashTable *ref_actions_file_table);
GHashTable *euu_flatpak_ref_action_application_progress_in_state_path (GCancellable  *cancellable,
                                                                       GError       **error);
GHashTable *euu_filter_for_new_flatpak_ref_actions (GHashTable *ref_actions,
                                                    GHashTable *progresses);
GHashTable *euu_filter_for_existing_flatpak_ref_actions (GHashTable *ref_actions,
                                                         GHashTable *progresses);
GHashTable *euu_squash_remote_ref_actions (GHashTable *ref_actions);
GPtrArray *euu_flatten_flatpak_ref_actions_table (GHashTable *flatpak_ref_actions);
GPtrArray * euu_add_dependency_ref_actions_for_installation (FlatpakInstallation  *installation,
                                                             GPtrArray            *ref_actions,
                                                             GCancellable         *cancellable,
                                                             GError              **error);

gchar *euu_format_all_flatpak_ref_actions (const gchar *title,
                                           GHashTable  *flatpak_ref_actions_for_this_boot);
gchar *euu_format_all_flatpak_ref_actions_progresses (GHashTable *flatpak_ref_action_progresses);
gchar *euu_format_flatpak_ref_actions_array (const gchar *title,
                                             GPtrArray   *flatpak_ref_actions);
gchar *euu_lookup_flatpak_remote_for_collection_id (FlatpakInstallation  *installation,
                                                    const gchar          *collection_id,
                                                    GError              **error);

const gchar *euu_pending_flatpak_deployments_state_path (void);
const gchar *euu_flatpak_autoinstall_override_paths (void);
const gchar *euu_get_system_architecture_string (void);

GHashTable *euu_flatpak_ref_actions_from_paths (GStrv    directories_to_search,
                                                GError **error);
GHashTable *euu_flatpak_ref_actions_from_ostree_commit (OstreeRepo    *repo,
                                                        const gchar   *checksum,
                                                        GCancellable  *cancellable,
                                                        GError       **error);

FlatpakInstallation *eos_updater_get_flatpak_installation (GCancellable  *cancellable,
                                                           GError       **error);

guint euu_flatpak_ref_hash (gconstpointer ref);
gboolean euu_flatpak_ref_equal (gconstpointer a, gconstpointer b);

G_END_DECLS
