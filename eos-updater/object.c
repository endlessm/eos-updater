/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
 * Copyright © 2017 Endless Mobile, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

#include "config.h"

#include <eos-updater/dbus.h>
#include <eos-updater/object.h>
#include <glib.h>
#include <libeos-updater-util/metrics-private.h>
#include <libeos-updater-util/types.h>

#ifdef HAS_EOSMETRICS_0
#include <eosmetrics/eosmetrics.h>
#endif /* HAS_EOSMETRICS_0 */

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
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *error_name = NULL;

  g_warn_if_fail (error != NULL);

  if (error == NULL)
    {
      /* This case should never be hit. If it is, there’s a bug in eos-updater;
       * but we’d prefer eos-updater to continue running and report some error
       * state rather than abort or not report any error state. */
      g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Error in updater: error state set without appropriate message");
      error = local_error;
    }

  error_name = g_dbus_error_encode_gerror (error);

  g_message ("Changing to error state: %s, %d, %s",
             error_name, error->code, error->message);

  eos_updater_set_error_name (updater, error_name);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  eos_updater_set_error_code (updater, (guint) error->code);
G_GNUC_END_IGNORE_DEPRECATIONS
  eos_updater_set_error_message (updater, error->message);
  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_ERROR);

  /* Report a metric. */
#ifdef HAS_EOSMETRICS_0
  if (euu_get_metrics_enabled () &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
      !g_error_matches (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_LIVE_BOOT) &&
      !g_error_matches (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM) &&
      !g_error_matches (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_METERED_CONNECTION))
    {
      emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                             EOS_UPDATER_METRIC_FAILURE,
                                             g_variant_new ("(ss)",
                                                            "eos-updater",
                                                            error->message));
    }
#endif
}

/* This must only be called from the main thread. All mutual exclusion of access
 * to the #EosUpdaterData structure, and the #OstreeRepo (and other things) is
 * based on the current updater state. To maintain safety, that must only be
 * modified from the main thread, and only one worker thread must be alive at
 * once, mutexed on this state.
 *
 * See https://phabricator.endlessm.com/T15923 */
void
eos_updater_clear_error (EosUpdater *updater,
                         EosUpdaterState state)
{
  if (g_strcmp0 (eos_updater_get_error_name (updater), "") != 0)
    g_message ("Clearing error state and changing to state %s",
               eos_updater_state_to_string (state));
  else
    g_message ("Changing to state %s", eos_updater_state_to_string (state));

  eos_updater_set_error_name (updater, "");
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  eos_updater_set_error_code (updater, 0);
G_GNUC_END_IGNORE_DEPRECATIONS
  eos_updater_set_error_message (updater, "");
  eos_updater_set_state_changed (updater, state);
}
