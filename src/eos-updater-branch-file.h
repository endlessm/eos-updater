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

#include "eos-refcounted.h"

#include <ostree.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define EOS_TYPE_BRANCH_FILE eos_branch_file_get_type ()
EOS_DECLARE_REFCOUNTED (EosBranchFile,
                        eos_branch_file,
                        EOS,
                        BRANCH_FILE)

struct _EosBranchFile
{
  GObject parent_instance;

  GBytes *raw_contents;
  GBytes *raw_signature;
  GKeyFile *branch_file;
  gchar *contents_sha512sum;
  GDateTime *download_time;
};

EosBranchFile *
eos_branch_file_new_empty (void);

EosBranchFile *
eos_branch_file_new_from_repo (OstreeRepo *repo,
                               GCancellable *cancellable,
                               GError **error);

EosBranchFile *
eos_branch_file_new_from_files (GFile *branch_file,
                                GFile *signature,
                                GCancellable *cancellable,
                                GError **error);

EosBranchFile *
eos_branch_file_new_from_raw (GBytes *contents,
                              GBytes *signature,
                              GDateTime *download_time,
                              GError **error);

gboolean
eos_branch_file_save_to_repo (EosBranchFile *branch_file,
                              OstreeRepo *repo,
                              GCancellable *cancellable,
                              GError **error);

gboolean
eos_branch_file_save (EosBranchFile *branch_file,
                      GFile *target,
                      GFile *target_signature,
                      GCancellable *cancellable,
                      GError **error);

G_END_DECLS
