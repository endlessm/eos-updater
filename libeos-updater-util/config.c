/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <glib-object.h>
#include <libeos-updater-util/config.h>
#include <stdlib.h>
#include <string.h>

/**
 * EuuConfigFile:
 *
 * This represents a configuration file, loaded from one or more layered
 * configuration files following the same schema. For each schema, there must
 * always be one canonical copy of the configuration file installed in a
 * read-only location on the system, which is passed as the final path to
 * euu_config_file_new(); ultimately, default values are loaded from this.
 *
 * When queried for keys, an #EuuConfigFile instance will return the value from
 * the first configuration file in its hierarchy which contains that key.
 * If an administrator wishes to override a value from a lower configuration
 * file, they must do so explicitly in a higher one.
 *
 * When listing groups, an #EuuConfigFile will return the deduplicated union
 * of all the groups in all of its hierarchy of configuration files. When
 * overriding a group of keys, the entire group must be copied from one
 * configuration file to a higher one; otherwise queries for some keys will fall
 * back to the lower configuration file.
 *
 * Since: UNRELEASED
 */
struct _EuuConfigFile
{
  GObject parent_instance;

  gchar **paths;  /* (array length=n_paths); final element is always the default path */
  gsize n_paths;
  GPtrArray *key_files;  /* (element-type GKeyFile); same indexing as paths */
};

G_DEFINE_TYPE (EuuConfigFile, euu_config_file, G_TYPE_OBJECT)

typedef enum
{
  PROP_PATHS = 1,
} EuuConfigFileProperty;

static GParamSpec *props[PROP_PATHS + 1] = { NULL, };

static void
euu_config_file_init (EuuConfigFile *self)
{
  self->key_files = g_ptr_array_new_with_free_func ((GDestroyNotify) g_key_file_unref);
}

