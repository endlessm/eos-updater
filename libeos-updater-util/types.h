/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  EOS_UPDATER_ERROR_WRONG_STATE,
  EOS_UPDATER_ERROR_LIVE_BOOT,
  EOS_UPDATER_ERROR_WRONG_CONFIGURATION,
  EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM,
  EOS_UPDATER_ERROR_FETCHING,
  EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
  EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC,
  EOS_UPDATER_ERROR_FLATPAK_REMOTE_CONFLICT,
  EOS_UPDATER_ERROR_METERED_CONNECTION,
  EOS_UPDATER_ERROR_LAST = EOS_UPDATER_ERROR_METERED_CONNECTION, /*< skip >*/
} EosUpdaterError;

#define EOS_UPDATER_ERROR (eos_updater_error_quark ())
GQuark eos_updater_error_quark (void);

typedef enum {
  EOS_UPDATER_STATE_NONE = 0,
  EOS_UPDATER_STATE_READY,
  EOS_UPDATER_STATE_ERROR,
  EOS_UPDATER_STATE_POLLING,
  EOS_UPDATER_STATE_UPDATE_AVAILABLE,
  EOS_UPDATER_STATE_FETCHING,
  EOS_UPDATER_STATE_UPDATE_READY,
  EOS_UPDATER_STATE_APPLYING_UPDATE,
  EOS_UPDATER_STATE_UPDATE_APPLIED,
  EOS_UPDATER_STATE_LAST = EOS_UPDATER_STATE_UPDATE_APPLIED, /*< skip > */
} EosUpdaterState;

const gchar *eos_updater_state_to_string (EosUpdaterState state);

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
 * Enum values used to specify the mode that the flatpak-installer runs in.
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
 *
 * Flags to change the behaviour of the flatpak-instasller.
 */
typedef enum {
  EU_INSTALLER_FLAGS_NONE = 0,
  EU_INSTALLER_FLAGS_ALSO_PULL = (1 << 0)
} EosUpdaterInstallerFlags;

/**
 * EuUpdateFlags:
 * @EU_UPDATE_FLAGS_NONE: No flags set.
 * @EU_UPDATE_FLAGS_USER_VISIBLE: Update contains significant user visible
 *   changes which should be notified to the user in advance of the update
 *   being applied.
 *
 * Flags describing the content of an update.
 *
 * Since: UNRELEASED
 */
typedef enum {
  EU_UPDATE_FLAGS_NONE = 0,
  EU_UPDATE_FLAGS_USER_VISIBLE = 1 << 0,
} EuUpdateFlags;

G_END_DECLS
