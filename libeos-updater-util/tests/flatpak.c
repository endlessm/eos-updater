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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <flatpak.h>
#include <glib.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/types.h>
#include <locale.h>

typedef struct
{
  EuuFlatpakRemoteRefActionType type;
  FlatpakRefKind kind;
  const gchar *app_id;
  const gchar *branch;
  gint32 serial;
  EuuFlatpakRemoteRefActionFlags flags;
} FlatpakToInstallEntry;

typedef struct
{
  const gchar *name;
  FlatpakToInstallEntry *entries;
  guint n_entries;
} FlatpakToInstallFile;

typedef struct
{
  FlatpakToInstallFile *files;
  guint n_files;
} FlatpakToInstallDirectory;

static EuuFlatpakRemoteRefAction *
flatpak_to_install_entry_to_remote_ref_action (const gchar           *source,
                                               FlatpakToInstallEntry *entry)
{
  g_autoptr(FlatpakRef) ref = g_object_new (FLATPAK_TYPE_REF,
                                            "kind", entry->kind,
                                            "name", entry->app_id,
                                            "arch", "arch",
                                            "branch", entry->branch,
                                            NULL);
  g_autoptr(EuuFlatpakLocationRef) location_ref = euu_flatpak_location_ref_new (ref, "none", NULL);

  return euu_flatpak_remote_ref_action_new (entry->type,
                                            location_ref,
                                            source,
                                            entry->serial,
                                            entry->flags);
}

static GPtrArray *
flatpak_to_install_file_to_actions (FlatpakToInstallFile *file)
{
  g_autoptr(GPtrArray) array = g_ptr_array_new_full (file->n_entries,
                                                     (GDestroyNotify) euu_flatpak_remote_ref_action_unref);
  gsize i;

  for (i = 0; i < file->n_entries; ++i)
    {
      g_ptr_array_add (array, flatpak_to_install_entry_to_remote_ref_action (file->name,
                                                                             &file->entries[i]));
    }

  return g_steal_pointer (&array);
}

static GHashTable *
flatpak_to_install_directory_to_hash_table (FlatpakToInstallDirectory *directory)
{
  g_autoptr(GHashTable) ref_actions_in_directory = g_hash_table_new_full (g_str_hash,
                                                                          g_str_equal,
                                                                          g_free,
                                                                          (GDestroyNotify) euu_flatpak_remote_ref_actions_file_free);
  gsize i;

  for (i = 0; i < directory->n_files; ++i)
    {
      g_autoptr(GPtrArray) remote_ref_actions = flatpak_to_install_file_to_actions (&directory->files[i]);
      g_hash_table_insert (ref_actions_in_directory,
                           g_strdup (directory->files[i].name),
                           euu_flatpak_remote_ref_actions_file_new (remote_ref_actions, 0));
    }

  return euu_hoist_flatpak_remote_ref_actions (ref_actions_in_directory);
}

/* Test that actions 'install', then 'update' get compressed as 'install' */
static void
test_compress_install_update_as_install (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test that actions 'uninstall', then 'update' get compressed as 'uninstall' */
static void
test_compress_uninstall_update_as_uninstall (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
}

/* Test that no compresson occurs if 'uninstall' and 'update' are on
 * different branches */
static void
test_no_compress_uninstall_update_different_branches (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "other", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 2);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 1))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE);
}

/* Test that actions 'install', then 'uninstall' get compressed as 'uninstall' */
static void
test_compress_install_uninstall_as_uninstall (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
}

/* Test that no compresson occurrs if 'install' and 'uninstall' are on
 * different branches */
static void
test_no_compress_install_uninstall_different_branches (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "other", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 2);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 1))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
}

/* Test that actions 'install', then 'uninstall', then 'install' get compressed
 * as 'install' */
static void
test_compress_install_uninstall_install_as_install (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 2, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 3, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test that actions 'update', then 'update' get compressed as 'update' */
static void
test_compress_update_update_as_update (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE);
}

/* Test that no compresson occurrs if 'update' and 'update' are on
 * different branches */
static void
test_no_compress_update_update_different_branches (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "other", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 2);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 1))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE);
}

