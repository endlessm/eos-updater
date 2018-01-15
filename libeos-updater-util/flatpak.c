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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <errno.h>
#include <flatpak.h>
#include <glib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>
#include <libeos-updater-util/enums.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>
#include <ostree.h>
#include <string.h>


EuuFlatpakLocationRef *
euu_flatpak_location_ref_new (FlatpakRef  *ref,
                              const gchar *remote,
                              const gchar *collection_id)
{
  EuuFlatpakLocationRef *location_ref = g_slice_new0 (EuuFlatpakLocationRef);

  location_ref->ref_count = 1;
  location_ref->ref = g_object_ref (ref);
  location_ref->remote = g_strdup (remote);
  location_ref->collection_id = g_strdup (collection_id);

  return location_ref;
}

void
euu_flatpak_location_ref_unref (EuuFlatpakLocationRef *location_ref)
{
  g_return_if_fail (location_ref->ref_count > 0);

  if (--location_ref->ref_count != 0)
    return;

  g_clear_object (&location_ref->ref);
  g_clear_pointer (&location_ref->remote, g_free);
  g_clear_pointer (&location_ref->collection_id, g_free);

  g_slice_free (EuuFlatpakLocationRef, location_ref);
}

EuuFlatpakLocationRef *
euu_flatpak_location_ref_ref (EuuFlatpakLocationRef *location_ref)
{
  g_return_val_if_fail (location_ref->ref_count > 0, NULL);
  g_return_val_if_fail (location_ref->ref_count < G_MAXINT, NULL);

  ++location_ref->ref_count;
  return location_ref;
}

EuuFlatpakRemoteRefAction *
euu_flatpak_remote_ref_action_new (EuuFlatpakRemoteRefActionType  type,
                                   EuuFlatpakLocationRef         *ref,
                                   const gchar                   *source,
                                   gint32                         serial)
{
  EuuFlatpakRemoteRefAction *action = g_slice_new0 (EuuFlatpakRemoteRefAction);

  action->ref_count = 1;
  action->type = type;
  action->ref = euu_flatpak_location_ref_ref (ref);
  action->source = g_strdup (source);
  action->serial = serial;

  return action;
}

EuuFlatpakRemoteRefAction *
euu_flatpak_remote_ref_action_ref (EuuFlatpakRemoteRefAction *action)
{
  g_return_val_if_fail (action->ref_count > 0, NULL);
  g_return_val_if_fail (action->ref_count < G_MAXINT, NULL);

  ++action->ref_count;
  return action;
}

void
euu_flatpak_remote_ref_action_unref (EuuFlatpakRemoteRefAction *action)
{
  g_return_if_fail (action->ref_count > 0);

  if (--action->ref_count != 0)
    return;

  euu_flatpak_location_ref_unref (action->ref);
  g_free (action->source);

  g_slice_free (EuuFlatpakRemoteRefAction, action);
}

static gboolean
flatpak_remote_ref_action_type_parse (const gchar                    *action,
                                      EuuFlatpakRemoteRefActionType  *out_action_type,
                                      GError                        **error)
{
  GEnumClass *enum_class = g_type_class_ref (EUU_TYPE_FLATPAK_REMOTE_REF_ACTION_TYPE);
  GEnumValue *enum_value = g_enum_get_value_by_nick (enum_class, action);

  g_type_class_unref (enum_class);

  if (!enum_value)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC,
                   "Unknown action type ‘%s’ specified in autoinstall spec",
                   action);
      return FALSE;
    }

  *out_action_type = (EuuFlatpakRemoteRefActionType) enum_value->value;

  return TRUE;
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

/* Get the member of @object named @key, if it exists and is a string. Otherwise
 * return an error. The return value is valid as long as @object is alive. */
static const gchar *
maybe_get_json_object_string_member (JsonObject   *object,
                                     const gchar  *key,
                                     GError      **error)
{
  JsonNode *member = json_object_get_member (object, key);

  if (member == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected a ‘%s’ member",
                   key);

      return NULL;
    }

  if (json_node_get_value_type (member) != G_TYPE_STRING)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected ‘%s’ member to be a string",
                   key);

      return NULL;
    }

  return json_node_get_string (member);
}

/* Parse the `app` and `ref-kind` members of the given @entry, which are common
 * to all #FlatpakRef representations. */
static gboolean
parse_flatpak_ref_from_entry (JsonObject      *entry,
                              const gchar    **out_name,
                              FlatpakRefKind  *out_ref_kind,
                              GError         **error)
{
  const gchar *name = NULL;
  const gchar *ref_kind_str = NULL;
  FlatpakRefKind kind;

  g_return_val_if_fail (out_ref_kind != NULL, FALSE);
  g_return_val_if_fail (out_name != NULL, FALSE);

  name = maybe_get_json_object_string_member (entry, "name", error);

  if (name == NULL)
    return FALSE;

  ref_kind_str = maybe_get_json_object_string_member (entry, "ref-kind", error);

  if (ref_kind_str == NULL)
    return FALSE;

  if (!parse_ref_kind (ref_kind_str, &kind, error))
    return FALSE;

  *out_name = name;
  *out_ref_kind = kind;

  return TRUE;
}

static const gchar *
eos_updater_get_system_architecture_string (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE",
                                    flatpak_get_default_arch ());
}

/* Parse an @entry of type %EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL to a
 * #EuuFlatpakLocationRef. See flatpak_remote_ref_from_action_entry(). */
static EuuFlatpakLocationRef *
flatpak_remote_ref_from_install_action_entry (JsonObject  *entry,
                                              GError     **error)
{
  const gchar *name = NULL;
  const gchar *collection_id = NULL;
  const gchar *remote = NULL;
  g_autoptr(FlatpakRef) ref = NULL;
  FlatpakRefKind kind;

  if (!parse_flatpak_ref_from_entry (entry, &name, &kind, error))
    return NULL;

  collection_id = maybe_get_json_object_string_member (entry, "collection-id", error);

  if (collection_id == NULL)
    return FALSE;

  remote = maybe_get_json_object_string_member (entry, "remote", error);

  if (remote == NULL)
    return FALSE;

  /* Invariant from this point onwards is that we have both a remote
   * and a collection-id */
  ref = g_object_new (FLATPAK_TYPE_REF,
                      "name", name,
                      "kind", kind,
                      "arch", eos_updater_get_system_architecture_string (),
                      NULL);

  return euu_flatpak_location_ref_new (ref, remote, collection_id);
}

/* Parse an @entry of type %EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL to a
 * #EuuFlatpakLocationRef. See flatpak_remote_ref_from_action_entry(). */
static EuuFlatpakLocationRef *
flatpak_remote_ref_from_uninstall_action_entry (JsonObject  *entry,
                                                GError     **error)
{
  const gchar *name = NULL;
  FlatpakRefKind kind;
  g_autoptr(FlatpakRef) ref = NULL;

  if (!parse_flatpak_ref_from_entry (entry, &name, &kind, error))
    return NULL;

  ref = g_object_new (FLATPAK_TYPE_REF,
                      "name", name,
                      "kind", kind,
                      "arch", eos_updater_get_system_architecture_string (),
                      NULL);

  return euu_flatpak_location_ref_new (ref, "none", NULL);
}

