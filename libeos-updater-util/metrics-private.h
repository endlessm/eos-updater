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
 * Records which branch will be used by the updater. The payload is a 4-tuple
 * of 3 strings and boolean: vendor name, product ID, selected OStree ref, and
 * whether the machine is on hold
 */
static const gchar *const EOS_UPDATER_METRIC_BRANCH_SELECTED = "99f48aac-b5a0-426d-95f4-18af7d081c4e";

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
