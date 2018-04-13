/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include <libeos-updater-util/util.h>

#include <test-common/gpg.h>
#include <test-common/misc-utils.h>
#include <test-common/spawn-utils.h>

#include <errno.h>

#ifndef GPG_BINARY
#error "GPG_BINARY is not defined"
#endif

GFile *
create_gpg_keys_directory (GFile       *containing_directory,
                           const gchar *source_gpg_home_path)
{
  gsize i;
  const gchar * const gpg_home_files[] =
    {
      "C1EB8F4E.asc",
      "keyid",
      "pubring.gpg",
      "random_seed",
      "secring.gpg",
    };
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile)  gpg_home = g_file_get_child (containing_directory, "gpghome");

  g_file_make_directory (gpg_home, NULL, &error);
  g_assert_no_error (error);

  g_file_set_attribute_uint32 (gpg_home, G_FILE_ATTRIBUTE_UNIX_MODE,
                               0700, G_FILE_QUERY_INFO_NONE, NULL, &error);
  g_assert_no_error (error);

  /* Copy the GPG files from the source directory into the fixture directory,
   * as running GPG with them as its homedir might alter them; we don’t want
   * that to happen in the source directory, which might be read-only (and in
   * any case, we want determinism). */
  for (i = 0; i < G_N_ELEMENTS (gpg_home_files); i++)
    {
      g_autofree gchar *source_path = NULL;
      g_autoptr (GFile) source = NULL, destination = NULL;

      source_path = g_build_filename (source_gpg_home_path,
                                      gpg_home_files[i],
                                      NULL);
      source = g_file_new_for_path (source_path);
      destination = g_file_get_child (gpg_home, gpg_home_files[i]);

      g_file_copy (source, destination,
                   G_FILE_COPY_NONE, NULL, NULL, NULL,
                   &error);
      g_assert_no_error (error);

      g_file_set_attribute_uint32 (destination, G_FILE_ATTRIBUTE_UNIX_MODE,
                                   0600, G_FILE_QUERY_INFO_NONE, NULL, &error);
      g_assert_no_error (error);
    }
  
  return g_steal_pointer (&gpg_home);
}

gchar *
get_keyid (GFile *gpg_home)
{
  g_autoptr(GFile) keyid = g_file_get_child (gpg_home, "keyid");
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GError) error = NULL;
  gsize len;

  load_to_bytes (keyid, &bytes, &error);
  g_assert_no_error (error);
  len = g_bytes_get_size (bytes);
  g_assert (len == 8);

  return g_strndup (g_bytes_get_data (bytes, NULL), len);
}

void
kill_gpg_agent (GFile *gpg_home)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *gpg_home_path = g_file_get_path (gpg_home);
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  const gchar *argv[] =
    {
      "gpg-connect-agent",
      "--homedir",
      gpg_home_path,
      "killagent",
      "/bye",
      NULL
    };

  /* kill the gpg-agent in order because if too many get spawned, it will
   * result in connections getting refused... */
  if (!test_spawn ((const gchar * const *) argv, NULL, &cmd, &error) ||
      !cmd_result_ensure_ok (&cmd, &error))
    {
      g_warning ("Failed to kill gpg-agent %s: %s", gpg_home_path,
                 error->message);
      g_clear_error (&error);
    }
}

GFile *
get_gpg_key_file_for_keyid (GFile *gpg_home, const gchar *keyid)
{
  g_autofree gchar *filename = g_strdup_printf ("%s.asc", keyid);

  return g_file_get_child (gpg_home, filename);
}