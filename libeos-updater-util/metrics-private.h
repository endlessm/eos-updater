/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2020 Endless OS Foundation LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
 * Authors:
 *  - Philip Withnall <withnall@endlessm.com>
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/*
 * Records a failure in the updater. The payload is an `(ss)` of the updater
 * component, and the error message.
 */
static const gchar *const EOS_UPDATER_METRIC_FAILURE = "927d0f61-4890-4912-a513-b2cb0205908f";

/*
 * Aggregate event, recorded when the system is blocked from crossing a checkpoint.
 *
 * The payload is a 5-tuple of strings: hardware vendor name, hardware product
 * name, current OSTree ref, target OSTree ref, and the stringified value of
 * EuuCheckpointBlock describing why the update was blocked.
 *
 * The count is to be ignored: it will be incremented whenever the system tries
 * and fails to update, but since this is a static condition the number of times
 * doesn't really matter. It is aggregated so each system only reports the event
 * once per day and month.
 */
static const gchar *const EOS_UPDATER_METRIC_CHECKPOINT_BLOCKED = "e3609b7e-88aa-4ba5-90f9-418bf9234139";

/**
 * euu_get_metrics_enabled:
 *
 * Check whether metrics are enabled at runtime. They can be disabled using an
 * environment variable for the unit tests.
 *
 * Returns: %TRUE if metrics are enabled, %FALSE otherwise
 */
static inline gboolean
euu_get_metrics_enabled (void)
{
#ifdef HAS_EOSMETRICS_0
  const gchar *disable_metrics = g_getenv ("EOS_DISABLE_METRICS");
  return (disable_metrics == NULL || !g_str_equal (disable_metrics, "1"));
#else
  return FALSE;
#endif
}

G_END_DECLS
