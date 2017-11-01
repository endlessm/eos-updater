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
#include <json-glib/json-glib.h>
#include <ostree.h>

#include "flatpak.h"
#include "util.h"

static FlatpakRemoteRefAction *
flatpak_remote_ref_action_new (EosUpdaterUtilFlatpakRemoteRefActionType  type,
                               FlatpakRemoteRef                         *ref,
                               gint32                                    serial)
{
  FlatpakRemoteRefAction *action = g_slice_new0 (FlatpakRemoteRefAction);

  action->ref_cnt = 1;
  action->type = type;
  action->ref = g_object_ref (ref);
  action->serial = serial;

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

static gchar *
json_node_to_string (JsonNode *node)
{
  g_autoptr(JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, node);

  return json_generator_to_data (gen, NULL);
}

static gboolean
parse_ref_kind (const gchar     *ref_kind_str,
                FlatpakRefKind  *out_ref_kind,
                GError         **error)
{
  FlatpakRefKind kind;

  g_assert (out_ref_kind != NULL);

  if (g_strcmp0 (ref_kind_str, "app") == 0)
    {
      kind = FLATPAK_REF_KIND_APP;
    }
  else if (g_strcmp0 (ref_kind_str, "runtime") == 0)
    {
      kind = FLATPAK_REF_KIND_RUNTIME;
    }
  else
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Invalid kind: %s", ref_kind_str);
      return FALSE;
    }

  *out_ref_kind = kind;
  return TRUE;
}

static gboolean
parse_flatpak_ref_from_detail (JsonObject      *detail,
                               const gchar    **out_app_name,
                               FlatpakRefKind  *out_ref_kind,
                               GError         **error)
{
  const gchar *app_name = NULL;
  const gchar *ref_kind_str = NULL;
  FlatpakRefKind kind;

  g_return_val_if_fail (out_ref_kind != NULL, FALSE);
  g_return_val_if_fail (out_app_name != NULL, FALSE);

  app_name = json_object_get_string_member (detail, "app");

  if (app_name == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected an 'app' member in the 'detail' member");

      return FALSE;
    }

  ref_kind_str = json_object_get_string_member (detail, "ref-kind");

  if (ref_kind_str == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected a 'ref-kind' member in the 'detail' member");
      return FALSE;
    }

  if (!parse_ref_kind (ref_kind_str, &kind, error))
    return FALSE;

  *out_app_name = app_name;
  *out_ref_kind = kind;

  return TRUE;
}

static FlatpakRemoteRef *
flatpak_remote_ref_from_install_action_detail (JsonObject *detail,
                                               GError    **error)
{
  const gchar *app_name = NULL;
  const gchar *collection_id = NULL;
  FlatpakRefKind kind;

  if (!parse_flatpak_ref_from_detail (detail, &app_name, &kind, error))
    return NULL;

  collection_id = json_object_get_string_member (detail, "collection-id");

  if (remote == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected a 'remote' member in the 'detail' member");
      return NULL;
    }

  /* TODO: Right now we "stuff" the collection-id in the remote-name part
   * of the FlatpakRemoteRef and look up the corresponding remote later on
   * when actually pulling the flatpaks */
  return g_object_new (FLATPAK_TYPE_REMOTE_REF,
                       "remote-name", remote,
                       "name", app_name,
                       "kind", kind,
                       NULL);
}

static FlatpakRemoteRef *
flatpak_remote_ref_from_uninstall_action_detail (JsonObject  *detail,
                                                 GError     **error)
{
  const gchar *app_name = NULL;
  FlatpakRefKind kind;

  if (!parse_flatpak_ref_from_detail (detail, &app_name, &kind, error))
    return NULL;

  return g_object_new (FLATPAK_TYPE_REMOTE_REF,
                       "remote-name", "none",
                       "name", app_name,
                       "kind", kind,
                       NULL);
}

