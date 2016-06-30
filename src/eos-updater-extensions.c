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

#include "eos-updater-extensions.h"

#include "eos-util.h"

static void
eos_extensions_dispose_impl (EosExtensions *extensions)
{
  g_clear_pointer (&extensions->summary, g_bytes_unref);
  g_clear_pointer (&extensions->summary_sig, g_bytes_unref);
  g_clear_pointer (&extensions->ref, g_bytes_unref);
  g_clear_pointer (&extensions->ref_sig, g_bytes_unref);
  g_clear_object (&extensions->branch_file);
}

static void
eos_extensions_finalize_impl (EosExtensions *extensions)
{
  g_free (extensions->ref_name);
}

EOS_DEFINE_REFCOUNTED (EOS_EXTENSIONS,
                       EosExtensions,
                       eos_extensions,
                       eos_extensions_dispose_impl,
                       eos_extensions_finalize_impl)

EosExtensions *
eos_extensions_new_empty (void)
{
  return g_object_new (EOS_TYPE_EXTENSIONS, NULL);
}

EosExtensions *
eos_extensions_new (OstreeRepo *repo,
                    GCancellable *cancellable,
                    GError **error)
{
  g_autoptr(EosBranchFile) branch_file = NULL;
  g_autoptr(EosExtensions) extensions = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);

  branch_file = eos_branch_file_new_from_repo (repo,
                                               cancellable,
                                               error);
  if (branch_file == NULL)
    return NULL;

  extensions = eos_extensions_new_empty ();
  extensions->branch_file = g_steal_pointer (&branch_file);
  return g_steal_pointer (&extensions);
}

gboolean
eos_extensions_save (EosExtensions *extensions,
                     OstreeRepo *repo,
                     GCancellable *cancellable,
                     GError **error)
{
  g_autoptr(GFile) ext_path = NULL;
  g_autoptr(GFile) branch_file_path = NULL;
  g_autoptr(GFile) branch_file_sig_path = NULL;

  g_return_val_if_fail (EOS_IS_EXTENSIONS (extensions), FALSE);
  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);

  if (!eos_updater_create_extensions_dir (repo, &ext_path, error))
    return FALSE;
  if (!eos_updater_save_or_delete (extensions->summary,
                                   ext_path,
                                   "summary",
                                   cancellable,
                                   error))
    return FALSE;

  if (!eos_updater_save_or_delete (extensions->summary_sig,
                                   ext_path,
                                   "summary.sig",
                                   cancellable,
                                   error))
    return FALSE;

  if (extensions->ref_name != NULL)
    {
      g_autofree gchar *ref_filename = g_build_filename ("refs.d",
                                                         extensions->ref_name,
                                                         NULL);
      g_autofree gchar *ref_sig_filename = NULL;

      if (!eos_updater_save_or_delete (extensions->ref,
                                       ext_path,
                                       ref_filename,
                                       cancellable,
                                       error))
        return FALSE;

      ref_sig_filename = g_strconcat (extensions->ref_name, ".sig", NULL);
      if (!eos_updater_save_or_delete (extensions->ref_sig,
                                       ext_path,
                                       ref_sig_filename,
                                       cancellable,
                                       error))
        return FALSE;
    }

  return eos_branch_file_save_to_repo (extensions->branch_file,
                                       repo,
                                       cancellable,
                                       error);
}