/* Parse an @entry of type %EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE to a
 * #EuuFlatpakLocationRef. See flatpak_remote_ref_from_action_entry(). */
static EuuFlatpakLocationRef *
flatpak_remote_ref_from_update_action_entry (JsonObject  *entry,
                                             GError     **error)
{
  const gchar *name = NULL;
  FlatpakRefKind kind;
  g_autoptr(FlatpakRef) ref = NULL;

  if (!parse_flatpak_ref_from_entry (entry, &name, &kind, error))
    return NULL;

  ref = g_object_new (FLATPAK_TYPE_REF,
                      "name", name,
                      "kind", kind,
                      "arch", eos_updater_get_system_architecture_string (),
                      NULL);

  return euu_flatpak_location_ref_new (ref, "none", NULL);
}

/* Parse the bits of @entry which are specific to the @action_type. */
static EuuFlatpakLocationRef *
flatpak_remote_ref_from_action_entry (EuuFlatpakRemoteRefActionType   action_type,
                                      JsonObject                                *entry,
                                      GError                                   **error)
{
  switch (action_type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return flatpak_remote_ref_from_install_action_entry (entry, error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
        return flatpak_remote_ref_from_uninstall_action_entry (entry, error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE:
        return flatpak_remote_ref_from_update_action_entry (entry, error);
      default:
        g_assert_not_reached ();
    }
}

static gboolean
is_valid_serial (gint64 maybe_serial)
{
  return (maybe_serial >= G_MININT32 && maybe_serial <= G_MAXINT32);
}

/* Parse @node into a #EuuFlatpakRemoteRefAction. It’s a programmer error if @node
 * is not a JSON object node. */
static EuuFlatpakRemoteRefAction *
flatpak_remote_ref_action_from_json_node (const gchar  *source,
                                          JsonNode     *node,
                                          GError      **error)
{
  const gchar *action_type_str = NULL;
  JsonObject *object = NULL;
  g_autoptr(EuuFlatpakLocationRef) flatpak_location_ref = NULL;
  g_autoptr(GError) local_error = NULL;
  JsonNode *serial_node = NULL;
  gint64 serial64;
  gint32 serial;
  EuuFlatpakRemoteRefActionType action_type;

  g_assert (JSON_NODE_HOLDS_OBJECT (node));

  object = json_node_get_object (node);
  action_type_str = maybe_get_json_object_string_member (object, "action", error);

  if (action_type_str == NULL)
    return NULL;

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
                   "Expected ‘serial’ member of type int in, %s", node_str);

      return NULL;
    }

  serial64 = json_node_get_int (serial_node);

  if (!is_valid_serial (serial64))
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "The ‘serial’ member in in the autoinstall spec must fit within a 32 bit integer (at %s)", node_str);
      return NULL;
    }

  serial = (gint32) serial64;

  flatpak_location_ref = flatpak_remote_ref_from_action_entry (action_type, object, &local_error);

  if (flatpak_location_ref == NULL)
    {
      if (g_error_matches (local_error,
                           EOS_UPDATER_ERROR,
                           EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC))
        {
          g_autofree gchar *node_str = json_node_to_string (node);
          g_propagate_prefixed_error (error,
                                      g_steal_pointer (&local_error),
                                      "Error parsing action detail (at %s) ‘%s’: ",
                                      action_type_str,
                                      node_str);
          return NULL;
        }

      /* Not currently possible to hit these lines of code, given that
       * flatpak_remote_ref_from_action_entry() always errors with
       * %MALFORMED_AUTOINSTALL_SPEC. */
      g_propagate_error (error, g_steal_pointer (&local_error));
      return NULL;
    }

  return euu_flatpak_remote_ref_action_new (action_type,
                                            flatpak_location_ref,
                                            source,
                                            serial);
}

static gint
sort_flatpak_remote_ref_actions (gconstpointer a, gconstpointer b)
{
  EuuFlatpakRemoteRefAction **action_a = (EuuFlatpakRemoteRefAction **) a;
  EuuFlatpakRemoteRefAction **action_b = (EuuFlatpakRemoteRefAction **) b;

  return (gint) (*action_a)->serial - (gint) (*action_b)->serial;
}

/* Synchronously parse @file as JSON. */
static JsonNode *
parse_json_from_file (GFile         *file,
                      GCancellable  *cancellable,
                      GError       **error)
{
  g_autoptr(GFileInputStream) input_stream = NULL;
  g_autoptr(JsonParser) parser = json_parser_new_immutable ();
  g_autoptr(JsonNode) root_node = NULL;
  g_autoptr(GError) local_error = NULL;

  input_stream = g_file_read (file, cancellable, error);

  if (input_stream == NULL)
    return NULL;

  if (!json_parser_load_from_stream (parser,
                                     G_INPUT_STREAM (input_stream),
                                     cancellable,
                                     &local_error))
    {
      g_autofree gchar *path = g_file_get_path (file);
      g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Error parsing JSON in ‘%s’: %s", path, local_error->message);
      return NULL;
    }

  /* Treat an empty file the same as an empty root array. */
  root_node = json_parser_get_root (parser);
  if (root_node != NULL)
    {
      json_node_ref (root_node);
    }
  else
    {
      root_node = json_node_new (JSON_NODE_ARRAY);
      json_node_take_array (root_node, json_array_new ());
    }

  return g_steal_pointer (&root_node);
}

/* Get the elements of the member named @key of @object, which must exist (it’s
 * a programmer error otherwise), and must be an array (an error is return if
 * it’s not). */
static GList *  /* (element-type JsonNode) (transfer container) */
lookup_array_nodes (JsonObject   *object,
                    const gchar  *key,
                    GError      **error)
{
  JsonNode *filter_value = json_object_get_member (object, key);
  JsonArray *filter_array = NULL;

  /* Asserting here, since this function is meant to be called with
   * an object that has a known key */
  g_return_val_if_fail (filter_value != NULL, NULL);

  if (!JSON_NODE_HOLDS_ARRAY (filter_value))
    {
      g_autofree gchar *node_str = json_node_to_string (filter_value);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected ‘%s’ filter to be an array, was: %s",
                   key,
                   node_str);
      return NULL;
    }

  filter_array = json_node_get_array (filter_value);
  return json_array_get_elements (filter_array);
}

/* Return %TRUE in @out_in_array if any of the elements of @strv are in the
 * @nodes array of strings; otherwise return %FALSE in @out_in_array. If any of
 * the elements in @nodes are not strings, an error is returned. */
static gboolean
strv_element_in_json_string_node_list (GStrv      strv,
                                       GList     *nodes,  /* (element-type JsonNode) */
                                       gboolean  *out_in_array,
                                       GError   **error)
{
  GList *iter;

  g_assert (out_in_array != NULL);

  for (iter = nodes; iter != NULL; iter = iter->next)
    {
      GStrv strv_iter = NULL;
      const gchar *string;

      if (json_node_get_value_type (iter->data) != G_TYPE_STRING)
        {
          g_autofree gchar *node_str = json_node_to_string (iter->data);
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                       "Unexpected non-string value: %s",
                       node_str);
          return FALSE;
        }

      string = json_node_get_string (iter->data);

      for (strv_iter = strv; *strv_iter != NULL; ++strv_iter)
        {
          if (g_strcmp0 (string, *strv_iter) == 0)
            {
              *out_in_array = TRUE;
              return TRUE;
            }
        }
    }

  *out_in_array = FALSE;
  return TRUE;
}