/* Test that actions 'install', then 'install' get compressed as 'install' */
static void
test_compress_install_install_as_install (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 2, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test that no compresson occurrs if 'install' and 'install' are on
 * different branches */
static void
test_no_compress_install_install_different_branches (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Runtime", "stable", 1, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 2);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 1))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

static void
assert_ref_name_in_remote_ref_action_array (GPtrArray   *flattened_actions_list,
                                            gsize        idx,
                                            const gchar *expected_ref_name)
{
  EuuFlatpakRemoteRefAction *ref_action = NULL;

  g_assert_cmpuint (idx, <, flattened_actions_list->len);
  ref_action = g_ptr_array_index (flattened_actions_list, idx);
  g_assert_cmpstr (flatpak_ref_get_name (FLATPAK_REF (ref_action->ref->ref)), ==, expected_ref_name);
}

/* Test that an install action for a dependency goes before its source */
static void
test_install_dependency_action_ordered_before_source (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Runtime", "stable", 1, EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 2);
  assert_ref_name_in_remote_ref_action_array (flattened_actions_list, 0, entries[1].app_id);
  assert_ref_name_in_remote_ref_action_array (flattened_actions_list, 1, entries[0].app_id);
}

/* Test that an update action for a dependency goes before its source */
static void
test_update_dependency_action_ordered_before_source (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Runtime", "stable", 1, EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 2);
  assert_ref_name_in_remote_ref_action_array (flattened_actions_list, 0, entries[1].app_id);
  assert_ref_name_in_remote_ref_action_array (flattened_actions_list, 1, entries[0].app_id);
}

/* Test that an uninstall action for a dependency goes after its source */
static void
test_uninstall_dependency_action_ordered_before_source (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Runtime", "stable", 1, EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", "stable", 1, 0 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 2);
  assert_ref_name_in_remote_ref_action_array (flattened_actions_list, 0, entries[1].app_id);
  assert_ref_name_in_remote_ref_action_array (flattened_actions_list, 1, entries[0].app_id);
}

/* Test the autoinstall file parser handles various different constructs (valid
 * and erroneous) in the format, returning success or an error when appropriate. */
static void
test_parse_autoinstall_file (void)
{
  const struct
    {
      const gchar *data;
      gsize expected_n_actions;
      gsize expected_n_skipped_actions;
      GQuark expected_error_domain;
      gint expected_error_code;
    } vectors[] =
    {
      { "", 0, 0, 0, 0 },
      { "'a json string'", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "not valid JSON", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[]", 0, 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 1, 0, 0, 0 },
      { "[{ 'action': 'uninstall', 'serial': 2017100101, 'ref-kind': 'app', "
        "   'name': 'org.example.OutdatedApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 1, 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017100500, 'ref-kind': 'runtime', "
        "   'name': 'org.example.PreinstalledRuntime', 'collection-id': 'com.endlessm.Runtimes', "
        "   'remote': 'eos-runtimes', 'branch': 'stable' }]", 1, 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110100, 'ref-kind': 'runtime', "
        "   'name': 'org.example.NVidiaRuntime', 'collection-id': 'com.endlessm.Runtimes', "
        "   'remote': 'eos-runtimes', 'branch': 'stable' }]", 1, 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': { 'locale': ['nonexistent'], '~architecture': ['armhf'] }}]",
        0, 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': {}}]", 1, 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': { '~locale': [], 'architecture': [] }}]", 0, 0, 0, 0 },
      { "[{ 'action': 'update', 'serial': 2017100101, 'ref-kind': 'app', "
        "   'name': 'org.example.OutdatedApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 1, 0, 0, 0 },
      { "[{ 'action': 'update', 'serial': 2018011900, 'ref-kind': 'runtime', "
        "   'name': 'org.freedesktop.Platform.Icontheme.Example', 'collection-id': 'com.endlessm.Sdk', "
        "   'remote': 'eos-sdk', 'branch': '1.0' }]", 1, 0, 0, 0 },

      { "[{ 'action': 123, 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'remote': 'eos-apps', "
        "   'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'invalid', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 123, "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{}]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "['a string']", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100 }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 123, 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 123, "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 123, 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2147483648, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': -2147483649, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[{ 'action': 'uninstall' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'uninstall', 'serial': 2017100100 }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[{ 'action': 'update' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'update', 'serial': 2017100100 }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': 'not an object' }]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': { 'locale': 'not an array' }}]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': { 'locale': [123] }}]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': { 'locale': ['not allowed both'], '~locale': ['filters'] }}]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': { 'architecture': ['not allowed both'], '~architecture': ['filters'] }}]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'branch': 'stable', "
        "   'filters': { 'architecture': ['not allowed both'], '~architecture': ['filters'] }}]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable' }]",
        1, 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 'stable', "
        "   'filters': { 'nonexistent': ['invalid'] }}]",
        0, 1, 0, 0 },

      /* no branch */
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps' }]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      /* invalid type for branch */
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', 'branch': 1 }}]",
        0, 0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[{ 'action': 'invalid' }]", 0, 1, 0, 0 },

      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }, "
        " { 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      /* nonsensical serial numbers, outside of 32 bit range */
      { "[{ 'action': 'install', 'serial': -2147483649, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2147483648, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 'not a number', 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 1.2, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps', 'branch': 'stable' }]", 0, 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
    };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GPtrArray) actions = NULL;
      g_autoptr(GPtrArray) skipped_actions = NULL;
      g_autoptr(GError) error = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": %s", i, vectors[i].data);

      actions = euu_flatpak_ref_actions_from_data (vectors[i].data, -1, "test", &skipped_actions,
                                                   NULL, &error);

      if (error != NULL)
        g_test_message ("Got error: %s", error->message);

      if (vectors[i].expected_error_domain != 0)
        {
          g_assert_error (error, vectors[i].expected_error_domain, vectors[i].expected_error_code);
          g_assert_null (actions);
          g_assert_null (skipped_actions);
        }
      else
        {
          g_assert_no_error (error);
          g_assert_nonnull (actions);
          g_assert_cmpuint (actions->len, ==, vectors[i].expected_n_actions);
          g_assert_nonnull (skipped_actions);
          g_assert_cmpuint (skipped_actions->len, ==, vectors[i].expected_n_skipped_actions);
        }
    }
}

