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

#include "misc-utils.h"

#include <errno.h>

#include <unistd.h>

gboolean
rm_rf (GFile *topdir,
       GError **error)
{
  GQueue queue = G_QUEUE_INIT;
  GList *dir_stack = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GFileInfo) top_info;

  top_info = g_file_query_info (topdir,
                                G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                NULL,
                                &local_error);
  if (top_info == NULL)
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        return TRUE;
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  if (g_file_info_get_file_type (top_info) != G_FILE_TYPE_DIRECTORY)
    return g_file_delete (topdir, NULL, error);

  g_queue_push_head (&queue, g_object_ref (topdir));
  dir_stack = g_list_prepend (dir_stack, g_object_ref (topdir));
  while (!g_queue_is_empty (&queue))
    {
      g_autoptr(GFile) dir = G_FILE (g_queue_pop_tail (&queue));
      g_autoptr(GFileEnumerator) enumerator = g_file_enumerate_children (dir,
                                                                         G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                                                         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                                         NULL,
                                                                         error);

      if (enumerator == NULL)
        return FALSE;

      for (;;)
        {
          GFileInfo *info;
          GFile* child;

          if (!g_file_enumerator_iterate (enumerator,
                                          &info,
                                          &child,
                                          NULL,
                                          error))
            return FALSE;

          if (info == NULL || child == NULL)
            break;

          if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
            {
              g_queue_push_head (&queue, g_object_ref (child));
              dir_stack = g_list_prepend (dir_stack, g_object_ref (child));
            }
          else if (!g_file_delete (child, NULL, error))
            return FALSE;
        }
    }

  while (dir_stack != NULL)
    {
      GList *first = dir_stack;
      g_autoptr(GFile) dir = G_FILE (first->data);

      dir_stack = g_list_remove_link (dir_stack, first);
      g_list_free_1 (first);
      if (!g_file_delete (dir, NULL, error))
        return FALSE;
    }

  return TRUE;
}

gchar **
generate_strv (const gchar *str, ...)
{
  g_autoptr(GPtrArray) vec = string_array_new ();
  va_list args;

  va_start (args, str);
  while (str != NULL)
    {
      g_ptr_array_add (vec, g_strdup (str));
      str = va_arg (args, const gchar *);
    }
  va_end (args);
  g_ptr_array_add (vec, NULL);

  return (gchar**)g_ptr_array_free (g_steal_pointer (&vec), FALSE);
}

gboolean
load_to_bytes (GFile *file,
               GBytes **bytes,
               GError **error)
{
  g_autofree gchar *data = NULL;
  gsize len;

  if (!g_file_load_contents (file,
                             NULL,
                             &data,
                             &len,
                             NULL,
                             error))
    return FALSE;

  *bytes = g_bytes_new_take (g_steal_pointer (&data), len);
  return TRUE;
}

gboolean
create_file (GFile *path,
             GBytes* bytes,
             GError **error)
{
  return g_file_replace_contents (path,
                                  (bytes != NULL) ? g_bytes_get_data (bytes, NULL) : "",
                                  (bytes != NULL) ? g_bytes_get_size (bytes) : 0,
                                  NULL,
                                  FALSE,
                                  G_FILE_CREATE_NONE,
                                  NULL,
                                  NULL,
                                  error);
}

gboolean
create_directory (GFile *path,
                  GError **error)
{
  g_autoptr(GError) local_error = NULL;

  if (!g_file_make_directory_with_parents (path, NULL, &local_error))
    if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
      {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
      }

  return TRUE;
}

gboolean
create_symlink (const gchar *target,
                GFile *link,
                GError **error)
{
  g_autofree gchar *path = g_file_get_path (link);

  if (symlink (target, path) < 0)
    {
      int saved_errno = errno;

      g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                           g_strerror (saved_errno));

      return FALSE;
    }

  return TRUE;
}

gboolean
load_key_file (GFile *file,
               GKeyFile **out_keyfile,
               GError **error)
{
  g_autofree gchar *path = g_file_get_path (file);
  g_autoptr(GKeyFile) keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (keyfile,
                                  path,
                                  G_KEY_FILE_NONE,
                                  error))
    return FALSE;

  *out_keyfile = g_steal_pointer (&keyfile);
  return TRUE;
}

gboolean
save_key_file (GFile *file,
               GKeyFile *keyfile,
               GError **error)
{
  g_autofree gchar *path = g_file_get_path (file);

  return g_key_file_save_to_file (keyfile, path, error);
}

static GDateTime *
get_now (void)
{
  static GDateTime *now = NULL;

  if (now == NULL)
    now = g_date_time_new_now_utc ();

  return g_date_time_ref (now);
}

GDateTime *
days_ago (gint days)
{
  g_autoptr(GDateTime) now = get_now ();
  g_autoptr(GDateTime) now_fixed = g_date_time_new_utc (g_date_time_get_year (now),
                                                        g_date_time_get_month (now),
                                                        g_date_time_get_day_of_month (now),
                                                        12,
                                                        0,
                                                        0);

  return g_date_time_add_days (now_fixed, -days);
}

gboolean
input_stream_to_string (GInputStream *stream,
                        gchar **out_str,
                        GError **error)
{
  gsize len = 2 * 1024 * 1024;
  g_autoptr(GString) str = g_string_new (NULL);

  for (;;)
    {
      g_autoptr(GBytes) bytes = g_input_stream_read_bytes (stream, len, NULL, error);
      gsize read_len;
      gconstpointer read_data;

      if (bytes == NULL)
        return FALSE;

      read_data = g_bytes_get_data (bytes, &read_len);
      if (read_len == 0)
        break;

      g_string_append_len (str, read_data, read_len);
    }

  *out_str = g_string_free (g_steal_pointer (&str), FALSE);
  return TRUE;
}

gboolean
cp (GFile *source,
    GFile *target,
    GError **error)
{
  g_autoptr(GBytes) bytes = NULL;

  if (!load_to_bytes (source,
		      &bytes,
		      error))
    return FALSE;

  if (!create_file (target,
		    bytes,
		    error))
    return FALSE;

  return TRUE;
}