/* Combination of lookup_array_nodes() and
 * strv_element_in_json_string_node_list(). If @key doesn’t exist in @object,
 * an error is returned. */
static gboolean
strv_element_in_json_member (GStrv        strv,
                             JsonObject   *object,
                             const gchar  *key,
                             gboolean     *out_in_array,
                             GError      **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GList) array_nodes = lookup_array_nodes (object, key, &local_error);

  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (!strv_element_in_json_string_node_list (strv, array_nodes, out_in_array, error))
    return FALSE;

  return TRUE;
}

/* This is just a composable way to get the outvalue */
static gboolean
invert_outvalue (gboolean *out_value)
{
  g_assert (out_value != NULL);

  *out_value = !(*out_value);

  return TRUE;
}

static GStrv
eos_updater_override_locales_list (void)
{
  const gchar *override_locales = eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES", NULL);

  if (override_locales != NULL)
    return g_strsplit (override_locales, ";", -1);

  /* Return an empty GStrv. */
  return g_new0 (gchar*, 1);
}

static gboolean
get_locales_list_from_flatpak_installation (GStrv  *out_strv,
                                            GError **error)
{
  g_autoptr(FlatpakInstallation) installation = eos_updater_get_flatpak_installation (NULL, error);
  g_assert (out_strv != NULL);

  if (installation == NULL)
    return FALSE;

  /* TODO: Right now this returns only the testing override or NULL,
   * but we might want to do something a little more clever based on what is
   * supported by Flatpak in future, see
   * https://github.com/flatpak/flatpak/issues/1156 */
  *out_strv = eos_updater_override_locales_list ();

  return TRUE;
}

/* Calculate whether this entry (@object) is filtered out of the list by the
 * value in @filter_key_name on @object (if present). If @object _is_ filtered
 * (should be removed from the list), @is_filtered will be set to %TRUE. It is
 * an error if @filter_key_name is not a valid filter name. */
static gboolean
action_filter_applies (JsonObject   *object,
                       const gchar  *filter_key_name,
                       gboolean     *is_filtered,
                       GError      **error)
{
  const gchar *current_architecture_strv[] =
    {
      eos_updater_get_system_architecture_string (),
      NULL
    };
  g_auto(GStrv) supported_languages = NULL;

  g_return_val_if_fail (filter_key_name != NULL, FALSE);
  g_return_val_if_fail (is_filtered != NULL, FALSE);

  if (!get_locales_list_from_flatpak_installation (&supported_languages,
                                                   error))
    return FALSE;

  /* If adding support for a new filter:
   *  - Expand the inverse check in action_node_should_be_filtered_out().
   *  - Add a checkpoint to the OSTree after releasing the new version of
   *    eos-updater, but before distributing an autoinstall list which uses the
   *    new filter, to guarantee that all clients receiving the autoinstall list
   *    know how to handle it.
   *  - Update the JSON Schema and the man page.
   */
  if (g_str_equal (filter_key_name, "architecture"))
    return strv_element_in_json_member ((GStrv) current_architecture_strv,
                                        object,
                                        filter_key_name,
                                        is_filtered,
                                        error) &&
           invert_outvalue (is_filtered);

  if (g_str_equal (filter_key_name, "~architecture"))
    return strv_element_in_json_member ((GStrv) current_architecture_strv,
                                        object,
                                        filter_key_name,
                                        is_filtered,
                                        error);

  if (g_str_equal (filter_key_name, "locale"))
    return strv_element_in_json_member (supported_languages,
                                        object,
                                        filter_key_name,
                                        is_filtered,
                                        error) &&
           invert_outvalue (is_filtered);

  if (g_str_equal (filter_key_name, "~locale"))
    return strv_element_in_json_member (supported_languages,
                                        object,
                                        filter_key_name,
                                        is_filtered,
                                        error);

  g_set_error (error,
               EOS_UPDATER_ERROR,
               EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC,
               "Unknown action filter value ‘%s’; "
               "expected one of ‘~architecture’, ‘architecture’, "
               "‘~locale’ and ‘locale’",
               filter_key_name);
  return FALSE;
}

/* Calculate whether the @node should be filtered out by any of its filters.
 * @node must be a JSON object node.
 *
 * We do this at the same time as reading the JSON node so that we don’t have
 * to keep filter information around in memory. */
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

  if (!JSON_NODE_HOLDS_OBJECT (filters_object_node))
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected ‘filters’ node to be an object, but was %s", node_str);

      return FALSE;
    }

  filters_object = json_node_get_object (filters_object_node);

  /* Specifying both a filter and its inverse isn’t allowed. */
  if ((json_object_has_member (filters_object, "locale") &&
       json_object_has_member (filters_object, "~locale")) ||
      (json_object_has_member (filters_object, "architecture") &&
       json_object_has_member (filters_object, "~architecture")))
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Invalid ‘filters’ object contained a filter and its inverse: %s", node_str);

      return FALSE;
    }

  filters_object_keys = json_object_get_members (filters_object);

  for (iter = filters_object_keys; iter != NULL; iter = iter->next)
    {
      gboolean action_is_filtered_on_this_filter;

      if (!action_filter_applies (filters_object,
                                  iter->data,
                                  &action_is_filtered_on_this_filter,
                                  error))
        return FALSE;

      if (action_is_filtered_on_this_filter)
        {
          *is_filtered = TRUE;
          return TRUE;
        }
    }

  *is_filtered = FALSE;
  return TRUE;
}

/* Load all the entries from the given @file, filtering out any which don’t
 * apply given their `filters`. If any entry fails to parse, an error is
 * returned overall. If any entry fails to parse non-fatally, its JSON
 * is listed in @skipped_action_entries and the next entry is parsed. */
