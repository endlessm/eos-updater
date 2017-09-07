/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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
 *  - Sam Spilsbury <sam@endlessm.com>
 */

#include "enums.h"
#include "types.h"

#include <string.h>
#include <errno.h>

#include <flatpak.h>
#include <ostree.h>

#include "flatpak.h"
#include "util.h"

static FlatpakRemoteRefAction *
flatpak_remote_ref_action_new (EosUpdaterUtilFlatpakRemoteRefActionType  type,
                               FlatpakRemoteRef                         *ref,
                               guint64                                   order)
{
  FlatpakRemoteRefAction *action = g_slice_new0 (FlatpakRemoteRefAction);

  action->ref_cnt = 1;
  action->type = type;
  action->ref = g_object_ref (ref);
  action->order = order;

  return action;
}

FlatpakRemoteRefAction *
flatpak_remote_ref_action_ref (FlatpakRemoteRefAction *action)
{
  ++action->ref_cnt;
  return action;
}

void
flatpak_remote_ref_action_unref (FlatpakRemoteRefAction *action)
{
  if (--action->ref_cnt != 0)
    return;

  g_object_unref (action->ref);

  g_slice_free (FlatpakRemoteRefAction, action);
}

static gboolean
flatpak_remote_ref_action_type_parse (const gchar                               *action,
                                      EosUpdaterUtilFlatpakRemoteRefActionType  *out_action_type,
                                      GError                                   **error)
{
  GEnumClass *enum_class = g_type_class_ref (EOS_TYPE_UPDATER_UTIL_FLATPAK_REMOTE_REF_ACTION_TYPE);
  GEnumValue *enum_value = g_enum_get_value_by_nick (enum_class, action);

  g_type_class_unref (enum_class);

  if (!enum_value)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Invalid action type %s specified in autoinstall spec",
                   action);
      return FALSE;
    }

  *out_action_type = (EosUpdaterUtilFlatpakRemoteRefActionType) enum_value->value;

  return TRUE;
}

static gboolean
parse_monotonic_counter (const gchar  *component,
                         guint64      *out_counter,
                         GError      **error)
{
  g_return_val_if_fail (out_counter != NULL, FALSE);

  return eos_string_to_unsigned (component, 10, 0, G_MAXUINT32, out_counter, error);
}

static FlatpakRemoteRefAction *
flatpak_remote_ref_action_parse (const gchar *line, GError **error)
{
  g_auto(GStrv) components = g_strsplit (line, " ", -1);
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autoptr(FlatpakRef) flatpak_ref = NULL;
  g_autoptr(FlatpakRemoteRef) flatpak_remote_ref = NULL;
  guint64 order;
  EosUpdaterUtilFlatpakRemoteRefActionType action_type;

  if (g_strv_length (components) != 3)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Invalid number of components in autoinstall spec line: %s",
                   line);
      return NULL;
    }

  if (!flatpak_remote_ref_action_type_parse (components[0], &action_type, error))
    return NULL;

  if (!ostree_parse_refspec (components[1], &remote, &ref, error))
    return NULL;

  if (!parse_monotonic_counter (components[2], &order, error))
    return NULL;

  flatpak_ref = flatpak_ref_parse (ref, error);

  if (!flatpak_ref)
    return NULL;

  flatpak_remote_ref = g_object_new (FLATPAK_TYPE_REMOTE_REF,
                                     "remote-name", remote,
                                     "name", flatpak_ref_get_name (flatpak_ref),
                                     "kind", flatpak_ref_get_kind (flatpak_ref),
                                     NULL);
  
  return flatpak_remote_ref_action_new (action_type, flatpak_remote_ref, order);
}

static gint
sort_flatpak_remote_ref_actions (gconstpointer a, gconstpointer b)
{
  FlatpakRemoteRefAction **action_a = (FlatpakRemoteRefAction **) a;
  FlatpakRemoteRefAction **action_b = (FlatpakRemoteRefAction **) b;

  return (gint) (*action_a)->order - (gint) (*action_b)->order;
}

