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

#pragma once

#include "eos-updater-branch-file.h"

#include "eos-refcounted.h"

#include <ostree.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define EOS_TYPE_EXTENSIONS eos_extensions_get_type ()
EOS_DECLARE_REFCOUNTED (EosExtensions,
                        eos_extensions,
                        EOS,
                        EXTENSIONS)

struct _EosExtensions
{
  GObject parent_instance;

  GBytes *summary;
  GBytes *summary_sig;
  GBytes *ref;
  GBytes *ref_sig;
  gchar *ref_name;
  EosBranchFile *branch_file;
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
