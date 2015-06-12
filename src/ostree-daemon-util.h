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

#include "ostree-daemon-generated.h"
#include <libgsystem.h>
#include <glib.h>
#include <ostree.h>

#define shuffle_out_values(out,local,null) \
    ({ if (out) { *out = local; local = null; } })

#define message(_f, ...) \
  g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, _f, ## __VA_ARGS__)

G_BEGIN_DECLS

#define OTD_ERROR (otd_error_quark())
GQuark otd_error_quark (void);

typedef enum {
  OTD_ERROR_WRONG_STATE,
  OTD_N_ERRORS /*< skip >*/
} OTDError;

typedef enum {
  OTD_STATE_NONE = 0,
  OTD_STATE_READY,
  OTD_STATE_ERROR,
  OTD_STATE_POLLING,
  OTD_STATE_UPDATE_AVAILABLE,
  OTD_STATE_FETCHING,
  OTD_STATE_UPDATE_READY,
  OTD_STATE_APPLYING_UPDATE,
  OTD_STATE_UPDATE_APPLIED,
  OTD_N_STATES,
} OTDState;

const gchar *otd_state_to_string (OTDState state);
void ostree_daemon_set_state (OTDOSTree *ostree, OTDState state);

void ostree_daemon_set_error (OTDOSTree *ostree, GError *error);

OstreeRepo * ostree_daemon_local_repo (void);

gboolean ostree_daemon_resolve_upgrade (OTDOSTree  *ostree,
                                        OstreeRepo *repo,
                                        gchar     **upgrade_remote,
                                        gchar     **upgrade_ref,
                                        gchar     **booted_checksum,
                                        GError    **error);

G_END_DECLS
