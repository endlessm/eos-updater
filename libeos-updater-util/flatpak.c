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

  action->ref_count = 1;
  action->type = type;
  action->ref = g_object_ref (ref);
  action->serial = serial;

  return action;
}

FlatpakRemoteRefAction *
flatpak_remote_ref_action_ref (FlatpakRemoteRefAction *action)
{
  g_return_val_if_fail (action->ref_count > 0, NULL);
  g_return_val_if_fail (action->ref_count < G_MAXUINT, NULL);

  ++action->ref_count;
  return action;
}

void
flatpak_remote_ref_action_unref (FlatpakRemoteRefAction *action)
{
  g_return_if_fail (action->ref_count > 0);

  if (--action->ref_count != 0)
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
                   EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC,
                   "Unknown action type %s specified in autoinstall spec",
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
parse_flatpak_ref_from_entry (JsonObject      *entry,
                              const gchar    **out_app_name,
                              FlatpakRefKind  *out_ref_kind,
                              GError         **error)
{
  const gchar *app_name = NULL;
  const gchar *ref_kind_str = NULL;
  FlatpakRefKind kind;

  g_return_val_if_fail (out_ref_kind != NULL, FALSE);
  g_return_val_if_fail (out_app_name != NULL, FALSE);

  app_name = json_object_get_string_member (entry, "app");

  if (app_name == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected an 'app' member");

      return FALSE;
    }

  ref_kind_str = json_object_get_string_member (entry, "ref-kind");

  if (ref_kind_str == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected a 'ref-kind' member");
      return FALSE;
    }

  if (!parse_ref_kind (ref_kind_str, &kind, error))
    return FALSE;

  *out_app_name = app_name;
  *out_ref_kind = kind;

  return TRUE;
}

static FlatpakRemoteRef *
flatpak_remote_ref_from_install_action_entry (JsonObject *entry,
                                              GError    **error)
{
  const gchar *app_name = NULL;
  const gchar *collection_id = NULL;
  FlatpakRefKind kind;

  if (!parse_flatpak_ref_from_entry (entry, &app_name, &kind, error))
    return NULL;

  collection_id = json_object_get_string_member (entry, "collection-id");

  if (collection_id == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected a 'collection-id' member");
      return NULL;
    }

  /* TODO: Right now we "stuff" the collection-id in the remote-name part
   * of the FlatpakRemoteRef and look up the corresponding remote later on
   * when actually pulling the flatpaks */
  return g_object_new (FLATPAK_TYPE_REMOTE_REF,
                       "remote-name", collection_id,
                       "name", app_name,
                       "kind", kind,
                       NULL);
}

static FlatpakRemoteRef *
flatpak_remote_ref_from_uninstall_action_entry (JsonObject  *entry,
                                                GError     **error)
{
  const gchar *app_name = NULL;
  FlatpakRefKind kind;

  if (!parse_flatpak_ref_from_entry (entry, &app_name, &kind, error))
    return NULL;

  return g_object_new (FLATPAK_TYPE_REMOTE_REF,
                       "remote-name", "none",
                       "name", app_name,
                       "kind", kind,
                       NULL);
}

static FlatpakRemoteRef *
flatpak_remote_ref_from_action_entry (EosUpdaterUtilFlatpakRemoteRefActionType   action_type,
                                      JsonObject                                *entry,
                                      GError                                   **error)
{
  switch (action_type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return flatpak_remote_ref_from_install_action_entry (entry, error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
        return flatpak_remote_ref_from_uninstall_action_entry (entry, error);
      default:
        g_assert_not_reached ();
    }
}

static FlatpakRemoteRefAction *
flatpak_remote_ref_action_from_json_node (JsonNode *node,
                                          GError **error)
{
  const gchar *action_type_str = NULL;
  JsonObject *object = json_node_get_object (node);
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

  flatpak_remote_ref = flatpak_remote_ref_from_action_entry (action_type, object, &local_error);

  if (!flatpak_remote_ref)
    {
      if (g_error_matches (local_error,
                           EOS_UPDATER_ERROR,
                           EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC))
        {
          g_autofree gchar *node_str = json_node_to_string (node);
          g_propagate_prefixed_error (error,
                                      local_error,
                                      "Error parsing action detail (at %s) '%s': ",
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

static const gchar *
eos_updater_get_system_architecture_string (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE",
                                    flatpak_get_default_arch ());
}

static gboolean
architecture_skip_filter_applies (JsonNode  *filter_value,
                                  gboolean  *is_filtered,
                                  GError   **error)
{
  JsonArray *skip_architectures_array = json_node_get_array (filter_value);
  g_autoptr(GList) architecture_nodes = NULL;
  GList *iter = NULL;

  g_return_val_if_fail (is_filtered != NULL, FALSE);

  if (skip_architectures_array == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (filter_value);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected '~architectures' filter to be an array, was: %s",
                   node_str);
      return FALSE;
    }

  architecture_nodes = json_array_get_elements (skip_architectures_array);

  for (iter = architecture_nodes; iter != NULL; iter = iter->next)
    {
      const gchar *string = json_node_get_string (iter->data);

      if (string == NULL)
        {
          g_autofree gchar *node_str = json_node_to_string (filter_value);
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                       "Expected '~architectures' array to contain string values, was: %s",
                       node_str);
          return FALSE;
        }

      if (g_strcmp0 (string, eos_updater_get_system_architecture_string ()) == 0)
        {
          *is_filtered = TRUE;
          return TRUE;
        }
    }

  *is_filtered = FALSE;
  return TRUE;
}

static gboolean
architecture_only_filter_applies (JsonNode  *filter_value,
                                  gboolean  *is_filtered,
                                  GError   **error)
{
  JsonArray *skip_architectures_array = json_node_get_array (filter_value);
  g_autoptr(GList) architecture_nodes = NULL;
  GList *iter = NULL;

  g_return_val_if_fail (is_filtered != NULL, FALSE);

  if (skip_architectures_array == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (filter_value);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected 'architectures' filter to be an array, was: %s",
                   node_str);
      return FALSE;
    }

  architecture_nodes = json_array_get_elements (skip_architectures_array);

  for (iter = architecture_nodes; iter != NULL; iter = iter->next)
    {
      const gchar *string = json_node_get_string (iter->data);

      if (string == NULL)
        {
          g_autofree gchar *node_str = json_node_to_string (filter_value);
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                       "Expected 'architectures' array to contain string values, was: %s",
                       node_str);
          return FALSE;
        }

      if (g_strcmp0 (string, eos_updater_get_system_architecture_string ()) == 0)
        {
          *is_filtered = FALSE;
          return TRUE;
        }
    }

  *is_filtered = TRUE;
  return TRUE;
}

static GStrv
eos_updater_override_locales_list (void)
{
  const gchar *override_locales = eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES", NULL);

  if (override_locales)
    return g_strsplit(override_locales, ";", -1);

  return NULL;
}

static gboolean
get_locales_list_from_flatpak_installation (GStrv  *out_strv,
                                            GError **error)
{
  g_autoptr(FlatpakInstallation) installation = eos_updater_get_flatpak_installation (NULL, error);
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *value = NULL;

  g_assert (out_strv != NULL);

  if (!installation)
    return FALSE;

  /* TODO: Right now this returns a only the testing override or NULL,
   * but we might want to do something a little more clever based on what is
   * supported by Flatpak in future */
  *out_strv = eos_updater_override_locales_list ();

  return TRUE;
}

static gboolean
locale_skip_filter_applies (JsonNode  *filter_value,
                            gboolean  *is_filtered,
                            GError   **error)
{
  JsonArray *skip_locales_array = json_node_get_array (filter_value);
  g_autoptr(GList) locale_nodes = NULL;
  GList *iter = NULL;
  g_auto(GStrv) supported_languages = NULL;

  g_return_val_if_fail (is_filtered != NULL, FALSE);

  if (!get_locales_list_from_flatpak_installation (&supported_languages,
                                                   error))
    return FALSE;

  if (skip_locales_array == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (filter_value);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected '~locales' filter to be an array, was: %s",
                   node_str);
      return FALSE;
    }

  locale_nodes = json_array_get_elements (skip_locales_array);

  for (iter = locale_nodes; iter != NULL; iter = iter->next)
    {
      const gchar *string = json_node_get_string (iter->data);

      if (string == NULL)
        {
          g_autofree gchar *node_str = json_node_to_string (filter_value);
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                       "Expected '~locales' array to contain string values, was: %s",
                       node_str);
          return FALSE;
        }

      if (g_strv_contains ((const gchar * const *) supported_languages, string))
        {
          *is_filtered = TRUE;
          return TRUE;
        }
    }

  *is_filtered = FALSE;
  return TRUE;
}

