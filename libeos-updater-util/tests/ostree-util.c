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

#include <gio/gio.h>
#include <glib.h>
#include <libeos-updater-util/ostree-util.h>
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
  g_autoptr(GFile) boot_id_file = NULL;

  tmp_dir = g_dir_make_tmp ("eos-updater-util-tests-ostree-XXXXXX", &error);
  g_assert_no_error (error);

  fixture->tmp_dir = g_file_new_for_path (tmp_dir);

  /* When running in a chroot (for example, when running ARM tests using
   * qemu-user), the kernel’s boot ID isn’t available so we need to fake it for
   * #OstreeRepo to work. */
  boot_id_file = g_file_new_for_path ("/proc/sys/kernel/random/boot_id");
  if (!g_file_query_exists (boot_id_file, NULL))
    {
      g_test_message ("Setting OSTREE_BOOTID since boot_id file doesn’t exist");
      g_setenv ("OSTREE_BOOTID", "test-bootid", FALSE);
    }

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

  retval = eos_updater_sysroot_get_advertisable_commit (fixture->sysroot,
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

static void
test_ostree_sysroot_boot_automount (Fixture       *fixture,
                                    gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  gboolean retval;
  GFile *sysroot_file = ostree_sysroot_get_path (fixture->sysroot);
  g_autofree gchar *sysroot_path = g_file_get_path (sysroot_file);
  g_autoptr(GFile) boot_file = g_file_get_child (sysroot_file, "boot");
  g_autofree gchar *boot_path = g_file_get_path (boot_file);
  g_autoptr(GFile) mountinfo_file = g_file_get_child (fixture->tmp_dir,
                                                      "mountinfo");
  g_autofree gchar *mountinfo_path = g_file_get_path (mountinfo_file);
  g_autofree gchar *mountinfo_contents = NULL;

  retval = g_file_make_directory (boot_file, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  /* No separate /boot mount */
  g_clear_pointer (&mountinfo_contents, g_free);
  mountinfo_contents =
    g_strdup_printf ("1 1 1:1 / %s rw - ext4 /dev/sda1 rw\n", sysroot_path);
  g_test_message ("boot %s, mountinfo:\n%s", boot_path, mountinfo_contents);
  retval = g_file_replace_contents (mountinfo_file, mountinfo_contents,
                                    strlen (mountinfo_contents), NULL, FALSE,
                                    G_FILE_CREATE_NONE, NULL, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
  retval = eos_updater_sysroot_boot_is_automount (fixture->sysroot, mountinfo_path);
  g_assert_false (retval);

  /* Non-automount /boot */
  g_clear_pointer (&mountinfo_contents, g_free);
  mountinfo_contents =
    g_strdup_printf ("1 1 1:2 / %s rw - ext4 /dev/sda2 rw\n"
                     "2 1 1:1 / %s rw - ext4 /dev/sda1 rw\n",
                     sysroot_path, boot_path);
  g_test_message ("boot %s, mountinfo:\n%s", boot_path, mountinfo_contents);
  retval = g_file_replace_contents (mountinfo_file, mountinfo_contents,
                                    strlen (mountinfo_contents), NULL, FALSE,
                                    G_FILE_CREATE_NONE, NULL, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
  retval = eos_updater_sysroot_boot_is_automount (fixture->sysroot, mountinfo_path);
  g_assert_false (retval);

  /* Automount /boot without target mount */
  g_clear_pointer (&mountinfo_contents, g_free);
  mountinfo_contents =
    g_strdup_printf ("1 1 1:2 / %s rw - ext4 /dev/sda2 rw\n"
                     "2 1 0:1 / %s rw - autofs systemd-1 rw\n",
                     sysroot_path, boot_path);
  g_test_message ("boot %s, mountinfo:\n%s", boot_path, mountinfo_contents);
  retval = g_file_replace_contents (mountinfo_file, mountinfo_contents,
                                    strlen (mountinfo_contents), NULL, FALSE,
                                    G_FILE_CREATE_NONE, NULL, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
  retval = eos_updater_sysroot_boot_is_automount (fixture->sysroot, mountinfo_path);
  g_assert_true (retval);

  /* Automount /boot with target mount */
  g_clear_pointer (&mountinfo_contents, g_free);
  mountinfo_contents =
    g_strdup_printf ("1 1 1:2 / %s rw - ext4 /dev/sda2 rw\n"
                     "2 1 0:1 / %s rw - autofs systemd-1 rw\n"
                     "3 2 1:1 / %s rw - vfat /dev/sda1 rw\n",
                     sysroot_path, boot_path, boot_path);
  g_test_message ("boot %s, mountinfo:\n%s", boot_path, mountinfo_contents);
  retval = g_file_replace_contents (mountinfo_file, mountinfo_contents,
                                    strlen (mountinfo_contents), NULL, FALSE,
                                    G_FILE_CREATE_NONE, NULL, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
  retval = eos_updater_sysroot_boot_is_automount (fixture->sysroot, mountinfo_path);
  g_assert_true (retval);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  g_test_add ("/ostree/no-deployments", Fixture, NULL, setup,
              test_ostree_no_deployments, teardown);
  g_test_add ("/ostree/sysroot-boot-automount", Fixture, NULL, setup,
              test_ostree_sysroot_boot_automount, teardown);
  /* TODO: More */

  return g_test_run ();
}
