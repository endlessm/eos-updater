/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>
#include <libsoup/soup.h>

#include <libeos-update-server/repo.h>

G_BEGIN_DECLS

#define EUS_TYPE_SERVER eus_server_get_type ()
G_DECLARE_FINAL_TYPE (EusServer, eus_server, EUS, SERVER, GObject)

EusServer *eus_server_new (SoupServer *server);

void eus_server_add_repo (EusServer *self,
                          EusRepo   *repo);

void eus_server_disconnect (EusServer *self);

guint eus_server_get_pending_requests (EusServer *self);
gint64 eus_server_get_last_request_time (EusServer *self);

G_END_DECLS
