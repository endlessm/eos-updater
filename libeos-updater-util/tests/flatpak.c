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
 */

#include <glib.h>
#include <flatpak.h>
#include <libeos-updater-util/flatpak.h>
#include <locale.h>

typedef struct
{
} Fixture;

typedef struct
{
  EosUpdaterUtilFlatpakRemoteRefActionType type;
  FlatpakRefKind                           kind;
  const gchar                              *app_id;
  gint                                     serial;
} FlatpakToInstallEntry;

typedef struct
{
  const gchar           *name;
  FlatpakToInstallEntry *entries;
  guint                 n_entries;
} FlatpakToInstallFile;

typedef struct
{
  FlatpakToInstallFile *files;
  guint                n_files;
} FlatpakToInstallDirectory;

static FlatpakRemoteRefAction *
flatpak_to_install_entry_to_remote_ref_action (FlatpakToInstallEntry *entry)
{
  g_autoptr(FlatpakRef) ref = g_object_new (FLATPAK_TYPE_REF,
                                            "name", entry->app_id,
                                            "kind", entry->kind,
                                            NULL);
  g_autoptr(FlatpakLocationRef) location_ref = flatpak_location_ref_new (ref, "none", NULL);

  return flatpak_remote_ref_action_new (entry->type,
                                        location_ref,
                                        entry->serial);
                                        
}

static GPtrArray *
flatpak_to_install_file_to_actions (FlatpakToInstallFile *file)
{
  g_autoptr(GPtrArray) array = g_ptr_array_new_full (file->n_entries,
                                                     (GDestroyNotify) flatpak_remote_ref_action_unref);
  gsize i;

  for (i = 0; i < file->n_entries; ++i)
    {
      g_ptr_array_add (array, flatpak_to_install_entry_to_remote_ref_action (&file->entries[i]));
    }

  return g_steal_pointer (&array);
}

static GHashTable *
flatpak_to_install_directory_to_hash_table (FlatpakToInstallDirectory *directory)
{
  g_autoptr(GHashTable) ref_actions_in_directory = g_hash_table_new_full (g_str_hash,
                                                                          g_str_equal,
                                                                          g_free,
                                                                          (GDestroyNotify) flatpak_remote_ref_actions_file_free);
  gsize i;

  for (i = 0; i < directory->n_files; ++i)
    {
      g_autoptr(GPtrArray) remote_ref_actions = flatpak_to_install_file_to_actions (&directory->files[i]);
      g_hash_table_insert (ref_actions_in_directory,
                           g_strdup (directory->files[i].name),
                           flatpak_remote_ref_actions_file_new (g_steal_pointer (&remote_ref_actions), 0));
    }

  return eos_updater_util_hoist_flatpak_remote_ref_actions (ref_actions_in_directory);
}

static void
setup (Fixture       *fixture,
       gconstpointer  user_data G_GNUC_UNUSED)
{
}

/* Inverse of setup(). */
static void
teardown (Fixture       *fixture,
          gconstpointer  user_data G_GNUC_UNUSED)
{
}

/* Test that actions 'install', then 'update' get compressed as 'install' */
static void
test_compress_install_update_as_install (Fixture       *fixture,
                                         gconstpointer  user_data G_GNUC_UNUSED)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = eos_updater_util_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert (flattened_actions_list->len == 1);
  g_assert (((FlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type == EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test that actions 'uninstall', then 'update' get compressed as 'uninstall' */
static void
test_compress_uninstall_update_as_uninstall (Fixture       *fixture,
                                             gconstpointer  user_data G_GNUC_UNUSED)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = eos_updater_util_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert (flattened_actions_list->len == 1);
  g_assert (((FlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type == EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
}

/* Test that actions 'install', then 'uninstall' get compressed as 'uninstall' */
static void
test_compress_install_uninstall_as_uninstall (Fixture       *fixture,
                                              gconstpointer  user_data G_GNUC_UNUSED)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = eos_updater_util_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert (flattened_actions_list->len == 1);
  g_assert (((FlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type == EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
}

/* Test that actions 'install', then 'uninstall', then 'install' get compressed
 * as 'install' */
static void
test_compress_install_uninstall_install_as_install (Fixture       *fixture,
                                                      gconstpointer  user_data G_GNUC_UNUSED)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 2 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 3 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = eos_updater_util_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert (flattened_actions_list->len == 1);
  g_assert (((FlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type == EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test that actions 'update', then 'update' get compressed as 'update' */
static void
test_compress_update_update_as_update (Fixture       *fixture,
                                       gconstpointer  user_data G_GNUC_UNUSED)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = eos_updater_util_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert (flattened_actions_list->len == 1);
  g_assert (((FlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type == EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE);
}

/* Test that actions 'update', then 'update' get compressed as 'update' */
static void
test_compress_install_install_as_install (Fixture       *fixture,
                                          gconstpointer  user_data G_GNUC_UNUSED)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = eos_updater_util_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert (flattened_actions_list->len == 1);
  g_assert (((FlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type == EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/flatpak/compress-install-update-as-install", Fixture, NULL, setup,
              test_compress_install_update_as_install, teardown);
  g_test_add ("/flatpak/compress-uninstall-update-as-uninstall", Fixture, NULL, setup,
              test_compress_uninstall_update_as_uninstall, teardown);
  g_test_add ("/flatpak/compress-install-uninstall-as-uninstall", Fixture, NULL, setup,
              test_compress_install_uninstall_as_uninstall, teardown);
  g_test_add ("/flatpak/compress-install-uninstall-install-as-install", Fixture, NULL, setup,
              test_compress_install_uninstall_install_as_install, teardown);
  g_test_add ("/flatpak/compress-update-update-as-update", Fixture, NULL, setup,
              test_compress_update_update_as_update, teardown);
  g_test_add ("/flatpak/compress-install-install-as-install", Fixture, NULL, setup,
              test_compress_install_install_as_install, teardown);

  return g_test_run ();
}
