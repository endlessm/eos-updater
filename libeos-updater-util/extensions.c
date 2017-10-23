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

#include <libeos-updater-util/extensions.h>

#include <libeos-updater-util/util.h>

#include <string.h>

static void
eos_extensions_dispose_impl (EosExtensions *extensions)
{
  g_clear_pointer (&extensions->summary, g_bytes_unref);
  g_clear_pointer (&extensions->summary_sig, g_bytes_unref);
}

EOS_DEFINE_REFCOUNTED (EOS_EXTENSIONS,
                       EosExtensions,
                       eos_extensions,
                       eos_extensions_dispose_impl,
                       NULL)

EosExtensions *
eos_extensions_new_empty (void)
{
  EosExtensions *extensions = g_object_new (EOS_TYPE_EXTENSIONS, NULL);

  return extensions;
}

static gboolean
get_summary (EosExtensions *extensions,
             OstreeRepo *repo,
             GCancellable *cancellable,
             GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GFile) ext_dir = eos_updater_get_eos_extensions_dir (repo);
  g_autoptr(GFile) summary = g_file_get_child (ext_dir, "eos-summary");
  g_autoptr(GFile) summary_sig = g_file_get_child (ext_dir, "eos-summary.sig");
  g_autoptr(GFileInfo) summary_info = NULL;

  if (!eos_updater_read_file_to_bytes (summary,
                                       cancellable,
                                       &extensions->summary,
                                       &local_error))
    if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
      {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
      }

  g_clear_error (&local_error);
  if (!eos_updater_read_file_to_bytes (summary_sig,
                                       cancellable,
                                       &extensions->summary_sig,
                                       &local_error))
    if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
      {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
      }

  g_clear_error (&local_error);
  summary_info = g_file_query_info (summary,
                                    G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                    G_FILE_QUERY_INFO_NONE,
                                    cancellable,
                                    &local_error);
  if (summary_info == NULL)
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }
  else
    {
      extensions->summary_modification_time_secs = g_file_info_get_attribute_uint64 (summary_info,
                                                                                     G_FILE_ATTRIBUTE_TIME_MODIFIED);
    }
  return TRUE;
}

EosExtensions *
eos_extensions_new_from_repo (OstreeRepo *repo,
                              GCancellable *cancellable,
                              GError **error)
{
  g_autoptr(EosExtensions) extensions = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  extensions = eos_extensions_new_empty ();

  if (!get_summary (extensions,
                    repo,
                    cancellable,
                    error))
    return NULL;

  return g_steal_pointer (&extensions);
}

gboolean
eos_extensions_save (EosExtensions *extensions,
                     OstreeRepo *repo,
                     GCancellable *cancellable,
                     GError **error)
{
  g_autoptr(GFile) ext_path = NULL;

  g_return_val_if_fail (EOS_IS_EXTENSIONS (extensions), FALSE);
  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!eos_updater_create_extensions_dir (repo, &ext_path, error))
    return FALSE;
  if (!eos_updater_save_or_delete (extensions->summary,
                                   ext_path,
                                   "eos-summary",
                                   cancellable,
                                   error))
    return FALSE;

  if (!eos_updater_save_or_delete (extensions->summary_sig,
                                   ext_path,
                                   "eos-summary.sig",
                                   cancellable,
                                   error))
    return FALSE;

  return TRUE;
}