static GPtrArray *  /* (element-type EuuFlatpakRemoteRefAction) */
read_flatpak_ref_actions_from_node (JsonNode      *node,
                                    const gchar   *filename,
                                    GPtrArray     *skipped_action_entries  /* (element-type utf8) */,
                                    GError       **error)
{
  /* Now that we have the file contents, time to read in the list of
   * flatpaks to install into a pointer array. Parse out the OSTree ref
   * and then parse the EuuFlatpakRemoteRefAction */
  g_autoptr(GPtrArray) actions = NULL;  /* (element-type EuuFlatpakRemoteRefAction) */
  g_autofree gchar *basename = g_path_get_basename (filename);
  JsonArray *array = NULL;
  g_autoptr(GList) elements = NULL;
  GList *iter = NULL;  /* (element-type JsonNode) */
  gsize i;

  g_assert (skipped_action_entries != NULL);

  /* Parse each entry of the underlying array */
  if (!JSON_NODE_HOLDS_ARRAY (node))
    {
      g_autofree gchar *node_str = json_node_to_string (node);
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Expected node to be an array when parsing %s at %s", node_str, filename);

      return NULL;
    }

  array = json_node_get_array (node);
  elements = json_array_get_elements (array);
  actions = g_ptr_array_new_with_free_func ((GDestroyNotify) euu_flatpak_remote_ref_action_unref);

  for (iter = elements; iter != NULL; iter = iter->next)
    {
      /* We use local_error here so that we can catch and
       * add detail on the filename if necessary */
      g_autoptr(GError) local_error = NULL;
      g_autoptr(EuuFlatpakRemoteRefAction) action = NULL;
      gboolean is_filtered = FALSE;
      JsonNode *element_node = iter->data;

      if (!JSON_NODE_HOLDS_OBJECT (element_node))
        {
          g_autofree gchar *node_str = json_node_to_string (element_node);
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                       "Expected node to be an object when parsing %s at %s", node_str, filename);

          return NULL;
        }

      if (!action_node_should_be_filtered_out (element_node, &is_filtered, &local_error))
        {
          if (g_error_matches (local_error,
                               EOS_UPDATER_ERROR,
                               EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC))
            {
              g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                          "Error parsing ‘%s’: ", filename);
              return NULL;
            }
          else if (g_error_matches (local_error,
                                    EOS_UPDATER_ERROR,
                                    EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC))
            {
              g_debug ("%s while parsing %s. Skipping this action and it "
                       "will not be reapplied later. System may be in an "
                       "inconsistent state from this point forward.",
                       local_error->message, filename);
              g_ptr_array_add (skipped_action_entries, json_node_to_string (element_node));
              g_clear_error (&local_error);
              continue;
            }

          /* This code can’t currently be reached due to the limited range of
           * errors which action_node_should_be_filtered_out() sets. */
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      if (is_filtered)
        continue;

      action = flatpak_remote_ref_action_from_json_node (basename, element_node, &local_error);

      if (action == NULL)
        {
          if (g_error_matches (local_error,
                               EOS_UPDATER_ERROR,
                               EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC))
            {
              g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                          "Error parsing ‘%s’: ", filename);
              return NULL;
            }
          else if (g_error_matches (local_error,
                                    EOS_UPDATER_ERROR,
                                    EOS_UPDATER_ERROR_UNKNOWN_ENTRY_IN_AUTOINSTALL_SPEC))
            {
              g_ptr_array_add (skipped_action_entries, json_node_to_string (element_node));
              g_clear_error (&local_error);
              continue;
            }

          /* This code can’t currently be reached due to the limited range of
           * errors which flatpak_remote_ref_action_from_json_node() sets. */
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      g_ptr_array_add (actions, g_steal_pointer (&action));
    }

  /* Now that we have the remote ref actions, sort them by their ordering */
  g_ptr_array_sort (actions, sort_flatpak_remote_ref_actions);

  /* Check there are no duplicate serial numbers. */
  for (i = 1; i < actions->len; i++)
    {
      const EuuFlatpakRemoteRefAction *prev_action = g_ptr_array_index (actions, i - 1);
      const EuuFlatpakRemoteRefAction *action = g_ptr_array_index (actions, i);

      if (prev_action->serial == action->serial)
        {
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                       "Two entries share serial number %" G_GINT32_FORMAT " in ‘%s’",
                       prev_action->serial, filename);

          return NULL;
        }
    }

  return g_steal_pointer (&actions);
}

GPtrArray *
euu_flatpak_ref_actions_from_file (GFile         *file,
                                   GPtrArray    **out_skipped_actions  /* (element-type utf8) */,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  g_autoptr(GPtrArray) actions = NULL;
  g_autoptr(GPtrArray) skipped_actions = g_ptr_array_new_with_free_func (g_free);
  g_autofree gchar *path = g_file_get_path (file);
  g_autoptr(JsonNode) node = parse_json_from_file (file, cancellable, error);
  if (node == NULL)
    return NULL;

  actions = read_flatpak_ref_actions_from_node (node, path, skipped_actions, error);

  if (actions == NULL)
    return NULL;

  if (out_skipped_actions != NULL)
    *out_skipped_actions = g_steal_pointer (&skipped_actions);

  return g_steal_pointer (&actions);
}

/* A version of euu_flatpak_ref_actions_from_file() which takes a
 * string constant to parse. Mostly used for the unit tests. */
GPtrArray *
euu_flatpak_ref_actions_from_data (const gchar   *data,
                                   gssize         length,
                                   const gchar   *path,
                                   GPtrArray    **out_skipped_actions  /* (element-type utf8) */,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  g_autoptr(GPtrArray) actions = NULL;
  g_autoptr(GPtrArray) skipped_actions = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(JsonParser) parser = json_parser_new_immutable ();
  g_autoptr(JsonNode) root_node = NULL;
  g_autoptr(GError) local_error = NULL;

  if (!json_parser_load_from_data (parser, data, length, &local_error))
    {
      g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC,
                   "Error parsing JSON in ‘%s’: %s", path, local_error->message);
      return NULL;
    }

/* Treat an empty file the same as an empty root array. */
  root_node = json_parser_get_root (parser);
  if (root_node != NULL)
    {
      json_node_ref (root_node);
    }
  else
    {
      root_node = json_node_new (JSON_NODE_ARRAY);
      json_node_take_array (root_node, json_array_new ());
    }

  actions = read_flatpak_ref_actions_from_node (root_node, path, skipped_actions, error);

  if (actions == NULL)
    return NULL;

  if (out_skipped_actions != NULL)
    *out_skipped_actions = g_steal_pointer (&skipped_actions);

  return g_steal_pointer (&actions);
}

EuuFlatpakRemoteRefActionsFile *
euu_flatpak_remote_ref_actions_file_new (GPtrArray *remote_ref_actions,
                                         gint       priority)
{
  EuuFlatpakRemoteRefActionsFile *file = g_slice_new0 (EuuFlatpakRemoteRefActionsFile);

  file->remote_ref_actions = g_ptr_array_ref (remote_ref_actions);
  file->priority = priority;

  return file;
}

void
euu_flatpak_remote_ref_actions_file_free (EuuFlatpakRemoteRefActionsFile *file)
{
  g_clear_pointer (&file->remote_ref_actions, g_ptr_array_unref);

  g_slice_free (EuuFlatpakRemoteRefActionsFile, file);
}

/* Update @ref_actions_for_files to add all the action lists from files in
 * @directory to it, at the given @priority. Lower numeric @priority values are
 * more important. If a filename from @directory is already listed in
 * @ref_actions_for_files, it will be replaced if @priority is more important
 * than the priority attached to the existing entry in the hash table.
 *
 * @ref_actions_for_files maps filenames (gchar*) to
 * #EuuFlatpakRemoteRefActionsFile instances.
 *
 * If any of the files in @directory fail to be parsed, all parsing will be
 * aborted and an error will be returned.
 *
 * If @directory does not exist, a %G_IO_ERROR_NOT_FOUND error will be returned
 * unless @allow_noent is %TRUE, in which case, %TRUE will be returned and
 * @ref_actions_for_files will not be modified. */