static gboolean
locale_only_filter_applies (JsonNode  *filter_value,
                                  gboolean  *is_filtered,
                                  GError   **error)
{
  JsonArray *skip_locales_array = json_node_get_array (filter_value);
  g_autoptr(GList) locale_nodes = NULL;
  GList *iter = NULL;
  g_auto(GStrv) supported_languages = NULL;

  g_return_val_if_fail (is_filtered != NULL, FALSE);

  if (!get_locales_list_from_flatpak_installation (&supported_languages,
                                                   error))
    return FALSE;

  if (skip_locales_array == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (filter_value);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected 'locales' filter to be an array, was: %s",
                   node_str);
      return FALSE;
    }

  locale_nodes = json_array_get_elements (skip_locales_array);

  for (iter = locale_nodes; iter != NULL; iter = iter->next)
    {
      const gchar *string = json_node_get_string (iter->data);

      if (string == NULL)
        {
          g_autofree gchar *node_str = json_node_to_string (filter_value);
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                       "Expected 'locales' array to contain string values, was: %s",
                       node_str);
          return FALSE;
        }

      if (g_strv_contains ((const gchar * const *) supported_languages, string))
        {
          *is_filtered = FALSE;
          return TRUE;
        }
    }

  *is_filtered = TRUE;
  return TRUE;
}

