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

#include "eos-extensions.h"

#include "eos-util.h"

#include <string.h>

static void
eos_ref_dispose_impl (EosRef *ref)
{
  g_clear_pointer (&ref->contents, g_bytes_unref);
  g_clear_pointer (&ref->signature, g_bytes_unref);
}

static void
eos_ref_finalize_impl (EosRef *ref)
{
  g_free (ref->name);
}

EOS_DEFINE_REFCOUNTED (EOS_REF,
                       EosRef,
                       eos_ref,
                       eos_ref_dispose_impl,
                       eos_ref_finalize_impl)

EosRef *
eos_ref_new_empty (void)
{
  return g_object_new (EOS_TYPE_REF, NULL);
}

static void
get_ref_file_paths (GFile *ext_path,
                    const gchar *ref_name,
                    GFile **ref_file,
                    GFile **ref_file_sig)
{
  g_autofree gchar *raw_rel_path = g_build_filename ("refs.d", ref_name, NULL);
  g_autofree gchar *raw_rel_sig_path = g_strconcat (raw_rel_path, ".sig", NULL);

  *ref_file = g_file_get_child (ext_path, raw_rel_path);
  *ref_file_sig = g_file_get_child (ext_path, raw_rel_sig_path);
}

static gchar *
get_ref_name (GBytes *contents,
              GError **error)
{
  g_autoptr(GKeyFile) keyfile = g_key_file_new ();

  if (!g_key_file_load_from_data (keyfile,
                                  g_bytes_get_data (contents, NULL),
                                  g_bytes_get_size (contents),
                                  G_KEY_FILE_NONE,
                                  error))
    return FALSE;

  return g_key_file_get_string (keyfile,
                                "mapping",
                                "ref",
                                error);
}

EosRef *
eos_ref_new_from_files (GFile *ref_file,
                        GFile *ref_sig_file,
                        const gchar *name,
                        GCancellable *cancellable,
                        GError **error)
{
  g_autoptr(GBytes) contents = NULL;
  g_autoptr(GBytes) signature = NULL;
  g_autoptr(EosRef) ref = NULL;
  g_autofree gchar *saved_name = NULL;

  g_return_val_if_fail (G_IS_FILE (ref_file), NULL);
  g_return_val_if_fail (G_IS_FILE (ref_sig_file), NULL);
  /* name can be NULL */
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!eos_updater_read_file_to_bytes (ref_file,
                                       cancellable,
                                       &contents,
                                       error))
    return FALSE;

  if (!eos_updater_read_file_to_bytes (ref_sig_file,
                                       cancellable,
                                       &signature,
                                       error))
    return FALSE;

  saved_name = get_ref_name (contents,
                             error);
  if (saved_name == NULL)
    return FALSE;

  if (name != NULL &&
      strcmp (name, saved_name) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected a ref file with name %s, got %s",
                   name,
                   saved_name);
      return FALSE;
    }

  ref = eos_ref_new_empty ();
  ref->name = g_steal_pointer (&saved_name);
  ref->contents = g_steal_pointer (&contents);
  ref->signature = g_steal_pointer (&signature);

  return g_steal_pointer (&ref);
}

EosRef *
eos_ref_new_from_repo (OstreeRepo *repo,
                       const gchar *name,
                       GCancellable *cancellable,
                       GError **error)
{
  g_autoptr(GFile) ref_file = NULL;
  g_autoptr(GFile) ref_sig_file = NULL;
  g_autoptr(GFile) ext_dir = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ext_dir = eos_updater_get_eos_extensions_dir (repo);
  get_ref_file_paths (ext_dir, name, &ref_file, &ref_sig_file);
  return eos_ref_new_from_files (ref_file,
                                 ref_sig_file,
                                 name,
                                 cancellable,
                                 error);
}

gboolean
eos_ref_save (EosRef *ref,
              OstreeRepo *repo,
              GCancellable *cancellable,
              GError **error)
{
  g_autoptr(GFile) ref_file = NULL;
  g_autoptr(GFile) ref_sig_file = NULL;
  g_autoptr(GFile) ext_dir = NULL;
  g_autofree gchar *raw_rel_path = NULL;
  g_autofree gchar *raw_rel_sig_path = NULL;

  g_return_val_if_fail (EOS_IS_REF (ref), FALSE);
  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ext_dir = eos_updater_get_eos_extensions_dir (repo);
  get_ref_file_paths (ext_dir, ref->name, &ref_file, &ref_sig_file);
  raw_rel_path = g_file_get_relative_path (ext_dir, ref_file);
  raw_rel_sig_path = g_file_get_relative_path (ext_dir, ref_sig_file);

  if (!eos_updater_save_or_delete (ref->contents,
                                   ext_dir,
                                   raw_rel_path,
                                   cancellable,
                                   error))
    return FALSE;

  if (!eos_updater_save_or_delete (ref->signature,
                                   ext_dir,
                                   raw_rel_sig_path,
                                   cancellable,
                                   error))
    return FALSE;

  return TRUE;
}

static void
eos_extensions_dispose_impl (EosExtensions *extensions)
{
  g_clear_pointer (&extensions->summary, g_bytes_unref);
  g_clear_pointer (&extensions->summary_sig, g_bytes_unref);
  g_clear_object (&extensions->branch_file);
  g_clear_pointer (&extensions->refs, g_ptr_array_unref);
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

  extensions->refs = object_array_new ();

  return extensions;
}

static gboolean
get_branch_file (EosExtensions *extensions,
                 OstreeRepo *repo,
                 GCancellable *cancellable,
                 GError **error)
{
  extensions->branch_file = eos_branch_file_new_from_repo (repo,
                                                           cancellable,
                                                           error);
  return extensions->branch_file != NULL;
}