static FlatpakRemoteRef *
flatpak_remote_ref_from_action_detail (EosUpdaterUtilFlatpakRemoteRefActionType   action_type,
                                       JsonObject                                *detail,
                                       GError                                   **error)
{
  switch (action_type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return flatpak_remote_ref_from_install_action_detail (detail, error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
        return flatpak_remote_ref_from_uninstall_action_detail (detail, error);
      default:
        g_assert_not_reached ();
    }
}

static FlatpakRemoteRefAction *
flatpak_remote_ref_action_from_json_node (JsonNode *node,
                                          GError **error)
{
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  const gchar *action_type_str = NULL;
  JsonObject *object = json_node_get_object (node);
  JsonObject *detail_object = NULL;
  g_autoptr(FlatpakRef) flatpak_ref = NULL;
  g_autoptr(FlatpakRemoteRef) flatpak_remote_ref = NULL;
  g_autoptr(GError) local_error = NULL;
  JsonNode *serial_node = NULL;
  gint64 serial64;
  gint32 serial;
  EosUpdaterUtilFlatpakRemoteRefActionType action_type;

  if (object == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected entry node to be an object, but was %s", node_str);

      return NULL;
    }

  action_type_str = json_object_get_string_member (object, "action");

  if (action_type_str == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected an 'action' member in the autoinstall spec (at %s)", node_str);

      return NULL;
    }

  if (!flatpak_remote_ref_action_type_parse (action_type_str, &action_type, error))
    return NULL;

  serial_node = json_object_get_member (object, "serial");
  if (serial_node == NULL ||
      !JSON_NODE_HOLDS_VALUE (serial_node) ||
      json_node_get_value_type (serial_node) != G_TYPE_INT64)
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected a 'serial' member in the autoinstall spec (at %s)", node_str);
      return NULL;
    }

  serial64 = json_node_get_int (serial_node);

  if (serial64 > G_MAXINT32 || serial64 < G_MININT32)
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "The 'serial' member in in the autoinstall spec must fit within a 32 bit integer (at %s)", node_str);
      return NULL;
    }

  serial = (gint32) serial64;

  detail_object = json_object_get_object_member (object, "detail");

  if (detail_object == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected a 'detail' member in the autoinstall spec (at %s)", node_str);
      return NULL;
    }

  flatpak_remote_ref = flatpak_remote_ref_from_action_detail (action_type, detail_object, &local_error);

  if (!flatpak_remote_ref)
    {
      if (g_error_matches (local_error,
                           EOS_UPDATER_ERROR,
                           EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC))
        {
          g_autofree gchar *node_str = json_node_to_string (node);
          g_propagate_prefixed_error (error,
                                      local_error,
                                      "Error parsing the 'detail' member for action (at %s) '%s': ",
                                      action_type_str,
                                      node_str);
          return NULL;
        }

      g_propagate_error (error, g_steal_pointer (&local_error));
      return NULL;
    }
  
  return flatpak_remote_ref_action_new (action_type, flatpak_remote_ref, serial);
}

static gint
sort_flatpak_remote_ref_actions (gconstpointer a, gconstpointer b)
{
  FlatpakRemoteRefAction **action_a = (FlatpakRemoteRefAction **) a;
  FlatpakRemoteRefAction **action_b = (FlatpakRemoteRefAction **) b;

  return (gint) (*action_a)->serial - (gint) (*action_b)->serial;
}

static JsonNode *
parse_json_from_file (GFile         *file,
                      GCancellable  *cancellable,
                      GError       **error)
{
  g_autoptr(GFileInputStream) input_stream = NULL;
  g_autoptr(JsonParser) parser = json_parser_new_immutable ();

  input_stream = g_file_read (file, cancellable, error);

  if (!input_stream)
    return NULL;

  if (!json_parser_load_from_stream (parser,
                                     G_INPUT_STREAM (input_stream),
                                     cancellable,
                                     error))
    return NULL;

  return json_node_ref (json_parser_get_root (parser));
}

