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

#include "eos-updater-generated.h"
#include "eos-updater-object.h"
#include "eos-updater-types.h"

static void
eos_updater_set_state_changed (EosUpdater *updater, EosUpdaterState state)
{
  eos_updater_set_state (updater, state);
  eos_updater_emit_state_changed (updater, state);
}

void
eos_updater_set_error (EosUpdater *updater,
                       const GError *error)
{
  gint code = error ? error->code : -1;
  const gchar *msg = (error && error->message) ? error->message : "Unspecified";
  g_autofree gchar *error_name = g_dbus_error_encode_gerror (error);

  g_warn_if_fail (error != NULL);

  g_message ("Changing to error state: %s, %d, %s", error_name, code, msg);

  eos_updater_set_error_name (updater, error_name);
  eos_updater_set_error_code (updater, code);
  eos_updater_set_error_message (updater, msg);
  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_ERROR);
}

void
eos_updater_clear_error (EosUpdater *updater,
                         EosUpdaterState state)
{
  if (eos_updater_get_error_code (updater) != 0)
    g_message ("Clearing error state and changing to state %s",
               eos_updater_state_to_string (state));
  else
    g_message ("Changing to state %s", eos_updater_state_to_string (state));

  eos_updater_set_error_name (updater, "");
  eos_updater_set_error_code (updater, 0);
  eos_updater_set_error_message (updater, "");
  eos_updater_set_state_changed (updater, state);
}