static gboolean
get_ref_counterpart (GFile *file,
                     GFileInfo *info,
                     GHashTable *found_files,
                     GFile **out_ref,
                     GFile **out_sig)
{
  const gchar *name = g_file_info_get_name (info);
  const gchar *suffix = ".sig";
  gboolean file_is_sig = g_str_has_suffix (name, suffix);
  g_autofree gchar *counterpart_name = NULL;
  GFile *counterpart_path;

  if (file_is_sig)
    {
      counterpart_name = g_strdup (name);
      counterpart_name[strlen(counterpart_name) - strlen(suffix)] = '\0';
    }
  else
    counterpart_name = g_strdup_printf ("%s%s", name, suffix);

  counterpart_path = g_hash_table_lookup (found_files, counterpart_name);

  if (counterpart_path == NULL)
    {
      g_hash_table_insert (found_files, g_strdup (name), g_object_ref (file));
      return FALSE;
    }

  if (file_is_sig)
    {
      *out_ref = g_object_ref (counterpart_path);
      *out_sig = g_object_ref (file);
    }
  else
    {
      *out_ref = g_object_ref (file);
      *out_sig = g_object_ref (counterpart_path);
    }

  g_hash_table_remove (found_files, counterpart_name);

  return TRUE;
}

static gboolean
handle_regular_ref_file (EosExtensions *extensions,
                         GFile *file,
                         GFileInfo *info,
                         GHashTable *found_files,
                         GCancellable *cancellable,
                         GError **error)
{
  g_autoptr(GFile) ref_path = NULL;
  g_autoptr(GFile) ref_sig_path = NULL;
  g_autoptr(EosRef) ref = NULL;

  if (!get_ref_counterpart (file,
                            info,
                            found_files,
                            &ref_path,
                            &ref_sig_path))
    return TRUE;

  ref = eos_ref_new_from_files (ref_path,
                                ref_sig_path,
                                NULL,
                                cancellable,
                                error);
  if (ref == NULL)
    return FALSE;

  g_ptr_array_add (extensions->refs,
                   g_steal_pointer (&ref));
  return TRUE;
}

static gboolean
gather_refs (EosExtensions *extensions,
             OstreeRepo *repo,
             GCancellable *cancellable,
             GError **error)
{
  g_autoptr(GFile) ext_dir = eos_updater_get_eos_extensions_dir (repo);
  g_autoptr(GFile) refs_dir = g_file_get_child (ext_dir, "refs.d");
  GQueue queue = G_QUEUE_INIT;

  if (!g_file_query_exists (refs_dir, cancellable))
    return TRUE;

  g_queue_push_head (&queue, g_object_ref (refs_dir));
  while (!g_queue_is_empty (&queue))
    {
      const gchar *attributes = G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                G_FILE_ATTRIBUTE_STANDARD_NAME;
      g_autoptr(GFile) dir = G_FILE (g_queue_pop_tail (&queue));
      g_autoptr(GFileEnumerator) enumerator = g_file_enumerate_children (dir,
                                                                         attributes,
                                                                         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                                         cancellable,
                                                                         error);
      g_autoptr(GHashTable) found_files = g_hash_table_new_full (g_str_hash,
                                                                 g_str_equal,
                                                                 g_free,
                                                                 g_object_unref);

      if (enumerator == NULL)
        return FALSE;

      for (;;)
        {
          GFileInfo *info;
          GFile* child;
          GFileType file_type;

          if (!g_file_enumerator_iterate (enumerator,
                                          &info,
                                          &child,
                                          cancellable,
                                          error))
            return FALSE;

          if (info == NULL || child == NULL)
            break;

          file_type = g_file_info_get_file_type (info);
          switch (file_type)
            {
            case G_FILE_TYPE_DIRECTORY:
              g_queue_push_head (&queue, g_object_ref (child));
              break;

            case G_FILE_TYPE_REGULAR:
              if (!handle_regular_ref_file (extensions,
                                            child,
                                            info,
                                            found_files,
                                            cancellable,
                                            error))
                return FALSE;
              break;

            case G_FILE_TYPE_UNKNOWN:
            case G_FILE_TYPE_SYMBOLIC_LINK:
            case G_FILE_TYPE_SPECIAL:
            case G_FILE_TYPE_SHORTCUT:
            case G_FILE_TYPE_MOUNTABLE:
            default:
              break;
            }
        }
    }

  return TRUE;
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

  return TRUE;
}

EosExtensions *
eos_extensions_new_from_repo (OstreeRepo *repo,
                              GCancellable *cancellable,
                              GError **error)
{
  g_autoptr(EosExtensions) extensions = NULL;
  g_autoptr(GPtrArray) refs = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  extensions = eos_extensions_new_empty ();
  if (!get_branch_file (extensions,
                        repo,
                        cancellable,
                        error))
    return NULL;

  if (!gather_refs (extensions,
                    repo,
                    cancellable,
                    error))
    return NULL;

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
  g_autoptr(GFile) branch_file_path = NULL;
  g_autoptr(GFile) branch_file_sig_path = NULL;
  guint idx;

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

  for (idx = 0; idx < extensions->refs->len; ++idx)
    {
      EosRef *ref = EOS_REF (g_ptr_array_index (extensions->refs, idx));

      if (!eos_ref_save (ref, repo, cancellable, error))
        return FALSE;
    }

  return eos_branch_file_save_to_repo (extensions->branch_file,
                                       repo,
                                       cancellable,
                                       error);
}
