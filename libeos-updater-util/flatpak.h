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

#include <libeos-updater-util/enums.h>

G_BEGIN_DECLS

#define FLATPAKS_IN_OSTREE_PRIORITY 100
#define FLATPAKS_IN_OVERRIDE_DIR_PRIORITY 0

typedef enum {
  EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL = 0,
  EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL = 1
} EosUpdaterUtilFlatpakRemoteRefActionType;

typedef struct {
  unsigned int                             ref_cnt;

  EosUpdaterUtilFlatpakRemoteRefActionType type;
  FlatpakRemoteRef                         *ref;

  gint32                                   serial;
} FlatpakRemoteRefAction;

typedef struct {
  GPtrArray *remote_ref_actions;
  gint priority;
} FlatpakRemoteRefActionsFile;

FlatpakRemoteRefAction * flatpak_remote_ref_action_ref (FlatpakRemoteRefAction *action);
void flatpak_remote_ref_action_unref (FlatpakRemoteRefAction *action);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakRemoteRefAction, flatpak_remote_ref_action_unref)

FlatpakRemoteRefActionsFile * flatpak_remote_ref_actions_file_new (GPtrArray *remote_ref_actions,
                                                                   gint       priority);
void flatpak_remote_ref_actions_file_free (FlatpakRemoteRefActionsFile *);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakRemoteRefActionsFile, flatpak_remote_ref_actions_file_free);

gboolean eos_updater_util_flatpak_ref_actions_append_from_directory (const gchar   *relative_parent_path,
                                                                     GFile         *directory,
                                                                     GHashTable    *ref_actions,
                                                                     gint           priority,
                                                                     GCancellable  *cancellable,
                                                                     GError       **error);
gboolean eos_updater_util_flatpak_ref_actions_maybe_append_from_directory (const gchar   *override_directory_path,
                                                                           GHashTable    *ref_actions,
                                                                           gint           priority,
                                                                           GCancellable  *cancellable,
                                                                           GError       **error);
GHashTable * eos_updater_util_flatpak_ref_actions_from_directory (const gchar   *relative_parent_path,
                                                                  GFile         *directory,
                                                                  gint           priority,
                                                                  GCancellable  *cancellable,
                                                                  GError       **error);
GHashTable * eos_updater_util_hoist_flatpak_remote_ref_actions (GHashTable *ref_actions_file_table);
GHashTable * eos_updater_util_flatpak_ref_action_application_progress_in_state_path (GCancellable  *cancellable,
                                                                                     GError       **error);
GHashTable * eos_updater_util_filter_for_new_flatpak_ref_actions (GHashTable *ref_actions,
                                                                  GHashTable *progresses);
GHashTable * eos_updater_util_filter_for_existing_flatpak_ref_actions (GHashTable *ref_actions,
                                                                       GHashTable *progresses);
GHashTable * eos_updater_util_squash_remote_ref_actions (GHashTable *ref_actions);
GPtrArray * eos_updater_util_flatten_flatpak_ref_actions_table (GHashTable *flatpak_ref_actions);
GHashTable * eos_updater_util_remote_ref_actions_to_expected_progresses (GHashTable *ref_actions_table);

gchar * eos_updater_util_format_all_flatpak_ref_actions (const gchar *title,
                                                         GHashTable  *flatpak_ref_actions_for_this_boot);
gchar * eos_updater_util_format_all_flatpak_ref_actions_progresses (GHashTable *flatpak_ref_action_progresses);


const gchar * eos_updater_util_pending_flatpak_deployments_state_path (void);
const gchar * eos_updater_util_flatpak_autoinstall_override_paths (void);

G_END_DECLS