gboolean
euu_flatpak_ref_actions_append_from_directory (GFile         *directory,
                                               GHashTable    *ref_actions_for_files,
                                               gint           priority,
                                               gboolean       allow_noent,
                                               GCancellable  *cancellable,
                                               GError       **error)
{
  g_autoptr(GFileEnumerator) autoinstall_d_enumerator = NULL;
  g_autoptr(GError) local_error = NULL;

  /* Repository checked out, read all files in order and build up a list
   * of flatpaks to auto-install */
  autoinstall_d_enumerator = g_file_enumerate_children (directory,
                                                        G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                        G_FILE_QUERY_INFO_NONE,
                                                        cancellable,
                                                        &local_error);

  if (autoinstall_d_enumerator == NULL)
    {
      if (allow_noent &&
          g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        return TRUE;

      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  while (TRUE)
    {
      GFile *file;
      GFileInfo *info;
      g_autoptr(GPtrArray) action_refs = NULL;
      g_autoptr(GPtrArray) skipped_action_refs = NULL;
      const gchar *filename = NULL;
      EuuFlatpakRemoteRefActionsFile *existing_actions_file = NULL;

      if (!g_file_enumerator_iterate (autoinstall_d_enumerator,
                                      &info, &file,
                                      cancellable, error))
        return FALSE;

      if (file == NULL || info == NULL)
        break;

      filename = g_file_info_get_name (info);

      /* We may already have a remote_ref_actions_file in the hash table
       * and we cannot just blindly replace it. Replace it only if
       * the incoming directory has a higher priority. */
      existing_actions_file = g_hash_table_lookup (ref_actions_for_files,
                                                   filename);

      if (existing_actions_file != NULL &&
          existing_actions_file->priority < priority)
        continue;

      action_refs = euu_flatpak_ref_actions_from_file (file, &skipped_action_refs, cancellable, error);

      if (action_refs == NULL)
        return FALSE;

      if (skipped_action_refs != NULL && skipped_action_refs->len > 0)
        {
          g_autofree gchar *list_str = g_strjoinv ("\n", (gchar **) skipped_action_refs->pdata);
          g_warning ("Skipping the following actions while parsing ‘%s’, due "
                     "to not supporting their contents. They will not be "
                     "reapplied later; the system may be in an inconsistent "
                     "state from this point forward.\n%s",
                     filename, list_str);
        }

      g_hash_table_replace (ref_actions_for_files,
                            g_strdup (filename),
                            euu_flatpak_remote_ref_actions_file_new (action_refs,
                                                                     priority));
    }

  return TRUE;
}

/* Returns an associative map from action-ref list to a pointer array of
 * actions. The action-ref lists are considered to be append-only */
GHashTable *  /* (element-type filename EuuFlatpakRemoteRefActionsFile) */
euu_flatpak_ref_actions_from_directory (GFile         *directory,
                                        gint           priority,
                                        GCancellable  *cancellable,
                                        GError       **error)
{
  g_autoptr(GHashTable) ref_actions_for_files = g_hash_table_new_full (g_str_hash,
                                                                       g_str_equal,
                                                                       g_free,
                                                                       (GDestroyNotify) euu_flatpak_remote_ref_actions_file_free);

  if (!euu_flatpak_ref_actions_append_from_directory (directory,
                                                      ref_actions_for_files,
                                                      priority,
                                                      FALSE,  /* error if @directory doesn’t exist */
                                                      cancellable,
                                                      error))
    return NULL;

  return g_steal_pointer (&ref_actions_for_files);
}

static guint
flatpak_ref_hash (gconstpointer data)
{
  FlatpakRef *ref = FLATPAK_REF (data);
  FlatpakRefKind kind = flatpak_ref_get_kind (ref);
  const gchar *name = flatpak_ref_get_name (ref);
  const gchar *arch = flatpak_ref_get_arch (ref);
  const gchar *branch = flatpak_ref_get_branch (ref);

  return ((guint) kind ^
          ((name != NULL) ? g_str_hash (name) : 0) ^
          ((arch != NULL) ? g_str_hash (arch) : 0) ^
          ((branch != NULL) ? g_str_hash (branch) : 0));
}

static gboolean
flatpak_ref_equal (gconstpointer a,
                   gconstpointer b)
{
  FlatpakRef *a_ref = FLATPAK_REF (a), *b_ref = FLATPAK_REF (b);
  return (flatpak_ref_get_kind (a_ref) == flatpak_ref_get_kind (b_ref) &&
          g_strcmp0 (flatpak_ref_get_name (a_ref), flatpak_ref_get_name (b_ref)) == 0 &&
          g_strcmp0 (flatpak_ref_get_arch (a_ref), flatpak_ref_get_arch (b_ref)) == 0 &&
          g_strcmp0 (flatpak_ref_get_branch (a_ref), flatpak_ref_get_branch (b_ref)) == 0);
}

/* Squash actions on the same ref into the last action on that ref, returning
 * a pointer array of remote ref actions, ordered by the order key in each
 * remote ref action. */
static GPtrArray *  /* (element-type EuuFlatpakRemoteRefAction) */
squash_ref_actions_ptr_array (GPtrArray *ref_actions  /* (element-type EuuFlatpakRemoteRefAction) */)
{
  g_autoptr(GHashTable) hash_table = g_hash_table_new_full (flatpak_ref_hash,
                                                            flatpak_ref_equal,
                                                            g_object_unref,
                                                            (GDestroyNotify) euu_flatpak_remote_ref_action_unref);
  g_autoptr(GPtrArray) squashed_ref_actions = NULL;
  gsize i;
  gpointer key;
  gpointer value;
  GHashTableIter hash_iter;

  for (i = 0; i < ref_actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *action = g_ptr_array_index (ref_actions, i);
      EuuFlatpakRemoteRefAction *existing_action_for_ref = NULL;

      /* A little trickier than just blindly replacing, there are special
       * rules regarding "update" since it only updates an existing installed
       * flatpak, as opposed to installing it.
       *
       * (1) "install" and "uninstall" always take priority over "update"
       *     since "install" means "install or update" and "uninstall"
       *     means "unconditionally remove".
       * (2) "update" does not take priority over "install" or "uninstall",
       *     since the former would subsumes it anyway and the latter would
       *     make the app no longer be installed in that run of the flatpak
       *     installer.
       */
      existing_action_for_ref = g_hash_table_lookup (hash_table, action->ref->ref);

      if (action->type == EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL ||
          action->type == EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL ||
          existing_action_for_ref == NULL ||
          existing_action_for_ref->type == EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE)
        {
          g_hash_table_replace (hash_table,
                                g_object_ref (action->ref->ref),
                                euu_flatpak_remote_ref_action_ref (action));
        }
    }

  squashed_ref_actions = g_ptr_array_new_full (g_hash_table_size (hash_table),
                                               (GDestroyNotify) euu_flatpak_remote_ref_action_unref);
  g_hash_table_iter_init (&hash_iter, hash_table);

  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    g_ptr_array_add (squashed_ref_actions,
                     euu_flatpak_remote_ref_action_ref (value));

  g_ptr_array_sort (squashed_ref_actions, sort_flatpak_remote_ref_actions);

  return g_steal_pointer (&squashed_ref_actions);
}

/* Given a hash table of filenames to #EuuFlatpakRemoteRefActionsFile, hoist
 * the underlying pointer array of remote ref actions and make that the value
 * of the new hash table.
 *
 * This will make the hash table suitable for passing to
 * euu_squash_remote_ref_actions() */
GHashTable *  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */
euu_hoist_flatpak_remote_ref_actions (GHashTable *ref_actions_file_table  /* (element-type filename EuuFlatpakRemoteRefActionsFile) */)
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
      EuuFlatpakRemoteRefActionsFile *ref_actions_file = value;

      g_hash_table_insert (hoisted_ref_actions_table,
                           g_strdup (key),
                           g_ptr_array_ref (ref_actions_file->remote_ref_actions));
    }

  return g_steal_pointer (&hoisted_ref_actions_table);
}

