/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include <libeos-updater-util/util.h>

#include <errno.h>
#include <string.h>

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

struct _EuuQuitFile
{
  GObject parent_instance;

  GFileMonitor *monitor;
  gulong signal_id;
  guint timeout_seconds;
  guint timeout_id;
  EuuQuitFileCheckCallback callback;
  gpointer user_data;
  GDestroyNotify notify;
};

static void
quit_clear_user_data (EuuQuitFile *quit_file)
{
  gpointer user_data = g_steal_pointer (&quit_file->user_data);
  GDestroyNotify notify = quit_file->notify;
  quit_file->notify = NULL;

  if (notify != NULL)
    notify (user_data);
}

static void
quit_disconnect_monitor (EuuQuitFile *quit_file)
{
  gulong id = quit_file->signal_id;

  quit_file->signal_id = 0;
  if (id > 0)
    g_signal_handler_disconnect (quit_file->monitor, id);
}

static void
quit_clear_source (EuuQuitFile *quit_file)
{
  guint id = quit_file->timeout_id;

  quit_file->timeout_id = 0;
  if (id > 0)
    g_source_remove (id);
}

G_DEFINE_TYPE (EuuQuitFile, euu_quit_file, G_TYPE_OBJECT)

static void
euu_quit_file_dispose (GObject *object)
{
  EuuQuitFile *self = EUU_QUIT_FILE (object);

  quit_clear_user_data (self);
  quit_clear_source (self);
  quit_disconnect_monitor (self);
  g_clear_object (&self->monitor);

  G_OBJECT_CLASS (euu_quit_file_parent_class)->dispose (object);
}

static void
euu_quit_file_class_init (EuuQuitFileClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = euu_quit_file_dispose;
}

static void
euu_quit_file_init (EuuQuitFile *self)
{
  /* nothing here */
}

static gboolean
quit_file_source_func (gpointer quit_file_ptr)
{
  EuuQuitFile *quit_file = EUU_QUIT_FILE (quit_file_ptr);

  if (quit_file->callback (quit_file->user_data) == EUU_QUIT_FILE_KEEP_CHECKING)
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
  EuuQuitFile *quit_file = EUU_QUIT_FILE (quit_file_ptr);

  if (event != G_FILE_MONITOR_EVENT_DELETED)
    return;

  if (quit_file->callback (quit_file->user_data) == EUU_QUIT_FILE_KEEP_CHECKING)
    quit_file->timeout_id = g_timeout_add_seconds (quit_file->timeout_seconds,
                                                   quit_file_source_func,
                                                   quit_file);
  g_signal_handler_disconnect (quit_file->monitor, quit_file->signal_id);
  quit_file->signal_id = 0;
}

/**
 * eos_updater_setup_quit_file: (skip)
 * @path:
 * @check_callback:
 * @user_data:
 * @notify:
 * @timeout_seconds:
 * @error:
 *
 * Returns:
 */
EuuQuitFile *
eos_updater_setup_quit_file (const gchar *path,
                             EuuQuitFileCheckCallback check_callback,
                             gpointer user_data,
                             GDestroyNotify notify,
                             guint timeout_seconds,
                             GError **error)
{
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileMonitor) monitor = NULL;
  g_autoptr(EuuQuitFile) quit_file = NULL;

  file = g_file_new_for_path (path);
  monitor = g_file_monitor_file (file,
                                 G_FILE_MONITOR_NONE,
                                 NULL,
                                 error);
  if (monitor == NULL)
    return NULL;

  quit_file = g_object_new (EUU_TYPE_QUIT_FILE, NULL);
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
                      gboolean       ignore_enotempty,
                      GCancellable  *cancellable,
                      GError       **error)
{
  g_autoptr(GError) local_error = NULL;

  if (g_file_delete (file, cancellable, &local_error))
    return TRUE;

  if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    return TRUE;
  if (ignore_enotempty &&
      g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY))
    return TRUE;

  g_propagate_error (error, g_steal_pointer (&local_error));
  return FALSE;
}

static gboolean
rm_rf_internal (GFile                     *topdir,
                EosUpdaterFileFilterFunc   filter_func,
                GError                   **error)
{
  GQueue queue = G_QUEUE_INIT;
  GList *dirs_to_delete = NULL;
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

  if (filter_func != NULL &&
      filter_func (topdir, top_info) == EOS_UPDATER_FILE_FILTER_IGNORE)
    return TRUE;
  if (g_file_info_get_file_type (top_info) != G_FILE_TYPE_DIRECTORY)
    return rm_file_ignore_noent (topdir, FALSE, NULL, error);

  gboolean any_ignored = FALSE;

  g_queue_push_head (&queue, g_object_ref (topdir));

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

      guint n_ignored = 0;

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

          if (filter_func != NULL &&
              filter_func (child, info) == EOS_UPDATER_FILE_FILTER_IGNORE)
            {
              n_ignored++;
              continue;
            }

          if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
            g_queue_push_head (&queue, g_object_ref (child));
          else if (!rm_file_ignore_noent (child, FALSE, NULL, error))
            return FALSE;
        }

      if (n_ignored == 0)
        dirs_to_delete = g_list_prepend (dirs_to_delete, g_object_ref (dir));
      else
        any_ignored = TRUE;
    }

  if (!any_ignored)
    dirs_to_delete = g_list_append (dirs_to_delete, g_object_ref (topdir));

  while (dirs_to_delete != NULL)
    {
      GList *first = dirs_to_delete;
      g_autoptr(GFile) dir = G_FILE (first->data);

      dirs_to_delete = g_list_remove_link (dirs_to_delete, first);
      g_list_free_1 (first);
      if (!rm_file_ignore_noent (dir, TRUE, NULL, error))
        return FALSE;
    }

  return TRUE;
}

/**
 * eos_updater_remove_recursive:
 * @topdir:
 * @filter_func: (scope call):
 * @error:
 *
 * Returns:
 */
gboolean
eos_updater_remove_recursive (GFile                     *topdir,
                              EosUpdaterFileFilterFunc   filter_func,
                              GError                   **error)
{
  if (!rm_rf_internal (topdir, filter_func, error))
    {
      g_autofree gchar *raw_path = g_file_get_path (topdir);
      g_prefix_error (error,
                      "Failed to remove the file or directory in %s, this should not happen: ",
                      raw_path);

      return FALSE;
    }

  return TRUE;
}