static GPtrArray *
read_flatpak_ref_actions_from_file (GFile         *file,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
  /* Now that we have the file contents, time to read in the list of
   * flatpaks to install into a pointer array. Parse out the OSTree ref
   * and then parse the FlatpakRemoteRefAction */
  g_autoptr(GFileInputStream) input_stream = NULL;
  g_autoptr(GDataInputStream) data_stream = NULL;
  g_autoptr(GPtrArray) actions = NULL;
  gchar *line_iter = NULL;
  g_autoptr(GError) local_error = NULL;

  input_stream = g_file_read (file, cancellable, error);

  if (!input_stream)
    return NULL;

  data_stream = g_data_input_stream_new (G_INPUT_STREAM (input_stream));
  actions = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_remote_ref_action_unref);

  while ((line_iter = g_data_input_stream_read_line (data_stream, NULL, cancellable, &local_error)) != NULL)
    {
      g_autofree gchar *line = g_steal_pointer (&line_iter);
      FlatpakRemoteRefAction *action = flatpak_remote_ref_action_parse (line, error);

      if (!action)
        return NULL;

      g_ptr_array_add (actions, action);
    }

  /* We have to check the error explicitly here */
  if (local_error)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return NULL;
    }

  /* Now that we have the remote ref actions, sort them by their ordering */
  g_ptr_array_sort (actions, sort_flatpak_remote_ref_actions);

  return g_steal_pointer (&actions);
}

/* Returns an associative map from action-ref list to a pointer array of
 * actions. The action-ref lists are considered to be append-only */
GHashTable *
eos_updater_util_flatpak_ref_actions_from_directory (GFile         *directory,
                                                     GCancellable  *cancellable,
                                                     GError       **error)
{
  g_autoptr(GFileEnumerator) autoinstall_d_enumerator = NULL;
  g_autoptr(GHashTable) ref_actions_for_files = g_hash_table_new_full (g_str_hash,
                                                                       g_str_equal,
                                                                       g_free,
                                                                       (GDestroyNotify) g_ptr_array_unref);
  g_autoptr(GPtrArray) autoinstall_flatpaks = g_ptr_array_new_with_free_func (g_object_unref);

  /* Repository checked out, read all files in order and build up a list
   * of flatpaks to auto-install */
  autoinstall_d_enumerator = g_file_enumerate_children (directory,
                                                        G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                        G_FILE_QUERY_INFO_NONE,
                                                        cancellable,
                                                        error);

  if (!autoinstall_d_enumerator)
    return NULL;

  while (TRUE)
    {
      GFile *file;
      GFileInfo *info;
      GPtrArray *action_refs = NULL;

      if (!g_file_enumerator_iterate (autoinstall_d_enumerator,
                                      &info, &file,
                                      cancellable, error))
        return NULL;

      if (!file || !info)
        break;

      action_refs = read_flatpak_ref_actions_from_file (file,
                                                        cancellable,
                                                        error);

      if (!action_refs)
        return NULL;

      g_hash_table_insert (ref_actions_for_files,
                           g_strdup (g_file_info_get_name (info)),
                           g_steal_pointer (&action_refs));
    }

  return g_steal_pointer (&ref_actions_for_files);
}

static guint
flatpak_ref_hash (gconstpointer data)
{
  return g_str_hash (flatpak_ref_get_name (FLATPAK_REF (data)));
}

static gboolean
flatpak_ref_equal (gconstpointer a, gconstpointer b)
{
  return g_str_equal (flatpak_ref_get_name (FLATPAK_REF (a)),
                      flatpak_ref_get_name (FLATPAK_REF (b)));
}

/* Squash actions on the same ref into the last action on that ref, returning
 * a pointer array of remote ref actions, ordered by the order key in each
 * remote ref action. */
static GPtrArray *
squash_ref_actions_ptr_array (GPtrArray *ref_actions)
{
  g_autoptr(GHashTable) hash_table = g_hash_table_new_full (flatpak_ref_hash,
                                                            flatpak_ref_equal,
                                                            g_object_unref,
                                                            (GDestroyNotify) flatpak_remote_ref_action_unref);
  GPtrArray *squashed_ref_actions = NULL;
  gsize i;
  gpointer key;
  gpointer value;
  GHashTableIter hash_iter;

  for (i = 0; i < ref_actions->len; ++i)
    {
      FlatpakRemoteRefAction *action = g_ptr_array_index (ref_actions, i);
      g_hash_table_replace (hash_table,
                            g_object_ref (action->ref),
                            flatpak_remote_ref_action_ref (action));
    }

  squashed_ref_actions = g_ptr_array_new_full (g_hash_table_size (hash_table),
                                               (GDestroyNotify) flatpak_remote_ref_action_unref);
  g_hash_table_iter_init (&hash_iter, hash_table);

  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    g_ptr_array_add (squashed_ref_actions,
                     flatpak_remote_ref_action_ref (value));

  g_ptr_array_sort (squashed_ref_actions, sort_flatpak_remote_ref_actions);

  return squashed_ref_actions;
}

