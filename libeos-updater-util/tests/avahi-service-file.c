/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libeos-updater-util/avahi-service-file.h>
#include <locale.h>
#include <string.h>

typedef struct
{
  gchar *tmp_dir;
  GDateTime *example_timestamp;  /* owned */
} Fixture;

/* Set up a temporary directory to create test service files in. */
static void
setup (Fixture       *fixture,
       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;

  fixture->tmp_dir = g_dir_make_tmp ("eos-updater-util-tests-avahi-service-file-XXXXXX",
                                     &error);
  g_assert_no_error (error);

  fixture->example_timestamp = g_date_time_new_utc (2017, 2, 17, 0, 0, 0);
}

/* Inverse of setup(). */
static void
teardown (Fixture       *fixture,
          gconstpointer  user_data G_GNUC_UNUSED)
{
  g_date_time_unref (fixture->example_timestamp);

  g_assert_cmpint (g_rmdir (fixture->tmp_dir), ==, 0);
  g_free (fixture->tmp_dir);
}

/* Check the contents of a generated .service file. */
static void
assert_service_file_contents_valid (const gchar *service_file)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *contents = NULL;
  gsize length;

  g_file_get_contents (service_file, &contents, &length, &error);
  g_assert_no_error (error);

  /* The eos_head_commit_timestamp value is the UNIX timestamp version of
   * fixture->example_timestamp. */
  g_assert_cmpstr (contents, ==,
                   "<service-group>\n"
                   "  <name replace-wildcards=\"yes\">EOS update service on %h</name>\n"
                   "  <service>\n"
                   "    <type>_eos_updater._tcp</type>\n"
                   "    <port>" G_STRINGIFY (EOS_AVAHI_PORT) "</port>\n"
                   "    <txt-record>eos_txt_version=1</txt-record>\n"
                   "    <txt-record>eos_ostree_path=ostree-path</txt-record>\n"
                   "    <txt-record>eos_head_commit_timestamp=1487289600</txt-record>\n"
                   "  </service>\n"
                   "</service-group>\n");
  g_assert_cmpuint (length, ==, strlen (contents));
}

/* Test that generating a .service file in an empty directory works. */
static void
test_avahi_service_file_generate_normal (Fixture       *fixture,
                                         gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Generate the file. */
  retval = eos_avahi_service_file_generate (fixture->tmp_dir, "ostree-path",
                                            fixture->example_timestamp,
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  /* Check its contents. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  assert_service_file_contents_valid (service_file);

  /* Tidy up. */
  g_assert_cmpint (g_unlink (service_file), ==, 0);
}

/* Test that generating a .service file in a directory which already contains
 * one overwrites the existing one. */
static void
test_avahi_service_file_generate_overwrite (Fixture       *fixture,
                                            gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Create a stub file. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service", NULL);
  g_file_set_contents (service_file, "overwrite me!", -1, &error);
  g_assert_no_error (error);

  /* Generate the file over the top. */
  retval = eos_avahi_service_file_generate (fixture->tmp_dir, "ostree-path",
                                            fixture->example_timestamp,
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  /* Check its contents. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  assert_service_file_contents_valid (service_file);

  /* Tidy up. */
  g_assert_cmpint (g_unlink (service_file), ==, 0);
}

/* Test that generating a .service file in a non-existent directory fails. */
static void
test_avahi_service_file_generate_nonexistent_directory (Fixture       *fixture,
                                                        gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  gboolean retval;

  subdirectory = g_build_filename (fixture->tmp_dir,
                                   "nonexistent-subdirectory",
                                   NULL);

  /* Try to generate a service file. */
  retval = eos_avahi_service_file_generate (subdirectory, "ostree-path",
                                            fixture->example_timestamp,
                                            NULL, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (retval);

  /* Directory should not have been created. */
  g_assert_false (g_file_test (subdirectory, G_FILE_TEST_EXISTS));
}

/* Test that generating a .service file in a directory we don’t have write
 * permission for fails. */
static void
test_avahi_service_file_generate_denied (Fixture       *fixture,
                                         gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  subdirectory = g_build_filename (fixture->tmp_dir,
                                   "unwriteable-subdirectory",
                                   NULL);
  g_assert_cmpint (g_mkdir (subdirectory, 0500), ==, 0);
  service_file = g_build_filename (subdirectory, "eos-updater.service", NULL);

  /* Try to generate a service file. */
  retval = eos_avahi_service_file_generate (subdirectory, "ostree-path",
                                            fixture->example_timestamp,
                                            NULL, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_assert_false (retval);

  /* File should not have been created. */
  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));

  /* Clean up. */
  g_assert_cmpint (g_chmod (subdirectory, 0700), ==, 0);
  g_assert_cmpint (g_rmdir (subdirectory), ==, 0);
}

/* Test that deleting an existing .service file works. */
static void
test_avahi_service_file_delete_normal (Fixture       *fixture,
                                       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Create a service file. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  g_file_set_contents (service_file, "irrelevant", -1, &error);
  g_assert_no_error (error);

  /* Try to delete it. */
  retval = eos_avahi_service_file_delete (fixture->tmp_dir, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  /* Actually gone? */
  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));
}

/* Test that deleting a non-existent .service file returns success. */
static void
test_avahi_service_file_delete_nonexistent_file (Fixture       *fixture,
                                                 gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Double-check no .service file exists. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));

  /* Try to delete it. */
  retval = eos_avahi_service_file_delete (fixture->tmp_dir, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
}

