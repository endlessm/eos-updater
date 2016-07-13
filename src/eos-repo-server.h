/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2016 Kinvolk GmbH
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
 * Author: Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include <ostree.h>

#include <libsoup/soup.h>

#include <glib.h>

G_BEGIN_DECLS

#define EOS_UPDATER_TYPE_REPO_SERVER eos_updater_repo_server_get_type ()
G_DECLARE_FINAL_TYPE (EosUpdaterRepoServer, eos_updater_repo_server, EOS_UPDATER, REPO_SERVER, SoupServer)

EosUpdaterRepoServer *eos_updater_repo_server_new (OstreeRepo *repo,
                                                   const gchar *served_remote,
                                                   GCancellable *cancellable,
                                                   GError **error);

guint eos_updater_repo_server_get_pending_requests (EosUpdaterRepoServer *repo_server);
gint64 eos_updater_repo_server_get_last_request_time (EosUpdaterRepoServer *repo_server);

G_END_DECLS