static void
euu_config_file_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *spec)
{
  EuuConfigFile *self = EUU_CONFIG_FILE (object);

  switch ((EuuConfigFileProperty) property_id)
    {
    case PROP_PATHS:
      g_value_set_boxed (value, self->paths);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
euu_config_file_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *spec)
{
  EuuConfigFile *self = EUU_CONFIG_FILE (object);

  switch ((EuuConfigFileProperty) property_id)
    {
    case PROP_PATHS:
      /* Construct-only; must be non-empty */
      self->paths = g_value_dup_boxed (value);
      g_assert (self->paths != NULL);
      self->n_paths = g_strv_length (self->paths);
      g_assert (self->n_paths > 0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
euu_config_file_finalize (GObject *object)
{
  EuuConfigFile *self = EUU_CONFIG_FILE (object);

  g_clear_pointer (&self->paths, g_strfreev);
  g_clear_pointer (&self->key_files, g_ptr_array_unref);

  G_OBJECT_CLASS (euu_config_file_parent_class)->finalize (object);
}

static void
euu_config_file_class_init (EuuConfigFileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = euu_config_file_finalize;
  object_class->get_property = euu_config_file_get_property;
  object_class->set_property = euu_config_file_set_property;

  /**
   * EuuConfigFile:paths:
   *
   * Ordered collection of paths of configuration files to load. This must
   * always contain at least one element; the final element in the collection is
   * treated as the default configuration file.
   *
   * Since: UNRELEASED
   */
  props[PROP_PATHS] = g_param_spec_boxed ("paths",
                                          "Paths",
                                          "Ordered collection of paths of "
                                          "configuration files to load.",
                                          G_TYPE_STRV,
                                          G_PARAM_READWRITE |
                                          G_PARAM_CONSTRUCT_ONLY |
                                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class,
                                     G_N_ELEMENTS (props),
                                     props);
}

/**
 * euu_config_file_new:
 * @key_file_paths: (array zero-terminated=1): %NULL-terminated ordered
 *    collection of paths of configuration files to load; must be non-empty
 *
 * Create a new #EuuConfigFile representing the configuration loaded from the
 * given collection of @key_file_paths, which must all follow the same schema.
 * @key_file_paths must contain at least one element; its final element is
 * treated as the default configuration file containing all default values.
 *
 * This function does no file I/O.
 *
 * Returns: (transfer full): a newly allocated #EuuConfigFile
 * Since: UNRELEASED
 */
EuuConfigFile *
euu_config_file_new (const gchar * const *key_file_paths)
{
  g_return_val_if_fail (key_file_paths != NULL, NULL);
  g_return_val_if_fail (key_file_paths[0] != NULL, NULL);

  return g_object_new (EUU_TYPE_CONFIG_FILE,
                       "paths", key_file_paths,
                       NULL);
}

static gboolean
euu_config_file_ensure_loaded (EuuConfigFile  *self,
                               gsize           idx,
                               GKeyFile      **key_file_out,
                               GError        **error)
{
  g_autoptr(GError) local_error = NULL;
  const gchar *path = self->paths[idx];
  gboolean is_default = (idx == self->n_paths - 1);
  GKeyFile *key_file = NULL;

  if (idx < self->key_files->len)
    key_file = g_ptr_array_index (self->key_files, idx);

  if (key_file == NULL)
    {
      key_file = g_key_file_new ();
      g_ptr_array_insert (self->key_files, idx, key_file);
      g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &local_error);
    }

  if (!is_default &&
      g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    {
      /* File doesn’t exist. Don’t propagate the error. */
      g_debug ("Configuration file ‘%s’ not found.", path);
      /* fall through */
    }
  else if (local_error != NULL)
    {
      /* The default config file should always be loadable, as it should be
       * installed as part of the package. */
      if (is_default)
        {
          g_error ("Configuration file ‘%s’ not found. The program is not "
                   "installed correctly.");
          g_assert_not_reached ();
        }
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (key_file_out != NULL)
    *key_file_out = key_file;

  return TRUE;
}

static gboolean
euu_config_file_get_file_for_key (EuuConfigFile  *self,
                                  const gchar    *group_name,
                                  const gchar    *key_name,
                                  GKeyFile      **key_file_out,
                                  const gchar   **path_out,
                                  GError        **error)
{
  GKeyFile *key_file;
  const gchar *path;
  gsize i;

  for (i = 0; i < self->n_paths; i++)
    {
      path = self->paths[i];

      if (!euu_config_file_ensure_loaded (self, i, &key_file, error))
        return FALSE;

      /* Try and find the key in this file. */
      if (g_key_file_has_key (key_file, group_name, key_name, NULL))
        break;
    }

  /* Not found? */
  if (i >= self->n_paths)
    {
      key_file = NULL;
      path = NULL;
    }

  if (key_file_out != NULL)
    *key_file_out = key_file;
  if (path_out != NULL)
    *path_out = path;

  return TRUE;
}

/**
 * euu_config_file_get_uint:
 * @self: an #EuuConfigFile
 * @group_name: name of the configuration group
 * @key_name: name of the configuration key
 * @min_value: minimum valid value (inclusive)
 * @max_value: maximum valid value (inclusive)
 * @error: return location for a #GError, or %NULL
 *
 * Load an unsigned integer value from the configuration, and validate that it
 * lies in [@min_value, @max_value]. The given key must exist in the default
 * configuration file, if not in any others. It will be loaded from the first
 * configuration file which contains it.
 *
 * If the loaded value does not validate, %G_KEY_FILE_ERROR_INVALID_VALUE is
 * returned.
 *
 * Returns: the loaded unsigned integer
 * Since: UNRELEASED
 */
guint
euu_config_file_get_uint (EuuConfigFile  *self,
                          const gchar    *group_name,
                          const gchar    *key_name,
                          guint           min_value,
                          guint           max_value,
                          GError        **error)
{
  g_autoptr(GError) local_error = NULL;
  GKeyFile *key_file;
  guint64 val;
  const gchar *path;

  g_return_val_if_fail (EUU_IS_CONFIG_FILE (self), 0);
  g_return_val_if_fail (group_name != NULL, 0);
  g_return_val_if_fail (key_name != NULL, 0);
  g_return_val_if_fail (min_value <= max_value, 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  if (!euu_config_file_get_file_for_key (self, group_name, key_name, &key_file, &path, error))
    return 0;
  g_assert (key_file != NULL);

  val = g_key_file_get_uint64 (key_file, group_name, key_name, &local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return 0;
    }

  if (val < min_value || val > max_value)
    {
      g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                   "Integer value %" G_GUINT64_FORMAT " for key ‘%s/%s’ in "
                   "configuration file ‘%s’ outside valid range [%u, %u].",
                   val, group_name, key_name, path, min_value, max_value);
      return 0;
    }

  return val;
}

/**
 * euu_config_file_get_boolean:
 * @self: an #EuuConfigFile
 * @group_name: name of the configuration group
 * @key_name: name of the configuration key
 * @error: return location for a #GError, or %NULL
 *
 * Load a boolean value from the configuration. The given key must exist in the
 * default configuration file, if not in any others. It will be loaded from the
 * first configuration file which contains it.
 *
 * Returns: the loaded boolean
 * Since: UNRELEASED
 */
gboolean
euu_config_file_get_boolean (EuuConfigFile  *self,
                             const gchar    *group_name,
                             const gchar    *key_name,
                             GError        **error)
{
  GKeyFile *key_file;

  g_return_val_if_fail (EUU_IS_CONFIG_FILE (self), FALSE);
  g_return_val_if_fail (group_name != NULL, FALSE);
  g_return_val_if_fail (key_name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!euu_config_file_get_file_for_key (self, group_name, key_name, &key_file, NULL, error))
    return FALSE;
  g_assert (key_file != NULL);

  return g_key_file_get_boolean (key_file, group_name, key_name, error);
}

/**
 * euu_config_file_get_boolean:
 * @self: an #EuuConfigFile
 * @group_name: name of the configuration group
 * @key_name: name of the configuration key
 * @error: return location for a #GError, or %NULL
 *
 * Load a string value from the configuration. The given key must exist in the
 * default configuration file, if not in any others. It will be loaded from the
 * first configuration file which contains it.
 *
 * Returns: (transfer full): the loaded string, which is guaranteed to be
 *    non-%NULL but may be empty
 * Since: UNRELEASED
 */
gchar *
euu_config_file_get_string (EuuConfigFile  *self,
                            const gchar    *group_name,
                            const gchar    *key_name,
                            GError        **error)
{
  GKeyFile *key_file;

  g_return_val_if_fail (EUU_IS_CONFIG_FILE (self), NULL);
  g_return_val_if_fail (group_name != NULL, NULL);
  g_return_val_if_fail (key_name != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!euu_config_file_get_file_for_key (self, group_name, key_name, &key_file, NULL, error))
    return NULL;
  g_assert (key_file != NULL);

  return g_key_file_get_string (key_file, group_name, key_name, error);
}

/**
 * euu_config_file_get_strv:
 * @self: an #EuuConfigFile
 * @group_name: name of the configuration group
 * @key_name: name of the configuration key
 * @n_elements_out: (out caller-allocates) (optional): return location for the
 *    number of elements in the string array (not including the terminating
 *    %NULL), or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Load a string array value from the configuration. The given key must exist
 * in the default configuration file, if not in any others. It will be loaded
 * from the first configuration file which contains it.
 *
 * Returns: (transfer full) (array zero-terminated=1) (array length=n_elements_out):
 *    the loaded string array, which is guaranteed to be non-%NULL but may be
 *    empty
 * Since: UNRELEASED
 */
gchar **
euu_config_file_get_strv (EuuConfigFile  *self,
                          const gchar    *group_name,
                          const gchar    *key_name,
                          gsize          *n_elements_out,
                          GError        **error)
{
  GKeyFile *key_file;

  g_return_val_if_fail (EUU_IS_CONFIG_FILE (self), NULL);
  g_return_val_if_fail (group_name != NULL, NULL);
  g_return_val_if_fail (key_name != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!euu_config_file_get_file_for_key (self, group_name, key_name, &key_file, NULL, error))
    return NULL;
  g_assert (key_file != NULL);

  return g_key_file_get_string_list (key_file, group_name, key_name, n_elements_out, error);
}

static int
strcmp_p (const void *p1,
          const void *p2)
{
  /* qsort() passes us pointer to the (gchar*)s. */
  return strcmp (*((char * const *) p1), *((char * const *) p2));
}

/**
 * euu_config_file_get_groups:
 * @self: an #EuuConfigFile
 * @n_groups_out: (out caller-allocates) (optional): return location for the
 *    number of groups returned (not including the terminating %NULL), or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * List the groups from all the configuration files, eliminating duplicates.
 * Empty groups are included in the list. The list is sorted lexicographically.
 *
 * Returns: (transfer full) (array zero-terminated=1) (array length=n_groups_out):
 *    the groups in the configuration files, which is guaranteed to be non-%NULL
 *    but may be empty
 * Since: UNRELEASED
 */
gchar **
euu_config_file_get_groups (EuuConfigFile  *self,
                            gsize          *n_groups_out,
                            GError        **error)
{
  gsize i;
  g_autoptr(GHashTable) groups = NULL;
  g_auto(GStrv) groups_array = NULL;
  gsize groups_array_len;

  g_return_val_if_fail (EUU_IS_CONFIG_FILE (self), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (i = 0; i < self->n_paths; i++)
    {
      GKeyFile *key_file;
      g_auto(GStrv) file_groups = NULL;
      gsize j;

      if (!euu_config_file_ensure_loaded (self, i, &key_file, error))
        return NULL;

      /* Get and deduplicate the groups for this file. */
      file_groups = g_key_file_get_groups (key_file, NULL);

      for (j = 0; file_groups != NULL && file_groups[j] != NULL; j++)
        g_hash_table_add (groups, g_steal_pointer (&file_groups[j]));
    }

  /* Convert to an array, sort and NULL terminate. */
  groups_array = (gchar **) g_hash_table_get_keys_as_array (groups, NULL);
  groups_array_len = g_hash_table_size (groups);
  g_hash_table_steal_all (groups);

  qsort (groups_array, groups_array_len, sizeof (*groups_array), strcmp_p);
  groups_array = g_realloc_n (g_steal_pointer (&groups_array),
                              groups_array_len + 1, sizeof (*groups_array));
  groups_array[groups_array_len] = NULL;

  if (n_groups_out != NULL)
    *n_groups_out = groups_array_len;

  return g_steal_pointer (&groups_array);
}
