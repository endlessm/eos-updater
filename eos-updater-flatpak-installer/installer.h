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
#include <flatpak.h>

G_BEGIN_DECLS

/**
 * EosUpdaterInstallerMode:
 * @EU_INSTALLER_MODE_PERFORM: Actually perform actions in the installer
 *                             installing or uninstalling flatpaks as necessary,
 *                             this is the default mode.
 * @EU_INSTALLER_MODE_STAMP: Just update the counter files to the most
 *                           up-to-date counter for each of the auto-install
 *                           files but don't perform actions. This is typically
 *                           used by the image builder to keep the auto-install
 *                           state in sync with the installed flatpaks.
 * @EU_INSTALLER_MODE_CHECK: Check that flatpak ref actions up to a
 *                           certain serial number are applied on the system,
 *                           that is that all flatpaks that should have been
 *                           installed are installed and all flatpaks that
 *                           should have been uninstalled are not installed.
 *                           Note that this mode is not useful as a debugging
 *                           tool, because a user can legitimately uninstall
 *                           or install flatpaks of the same name after
 *                           an update has occurred.
 *
 * Enum values used to specify the mode that the flatpak-installer runs in
 *
 */
typedef enum {
  EU_INSTALLER_MODE_PERFORM = 0,
  EU_INSTALLER_MODE_STAMP = 1,
  EU_INSTALLER_MODE_CHECK = 2
} EosUpdaterInstallerMode;

/**
 * EosUpdaterInstallerFlags
 * @EU_INSTALLER_FLAGS_NONE: Just run the installer normally.
 * @EU_INSTALLER_FLAGS_ALSO_PULL: Pull flatpaks as well as deploying them. This
 *                                is not something that would run on normal
 *                                operation, rather it is a tool for developers
 *                                to keep installed flatpaks up to date with
 *                                their system without having to use the
 *                                regular updater.
 */
typedef enum {
  EU_INSTALLER_FLAGS_NONE = 0,
  EU_INSTALLER_FLAGS_ALSO_PULL = (1 << 0)
} EosUpdaterInstallerFlags;

gboolean eos_updater_flatpak_installer_check_ref_actions_applied (FlatpakInstallation  *installation,
                                                                  const gchar          *pending_flatpak_deployments_state_path,
                                                                  GHashTable           *table,
                                                                  GError              **error);


gboolean eos_updater_flatpak_installer_apply_flatpak_ref_actions (FlatpakInstallation      *installation,
                                                                  const gchar              *state_counter_path,
                                                                  GHashTable               *table,
                                                                  EosUpdaterInstallerMode   mode,
                                                                  EosUpdaterInstallerFlags  pull,
                                                                  GError                  **error);

GHashTable * eos_updater_flatpak_installer_determine_flatpak_ref_actions_to_check (GStrv    directories_to_search,
                                                                                   GError **error);


GHashTable * eos_updater_flatpak_installer_determine_flatpak_ref_actions_to_apply (GStrv    directories_to_search,
                                                                                   GError **error);

G_END_DECLS