static gboolean
action_filter_applies (JsonObject   *object,
                       const gchar  *filter_key_name,
                       gboolean     *is_filtered,
                       GError      **error)
{
  JsonNode *filter_value = json_object_get_member (object, filter_key_name);

  g_return_val_if_fail (is_filtered != NULL, FALSE);

  if (g_strcmp0 (filter_key_name, "~architectures") == 0 &&
      architecture_skip_filter_applies (filter_value, is_filtered, error))
    return TRUE;
  if (g_strcmp0 (filter_key_name, "architectures") == 0 &&
           architecture_only_filter_applies (filter_value, is_filtered, error))
    return TRUE;
  if (g_strcmp0 (filter_key_name, "~locales") == 0 &&
           locale_skip_filter_applies (filter_value, is_filtered, error))
    return TRUE;
  if (g_strcmp0 (filter_key_name, "locales") == 0 &&
           locale_only_filter_applies (filter_value, is_filtered, error))
    return TRUE;

  g_set_error (error,
               EOS_UPDATER_ERROR,
               EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC,
               "Unknown action filter value '%s', "
               "expected one of '~architectures', 'architectures', "
               "'~locales' and 'locales'",
               filter_key_name);
  return FALSE;
}

/* We do this at the same time as reading the JSON node so that we don't have
 * to keep filter information around in memory */
