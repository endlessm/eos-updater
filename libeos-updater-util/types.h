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
  EOS_UPDATER_ERROR_LAN_DISCOVERY_ERROR,
  EOS_UPDATER_ERROR_WRONG_CONFIGURATION,
  EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM,
  EOS_UPDATER_ERROR_FETCHING,
  EOS_UPDATER_ERROR_LAST = EOS_UPDATER_ERROR_FETCHING /*< skip >*/
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

G_END_DECLS
