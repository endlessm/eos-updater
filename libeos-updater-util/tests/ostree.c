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

#include <gio/gio.h>
#include <glib.h>
#include <libeos-updater-util/ostree.h>
#include <locale.h>
#include <ostree.h>

typedef struct
{
  GFile *tmp_dir;  /* owned */
  OstreeSysroot *sysroot;  /* owned */
} Fixture;

/* Set up a temporary directory with a loaded sysroot in. */
static void
setup (Fixture       *fixture,
       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *tmp_dir = NULL;

  tmp_dir = g_dir_make_tmp ("eos-updater-util-tests-ostree-XXXXXX", &error);
  g_assert_no_error (error);

  fixture->tmp_dir = g_file_new_for_path (tmp_dir);

  /* Set up the sysroot. */
  fixture->sysroot = ostree_sysroot_new (fixture->tmp_dir);

  ostree_sysroot_ensure_initialized (fixture->sysroot, NULL, &error);
  g_assert_no_error (error);

  ostree_sysroot_load (fixture->sysroot, NULL, &error);
  g_assert_no_error (error);

  /* Make the util.c code think that the first deployment is the booted one. */
  g_setenv ("EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK", "1", TRUE);
}

/* GIO equivalent of `rm -rf`. */
static gboolean
file_delete_recursive (GFile         *file,
                       GCancellable  *cancellable,
                       GError       **error)
{
  g_autoptr(GFileEnumerator) enumerator = NULL;
  g_autoptr(GError) local_error = NULL;

  enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          cancellable, &local_error);

  if (local_error != NULL &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  else if (local_error == NULL)
    {
      while (TRUE)
        {
          GFile *child;

          if (!g_file_enumerator_iterate (enumerator, NULL, &child,
                                          cancellable, error))
            return FALSE;
          if (child == NULL)
            break;
          if (!file_delete_recursive (child, cancellable, error))
            return FALSE;
        }
    }

  return g_file_delete (file, cancellable, error);
}

/* Inverse of setup(). */
static void
teardown (Fixture       *fixture,
          gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;

  g_unsetenv ("EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK");

  g_clear_object (&fixture->sysroot);

  file_delete_recursive (fixture->tmp_dir, NULL, &error);
  g_assert_no_error (error);
  g_clear_object (&fixture->tmp_dir);
}

/* Test that listing commits from a sysroot with no deployments errors. */
static void
test_ostree_no_deployments (Fixture       *fixture,
                            gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  gboolean retval;
  g_autofree gchar *commit_checksum = NULL;
  g_autofree gchar *commit_ostree_path = NULL;
  guint64 commit_timestamp = 0;

  retval = eos_sysroot_get_advertisable_commit (fixture->sysroot,
                                                &commit_checksum,
                                                &commit_ostree_path,
                                                &commit_timestamp,
                                                &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (retval);

  g_assert_null (commit_checksum);
  g_assert_null (commit_ostree_path);
  g_assert_cmpuint (commit_timestamp, ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/ostree/no-deployments", Fixture, NULL, setup,
              test_ostree_no_deployments, teardown);
  /* TODO: More */

  return g_test_run ();
}