/* Test the autoinstall file parser successfully sorts entries by their serial
 * numbers. Also take the opportunity to check the fields of the returned
 * structs. */
static void
test_parse_autoinstall_file_unsorted (void)
{
  const EuuFlatpakRemoteRefAction *action0, *action1;
  g_autofree gchar *action0_ref = NULL;
  g_autofree gchar *action1_ref = NULL;
  g_autofree gchar *old_env_arch = g_strdup (g_getenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE"));
  const gchar *data =
    "["
      "{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
      "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
      "   'remote': 'eos-apps', 'branch': 'stable' },"
      "{ 'action': 'install', 'serial': 2017090100, 'ref-kind': 'app', "
      "   'name': 'org.example.OtherApp', 'collection-id': 'com.endlessm.Apps', "
      "   'remote': 'eos-apps', 'branch': 'stable' }"
    "]";

  g_autoptr(GPtrArray) actions = NULL;
  g_autoptr(GPtrArray) skipped_actions = NULL;
  g_autoptr(GError) error = NULL;

  g_setenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", "arch", TRUE);

  actions = euu_flatpak_ref_actions_from_data (data, -1, "test", &skipped_actions,
                                               NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (actions);
  g_assert_cmpuint (actions->len, ==, 2);
  g_assert_nonnull (skipped_actions);
  g_assert_cmpuint (skipped_actions->len, ==, 0);

  /* Check the actions are in the right order, and that their fields are correct. */
  action0 = g_ptr_array_index (actions, 0);
  action0_ref = flatpak_ref_format_ref (action0->ref->ref);
  action1 = g_ptr_array_index (actions, 1);
  action1_ref = flatpak_ref_format_ref (action1->ref->ref);

  g_assert_cmpint (action0->ref_count, >=, 1);
  g_assert_cmpint (action0->type, ==, EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
  g_assert_cmpint (action0->ref->ref_count, >=, 1);
  g_assert_cmpstr (action0_ref, ==, "app/org.example.OtherApp/arch/stable");
  g_assert_cmpstr (action0->ref->remote, ==, "eos-apps");
  g_assert_cmpstr (action0->ref->collection_id, ==, "com.endlessm.Apps");
  g_assert_cmpstr (action0->source, ==, "test");
  g_assert_cmpint (action0->serial, ==, 2017090100);

  g_assert_cmpint (action1->ref_count, >=, 1);
  g_assert_cmpint (action1->type, ==, EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
  g_assert_cmpint (action1->ref->ref_count, >=, 1);
  g_assert_cmpstr (action1_ref, ==, "app/org.example.MyApp/arch/stable");
  g_assert_cmpstr (action1->ref->remote, ==, "eos-apps");
  g_assert_cmpstr (action1->ref->collection_id, ==, "com.endlessm.Apps");
  g_assert_cmpstr (action1->source, ==, "test");
  g_assert_cmpint (action1->serial, ==, 2017100100);

  if (old_env_arch)
    g_setenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", old_env_arch, TRUE);
  else
    g_unsetenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE");
}

/* Test that the filters on autoinstall files work correctly. */
static void
test_autoinstall_file_filters (void)
{
  const gchar *data =
      "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
      "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
      "   'remote': 'example-apps', "
      "   'branch': 'stable', "
      "   'filters': { %s }"
      "}]";

  g_autofree gchar *old_env_arch = g_strdup (g_getenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE"));
  g_autofree gchar *old_env_locales = g_strdup (g_getenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES"));

  const struct
    {
      const gchar *filters;
      const gchar *env_arch;
      const gchar *env_locales;
      gsize expected_n_actions;
      gsize expected_n_skipped_actions;
    } vectors[] =
    {
      { "", "", "", 1 },

      { "'architecture': []", "", "", 0, 0 },
      { "'architecture': ['arch1']", "arch1", "", 1, 0 },
      { "'architecture': ['arch1', 'arch2']", "arch1", "", 1, 0 },
      { "'architecture': ['arch1', 'arch2']", "arch2", "", 1, 0 },
      { "'architecture': ['arch1', 'arch2']", "arch3", "", 0, 0 },

      { "'~architecture': []", "", "", 1, 0 },
      { "'~architecture': ['arch1']", "arch1", "", 0, 0 },
      { "'~architecture': ['arch1', 'arch2']", "arch1", "", 0, 0 },
      { "'~architecture': ['arch1', 'arch2']", "arch2", "", 0, 0 },
      { "'~architecture': ['arch1', 'arch2']", "arch3", "", 1, 0 },

      { "'locale': []", "", "", 0, 0 },
      { "'locale': ['locale1']", "", "locale1", 1, 0 },
      { "'locale': ['locale1']", "", "locale2;locale1", 1, 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale1", 1, 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale2;locale1", 1, 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale3;locale1", 1, 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale2", 1, 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale1;locale2", 1, 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale3", 0, 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale3;locale4", 0, 0 },

      { "'~locale': []", "", "", 1, 0 },
      { "'~locale': ['locale1']", "", "locale1", 0, 0 },
      { "'~locale': ['locale1']", "", "locale2;locale1", 0, 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale1", 0, 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale2;locale1", 0, 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale3;locale1", 0, 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale2", 0, 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale1;locale2", 0, 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale3", 1, 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale3;locale4", 1, 0 },
    };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GPtrArray) actions = NULL;
      g_autoptr(GPtrArray) skipped_actions = NULL;
      g_autoptr(GError) error = NULL;
      g_autofree gchar *formatted_data = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": %s, %s, %s", i,
                      vectors[i].filters, vectors[i].env_arch, vectors[i].env_locales);
      g_setenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", vectors[i].env_arch, TRUE);
      g_setenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES", vectors[i].env_locales, TRUE);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
      formatted_data = g_strdup_printf (data, vectors[i].filters);
#pragma GCC diagnostic pop

      g_test_message ("%s", formatted_data);

      actions = euu_flatpak_ref_actions_from_data (formatted_data, -1, "test", &skipped_actions,
                                                   NULL, &error);

      g_assert_no_error (error);
      g_assert_nonnull (actions);
      g_assert_cmpuint (actions->len, ==, vectors[i].expected_n_actions);
      g_assert_nonnull (skipped_actions);
      g_assert_cmpuint (skipped_actions->len, ==, vectors[i].expected_n_skipped_actions);
    }

  if (old_env_arch != NULL)
    g_setenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", old_env_arch, TRUE);
  else
    g_unsetenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE");
  if (old_env_locales != NULL)
    g_setenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES", old_env_locales, TRUE);
  else
    g_unsetenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES");
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/flatpak/compress/install-update-as-install",
              test_compress_install_update_as_install);
  g_test_add_func ("/flatpak/compress/uninstall-update-as-uninstall",
              test_compress_uninstall_update_as_uninstall);
  g_test_add_func ("/flatpak/compress/no-compress-uninstall-update-different-branches",
              test_no_compress_uninstall_update_different_branches);
  g_test_add_func ("/flatpak/compress/install-uninstall-as-uninstall",
              test_compress_install_uninstall_as_uninstall);
  g_test_add_func ("/flatpak/compress/no-compress-install-uninstall-different-branches",
              test_no_compress_install_uninstall_different_branches);
  g_test_add_func ("/flatpak/compress/install-uninstall-install-as-install",
              test_compress_install_uninstall_install_as_install);
  g_test_add_func ("/flatpak/compress/update-update-as-update",
              test_compress_update_update_as_update);
  g_test_add_func ("/flatpak/compress/no-compress-update-update-different-branches",
              test_no_compress_update_update_different_branches);
  g_test_add_func ("/flatpak/compress/install-install-as-install",
              test_compress_install_install_as_install);
  g_test_add_func ("/flatpak/compress/no-compress-install-install-different-branches",
              test_no_compress_install_install_different_branches);
  g_test_add_func ("/flatpak/compress/install-dependency-before-source",
              test_install_dependency_action_ordered_before_source);
  g_test_add_func ("/flatpak/compress/update-dependency-before-source",
              test_update_dependency_action_ordered_before_source);
  g_test_add_func ("/flatpak/compress/uninstall-dependency-before-source",
              test_uninstall_dependency_action_ordered_before_source);
  g_test_add_func ("/flatpak/parse-autoinstall-file",
                   test_parse_autoinstall_file);
  g_test_add_func ("/flatpak/parse-autoinstall-file/unsorted",
                   test_parse_autoinstall_file_unsorted);
  g_test_add_func ("/flatpak/autoinstall-file-filters",
                   test_autoinstall_file_filters);

  return g_test_run ();
}
