/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 * Author: Vivek Dasmohapatra <vivek@etla.org>
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  EOS_UPDATER_ERROR_WRONG_STATE,
  EOS_UPDATER_ERROR_LAN_DISCOVERY_ERROR,
  EOS_UPDATER_ERROR_WRONG_CONFIGURATION,
  EOS_UPDATER_N_ERRORS /*< skip >*/
} EosUpdaterError;

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
  EOS_UPDATER_N_STATES,
} EosUpdaterState;

G_END_DECLS
