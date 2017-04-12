/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <ostree.h>

#include <libsoup/soup.h>

#include <glib.h>

G_BEGIN_DECLS

#define EUS_TYPE_REPO eus_repo_get_type ()
G_DECLARE_FINAL_TYPE (EusRepo, eus_repo, EUS, REPO, GObject)

EusRepo *eus_repo_new (SoupServer    *server,
                       OstreeRepo    *repo,
                       const gchar   *root_path,
                       const gchar   *served_remote,
                       GCancellable  *cancellable,
                       GError       **error);

guint eus_repo_get_pending_requests (EusRepo *self);
gint64 eus_repo_get_last_request_time (EusRepo *self);

G_END_DECLS
