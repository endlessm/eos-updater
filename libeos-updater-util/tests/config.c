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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libeos-updater-util/config.h>
#include <libeos-updater-util/tests/resources.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct
{
  gchar *tmp_dir;
  gchar *key_file1_path;
  gchar *key_file2_path;
  gchar *key_file_nonexistent_path;
  gchar *key_file_unreadable_path;
  gchar *key_file_invalid_path;

  GResource *default_resource;  /* unowned */
  const gchar *default_path;
  const gchar *default_path_invalid;
} Fixture;

/* Set up a temporary directory with various test configuration files in. */
static void
setup (Fixture       *fixture,
       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;

  fixture->tmp_dir = g_dir_make_tmp ("eos-updater-util-tests-config-XXXXXX",
                                     &error);
  g_assert_no_error (error);

  fixture->key_file1_path = g_build_filename (fixture->tmp_dir, "key-file1",
                                              NULL);
  g_file_set_contents (fixture->key_file1_path,
                       "[Test]\n"
                       "File=1\n"
                       "File1=true\n"
                       "[Group1]\n",
                       -1, &error);
  g_assert_no_error (error);

  fixture->key_file2_path = g_build_filename (fixture->tmp_dir, "key-file2",
                                              NULL);
  g_file_set_contents (fixture->key_file2_path,
                       "[Test]\n"
                       "File=2\n"
                       "File2=true\n"
                       "[Group2]\n",
                       -1, &error);
  g_assert_no_error (error);

  fixture->key_file_nonexistent_path = g_build_filename (fixture->tmp_dir,
                                                         "key-file-nonexistent",
                                                         NULL);

  fixture->key_file_unreadable_path = g_build_filename (fixture->tmp_dir,
                                                        "key-file-unreadable",
                                                        NULL);
  g_file_set_contents (fixture->key_file_unreadable_path, "[Test]\nFile=3",
                       -1, &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_chmod (fixture->key_file_unreadable_path, 0200), ==, 0);

  fixture->key_file_invalid_path = g_build_filename (fixture->tmp_dir,
                                                     "key-file-invalid",
                                                     NULL);
  g_file_set_contents (fixture->key_file_invalid_path, "really not valid", -1,
                       &error);
  g_assert_no_error (error);

  fixture->default_resource = euu_tests_resources_get_resource ();
  fixture->default_path = "/com/endlessm/Updater/config/config-test.conf";
  fixture->default_path_invalid = "/com/endlessm/Updater/config/config-test-invalid.conf";
}

static void
unlink_and_free (gchar **path)
{
  if (*path != NULL)
    g_assert_cmpint (g_unlink (*path), ==, 0);
  g_clear_pointer (path, g_free);
}

/* Inverse of setup(). */
static void
teardown (Fixture       *fixture,
          gconstpointer  user_data G_GNUC_UNUSED)
{
  unlink_and_free (&fixture->key_file_invalid_path);
  unlink_and_free (&fixture->key_file_unreadable_path);
  g_free (fixture->key_file_nonexistent_path);
  unlink_and_free (&fixture->key_file2_path);
  unlink_and_free (&fixture->key_file1_path);

  g_assert_cmpint (g_rmdir (fixture->tmp_dir), ==, 0);
  g_free (fixture->tmp_dir);
}

/* Test that loading a single configuration file works. */
static void
test_config_file_load_one (Fixture       *fixture,
                           gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file1_path,
      NULL
    };
  guint loaded_file;

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  loaded_file = euu_config_file_get_uint (config, "Test", "File", 0, G_MAXUINT, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (loaded_file, ==, 1);
}

/* Test that priority ordering of configuration files works. */
static void
test_config_file_load_many (Fixture       *fixture,
                            gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file1_path,
      fixture->key_file2_path,
      NULL
    };
  guint loaded_file;

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  loaded_file = euu_config_file_get_uint (config, "Test", "File", 0, G_MAXUINT, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (loaded_file, ==, 1);
}

/* Test that error reporting from an unreadable file reports an error. */
static void
test_config_file_unreadable (Fixture       *fixture,
                             gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  g_autofree gchar *temp = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file_nonexistent_path,
      fixture->key_file_unreadable_path,
      fixture->key_file1_path,
      NULL
    };

  /* If the test is run as root (or another user with CAP_DAC_OVERRIDE), the
   * user can read any file anyway. */
  if (g_file_get_contents (fixture->key_file_unreadable_path, &temp, NULL, NULL))
    {
      g_test_skip ("Test cannot be run as a user with CAP_DAC_OVERRIDE or "
                   "CAP_DAC_READ_SEARCH.");
      return;
    }

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  euu_config_file_get_uint (config, "Any", "Thing", 0, G_MAXUINT, &error);
  g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_ACCES);
}

