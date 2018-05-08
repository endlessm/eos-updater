/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include <libeos-updater-util/util.h>

#include <libsoup/soup.h>

#include <errno.h>
#include <string.h>

static gboolean
is_ancestor (GFile *dir,
             GFile *file)
{
  g_autoptr(GFile) child = g_object_ref (file);

  for (;;)
    {
      g_autoptr(GFile) parent = g_file_get_parent (child);

      if (parent == NULL)
        return FALSE;

      if (g_file_equal (dir, parent))
        break;

      g_set_object (&child, parent);
    }

  return TRUE;
}

/* pass /a as the dir parameter and /a/b/c/d as the file parameter,
 * will delete the /a/b/c/d file then /a/b/c and /a/b directories if
 * empty.
 */
static gboolean
delete_files_and_empty_parents (GFile *dir,
                                GFile *file,
                                GCancellable *cancellable,
                                GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GFile) child = NULL;

  if (!is_ancestor (dir, file))
    {
      g_autofree gchar *raw_dir_path = g_file_get_path (dir);
      g_autofree gchar *raw_file_path = g_file_get_path (file);

      g_warning ("%s is not an ancestor of %s, not deleting anything",
                 raw_dir_path,
                 raw_file_path);
      return FALSE;
    }

  if (!g_file_delete (file, cancellable, &local_error))
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_clear_error (&local_error);
      else
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }

  child = g_object_ref (file);
  for (;;)
    {
      g_autoptr(GFile) parent = g_file_get_parent (child);

      if (g_file_equal (dir, parent))
        break;

      if (!g_file_delete (parent, cancellable, &local_error))
        {
          if (!(g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
                g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY)))
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }

          break;
        }

      g_set_object (&child, parent);
    }

  return TRUE;
}

static gboolean
create_directories (GFile *directory,
                    GCancellable *cancellable,
                    GError **error)
{
  g_autoptr(GError) local_error = NULL;

  if (g_file_make_directory_with_parents (directory, cancellable, &local_error))
    return TRUE;

  if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    return TRUE;

  g_propagate_error (error, g_steal_pointer (&local_error));
  return FALSE;
}

static gboolean
create_directories_and_file (GFile *target,
                             GBytes *contents,
                             GCancellable *cancellable,
                             GError **error)
{
  g_autoptr(GFile) target_parent = g_file_get_parent (target);
  gconstpointer raw;
  gsize len;

  if (!create_directories (target_parent, cancellable, error))
    return FALSE;

  raw = g_bytes_get_data (contents, &len);
  return g_file_replace_contents (target,
                                  raw,
                                  len,
                                  NULL,
                                  FALSE,
                                  G_FILE_CREATE_NONE,
                                  NULL,
                                  cancellable,
                                  error);
}

gboolean
eos_updater_save_or_delete  (GBytes *contents,
                             GFile *dir,
                             const gchar *filename,
                             GCancellable *cancellable,
                             GError **error)
{
  g_autoptr(GFile) target = g_file_get_child (dir, filename);

  if (contents == NULL)
    return delete_files_and_empty_parents (dir, target, cancellable, error);

  return create_directories_and_file (target, contents, cancellable, error);
}

guint
eos_updater_queue_callback (GMainContext *context,
                            GSourceFunc function,
                            gpointer user_data,
                            const gchar *name)
{
  g_autoptr(GSource) source = g_idle_source_new ();

  if (name != NULL)
    g_source_set_name (source, name);
  g_source_set_callback (source, function, user_data, NULL);

  return g_source_attach (source, context);
}

const gchar *
eos_updater_get_envvar_or (const gchar *envvar,
                           const gchar *default_value)
{
  const gchar *value = g_getenv (envvar);

  if (value != NULL)
    return value;

  return default_value;
}

gboolean
eos_updater_read_file_to_bytes (GFile *file,
                                GCancellable *cancellable,
                                GBytes **out_bytes,
                                GError **error)
{
  g_autofree gchar *contents = NULL;
  gsize len = 0;

  if (!g_file_load_contents (file,
                             cancellable,
                             &contents,
                             &len,
                             NULL,
                             error))
    return FALSE;

  *out_bytes = g_bytes_new_take (g_steal_pointer (&contents), len);
  return TRUE;
}

struct _EosQuitFile
{
  GObject parent_instance;

  GFileMonitor *monitor;
  gulong signal_id;
  guint timeout_seconds;
  guint timeout_id;
  EosQuitFileCheckCallback callback;
  gpointer user_data;
  GDestroyNotify notify;
};

static void
quit_clear_user_data (EosQuitFile *quit_file)
{
  gpointer user_data = g_steal_pointer (&quit_file->user_data);
  GDestroyNotify notify = quit_file->notify;
  quit_file->notify = NULL;

  if (notify != NULL)
    notify (user_data);
}