/* Examine each of the remote ref action lists in ref_action_table
 * and squash them down into a list where only one action is applied for
 * each flatpak ref (the latest one) */
GHashTable *
eos_updater_util_squash_remote_ref_actions (GHashTable *ref_actions_table)
{
  gpointer key;
  gpointer value;
  GHashTableIter hash_iter;
  GHashTable *squashed_ref_actions_table = g_hash_table_new_full (g_str_hash,
                                                                  g_str_equal,
                                                                  g_free,
                                                                  (GDestroyNotify) g_ptr_array_unref);

  g_hash_table_iter_init (&hash_iter, ref_actions_table);

  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    {
      GPtrArray *unsquashed_remote_ref_actions = value;
      GPtrArray *squashed_remote_ref_actions = squash_ref_actions_ptr_array (unsquashed_remote_ref_actions);

      g_hash_table_insert (squashed_ref_actions_table,
                           g_strdup (key),
                           squashed_remote_ref_actions);
    }

  return squashed_ref_actions_table;
}

typedef GPtrArray * (*FilterFlatpakRefActionsFunc)(const gchar  *table_name,
                                                   GPtrArray    *actions, 
                                                   gpointer      data);

/* Given a hashtable of action-ref filenames and a pointer array of
 * ref-actions, use the provided filter_func return a hash-table of
 * ref-actions to keep around for later processing. For instance, the caller
 * may want to filter out all ref actions except uninstalls */
static GHashTable *
filter_flatpak_ref_actions_table (GHashTable                  *ref_actions_table,
                                  FilterFlatpakRefActionsFunc  filter_func,
                                  gpointer                     filter_func_data)
{
  g_autoptr(GHashTable) filtered_flatpak_ref_actions_table = g_hash_table_new_full (g_str_hash,
                                                                                    g_str_equal,
                                                                                    g_free,
                                                                                    (GDestroyNotify) g_ptr_array_unref);
  GHashTableIter ref_actions_iter;
  gpointer key;
  gpointer value;

  g_hash_table_iter_init (&ref_actions_iter, ref_actions_table);

  while (g_hash_table_iter_next (&ref_actions_iter, &key, &value))
    g_hash_table_insert (filtered_flatpak_ref_actions_table,
                         g_strdup (key),
                         (*filter_func) (key, value, filter_func_data));

  return eos_updater_util_squash_remote_ref_actions (filtered_flatpak_ref_actions_table);
}

static GPtrArray *
keep_only_new_actions (const gchar  *table_name,
                       GPtrArray    *incoming_actions,
                       gpointer      data)
{
  GHashTable *already_applied_actions_table = data;
  guint64 already_applied_actions_progress;
  gpointer already_applied_actions_progress_value;
  GPtrArray *filtered_actions = NULL;
  guint i;

  /* We haven't applied any actions for this name yet, so return a copy of
   * the incoming actions in every case */
  if (!g_hash_table_lookup_extended (already_applied_actions_table,
                                     table_name,
                                     NULL,
                                     &already_applied_actions_progress_value))
    return g_ptr_array_ref (incoming_actions);

  already_applied_actions_progress = GPOINTER_TO_UINT (already_applied_actions_progress_value);

  for (i = 0; i < incoming_actions->len; ++i)
    {
      FlatpakRemoteRefAction *action = g_ptr_array_index (incoming_actions, i);

      /* We saw a new action. Change to the adding state and add all actions
       * to the filtered actions list */
      if (!filtered_actions && action->order > already_applied_actions_progress)
        filtered_actions = g_ptr_array_new_full (incoming_actions->len - i,
                                                 (GDestroyNotify) flatpak_remote_ref_action_unref);

      if (filtered_actions)
          g_ptr_array_add (filtered_actions,
                           flatpak_remote_ref_action_ref (action));
    }

  return filtered_actions ?
         filtered_actions : g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_remote_ref_action_unref);
}

static GPtrArray *
keep_only_existing_actions (const gchar  *table_name,
                            GPtrArray    *incoming_actions,
                            gpointer      data)
{
  GHashTable *already_applied_actions_table = data;
  guint64 already_applied_actions_progress;
  gpointer already_applied_actions_progress_value;
  GPtrArray *filtered_actions = NULL;
  guint i;

  /* We haven't applied any actions for this name yet, so return an empty array */
  if (!g_hash_table_lookup_extended (already_applied_actions_table,
                                     table_name,
                                     NULL,
                                     &already_applied_actions_progress_value))
    return g_ptr_array_new ();

  already_applied_actions_progress = GPOINTER_TO_UINT (already_applied_actions_progress_value);
  filtered_actions = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_remote_ref_action_unref);

  for (i = 0; i < incoming_actions->len; ++i)
    {
      FlatpakRemoteRefAction *action = g_ptr_array_index (incoming_actions, i);

      /* If we see an action newer than the progress, abort the loop early */
      if (action->order > already_applied_actions_progress)
        break;

      g_ptr_array_add (filtered_actions,
                       flatpak_remote_ref_action_ref (action));
    }

  return filtered_actions;
}

