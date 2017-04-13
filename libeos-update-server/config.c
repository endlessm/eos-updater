/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <errno.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>
#include <libeos-update-server/config.h>
#include <libeos-updater-util/config.h>
#include <string.h>

/**
 * SECTION:config
 * @title: Configuration file parsing
 * @short_description: Config parser for eos-update-server.conf
 * @include: libeos-update-server/config.h
 *
 * Utility functions to parse the
 * [`eos-update-server.conf`](man:eos-update-server.conf(5)) configuration file
 * and return its contents in a structured form.
 *
 * For more information about the config file format and the locations it’s
 * looked for, see the [man page](man:eos-update-server.conf(5)).
 *
 * Since: UNRELEASED
 */

/* Paths for the configuration file. */
static const char *CONFIG_FILE_PATH = SYSCONFDIR "/" PACKAGE "/eos-update-server.conf";
static const char *STATIC_CONFIG_FILE_PATH = PKGDATADIR "/eos-update-server.conf";
static const char *LOCAL_CONFIG_FILE_PATH = PREFIX "/local/share/" PACKAGE "/eos-update-server.conf";

/* Configuration file keys. */
static const char *LOCAL_NETWORK_UPDATES_GROUP = "Local Network Updates";
static const char *ADVERTISE_UPDATES_KEY = "AdvertiseUpdates";

static const gchar *REPOSITORY_GROUP = "Repository ";  /* should be followed by an integer */
static const gchar *PATH_KEY = "Path";
static const gchar *REMOTE_NAME_KEY = "RemoteName";

/**
 * eus_repo_config_free:
 * @config: (transfer full): an #EusRepoConfig
 *
 * Free the given @config, which must be non-%NULL.
 *
 * Since: UNRELEASED
 */
void
eus_repo_config_free (EusRepoConfig *config)
{
  g_free (config->remote_name);
  g_free (config->path);
  g_free (config);
}

static EusRepoConfig *
eus_repo_config_new_steal (guint  index,
                           gchar *path,
                           gchar *remote_name)
{
  g_autoptr(EusRepoConfig) config = NULL;

  config = g_new0 (EusRepoConfig, 1);
  config->index = index;
  config->path = g_steal_pointer (&path);
  config->remote_name = g_steal_pointer (&remote_name);

  return g_steal_pointer (&config);
}

static gboolean
repository_configs_contains_index (GPtrArray *repository_configs,
                                   guint16    idx)
{
  gsize i;

  for (i = 0; i < repository_configs->len; i++)
    {
      const EusRepoConfig *config = g_ptr_array_index (repository_configs, i);

      if (config->index == idx)
        return TRUE;
    }

  return FALSE;
}

/**
 * eus_read_config_file:
 * @config_file_path: (nullable): path to the configuration file, or %NULL to
 *    use the system search paths
 * @out_advertise_updates: (out caller-allocates) (optional): return location
 *    for the `AdvertiseUpdates=` parameter
 * @out_repository_configs: (out callee-allocates) (transfer container)
 *    (element-type EusRepoConfig) (optional): return location for the
 *    `[Repository 0–9]` sections
 * @error: return location for a #GError, or %NULL
 *
 * Find and load the `eos-update-server.conf` configuration file. If
 * @config_file_path is non-%NULL, the file will be loaded from that path.
 * Otherwise, it will be loaded from the system search paths as documented in
 * [`eos-update-server.conf(5)`](man:eos-update-server.conf(5)).
 *
 * The configuration values loaded from the file will be returned in
 * @out_advertise_updates and @out_repository_configs. See
 * [`eos-update-server.conf(5)`](man:eos-update-server.conf(5)) for the
 * semantics of the options.
 *
 * Each #EusRepoConfig element in the returned @out_repository_configs array
 * contains the options from a single `[Repository 0–9]` section.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: UNRELEASED
 */
gboolean
eus_read_config_file (const gchar  *config_file_path,
                      gboolean     *out_advertise_updates,
                      GPtrArray   **out_repository_configs,
                      GError      **error)
{
  g_autoptr(GKeyFile) config = NULL;
  g_autoptr(GError) local_error = NULL;
  const gchar * const default_paths[] =
    {
      CONFIG_FILE_PATH,
      LOCAL_CONFIG_FILE_PATH,
      STATIC_CONFIG_FILE_PATH,
      NULL
    };
  const gchar * const override_paths[] =
    {
      config_file_path,
      NULL
    };
  g_auto(GStrv) groups = NULL;
  gsize n_groups, i;
  gboolean advertise_updates;
  g_autoptr(GPtrArray) repository_configs = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Try loading the files in order. If the user specified a configuration file
   * on the command line, use only that. Otherwise use the normal hierarchy. */
  config = eos_updater_load_config_file ((config_file_path != NULL) ? override_paths : default_paths,
                                         error);
  if (config == NULL)
    return FALSE;

  /* Successfully loaded a file. Parse it. */
  advertise_updates = g_key_file_get_boolean (config,
                                              LOCAL_NETWORK_UPDATES_GROUP,
                                              ADVERTISE_UPDATES_KEY,
                                              &local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  groups = g_key_file_get_groups (config, &n_groups);
  repository_configs = g_ptr_array_new_with_free_func ((GDestroyNotify) eus_repo_config_free);

  for (i = 0; i < n_groups; i++)
    {
      guint64 index;
      g_autofree gchar *repository_path = NULL, *remote_name = NULL;
      const gchar *end_ptr;

      if (!g_str_has_prefix (groups[i], REPOSITORY_GROUP))
        continue;

      errno = 0;
      index = g_ascii_strtoull (groups[i] + strlen (REPOSITORY_GROUP),
                                (gchar **) &end_ptr, 10);

      if (errno != 0 || end_ptr == NULL || *end_ptr != '\0' || index > G_MAXUINT)
        {
          g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                       "Invalid group name: %s", groups[i]);
          return FALSE;
        }

      repository_path = g_key_file_get_string (config, groups[i], PATH_KEY,
                                               error);

      if (repository_path == NULL)
        return FALSE;

      remote_name = g_key_file_get_string (config, groups[i], REMOTE_NAME_KEY,
                                           error);

      if (remote_name == NULL)
        return FALSE;

      if (repository_configs_contains_index (repository_configs, index))
        {
          g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                       "Duplicate group name: %s", groups[i]);
          return FALSE;
        }

      g_ptr_array_add (repository_configs,
                       eus_repo_config_new_steal (index,
                                                  g_steal_pointer (&repository_path),
                                                  g_steal_pointer (&remote_name)));
    }

  /* Success. */
  if (out_advertise_updates != NULL)
    *out_advertise_updates = advertise_updates;
  if (out_repository_configs != NULL)
    *out_repository_configs = g_steal_pointer (&repository_configs);

  return TRUE;
}