/* Examine each of the remote ref action lists in @ref_action_table
 * and squash them down into a list where only one action is applied for
 * each flatpak ref (the latest one) */
GHashTable *  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */
euu_squash_remote_ref_actions (GHashTable *ref_actions_table)
{
  gpointer key;
  gpointer value;
  GHashTableIter hash_iter;
  g_autoptr(GHashTable) squashed_ref_actions_table = NULL;

  squashed_ref_actions_table = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_ptr_array_unref);

  g_hash_table_iter_init (&hash_iter, ref_actions_table);

  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    {
      GPtrArray *unsquashed_remote_ref_actions = value;
      g_autoptr(GPtrArray) squashed_remote_ref_actions = NULL;

      squashed_remote_ref_actions = squash_ref_actions_ptr_array (unsquashed_remote_ref_actions);

      g_hash_table_insert (squashed_ref_actions_table,
                           g_strdup (key),
                           g_steal_pointer (&squashed_remote_ref_actions));
    }

  return g_steal_pointer (&squashed_ref_actions_table);
}

typedef GPtrArray *(*FilterFlatpakRefActionsFunc) (const gchar *table_name,
                                                   GPtrArray   *actions,
                                                   gpointer     user_data);

/* Given a hashtable of action-ref filenames and a pointer array of
 * ref-actions, use the provided filter_func return a hash-table of
 * ref-actions to keep around for later processing. For instance, the caller
 * may want to filter out all ref actions except uninstalls */
static GHashTable *  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */
filter_flatpak_ref_actions_table (GHashTable                  *ref_actions_table  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */,
                                  FilterFlatpakRefActionsFunc  filter_func,
                                  gpointer                     filter_func_user_data)
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
                         (*filter_func) (key, value, filter_func_user_data));

  return euu_squash_remote_ref_actions (filtered_flatpak_ref_actions_table);
}

/* A callback to pass to filter_flatpak_ref_actions_table(). It filters out
 * the elements from @incoming_actions whose serial number is less than or equal
 * to the latest progress value from the hash table provided in @user_data.
 *
 * @user_data is a hash table from autoinstall filename to latest applied serial
 * number. @table_name will be used to look up a progress value from it. */
static GPtrArray *  /* (element-type EuuFlatpakRemoteRefAction) */
keep_only_new_actions (const gchar *table_name,
                       GPtrArray   *incoming_actions  /* (element-type EuuFlatpakRemoteRefAction) */,
                       gpointer     user_data)
{
  GHashTable *already_applied_actions_table = user_data;  /* (element-type filename gint32) */
  gint32 already_applied_actions_progress;
  gpointer already_applied_actions_progress_value;
  g_autoptr(GPtrArray) filtered_actions = NULL;  /* (element-type EuuFlatpakRemoteRefAction) */
  guint i;

  /* We haven’t applied any actions for this name yet, so return a copy of
   * the incoming actions in every case */
  if (!g_hash_table_lookup_extended (already_applied_actions_table,
                                     table_name,
                                     NULL,
                                     &already_applied_actions_progress_value))
    return g_ptr_array_ref (incoming_actions);

  already_applied_actions_progress = GPOINTER_TO_INT (already_applied_actions_progress_value);

  for (i = 0; i < incoming_actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *action = g_ptr_array_index (incoming_actions, i);

      /* We saw a new action. Change to the adding state and add all actions
       * to the filtered actions list */
      if (filtered_actions == NULL && action->serial > already_applied_actions_progress)
        filtered_actions = g_ptr_array_new_full (incoming_actions->len - i,
                                                 (GDestroyNotify) euu_flatpak_remote_ref_action_unref);

      if (filtered_actions != NULL)
          g_ptr_array_add (filtered_actions,
                           euu_flatpak_remote_ref_action_ref (action));
    }

  if (filtered_actions == NULL)
    filtered_actions = g_ptr_array_new_with_free_func ((GDestroyNotify) euu_flatpak_remote_ref_action_unref);

  return g_steal_pointer (&filtered_actions);
}

/* A callback to pass to filter_flatpak_ref_actions_table(). It filters out
 * the elements from @incoming_actions whose serial number is greater than the
 * latest progress value from the hash table provided in @user_data.
 *
 * @user_data is a hash table from autoinstall filename to latest applied serial
 * number. @table_name will be used to look up a progress value from it. */
static GPtrArray *  /* (element-type EuuFlatpakRemoteRefAction) */
keep_only_existing_actions (const gchar  *table_name,
                            GPtrArray    *incoming_actions  /* (element-type EuuFlatpakRemoteRefAction) */,
                            gpointer      user_data)
{
  GHashTable *already_applied_actions_table = user_data;  /* (element-type filename gint32) */
  gint32 already_applied_actions_progress;
  gpointer already_applied_actions_progress_value;
  g_autoptr(GPtrArray) filtered_actions = NULL;  /* (element-type EuuFlatpakRemoteRefAction) */
  guint i;

  /* We haven’t applied any actions for this name yet, so return an empty array */
  filtered_actions = g_ptr_array_new_with_free_func ((GDestroyNotify) euu_flatpak_remote_ref_action_unref);

  if (!g_hash_table_lookup_extended (already_applied_actions_table,
                                     table_name,
                                     NULL,
                                     &already_applied_actions_progress_value))
    return g_steal_pointer (&filtered_actions);

  already_applied_actions_progress = GPOINTER_TO_INT (already_applied_actions_progress_value);

  for (i = 0; i < incoming_actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *action = g_ptr_array_index (incoming_actions, i);

      /* If we see an action newer than the progress, abort the loop early */
      if (action->serial > already_applied_actions_progress)
        break;

      g_ptr_array_add (filtered_actions,
                       euu_flatpak_remote_ref_action_ref (action));
    }

  return g_steal_pointer (&filtered_actions);
}

/* See documentation for keep_only_new_actions(). */
GHashTable *
euu_filter_for_new_flatpak_ref_actions (GHashTable *ref_actions  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */,
                                        GHashTable *progresses)
{
  return filter_flatpak_ref_actions_table (ref_actions,
                                           keep_only_new_actions,
                                           progresses);
}