static GPtrArray *
read_flatpak_ref_actions_from_file (GFile         *file,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
  /* Now that we have the file contents, time to read in the list of
   * flatpaks to install into a pointer array. Parse out the OSTree ref
   * and then parse the FlatpakRemoteRefAction */
  g_autoptr(JsonNode) node = parse_json_from_file (file, cancellable, error);
  g_autoptr(GPtrArray) actions = NULL;
  JsonArray *array = NULL;
  g_autoptr(GList) elements = NULL;
  GList *iter = NULL;

  if (node == NULL)
    return NULL;

  /* Parse each entry of the underlying array */
  array = json_node_get_array (node);

  if (array == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_autofree gchar *filename = g_file_get_path (file);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected node to be an array when parsing %s at %s", node_str, filename);

      return NULL;
    }

  elements = json_array_get_elements (array);
  actions = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_remote_ref_action_unref);

  for (iter = elements; iter != NULL; iter = iter->next)
    {
      /* We use local_error here so that we can catch and
       * add detail on the filename if necessary */
      g_autoptr(GError) local_error = NULL;
      FlatpakRemoteRefAction *action = NULL;
      gboolean is_filtered = FALSE;

      if (!action_node_should_be_filtered_out (iter->data, &is_filtered, &local_error))
        {
          if (g_error_matches (local_error,
                               EOS_UPDATER_ERROR,
                               EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC))
            {
              g_autofree gchar *filename = g_file_get_path (file);
              g_propagate_prefixed_error (error, local_error, "Error parsing %s: ", filename);
              return NULL;
            }

          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      if (is_filtered)
        continue;

      action = flatpak_remote_ref_action_from_json_node (iter->data, &local_error);

      if (action == NULL)
        {
          if (g_error_matches (local_error,
                               EOS_UPDATER_ERROR,
                               EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC))
            {
              g_autofree gchar *filename = g_file_get_path (file);
              g_propagate_prefixed_error (error, local_error, "Error parsing %s: ", filename);
              return NULL;
            }

          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      g_ptr_array_add (actions, action);
    }

  /* Now that we have the remote ref actions, sort them by their ordering */
  g_ptr_array_sort (actions, sort_flatpak_remote_ref_actions);

  return g_steal_pointer (&actions);
}

/* Returns an associative map from action-ref list to a pointer array of
 * actions. The action-ref lists are considered to be append-only */
GHashTable *
eos_updater_util_flatpak_ref_actions_from_directory (const gchar   *relative_parent_path,
                                                     GFile         *directory,
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
                           g_build_filename (relative_parent_path,
                                             g_file_info_get_name (info),
                                             NULL),
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
  gint32 already_applied_actions_progress;
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

  already_applied_actions_progress = GPOINTER_TO_INT (already_applied_actions_progress_value);

  for (i = 0; i < incoming_actions->len; ++i)
    {
      FlatpakRemoteRefAction *action = g_ptr_array_index (incoming_actions, i);

      /* We saw a new action. Change to the adding state and add all actions
       * to the filtered actions list */
      if (!filtered_actions && action->serial > already_applied_actions_progress)
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
  gint32 already_applied_actions_progress;
  gpointer already_applied_actions_progress_value;
  GPtrArray *filtered_actions = NULL;
  guint i;

  /* We haven't applied any actions for this name yet, so return an empty array */
  if (!g_hash_table_lookup_extended (already_applied_actions_table,
                                     table_name,
                                     NULL,
                                     &already_applied_actions_progress_value))
    return g_ptr_array_new ();

  already_applied_actions_progress = GPOINTER_TO_INT (already_applied_actions_progress_value);
  filtered_actions = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_remote_ref_action_unref);

  for (i = 0; i < incoming_actions->len; ++i)
    {
      FlatpakRemoteRefAction *action = g_ptr_array_index (incoming_actions, i);

      /* If we see an action newer than the progress, abort the loop early */
      if (action->serial > already_applied_actions_progress)
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
                                    LOCALSTATEDIR "/lib/eos-application-tools/flatpak-autoinstall.progress");
}

GHashTable *
eos_updater_util_flatpak_ref_action_application_progress_in_state_path (GCancellable  *cancellable,
                                                                        GError       **error)
{
  const gchar *state_file_path = eos_updater_util_pending_flatpak_deployments_state_path ();
  g_autoptr(GKeyFile) state_key_file = g_key_file_new ();
  g_auto(GStrv) groups = NULL;
  g_autoptr(GHashTable) ref_action_progress_for_files = g_hash_table_new_full (g_str_hash,
                                                                               g_str_equal,
                                                                               g_free,
                                                                               NULL);

  GStrv groups_iter = NULL;
  g_autoptr(GError) local_error = NULL;

  /* Read the key file for sections about the application progress
   * of each autoinstall file */
  if (!g_key_file_load_from_file (state_key_file,
                                  state_file_path,
                                  G_KEY_FILE_NONE,
                                  &local_error))
    {
      if (!g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      return g_steal_pointer (&ref_action_progress_for_files);
    }

  /* Enumerate each section. The section name is the path to the file */
  groups_iter = groups = g_key_file_get_groups (state_key_file, NULL);

  while (*groups_iter != NULL)
    {
      const gchar *source_path = *groups_iter;
      g_autofree gchar *progress_string = NULL;
      guint64 progress;

      /* We need to use g_key_file_get_value here to guard against errors */
      progress_string = g_key_file_get_value (state_key_file,
                                              *groups_iter,
                                              "Progress",
                                              error);

      if (progress_string == NULL)
        return NULL;

      if (!parse_monotonic_counter (progress_string, &progress, error))
        return NULL;

      g_hash_table_insert (ref_action_progress_for_files,
                           g_strdup (source_path),
                           GUINT_TO_POINTER (progress));

      ++groups_iter;
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