GHashTable *
eos_updater_util_filter_for_new_flatpak_ref_actions (GHashTable *ref_actions,
                                                     GHashTable *progresses)
{
  return filter_flatpak_ref_actions_table (ref_actions,
                                           keep_only_new_actions,
                                           progresses);
}

GHashTable *
eos_updater_util_filter_for_existing_flatpak_ref_actions (GHashTable *ref_actions,
                                                          GHashTable *progresses)
{
  return filter_flatpak_ref_actions_table (ref_actions,
                                           keep_only_existing_actions,
                                           progresses);
}

const gchar *
eos_updater_util_pending_flatpak_deployments_state_path (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_FLATPAK_UPGRADE_STATE_DIR",
                                    LOCALSTATEDIR "/lib/eos-application-tools/flatpak-autoinstall.d");
}

static gboolean
flatpak_refs_progress_from_file (GFile         *file,
                                 guint64       *out_ref_actions_progress,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  g_autofree gchar *contents = NULL;
  guint64 progress;

  g_return_val_if_fail (out_ref_actions_progress != NULL, FALSE);

  if (!g_file_load_contents (file, cancellable, &contents, NULL, NULL, error))
    return FALSE;

  if (!parse_monotonic_counter (contents, &progress, error))
    return FALSE;

  *out_ref_actions_progress = progress;
  return TRUE;
}

GHashTable *
eos_updater_util_flatpak_ref_action_application_progress_in_state_path (GCancellable  *cancellable,
                                                                        GError       **error)
{
  g_autoptr(GFile) state_directory = g_file_new_for_path (eos_updater_util_pending_flatpak_deployments_state_path ());

  g_autoptr(GFileEnumerator) autoinstall_d_enumerator = NULL;
  g_autoptr(GHashTable) ref_action_progress_for_files = g_hash_table_new_full (g_str_hash,
                                                                               g_str_equal,
                                                                               g_free,
                                                                               NULL);
  g_autoptr(GError) local_error = NULL;

  /* Repository checked out, read all files in order and build up a list
   * of flatpaks to auto-install */
  autoinstall_d_enumerator = g_file_enumerate_children (state_directory,
                                                        G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                        G_FILE_QUERY_INFO_NONE,
                                                        cancellable,
                                                        &local_error);

  /* If the directory doesn't exist, use an empty hash table to signify that
   * there isn't anything to check against */
  if (!autoinstall_d_enumerator)
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      return g_steal_pointer (&ref_action_progress_for_files);
    }

  while (TRUE)
    {
      GFile *file;
      GFileInfo *info;
      guint64 progress;

      if (!g_file_enumerator_iterate (autoinstall_d_enumerator,
                                      &info, &file,
                                      cancellable, error))
        return NULL;

      if (!file || !info)
        break;

      if (!flatpak_refs_progress_from_file (file,
                                            &progress,
                                            cancellable,
                                            error))
        return NULL;

      g_hash_table_insert (ref_action_progress_for_files,
                           g_strdup (g_file_info_get_name (info)),
                           GUINT_TO_POINTER (progress));
    }

  return g_steal_pointer (&ref_action_progress_for_files);
}

GPtrArray *
eos_updater_util_flatten_flatpak_ref_actions_table (GHashTable *flatpak_ref_actions)
{
  GPtrArray *flatpaks = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_remote_ref_action_unref);
  GHashTableIter hash_iter;
  gpointer key;
  gpointer value;

  g_hash_table_iter_init (&hash_iter, flatpak_ref_actions);

  /* Collect all the flatpaks from "install" events in the hash table */
  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    {
      GPtrArray *flatpak_ref_actions_for_this_file = value;
      gsize i;

      for (i = 0; i < flatpak_ref_actions_for_this_file->len; ++i)
        {
          FlatpakRemoteRefAction *action = g_ptr_array_index (flatpak_ref_actions_for_this_file, i);

          g_ptr_array_add (flatpaks, flatpak_remote_ref_action_ref (action));
        }
    }

  return flatpaks;
}
