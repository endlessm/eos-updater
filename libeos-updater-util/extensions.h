/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
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

#include <ostree.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define EOS_TYPE_EXTENSIONS eos_extensions_get_type ()
G_DECLARE_FINAL_TYPE (EosExtensions,
                      eos_extensions,
                      EOS,
                      EXTENSIONS,
                      GObject)

struct _EosExtensions
{
  GObject parent_instance;

  GBytes *summary;
  GBytes *summary_sig;
  guint64 summary_modification_time_secs; /* since the Unix epoch, UTC */
};

EosExtensions *eos_extensions_new_empty (void);

EosExtensions *eos_extensions_new_from_repo (OstreeRepo *repo,
                                             GCancellable *cancellable,
                                             GError **error);

gboolean eos_extensions_save (EosExtensions *extensions,
                              OstreeRepo *repo,
                              GCancellable *cancellable,
                              GError **error);

G_END_DECLS
