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

#include <flatpak.h>
#include <glib.h>
#include <libeos-updater-util/types.h>

G_BEGIN_DECLS

gboolean eufi_check_ref_actions_applied (FlatpakInstallation  *installation,
                                         const gchar          *pending_flatpak_deployments_state_path,
                                         GPtrArray            *actions,
                                         GError              **error);

gboolean eufi_apply_flatpak_ref_actions (FlatpakInstallation       *installation,
                                         const gchar               *state_counter_path,
                                         GPtrArray                 *actions,
                                         EosUpdaterInstallerMode    mode,
                                         EosUpdaterInstallerFlags   flags,
                                         GError                   **error);

GHashTable *eufi_determine_flatpak_ref_actions_to_check (GStrv    directories_to_search,
                                                         GError **error);

GHashTable *eufi_determine_flatpak_ref_actions_to_apply (GStrv    directories_to_search,
                                                         GError **error);

G_END_DECLS
