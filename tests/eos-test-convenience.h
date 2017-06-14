/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Kinvolk GmbH
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#pragma once

#include "eos-test-utils.h"

G_BEGIN_DECLS

typedef struct
{
  EosUpdaterFixture *fixture;
  EosTestServer *server;
  EosTestSubserver *subserver;
  EosTestClient *client;
} EtcData;

void etc_data_init (EtcData *data,
                    EosUpdaterFixture *fixture);

void etc_data_clear (EtcData *data);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(EtcData, etc_data_clear);

void etc_set_up_server (EtcData *data);

void etc_set_up_client_synced_to_server (EtcData *data);

void etc_update_server (EtcData *data,
                        guint commit);

void etc_update_client (EtcData *data);

void etc_delete_object (GFile *repo,
                        const gchar *object);

G_END_DECLS
