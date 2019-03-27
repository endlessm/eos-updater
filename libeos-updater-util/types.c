/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <gio/gio.h>

#include "types.h"

static const GDBusErrorEntry eos_updater_error_entries[] = {
  { EOS_UPDATER_ERROR_WRONG_STATE, "com.endlessm.Updater.Error.WrongState" },
  { EOS_UPDATER_ERROR_LIVE_BOOT, "com.endlessm.Updater.Error.LiveBoot" },
  { EOS_UPDATER_ERROR_WRONG_CONFIGURATION, "com.endlessm.Updater.Error.WrongConfiguration" },
  { EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM, "com.endlessm.Updater.Error.NotOstreeSystem" },
  { EOS_UPDATER_ERROR_FETCHING, "com.endlessm.Updater.Error.Fetching" },
  { EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC, "com.endlessm.Updater.Error.MalformedAutoinstallSpec" },
  { EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC, "com.endlessm.Updater.Error.UnknownEntryInAutoinstallSpec" },
  { EOS_UPDATER_ERROR_FLATPAK_REMOTE_CONFLICT, "com.endlessm.Updater.Error.FlatpakRemoteConflict" },
  { EOS_UPDATER_ERROR_METERED_CONNECTION, "com.endlessm.Updater.Error.MeteredConnection" },
};

/* Ensure that every error code has an associated D-Bus error name */
G_STATIC_ASSERT (G_N_ELEMENTS (eos_updater_error_entries) == EOS_UPDATER_ERROR_LAST + 1);

GQuark
eos_updater_error_quark (void)
{
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("eos-updater-error-quark",
                                      &quark_volatile,
                                      eos_updater_error_entries,
                                      G_N_ELEMENTS (eos_updater_error_entries));
  return (GQuark) quark_volatile;
}

static const gchar * state_str[] = {
   "None",
   "Ready",
   "Error",
   "Polling",
   "UpdateAvailable",
   "Fetching",
   "UpdateReady",
   "ApplyUpdate",
   "UpdateApplied"
};

G_STATIC_ASSERT (G_N_ELEMENTS (state_str) == EOS_UPDATER_STATE_LAST + 1);

const gchar *
eos_updater_state_to_string (EosUpdaterState state)
{
  g_assert (state <= EOS_UPDATER_STATE_LAST);

  return state_str[state];
};