/* Test that error reporting from an invalid file reports an error. */
static void
test_config_file_invalid (Fixture       *fixture,
                          gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file_invalid_path,
      fixture->key_file1_path,
      NULL
    };

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  euu_config_file_get_uint (config, "Any", "Thing", 0, G_MAXUINT, &error);
  g_assert_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE);
}

/* Test that multiple non-existent paths are handled correctly. */
static void
test_config_file_nonexistent (Fixture       *fixture,
                              gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file_nonexistent_path,
      fixture->key_file_nonexistent_path,
      fixture->key_file_nonexistent_path,
      fixture->key_file_nonexistent_path,
      fixture->key_file1_path,
      NULL
    };
  guint loaded_file;

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  loaded_file = euu_config_file_get_uint (config, "Test", "File", 0, G_MAXUINT, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (loaded_file, ==, 1);
}

/* Test that if none of the files exist, but the GResource does, we successfully
 * use that.. */
static void
test_config_file_resource_only (Fixture       *fixture,
                                gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file_nonexistent_path,
      NULL
    };
  guint loaded_file;

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  loaded_file = euu_config_file_get_uint (config, "Test", "File", 0, G_MAXUINT, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (loaded_file, ==, 1000);
}

/* Test that if no configuration files are found, we abort. */
static void
test_config_file_fallback_per_file (Fixture       *fixture,
                                    gconstpointer  user_data G_GNUC_UNUSED)
{
  const gchar * const paths[] =
    {
      fixture->key_file_nonexistent_path,
      fixture->key_file_nonexistent_path,
      NULL
    };

  if (g_test_subprocess ())
    {
      g_autoptr(EuuConfigFile) config = NULL;

      config = euu_config_file_new (paths, fixture->default_resource,
                                    fixture->default_path_invalid);
      g_assert_not_reached ();
    }
  else
    {
      g_test_trap_subprocess (NULL, 0, 0);
      g_test_trap_assert_failed ();
      g_test_trap_assert_stderr ("*ERROR*euu_config_file_constructed: "
                                 "assertion failed (error == NULL)*");
    }
}

/* Test that loading a key from the second file works if it’s not set in the
 * first. */
static void
test_config_file_fallback_per_key (Fixture       *fixture,
                                   gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file1_path,
      fixture->key_file2_path,
      NULL
    };
  guint loaded_file;
  gboolean file1_key, file2_key;

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  loaded_file = euu_config_file_get_uint (config, "Test", "File", 0, G_MAXUINT, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (loaded_file, ==, 1);

  file1_key = euu_config_file_get_boolean (config, "Test", "File1", &error);
  g_assert_no_error (error);
  g_assert_true (file1_key);

  file2_key = euu_config_file_get_boolean (config, "Test", "File2", &error);
  g_assert_no_error (error);
  g_assert_true (file2_key);
}

/* Test that the groups from all loaded files are returned. */
static void
test_config_file_groups (Fixture       *fixture,
                         gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EuuConfigFile) config = NULL;
  const gchar * const paths[] =
    {
      fixture->key_file1_path,
      fixture->key_file2_path,
      NULL
    };
  g_auto(GStrv) groups = NULL;
  gsize n_groups;

  config = euu_config_file_new (paths, fixture->default_resource, fixture->default_path);

  groups = euu_config_file_get_groups (config, &n_groups, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (n_groups, ==, 4);
  g_assert_nonnull (groups);
  g_assert_cmpstr (groups[0], ==, "DefaultGroup");
  g_assert_cmpstr (groups[1], ==, "Group1");
  g_assert_cmpstr (groups[2], ==, "Group2");
  g_assert_cmpstr (groups[3], ==, "Test");
  g_assert_null (groups[4]);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  g_test_add ("/config/load-one", Fixture, NULL, setup,
              test_config_file_load_one, teardown);
  g_test_add ("/config/load-many", Fixture, NULL, setup,
              test_config_file_load_many, teardown);
  g_test_add ("/config/unreadable", Fixture, NULL, setup,
              test_config_file_unreadable, teardown);
  g_test_add ("/config/invalid", Fixture, NULL, setup,
              test_config_file_invalid, teardown);
  g_test_add ("/config/nonexistent", Fixture, NULL, setup,
              test_config_file_nonexistent, teardown);
  g_test_add ("/config/resource-only", Fixture, NULL, setup,
              test_config_file_resource_only, teardown);
  g_test_add ("/config/fallback/per-file", Fixture, NULL, setup,
              test_config_file_fallback_per_file, teardown);
  g_test_add ("/config/fallback/per-key", Fixture, NULL, setup,
              test_config_file_fallback_per_key, teardown);
  g_test_add ("/config/groups", Fixture, NULL, setup,
              test_config_file_groups, teardown);

  return g_test_run ();
}