static gboolean
action_node_should_be_filtered_out (JsonNode  *node,
                                    gboolean  *is_filtered,
                                    GError   **error)
{
  JsonObject *object = json_node_get_object (node);
  JsonNode *filters_object_node = json_object_get_member (object, "filters");
  JsonObject *filters_object = NULL;
  g_autoptr(GList) filters_object_keys = NULL;
  GList *iter = NULL;

  g_return_val_if_fail (is_filtered != NULL, FALSE);

  /* No filters, so this action cannot be filtered out */
  if (filters_object_node == NULL)
    {
      *is_filtered = FALSE;
      return TRUE;
    }

  filters_object = json_node_get_object (filters_object_node);

  if (filters_object == NULL)
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected 'filters' node to be an object, but was %s", node_str);

      return FALSE;
    }

  filters_object_keys = json_object_get_members (filters_object);

  for (iter = filters_object_keys; iter != NULL; iter = iter->next)
    {
      gboolean action_is_filtered_on_this_filter;
      g_autoptr(GError) local_error = NULL;

      if (!action_filter_applies (filters_object,
                                  iter->data,
                                  &action_is_filtered_on_this_filter,
                                  &local_error))
        {
          if (g_error_matches (local_error,
                               EOS_UPDATER_ERROR,
                               EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC))
            {
              g_warning ("%s. Skipping this filter and not applying action, system may be in an inconsistent state from this point forward", local_error->message);
              *is_filtered = TRUE;
              g_clear_error (&local_error);
              continue;
            }

          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      if (action_is_filtered_on_this_filter)
        {
          *is_filtered = TRUE;
          return TRUE;
        }
    }

  *is_filtered = FALSE;
  return TRUE;
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
          else if (g_error_matches (local_error,
                                    EOS_UPDATER_ERROR,
                                    EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC))
            {
              g_autofree gchar *filename = g_file_get_path (file);
              g_warning ("%s while parsing %s. Skipping this action and it will not be reapplied later, system may be in an inconsistent state from this point forward", local_error->message, filename);
              g_clear_error (&local_error);
              continue;
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

FlatpakRemoteRefActionsFile *
flatpak_remote_ref_actions_file_new (GPtrArray *remote_ref_actions,
                                     gint       priority)
{
  FlatpakRemoteRefActionsFile *file = g_slice_new0 (FlatpakRemoteRefActionsFile);

  file->remote_ref_actions = g_ptr_array_ref (remote_ref_actions);
  file->priority = priority;

  return file;
}

void
flatpak_remote_ref_actions_file_free (FlatpakRemoteRefActionsFile *file)
{
  g_clear_pointer (&file->remote_ref_actions, g_ptr_array_unref);

  g_slice_free (FlatpakRemoteRefActionsFile, file);
}

gboolean
eos_updater_util_flatpak_ref_actions_append_from_directory (const gchar   *relative_parent_path,
                                                            GFile         *directory,
                                                            GHashTable    *ref_actions_for_files,
                                                            gint           priority,
                                                            GCancellable  *cancellable,
                                                            GError       **error)
{
  g_autoptr(GFileEnumerator) autoinstall_d_enumerator = NULL;

  /* Repository checked out, read all files in order and build up a list
   * of flatpaks to auto-install */
  autoinstall_d_enumerator = g_file_enumerate_children (directory,
                                                        G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                        G_FILE_QUERY_INFO_NONE,
                                                        cancellable,
                                                        error);

  if (!autoinstall_d_enumerator)
    return FALSE;

  while (TRUE)
    {
      GFile *file;
      GFileInfo *info;
      g_autoptr(GPtrArray) action_refs = NULL;
      const gchar *filename = NULL;
      g_autoptr(FlatpakRemoteRefActionsFile) actions_file = NULL;
      FlatpakRemoteRefActionsFile *existing_actions_file = NULL;

      if (!g_file_enumerator_iterate (autoinstall_d_enumerator,
                                      &info, &file,
                                      cancellable, error))
        return FALSE;

      if (!file || !info)
        break;

      action_refs = read_flatpak_ref_actions_from_file (file,
                                                        cancellable,
                                                        error);

      if (!action_refs)
        return FALSE;

      filename = g_file_info_get_name (info);

      /* We may already have a remote_ref_actions_file in the hash table
       * and we cannot just blindly replace it. Replace it only if
       * the priority of the incoming directory is less */
      existing_actions_file = g_hash_table_lookup (ref_actions_for_files,
                                                   filename);

      if (existing_actions_file &&
          existing_actions_file->priority < priority)
        continue;

      g_hash_table_replace (ref_actions_for_files,
                            g_strdup (filename),
                            flatpak_remote_ref_actions_file_new (action_refs,
                                                                 priority));
    }

  return TRUE;
}

gboolean
eos_updater_util_flatpak_ref_actions_maybe_append_from_directory (const gchar   *override_directory_path,
                                                                  GHashTable    *ref_actions,
                                                                  gint           priority,
                                                                  GCancellable  *cancellable,
                                                                  GError       **error)
{
  g_autoptr(GFile) override_directory = g_file_new_for_path (override_directory_path);
  g_autoptr(GError) local_error = NULL;
  gboolean appended = eos_updater_util_flatpak_ref_actions_append_from_directory (override_directory_path,
                                                                                  override_directory,
                                                                                  ref_actions,
                                                                                  priority,
                                                                                  cancellable,
                                                                                  &local_error);

  if (!appended)
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
    }

  /* Returning TRUE here, since it is not an error if the override directory
   * does not exist */
  return TRUE;
}

/* Returns an associative map from action-ref list to a pointer array of
 * actions. The action-ref lists are considered to be append-only */
GHashTable *
eos_updater_util_flatpak_ref_actions_from_directory (const gchar   *relative_parent_path,
                                                     GFile         *directory,
                                                     gint           priority,
                                                     GCancellable  *cancellable,
                                                     GError       **error)
{
  g_autoptr(GHashTable) ref_actions_for_files = g_hash_table_new_full (g_str_hash,
                                                                       g_str_equal,
                                                                       g_free,
                                                                       (GDestroyNotify) flatpak_remote_ref_actions_file_free);

  if (!eos_updater_util_flatpak_ref_actions_append_from_directory (relative_parent_path,
                                                                   directory,
                                                                   ref_actions_for_files,
                                                                   priority,
                                                                   cancellable,
                                                                   error))
    return NULL;

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

/* Given a hash table of filenames to FlatpakRemoteRefActionsFile, hoist
 * the underlying pointer array of remote ref actions and make that the value
 * of the new hash table.
 *
 * This will make the hash table suitable for passing to
 * eos_updater_util_squash_remote_ref_actions */
GHashTable *
eos_updater_util_hoist_flatpak_remote_ref_actions (GHashTable *ref_actions_file_table)
{
  g_autoptr(GHashTable) hoisted_ref_actions_table = g_hash_table_new_full (g_str_hash,
                                                                           g_str_equal,
                                                                           g_free,
                                                                           (GDestroyNotify) g_ptr_array_unref);
  gpointer key, value;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, ref_actions_file_table);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      FlatpakRemoteRefActionsFile *ref_actions_file = value;

      g_hash_table_insert (hoisted_ref_actions_table,
                           g_strdup (key),
                           g_ptr_array_ref (ref_actions_file->remote_ref_actions));
    }

  return g_steal_pointer (&hoisted_ref_actions_table);
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

/* Take the most recent (eg, last) entry in each ref actions list in the
 * ref_actions_table and return a hash table mapping each ref action filename
 * to a counter indicating how up to date we are on the system for that
 * file */
GHashTable *
eos_updater_util_remote_ref_actions_to_expected_progresses (GHashTable *ref_actions_table)
{
  g_autoptr(GHashTable) counts = g_hash_table_new_full (g_str_hash,
                                                        g_str_equal,
                                                        g_free,
                                                        NULL);
  GHashTableIter iter;
  gpointer key, value;

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      GPtrArray *ref_actions = value;

      /* This probably shouldn't happen, but at least we shouldn't crash if
       * it does */
      if (ref_actions->len == 0)
        {
          g_warning ("Remote ref actions file %s has no actions, that's odd...",
                     key);
          continue;
        }

      g_hash_table_insert (counts,
                           g_strdup (key),
                           GINT_TO_POINTER (g_ptr_array_index (ref_actions,
                                                               ref_actions->len - 1)));
    }

  return g_steal_pointer (&counts);
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

const gchar *
eos_updater_util_flatpak_autoinstall_override_paths (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_FLATPAK_AUTOINSTALL_OVERRIDE_DIRS",
                                    LOCALSTATEDIR "/lib/eos-application-tools/flatpak-autoinstall.d;"
                                    SYSCONFDIR "/eos-application-tools/flatpak-autoinstall.d");
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

/* Examine remote ref actions coming from multiple sources and flatten
 * them into a single squashed list based on their lexicographical
 * priority */
GPtrArray *
eos_updater_util_flatten_flatpak_ref_actions_table (GHashTable *ref_actions_table)
{
  g_autoptr(GList) remote_ref_actions_keys = g_hash_table_get_keys (ref_actions_table);
  g_autoptr(GPtrArray) concatenated_actions_pointer_array = g_ptr_array_new_with_free_func ((GDestroyNotify) flatpak_remote_ref_action_unref);
  GList *iter = NULL;

  remote_ref_actions_keys = g_list_sort (remote_ref_actions_keys, (GCompareFunc) g_strcmp0);

  for (iter = remote_ref_actions_keys; iter != NULL; iter = iter->next)
    {
      GPtrArray *ref_actions = g_hash_table_lookup (ref_actions_table, iter->data);
      gsize i;

      for (i = 0; i < ref_actions->len; ++i)
        g_ptr_array_add (concatenated_actions_pointer_array,
                         flatpak_remote_ref_action_ref (g_ptr_array_index (ref_actions, i)));
    }

  return squash_ref_actions_ptr_array (concatenated_actions_pointer_array);
}

static const gchar *
format_remote_ref_action_type (EosUpdaterUtilFlatpakRemoteRefActionType action_type)
{
  GEnumClass *enum_class = g_type_class_ref (EOS_TYPE_UPDATER_UTIL_FLATPAK_REMOTE_REF_ACTION_TYPE);
  GEnumValue *enum_value = g_enum_get_value (enum_class, action_type);

  g_type_class_unref (enum_class);

  g_assert (enum_value != NULL);

  return enum_value->value_nick;
}

gchar *
eos_updater_util_format_all_flatpak_ref_actions (const gchar *title,
                                                 GHashTable  *flatpak_ref_actions_for_this_boot)
{
  gpointer key, value;
  GHashTableIter iter;
  GString *string = g_string_new ("");

  g_string_append_printf (string, "%s:\n", title);

  g_hash_table_iter_init (&iter, flatpak_ref_actions_for_this_boot);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *source = (const gchar *) key;
      GPtrArray *actions = (GPtrArray *) value;

      gsize i;

      g_message ("  %s:", source);

      for (i = 0; i < actions->len; ++i)
        {
          FlatpakRemoteRefAction *action = g_ptr_array_index (actions, i);
          const gchar *formatted_action_type = NULL;
          g_autofree gchar *formatted_ref = NULL;

          formatted_action_type = format_remote_ref_action_type (action->type);
          formatted_ref = flatpak_ref_format_ref (FLATPAK_REF (action->ref));

          g_string_append_printf (string,
                                  "    - %s %s:%s\n",
                                  formatted_action_type,
                                  flatpak_remote_ref_get_remote_name (action->ref),
                                  formatted_ref);
        }
    }

  return g_string_free (string, FALSE);
}

gchar *
eos_updater_util_format_all_flatpak_ref_actions_progresses (GHashTable *flatpak_ref_action_progresses)
{
  gpointer key, value;
  GHashTableIter iter;
  GString *string = g_string_new ("Action application progresses:\n");

  g_hash_table_iter_init (&iter, flatpak_ref_action_progresses);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *source = (const gchar *) key;
      gint32 progress = GPOINTER_TO_INT (value);

      g_message ("  %s: %lli", source, progress);
    }

  return g_string_free (string, FALSE);
}
