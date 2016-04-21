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

#include "eos-updater-generated.h"
#include "eos-updater-types.h"
#include <libgsystem.h>
#include <glib.h>
#include <ostree.h>

#define shuffle_out_values(out,local,null) \
    ({ if (out) { *out = local; local = null; } })

#define message(_f, ...) \
  g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, _f, ## __VA_ARGS__)

G_BEGIN_DECLS

#define EOS_UPDATER_ERROR (eos_updater_error_quark())
GQuark eos_updater_error_quark (void);

const gchar *eos_updater_state_to_string (EosUpdaterState state);
void eos_updater_set_state_changed (EosUpdater *updater,
                                    EosUpdaterState state);

void eos_updater_set_error (EosUpdater *updater,
                            GError *error);

OstreeRepo *eos_updater_local_repo (void);

gboolean eos_updater_get_upgrade_info (OstreeRepo *repo,
                                       gchar **upgrade_refspec,
                                       gchar **original_refspec,
                                       GError **error);

gchar *eos_updater_get_booted_checksum (GError **error);

G_END_DECLS
