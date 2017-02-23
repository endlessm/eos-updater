/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
 * Copyright © 2017 Endless Mobile, Inc.
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
 * Authors:
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <libeos-updater-util/config.h>
#include <string.h>

/**
 * eos_updater_load_config_file:
 * @key_file_paths: (transfer none) (array zero-terminated=1): priority list of
 *     paths to try loading, most important first; %NULL-terminated
 * @error: return location for a #GError
 *
 * Load a configuration file from one of a number of paths, trying them in
 * order until one of the files exists. If one of the files exists, but there
 * is an error in loading it (for example, it contains invalid syntax), that
 * error will be returned; the next file in @key_file_paths will not be loaded.
 *
 * There must be at least one path in @key_file_paths, and at least one of the
 * paths in @key_file_paths must be guaranteed to exist (for example, as a
 * default configuration file installed by the package).
 *
 * Returns: (transfer full): loaded configuration file
 */
GKeyFile *
eos_updater_load_config_file (const gchar * const  *key_file_paths,
                              GError              **error)
{
  g_autoptr(GKeyFile) config = NULL;
  g_autoptr(GError) local_error = NULL;
  gsize i;

  g_return_val_if_fail (key_file_paths != NULL, NULL);
  g_return_val_if_fail (key_file_paths[0] != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  config = g_key_file_new ();

  /* Try the files in order. */
  for (i = 0; key_file_paths[i] != NULL; i++)
    {
      g_key_file_load_from_file (config, key_file_paths[i], G_KEY_FILE_NONE,
                                 &local_error);

      if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT) &&
          key_file_paths[i + 1] != NULL)
        {
          g_debug ("Configuration file ‘%s’ not found. Trying next path ‘%s’.",
                   key_file_paths[i], key_file_paths[i + 1]);
          g_clear_error (&local_error);
          continue;
        }
      else if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          g_error ("Configuration file ‘%s’ not found. The program is not "
                   "installed correctly.", key_file_paths[i]);
          g_clear_error (&local_error);
          g_assert_not_reached ();
        }
      else if (local_error != NULL)
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }
      else
        {
          /* Successfully loaded a file. */
          return g_steal_pointer (&config);
        }
    }

  return config;
}
