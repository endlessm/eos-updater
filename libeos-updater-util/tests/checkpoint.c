/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2024 Endless OS Foundation LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include <glib/gi18n.h>
#include <libeos-updater-util/checkpoint-private.h>

typedef struct
{
  GFile *root_dir;  /* owned */
  OstreeSysroot *sysroot;  /* owned */
  OstreeRepo *repo;  /* owned */
} Fixture;

/* Set up a temporary directory with a loaded sysroot in. */
static void
setup (Fixture       *fixture,
       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;

  g_unsetenv ("EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT");

  fixture->root_dir = g_file_new_for_path (g_get_user_cache_dir ());
  g_file_make_directory (fixture->root_dir, NULL, &error);
  g_assert_no_error (error);

  /* Set up the sysroot. */
  fixture->sysroot = ostree_sysroot_new (fixture->root_dir);

  ostree_sysroot_ensure_initialized (fixture->sysroot, NULL, &error);
  g_assert_no_error (error);

  ostree_sysroot_load (fixture->sysroot, NULL, &error);
  g_assert_no_error (error);

  ostree_sysroot_get_repo (fixture->sysroot, &fixture->repo, NULL, &error);
  g_assert_no_error (error);
}

static void
teardown (Fixture       *fixture,
          gconstpointer  user_data G_GNUC_UNUSED)
{
  g_clear_object (&fixture->sysroot);
  g_clear_object (&fixture->repo);
  g_clear_object (&fixture->root_dir);
}

/* Test that checkpoints are followed unless there is a particular reason not
 * to.
 */
static void
test_default_follow (Fixture                  *fixture,
                     gconstpointer  user_data  G_GNUC_UNUSED)
{
  gchar *reason = NULL;
  g_assert_true (euu_should_follow_checkpoint (fixture->sysroot, "os/eos/amd64/latest2", "os/eos/amd64/latest3", &reason));
  g_assert_cmpstr (reason, ==, NULL);
}

/* Test that setting EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT=0 prevents following
 * a checkpoint. This is needed because the integration tests for the daemon
 * need a way to trigger the path where a checkpoint is not followed, and
 * an environment variable is the path of least resistance.
 */
static void
test_force_no_follow (Fixture                  *fixture,
                      gconstpointer  user_data  G_GNUC_UNUSED)
{
  g_autofree gchar *reason = NULL;

  g_setenv ("EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT", "0", TRUE);

  g_assert_false (euu_should_follow_checkpoint (fixture->sysroot, "os/eos/amd64/latest2", "os/eos/amd64/latest3", &reason));
  g_assert_cmpstr (reason, !=, NULL);

  g_unsetenv ("EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT");
}

/* Up to & including eos5.1 (a.k.a. latest2), our kernel had an nvme-remap
 * driver to support a weird Intel storage configuration. We are removing this
 * driver in eos6.0 (a.k.a. latest3). Test that it is detected correctly.
 */
static void
test_nvme_remap (Fixture       *fixture,
                 gconstpointer  user_data)
{
  g_autoptr(GFile) driver_dir = NULL;
  g_autoptr(GError) error = NULL;
  gboolean nvme_remap_in_use = GPOINTER_TO_INT (user_data);
  gboolean should_follow_checkpoint;

  driver_dir = g_file_resolve_relative_path (fixture->root_dir, "sys/bus/pci/drivers/intel-nvme-remap");
  g_file_make_directory_with_parents (driver_dir, NULL, &error);
  g_assert_no_error (error);

  /* We built this driver into the kernel. These files exist on all systems: */
  const char * const standard_files[] = { "bind", "new_id", "remove_id", "uevent", "unbind" };
  for (size_t i = 0; i < G_N_ELEMENTS (standard_files); i++)
    {
      g_autoptr(GFile) file = g_file_get_child (driver_dir, standard_files[i]);
      g_autoptr(GFileOutputStream) o = g_file_create (file, G_FILE_CREATE_NONE, NULL, &error);
      g_assert_no_error (error);
      g_assert_nonnull (o);
    }

  if (nvme_remap_in_use)
    {
      /* If nvme-remap is in use, there will be at least 1 symlink starting with
       * "0000:", which points at the PCI device where we found NVMe devices
       * hiding behind.
       */
      g_autoptr(GFile) symlink = g_file_get_child (driver_dir, "0000:39:00.0");
      /* Where the symlink points doesn't matter to the code under test.
       * It should really point to a directory, but when GLib cleans up the
       * test's temporary directory it follows symbolic links
       * https://gitlab.gnome.org/GNOME/glib/-/issues/3290
       * so the link mustn't form a cycle or (ahem) point to /.
       * Just point it to nowhere.
       */
      g_file_make_symbolic_link (symlink, "nonexistent", NULL, &error);
      g_assert_no_error (error);
    }

  g_autofree gchar *reason = NULL;

  should_follow_checkpoint = euu_should_follow_checkpoint (fixture->sysroot, "os/eos/amd64/latest2", "os/eos/amd64/latest3", &reason);

  if (nvme_remap_in_use)
    {
      g_assert_false (should_follow_checkpoint);
      g_assert_cmpstr (reason, !=, NULL);
    }
  else
    {
      g_assert_true (should_follow_checkpoint);
      g_assert_cmpstr (reason, ==, NULL);
    }
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  g_test_add ("/checkpoint/default-follow", Fixture, NULL, setup,
              test_default_follow, teardown);
  g_test_add ("/checkpoint/force-no-follow", Fixture, NULL, setup,
              test_force_no_follow, teardown);
  g_test_add ("/checkpoint/nvme-remap/not-in-use",
              Fixture,
              GINT_TO_POINTER (FALSE),
              setup,
              test_nvme_remap,
              teardown);
  g_test_add ("/checkpoint/nvme-remap/in-use",
              Fixture,
              GINT_TO_POINTER (TRUE),
              setup,
              test_nvme_remap,
              teardown);

  return g_test_run ();
}