/* See documentation for keep_only_existing_actions(). */
GHashTable *
euu_filter_for_existing_flatpak_ref_actions (GHashTable *ref_actions  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */,
                                             GHashTable *progresses)
{
  return filter_flatpak_ref_actions_table (ref_actions,
                                           keep_only_existing_actions,
                                           progresses);
}

const gchar *
euu_pending_flatpak_deployments_state_path (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_FLATPAK_UPGRADE_STATE_DIR",
                                    LOCALSTATEDIR "/lib/eos-application-tools/flatpak-autoinstall.progress");
}

const gchar *
euu_flatpak_autoinstall_override_paths (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_FLATPAK_AUTOINSTALL_OVERRIDE_DIRS",
                                    SYSCONFDIR "/eos-application-tools/flatpak-autoinstall.d;"
                                    LOCALSTATEDIR "/lib/eos-application-tools/flatpak-autoinstall.d");
}

/* Load the progress information from euu_pending_flatpak_deployments_state_path()
 * and return it in a hash table of filename → progress. Each progress value
 * is an integer which is the serial number of the last applied autoinstall
 * entry for that filename. */
GHashTable *  /* (element-type filename gint32) */
euu_flatpak_ref_action_application_progress_in_state_path (GCancellable  *cancellable,
                                                           GError       **error)
{
  const gchar *state_file_path = euu_pending_flatpak_deployments_state_path ();
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
  groups = g_key_file_get_groups (state_key_file, NULL);

  for (groups_iter = groups; *groups_iter != NULL; ++groups_iter)
    {
      const gchar *source_path = *groups_iter;
      gint progress = g_key_file_get_integer (state_key_file,
                                              *groups_iter,
                                              "Progress",
                                              &local_error);

      if (local_error != NULL)
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }
      else if (!is_valid_serial (progress))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "‘Progress’ must be in range [%d, %d] in key file",
                       G_MININT32, G_MAXINT32);
          return NULL;
        }

      g_hash_table_insert (ref_action_progress_for_files,
                           g_strdup (source_path),
                           GINT_TO_POINTER (progress));
    }

  return g_steal_pointer (&ref_action_progress_for_files);
}

/* Examine remote ref actions coming from multiple sources and flatten
 * them into a single squashed list based on their lexicographical
 * priority */
GPtrArray *  /* (element-type EuuFlatpakRemoteRefAction) */
euu_flatten_flatpak_ref_actions_table (GHashTable *ref_actions_table  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */)
{
  g_autoptr(GList) remote_ref_actions_keys = g_hash_table_get_keys (ref_actions_table);
  g_autoptr(GPtrArray) concatenated_actions_pointer_array = g_ptr_array_new_with_free_func ((GDestroyNotify) euu_flatpak_remote_ref_action_unref);
  GList *iter = NULL;

  remote_ref_actions_keys = g_list_sort (remote_ref_actions_keys, (GCompareFunc) g_strcmp0);

  for (iter = remote_ref_actions_keys; iter != NULL; iter = iter->next)
    {
      GPtrArray *ref_actions = g_hash_table_lookup (ref_actions_table, iter->data);
      gsize i;

      for (i = 0; i < ref_actions->len; ++i)
        g_ptr_array_add (concatenated_actions_pointer_array,
                         euu_flatpak_remote_ref_action_ref (g_ptr_array_index (ref_actions, i)));
    }

  return squash_ref_actions_ptr_array (concatenated_actions_pointer_array);
}

/* Format @action_type into a human-readable string. */
static const gchar *
format_remote_ref_action_type (EuuFlatpakRemoteRefActionType action_type)
{
  GEnumClass *enum_class = g_type_class_ref (EUU_TYPE_FLATPAK_REMOTE_REF_ACTION_TYPE);
  GEnumValue *enum_value = g_enum_get_value (enum_class, action_type);

  g_type_class_unref (enum_class);

  g_assert (enum_value != NULL);

  return enum_value->value_nick;
}

gchar *
euu_format_all_flatpak_ref_actions (const gchar *title,
                                    GHashTable  *flatpak_ref_actions_for_this_boot)
{
  gpointer key, value;
  GHashTableIter iter;
  g_autoptr(GString) string = g_string_new ("");

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
          EuuFlatpakRemoteRefAction *action = g_ptr_array_index (actions, i);
          const gchar *formatted_action_type = NULL;
          g_autofree gchar *formatted_ref = NULL;

          formatted_action_type = format_remote_ref_action_type (action->type);
          formatted_ref = flatpak_ref_format_ref (action->ref->ref);

          g_string_append_printf (string,
                                  "    - %s (collection-id: %s|remote: %s):%s\n",
                                  formatted_action_type,
                                  action->ref->collection_id,
                                  action->ref->remote,
                                  formatted_ref);
        }
    }

  if (g_hash_table_size (flatpak_ref_actions_for_this_boot) == 0)
    g_string_append (string, "    (None)");

  return g_string_free (g_steal_pointer (&string), FALSE);
}

gchar *
euu_format_flatpak_ref_actions_array (const gchar *title,
                                      GPtrArray   *flatpak_ref_actions)
{
  g_autoptr(GString) string = g_string_new ("");
  gsize i;

  g_string_append_printf (string, "%s:\n", title);

  for (i = 0; i < flatpak_ref_actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *action = g_ptr_array_index (flatpak_ref_actions, i);
      const gchar *formatted_action_type = NULL;
      g_autofree gchar *formatted_ref = NULL;

      formatted_action_type = format_remote_ref_action_type (action->type);
      formatted_ref = flatpak_ref_format_ref (action->ref->ref);

      g_string_append_printf (string,
                              "    - %s (collection-id: %s|remote: %s):%s (source: %s)\n",
                              formatted_action_type,
                              action->ref->collection_id,
                              action->ref->remote,
                              formatted_ref,
                              action->source);
    }

  if (flatpak_ref_actions->len == 0)
    g_string_append (string, "    (None)");

  return g_string_free (g_steal_pointer (&string), FALSE);
}

gchar *
euu_format_all_flatpak_ref_actions_progresses (GHashTable *flatpak_ref_action_progresses)
{
  gpointer key, value;
  GHashTableIter iter;
  g_autoptr(GString) string = g_string_new ("Action application progresses:\n");

  g_hash_table_iter_init (&iter, flatpak_ref_action_progresses);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *source = (const gchar *) key;
      gint32 progress = GPOINTER_TO_INT (value);

      g_message ("  %s: %" G_GINT32_FORMAT, source, progress);
    }

  if (g_hash_table_size (flatpak_ref_action_progresses) == 0)
    g_string_append (string, "    (None)");

  return g_string_free (g_steal_pointer (&string), FALSE);
}

/* FIXME: Flatpak doesn’t have any concept of installing from a collection-id
 * right now, but to future proof the file format against the upcoming change
 * we need to simulate that in the autoinstall file. We can’t use the conventional
 * method of ostree_repo_find_remotes_async() since this code does not have
 * network access. Instead, we have to be a little more naive and hope that
 * the collection ID we’re after is specified in at least one remote configuration
 * on the underlying OSTree repo. */
