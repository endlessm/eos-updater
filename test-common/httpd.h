/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2023 Endless OS Foundation, LLC
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _Httpd Httpd;

Httpd *httpd_new (GFile *root);

void httpd_free (Httpd *httpd);

gboolean httpd_start (Httpd   *httpd,
                      GError **error);

gboolean httpd_stop (Httpd   *httpd,
                     GError **error);

const gchar *httpd_get_url (Httpd *httpd);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Httpd, httpd_free)

G_END_DECLS