/* Test that deleting a .service file from a non-existent directory returns
 * success. */
static void
test_avahi_service_file_delete_nonexistent_directory (Fixture       *fixture,
                                                      gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  gboolean retval;

  /* Double-check the subdirectory doesn’t exist. */
  subdirectory = g_build_filename (fixture->tmp_dir, "some-subdirectory", NULL);
  g_assert_false (g_file_test (subdirectory, G_FILE_TEST_EXISTS));

  /* Try to delete from it. */
  retval = eos_avahi_service_file_delete (subdirectory, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
}

/* Test that deleting a .service file from a directory we don’t have write
 * permissions on fails. */
static void
test_avahi_service_file_delete_denied (Fixture       *fixture,
                                       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Create a service file. */
  subdirectory = g_build_filename (fixture->tmp_dir, "unwriteable-subdirectory",
                                   NULL);
  g_assert_cmpint (g_mkdir (subdirectory, 0700), ==, 0);

  service_file = g_build_filename (subdirectory, "eos-updater.service", NULL);
  g_file_set_contents (service_file, "irrelevant", -1, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_chmod (subdirectory, 0500), ==, 0);

  /* Try to delete it. */
  retval = eos_avahi_service_file_delete (subdirectory, NULL, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_assert_false (retval);

  /* Clean up. */
  g_assert_cmpint (g_chmod (subdirectory, 0700), ==, 0);
  g_assert_cmpint (g_unlink (service_file), ==, 0);
  g_assert_cmpint (g_rmdir (subdirectory), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/avahi-service-file/generate/normal", Fixture, NULL, setup,
              test_avahi_service_file_generate_normal, teardown);
  g_test_add ("/avahi-service-file/generate/overwrite", Fixture, NULL, setup,
              test_avahi_service_file_generate_overwrite, teardown);
  g_test_add ("/avahi-service-file/generate/nonexistent-directory", Fixture,
              NULL, setup,
              test_avahi_service_file_generate_nonexistent_directory, teardown);
  g_test_add ("/avahi-service-file/generate/denied", Fixture, NULL, setup,
              test_avahi_service_file_generate_denied, teardown);

  g_test_add ("/avahi-service-file/delete/normal", Fixture, NULL, setup,
              test_avahi_service_file_delete_normal, teardown);
  g_test_add ("/avahi-service-file/delete/nonexistent-file", Fixture, NULL,
              setup, test_avahi_service_file_delete_nonexistent_file, teardown);
  g_test_add ("/avahi-service-file/delete/nonexistent-directory", Fixture, NULL,
              setup, test_avahi_service_file_delete_nonexistent_directory,
              teardown);
  g_test_add ("/avahi-service-file/delete/denied", Fixture, NULL, setup,
              test_avahi_service_file_delete_denied, teardown);

  return g_test_run ();
}