static void
quit_disconnect_monitor (EosQuitFile *quit_file)
{
  gulong id = quit_file->signal_id;

  quit_file->signal_id = 0;
  if (id > 0)
    g_signal_handler_disconnect (quit_file->monitor, id);
}

static void
quit_clear_source (EosQuitFile *quit_file)
{
  guint id = quit_file->timeout_id;

  quit_file->timeout_id = 0;
  if (id > 0)
    g_source_remove (id);
}

static void
eos_quit_file_dispose_impl (EosQuitFile *quit_file)
{
  quit_clear_user_data (quit_file);
  quit_clear_source (quit_file);
  quit_disconnect_monitor (quit_file);
  g_clear_object (&quit_file->monitor);
}

EOS_DEFINE_REFCOUNTED (EOS_QUIT_FILE,
                       EosQuitFile,
                       eos_quit_file,
                       eos_quit_file_dispose_impl,
                       NULL)

static gboolean
quit_file_source_func (gpointer quit_file_ptr)
{
  EosQuitFile *quit_file = EOS_QUIT_FILE (quit_file_ptr);

  if (quit_file->callback (quit_file->user_data) == EOS_QUIT_FILE_KEEP_CHECKING)
    return G_SOURCE_CONTINUE;

  quit_file->timeout_id = 0;
  quit_clear_user_data (quit_file);
  return G_SOURCE_REMOVE;
}

static void
on_quit_file_changed (GFileMonitor *monitor,
                      GFile *file,
                      GFile *other,
                      GFileMonitorEvent event,
                      gpointer quit_file_ptr)
{
  EosQuitFile *quit_file = EOS_QUIT_FILE (quit_file_ptr);

  if (event != G_FILE_MONITOR_EVENT_DELETED)
    return;

  if (quit_file->callback (quit_file->user_data) == EOS_QUIT_FILE_KEEP_CHECKING)
    quit_file->timeout_id = g_timeout_add_seconds (quit_file->timeout_seconds,
                                                   quit_file_source_func,
                                                   quit_file);
  g_signal_handler_disconnect (quit_file->monitor, quit_file->signal_id);
  quit_file->signal_id = 0;
}

EosQuitFile *
eos_updater_setup_quit_file (const gchar *path,
                             EosQuitFileCheckCallback check_callback,
                             gpointer user_data,
                             GDestroyNotify notify,
                             guint timeout_seconds,
                             GError **error)
{
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileMonitor) monitor = NULL;
  g_autoptr(EosQuitFile) quit_file = NULL;

  file = g_file_new_for_path (path);
  monitor = g_file_monitor_file (file,
                                 G_FILE_MONITOR_NONE,
                                 NULL,
                                 error);
  if (monitor == NULL)
    return NULL;

  quit_file = g_object_new (EOS_TYPE_QUIT_FILE, NULL);
  quit_file->monitor = g_steal_pointer (&monitor);
  quit_file->signal_id = g_signal_connect (quit_file->monitor,
                                           "changed",
                                           G_CALLBACK (on_quit_file_changed),
                                           quit_file);
  quit_file->timeout_seconds = timeout_seconds;
  quit_file->callback = check_callback;
  quit_file->user_data = user_data;
  quit_file->notify = notify;

  return g_steal_pointer (&quit_file);
}

static gboolean
rm_file_ignore_noent (GFile         *file,
                      GCancellable  *cancellable,
                      GError       **error)
{
  g_autoptr(GError) local_error = NULL;

  if (g_file_delete (file, cancellable, &local_error))
    return TRUE;

  if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    return TRUE;

  g_propagate_error (error, g_steal_pointer (&local_error));
  return FALSE;
}

static gboolean
rm_rf_internal (GFile   *topdir,
                GError **error)
{
  GQueue queue = G_QUEUE_INIT;
  GList *dir_stack = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GFileInfo) top_info = NULL;

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
    return rm_file_ignore_noent (topdir, NULL, error);

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
          else if (!rm_file_ignore_noent (child, NULL, error))
            return FALSE;
        }
    }

  while (dir_stack != NULL)
    {
      GList *first = dir_stack;
      g_autoptr(GFile) dir = G_FILE (first->data);

      dir_stack = g_list_remove_link (dir_stack, first);
      g_list_free_1 (first);
      if (!rm_file_ignore_noent (dir, NULL, error))
        return FALSE;
    }

  return TRUE;
}

gboolean
eos_updater_remove_recursive (GFile *topdir, GError **error)
{
  if (!rm_rf_internal (topdir, error))
    {
      g_autofree gchar *raw_path = g_file_get_path (topdir);
      g_prefix_error (error,
                      "Failed to remove the file or directory in %s, this should not happen: ",
                      raw_path);

      return FALSE;
    }

  return TRUE;
}

