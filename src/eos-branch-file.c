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

#include "eos-branch-file.h"

#include "eos-util.h"

static void
eos_branch_file_dispose_impl (EosBranchFile *bf)
{
  g_clear_pointer (&bf->raw_contents, g_bytes_unref);
  g_clear_pointer (&bf->raw_signature, g_bytes_unref);
  g_clear_pointer (&bf->branch_file, g_key_file_unref);
  g_clear_pointer (&bf->download_time, g_date_time_unref);
}

static void
eos_branch_file_finalize_impl (EosBranchFile *bf)
{
  g_free (bf->contents_sha512sum);
}

EOS_DEFINE_REFCOUNTED (EOS_BRANCH_FILE,
                       EosBranchFile,
                       eos_branch_file,
                       eos_branch_file_dispose_impl,
                       eos_branch_file_finalize_impl)

static gboolean
get_download_time (GFile *file,
                   GCancellable *cancellable,
                   GDateTime **out_download_time,
                   GError **error)
{
  g_autoptr(GFileInfo) info = NULL;
  guint64 mod_time;
  g_autoptr(GDateTime) download_time = NULL;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_TIME_MODIFIED,
                            G_FILE_QUERY_INFO_NONE,
                            cancellable,
                            error);
  if (info == NULL)
    return FALSE;

  mod_time = g_file_info_get_attribute_uint64 (info,
                                               G_FILE_ATTRIBUTE_TIME_MODIFIED);
  download_time = g_date_time_new_from_unix_utc (mod_time);
  if (download_time == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Invalid branch file modification time");
      return FALSE;
    }

  *out_download_time = g_steal_pointer (&download_time);
  return TRUE;
}

EosBranchFile *
eos_branch_file_new_empty (void)
{
  return g_object_new (EOS_TYPE_BRANCH_FILE, NULL);
};

static void
get_branch_file_paths (GFile *ext_path,
                       GFile **branch_file,
                       GFile **branch_file_sig)
{
  *branch_file = g_file_get_child (ext_path, "branch_file");
  *branch_file_sig = g_file_get_child (ext_path, "branch_file.sig");
}

EosBranchFile *
eos_branch_file_new_from_repo (OstreeRepo *repo,
                               GCancellable *cancellable,
                               GError **error)
{
  g_autoptr(GFile) ext_dir = eos_updater_get_eos_extensions_dir (repo);
  g_autoptr(GFile) branch_file = NULL;
  g_autoptr(GFile) signature = NULL;

  get_branch_file_paths (ext_dir, &branch_file, &signature);
  return eos_branch_file_new_from_files (branch_file, signature, cancellable, error);
}

EosBranchFile *
eos_branch_file_new_from_files (GFile *branch_file,
                                GFile *signature,
                                GCancellable *cancellable,
                                GError **error)
{
  g_autoptr(GBytes) branch_file_bytes = NULL;
  g_autoptr(GBytes) signature_bytes = NULL;
  g_autoptr(GDateTime) download_time = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (G_IS_FILE (branch_file), NULL);
  g_return_val_if_fail (G_IS_FILE (signature), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!eos_updater_read_file_to_bytes (branch_file,
                                       cancellable,
                                       &branch_file_bytes,
                                       error))
    return NULL;

  if (!eos_updater_read_file_to_bytes (signature,
                                       cancellable,
                                       &signature_bytes,
                                       &local_error))
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      /* signature file not found, means we have an old version of the
       * branch file, read the download time from branch file's
       * mtime */
      if (!get_download_time (branch_file,
                              cancellable,
                              &download_time,
                              error))
        return NULL;
    }

  return eos_branch_file_new_from_raw (branch_file_bytes,
                                       signature_bytes,
                                       download_time,
                                       error);
}

EosBranchFile *
eos_branch_file_new_from_raw (GBytes *contents,
                              GBytes *signature,
                              GDateTime *download_time,
                              GError **error)
{
  g_autoptr(EosBranchFile) bf = NULL;
  gconstpointer contents_bytes;
  gsize contents_len;
  g_autoptr(GChecksum) sha512 = NULL;

  g_return_val_if_fail (contents != NULL, NULL);
  g_return_val_if_fail (signature != NULL || download_time != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  bf = eos_branch_file_new_empty ();

  bf->raw_contents = g_bytes_ref (contents);

  if (signature != NULL)
    bf->raw_signature = g_bytes_ref (signature);

  bf->branch_file = g_key_file_new ();
  contents_bytes = g_bytes_get_data (bf->raw_contents, &contents_len);
  if (!g_key_file_load_from_data (bf->branch_file,
                                  contents_bytes,
                                  contents_len,
                                  G_KEY_FILE_NONE,
                                  error))
    return NULL;

  sha512 = g_checksum_new (G_CHECKSUM_SHA512);
  g_checksum_update (sha512, contents_bytes, contents_len);
  bf->contents_sha512sum = g_strdup (g_checksum_get_string (sha512));

  if (signature != NULL)
    {
      if (!eos_updater_get_timestamp_from_branch_file_keyfile (bf->branch_file,
                                                               &bf->download_time,
                                                               error))
        return NULL;
    }
  else
    bf->download_time = g_date_time_ref (download_time);

  return g_steal_pointer (&bf);
}

gboolean
eos_branch_file_save_to_repo (EosBranchFile *branch_file,
                              OstreeRepo *repo,
                              GCancellable *cancellable,
                              GError **error)
{
  g_autoptr(GFile) ext_dir = eos_updater_get_eos_extensions_dir (repo);
  g_autoptr(GFile) branch_file_path = NULL;
  g_autoptr(GFile) signature_path = NULL;

  get_branch_file_paths (ext_dir, &branch_file_path, &signature_path);
  return eos_branch_file_save (branch_file,
                               branch_file_path,
                               signature_path,
                               cancellable,
                               error);
}

gboolean
eos_branch_file_save (EosBranchFile *branch_file,
                      GFile *target,
                      GFile *target_signature,
                      GCancellable *cancellable,
                      GError **error)
{
  gconstpointer contents;
  gsize len;

  g_return_val_if_fail (EOS_IS_BRANCH_FILE (branch_file), FALSE);
  g_return_val_if_fail (G_IS_FILE (target), FALSE);
  g_return_val_if_fail (G_IS_FILE (target_signature), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  contents = g_bytes_get_data (branch_file->raw_contents,
                               &len);
  if (!g_file_replace_contents (target,
                                contents,
                                len,
                                NULL,
                                FALSE,
                                G_FILE_CREATE_NONE,
                                NULL,
                                cancellable,
                                error))
    return FALSE;

  if (branch_file->raw_signature == NULL)
    {
      gint64 unix_time = g_date_time_to_unix (branch_file->download_time);
      g_autoptr(GError) local_error;

      if (unix_time < 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "invalid download time of the branch file");
          return FALSE;
        }

      if (!g_file_set_attribute_uint64 (target,
                                        G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                        unix_time,
                                        G_FILE_QUERY_INFO_NONE,
                                        cancellable,
                                        error))
        return FALSE;

      if (!g_file_delete (target_signature,
                          cancellable,
                          &local_error))
        {
          if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return FALSE;
          g_clear_error (&local_error);
        }
    }
  else
    {
      contents = g_bytes_get_data (branch_file->raw_signature,
                                   &len);
      if (!g_file_replace_contents (target_signature,
                                    contents,
                                    len,
                                    NULL,
                                    FALSE,
                                    G_FILE_CREATE_NONE,
                                    NULL,
                                    cancellable,
                                    error))
        return FALSE;
    }

  return TRUE;
}