gchar *
euu_lookup_flatpak_remote_for_collection_id (FlatpakInstallation  *installation,
                                             const gchar          *collection_id,
                                             GError              **error)
{
  g_autoptr(GFile) installation_directory = flatpak_installation_get_path (installation);
  g_autoptr(GFile) repo_directory = g_file_get_child (installation_directory, "repo");
  g_autoptr(OstreeRepo) repo = ostree_repo_new (repo_directory);
  g_auto(GStrv) remotes = NULL;
  GStrv iter = NULL;

  if (!ostree_repo_open (repo, NULL, error))
    return NULL;

  remotes = ostree_repo_remote_list (repo, NULL);

  for (iter = remotes; *iter != NULL; ++iter)
    {
      g_autofree gchar *remote_collection_id = NULL;

      if (!ostree_repo_get_remote_option (repo,
                                          *iter,
                                          "collection-id",
                                          NULL,
                                          &remote_collection_id,
                                          error))
        return NULL;

      if (g_strcmp0 (remote_collection_id, collection_id) == 0)
        return g_strdup (*iter);
    }

  g_set_error (error,
               G_IO_ERROR,
               G_IO_ERROR_NOT_FOUND,
               "Could not find remote with collection ID ‘%s’",
               collection_id);
  return NULL;
}

static const gchar *
get_datadir (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_OSTREE_DATADIR",
                                    DATADIR);
}

static GStrv
directories_to_search_from_environment (void)
{
  g_autofree gchar *ref_actions_path = g_build_filename (get_datadir (),
                                                         "eos-application-tools",
                                                         "flatpak-autoinstall.d",
                                                         NULL);
  g_autoptr(GFile) ref_actions_directory = g_file_new_for_path (ref_actions_path);
  const gchar *override_paths = euu_flatpak_autoinstall_override_paths ();
  g_autofree gchar *paths_to_search_string = g_strjoin (";", override_paths, ref_actions_path, NULL);
  return g_strsplit (paths_to_search_string, ";", -1);
}

GHashTable *
euu_flatpak_ref_actions_from_paths (GStrv    directories_to_search,
                                    GError **error)
{
  g_auto(GStrv) default_directories_to_search = NULL;
  GStrv iter = NULL;
  gint priority_counter = 0;
  g_autoptr(GHashTable) ref_actions = g_hash_table_new_full (g_str_hash,
                                                             g_str_equal,
                                                             g_free,
                                                             (GDestroyNotify) euu_flatpak_remote_ref_actions_file_free);

  if (directories_to_search == NULL)
    {
      default_directories_to_search = directories_to_search_from_environment ();
      directories_to_search = default_directories_to_search;
    }

  for (iter = directories_to_search; *iter != NULL; ++iter, ++priority_counter)
    {
      g_autoptr(GFile) directory = g_file_new_for_path (*iter);
      if (!euu_flatpak_ref_actions_append_from_directory (directory,
                                                          ref_actions,
                                                          priority_counter,
                                                          TRUE,  /* ignore ENOENT */
                                                          NULL,
                                                          error))
        return NULL;
    }

  return euu_hoist_flatpak_remote_ref_actions (ref_actions);
}

static GFile *
get_temporary_directory_to_check_out_in (GError **error)
{
  g_autofree gchar *temp_dir = g_dir_make_tmp ("ostree-checkout-XXXXXX", error);
  g_autofree gchar *path = NULL;

  if (temp_dir == NULL)
    return NULL;

  path = g_build_filename (temp_dir, "checkout", NULL);
  return g_file_new_for_path (path);
}

static GFile *
inspect_directory_in_ostree_repo (OstreeRepo    *repo,
                                  const gchar   *checksum,
                                  const gchar   *subpath,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
  g_autofree gchar *checkout_directory_path = NULL;
  g_autoptr(GFile) checkout_directory = NULL;
  OstreeRepoCheckoutAtOptions options = { 0, };

  checkout_directory = get_temporary_directory_to_check_out_in (error);

  if (!checkout_directory)
    return NULL;

  checkout_directory_path = g_file_get_path (checkout_directory);

  /* Now that we have a temporary directory, checkout the OSTree in it
   * at the nominated path */
  options.subpath = subpath;

  if (!ostree_repo_checkout_at (repo,
                                &options,
                                -1,
                                checkout_directory_path,
                                checksum,
                                cancellable,
                                error))
    {
      eos_updater_remove_recursive (checkout_directory, NULL);
      return NULL;
    }

  return g_steal_pointer (&checkout_directory);
}

GHashTable *
euu_flatpak_ref_actions_from_ostree_commit (OstreeRepo    *repo,
                                            const gchar   *checksum,
                                            GCancellable  *cancellable,
                                            GError       **error)
{
  g_autoptr(GFile) checkout_directory = NULL;
  const gchar *path_relative_to_deployment = "usr/share/eos-application-tools/flatpak-autoinstall.d";
  g_autoptr(GHashTable) flatpak_ref_actions_table = NULL;
  const gchar *override_paths = euu_flatpak_autoinstall_override_paths ();
  const gchar *paths_to_search_string = NULL;
  g_autofree gchar *allocated_paths_to_search_string = NULL;
  g_auto(GStrv) paths_to_search = NULL;
  g_autoptr(GError) local_error = NULL;

  /* Now that we have a temporary directory, checkout the OSTree in it
   * at the /usr/share/eos-application-tools path. If it fails, there’s nothing to
   * read, otherwise we can read in the list of flatpaks to be auto-installed
   * for this commit. */
  checkout_directory = inspect_directory_in_ostree_repo (repo,
                                                         checksum,
                                                         path_relative_to_deployment,
                                                         cancellable,
                                                         &local_error);

  if (checkout_directory != NULL)
    {
      g_autofree gchar *checkout_directory_path = g_file_get_path (checkout_directory);

      /* Checkout directory has the lowest priority, if it is specified */
      allocated_paths_to_search_string = g_strjoin (";", override_paths, checkout_directory_path, NULL);
      paths_to_search_string = allocated_paths_to_search_string;
    }
  else
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      paths_to_search_string = override_paths;
    }

  paths_to_search = g_strsplit (paths_to_search_string, ";", -1);
  flatpak_ref_actions_table = euu_flatpak_ref_actions_from_paths (paths_to_search,
                                                                  error);

  /* Regardless of whether there was an error, we always want to remove
   * the checkout directory at this point and garbage-collect on the
   * OstreeRepo. Note that these operations may fail, but we don’t
   * really care. */
  if (checkout_directory != NULL)
    eos_updater_remove_recursive (checkout_directory, NULL);
  ostree_repo_checkout_gc (repo, cancellable, NULL);

  return g_steal_pointer (&flatpak_ref_actions_table);
}

FlatpakInstallation *
eos_updater_get_flatpak_installation (GCancellable *cancellable, GError **error)
{
  const gchar *override_path = g_getenv ("EOS_UPDATER_TEST_FLATPAK_INSTALLATION_DIR");

  if (override_path != NULL)
    {
      g_autoptr(GFile) override = g_file_new_for_path (override_path);
      return flatpak_installation_new_for_path (override, TRUE, cancellable, error);
    }

  return flatpak_installation_new_system (cancellable, error);
}
