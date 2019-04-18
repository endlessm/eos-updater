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
#include <libeos-updater-util/util.h>
#include <libeos-updater-flatpak-installer/installer.h>
#include <test-common/gpg.h>
#include <test-common/utils.h>
#include <locale.h>

typedef struct
{
  EosUpdaterFixture parent;
  GFile *flatpak_deployments_directory;
  GFile *flatpak_installation_directory;
  GFile *flatpak_remote_directory;
  GFile *counter_file;
} FlatpakDeploymentsFixture;

static void
flatpak_deployments_fixture_setup (FlatpakDeploymentsFixture  *fixture,
                                   gconstpointer               user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *flatpak_deployments_path = g_dir_make_tmp ("eos-updater-test-flatpak-deployments-XXXXXX", &error);
  g_autoptr(GFile) flatpak_deployments_directory = g_file_new_for_path (flatpak_deployments_path);
  g_autoptr(GFile) flatpak_build_dir = g_file_get_child (flatpak_deployments_directory, "flatpak");
  g_autofree gchar *top_srcdir = g_test_build_filename (G_TEST_DIST, "..", "..", NULL);
  g_autoptr(GFile) gpg_key_file = NULL;
  g_autofree gchar *keyid = NULL;
  const gchar *flatpak_names[] = {
    "org.test.Test",
    "org.test.Test2",
    "org.test.Test3",
    "org.test.Preinstalled",
    NULL
  };
  const gchar *preinstall_flatpak_names[] = {
    "org.test.Preinstalled",
    NULL
  };

  /* Chain up and pass in the $top_srcdir path so we can find tests/gpghome/
   * relative to `G_TEST_SRCDIR`. */
  eos_updater_fixture_setup_full ((EosUpdaterFixture *) fixture, top_srcdir);

  /* Initialisation specific to this test suite. */
  keyid = get_keyid (((EosUpdaterFixture *) fixture)->gpg_home);
  gpg_key_file = get_gpg_key_file_for_keyid (((EosUpdaterFixture *) fixture)->gpg_home, keyid);

  eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (flatpak_deployments_directory,
                                                             "stable",
                                                             "test-repo",
                                                             "com.test.CollectionId",
                                                             "com.test.CollectionId",
                                                             flatpak_names,
                                                             preinstall_flatpak_names,
                                                             gpg_key_file,
                                                             keyid,
                                                             &error);
  g_assert_no_error (error);

  fixture->flatpak_deployments_directory = g_object_ref (flatpak_deployments_directory);
  fixture->flatpak_remote_directory = g_file_get_child (flatpak_build_dir, "repo");
  fixture->flatpak_installation_directory = g_file_get_child (flatpak_deployments_directory, "flatpak-user");
  fixture->counter_file = g_file_get_child (flatpak_deployments_directory, "counter");
}

/* Inverse of setup(). */
static void
flatpak_deployments_fixture_teardown (FlatpakDeploymentsFixture  *fixture,
                                      gconstpointer               user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;

  eos_updater_remove_recursive (fixture->flatpak_deployments_directory, NULL, &error);
  g_assert_no_error (error);

  g_object_unref (fixture->flatpak_deployments_directory);
  g_object_unref (fixture->flatpak_remote_directory);
  g_object_unref (fixture->flatpak_installation_directory);
  g_object_unref (fixture->counter_file);

  /* Chain up. */
  eos_updater_fixture_teardown ((EosUpdaterFixture *) fixture, user_data);
}

static GPtrArray *
sample_flatpak_ref_actions_of_type (const gchar                   *source,
                                    const gchar * const           *flatpaks_to_install,
                                    EuuFlatpakRemoteRefActionType  action_type)
{
  g_autoptr(GPtrArray) actions = g_ptr_array_new_full (1, (GDestroyNotify) euu_flatpak_remote_ref_action_unref);
  g_autoptr(GHashTable) table = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       (GDestroyNotify) g_ptr_array_unref);
  gint i;

  for (i = 0; flatpaks_to_install[i] != NULL; ++i)
    {
      g_autoptr(FlatpakRef) ref = g_object_new (FLATPAK_TYPE_REF,
                                                "kind", FLATPAK_REF_KIND_APP,
                                                "name", flatpaks_to_install[i],
                                                "arch", euu_get_system_architecture_string (),
                                                "branch", "stable",
                                                NULL);
      g_autoptr(EuuFlatpakLocationRef) location_ref = euu_flatpak_location_ref_new (ref,
                                                                                    "test-repo",
                                                                                    NULL);
      g_autoptr(EuuFlatpakRemoteRefAction) ref_action =
        euu_flatpak_remote_ref_action_new (action_type,
                                           location_ref,
                                           source,
                                           i + 1,
                                           EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_NONE);

      g_ptr_array_add (actions, euu_flatpak_remote_ref_action_ref (ref_action));
      g_hash_table_insert (table, g_strdup ("autoinstall"), g_ptr_array_ref (actions));
    }

  return euu_flatten_flatpak_ref_actions_table (table);
}

static GPtrArray *
sample_flatpak_ref_actions (const gchar         *source,
                            const gchar * const *flatpaks_to_install)
{
  return sample_flatpak_ref_actions_of_type (source,
                                             flatpaks_to_install,
                                             EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}


/* Deploy some flatpak ref actions and check that the files got deployed in
 * the right place */
static void
test_deploy_flatpak_files_as_expected (FlatpakDeploymentsFixture *fixture,
                                       gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autofree gchar *state_counter_path = g_file_get_path (fixture->counter_file);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);

  g_assert_no_error (error);

  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_PERFORM,
                                  TRUE,
                                  &error);
  g_assert_no_error (error);

  g_assert (g_file_test (directory_expected_to_exist_path, G_FILE_TEST_EXISTS));
}

static void
test_stamp_no_deploy_flatpaks (FlatpakDeploymentsFixture *fixture,
                               gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autofree gchar *state_counter_path = g_file_get_path (fixture->counter_file);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);

  g_assert_no_error (error);

  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_STAMP,
                                  TRUE,
                                  &error);
  g_assert_no_error (error);

  g_assert (!g_file_test (directory_expected_to_exist_path, G_FILE_TEST_EXISTS));
}

static void
test_stamp_counter_state_updated (FlatpakDeploymentsFixture *fixture,
                                  gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autofree gchar *state_counter_path = g_file_get_path (fixture->counter_file);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);
  g_autofree gchar *counter_key_file_path = g_file_get_path (fixture->counter_file);
  g_autoptr(GKeyFile) counter_key_file = g_key_file_new ();

  g_assert_no_error (error);

  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_STAMP,
                                  TRUE,
                                  &error);
  g_assert_no_error (error);

  g_clear_error (&error);
  g_key_file_load_from_file (counter_key_file,
                             counter_key_file_path,
                             G_KEY_FILE_NONE,
                             &error);
  g_assert_no_error (error);

  g_assert (g_key_file_get_integer (counter_key_file, "autoinstall", "Progress", NULL) == 1);
}

static void
test_deploy_failure_previous_flatpaks_stay_deployed (FlatpakDeploymentsFixture *fixture,
                                                     gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", "org.test.Test2", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autofree gchar *state_counter_path = g_file_get_path (fixture->counter_file);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_fail_path = g_build_filename (installation_directory_path,
                                                                        "app",
                                                                        "org.test.Test2",
                                                                        NULL);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);

  g_assert_no_error (error);

  /* Put a file in the way of where flatpak will want to put a directory */
  g_file_set_contents (directory_expected_to_fail_path, "evil", -1, &error);
  g_assert_no_error (error);

  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_PERFORM,
                                  TRUE,
                                  &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);

  g_assert (g_file_test (directory_expected_to_exist_path, G_FILE_TEST_EXISTS));
}

static void
test_deploy_failure_counter_state_updated (FlatpakDeploymentsFixture *fixture,
                                                     gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", "org.test.Test2", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autofree gchar *state_counter_path = g_file_get_path (fixture->counter_file);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_fail_path = g_build_filename (installation_directory_path,
                                                                        "app",
                                                                        "org.test.Test2",
                                                                        NULL);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);
  g_autofree gchar *counter_key_file_path = g_file_get_path (fixture->counter_file);
  g_autoptr(GKeyFile) counter_key_file = g_key_file_new ();

  g_assert_no_error (error);

  /* Put a file in the way of where flatpak will want to put a directory */
  g_file_set_contents (directory_expected_to_fail_path, "evil", -1, &error);
  g_assert_no_error (error);

  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_PERFORM,
                                  TRUE,
                                  &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);

  g_clear_error (&error);
  g_key_file_load_from_file (counter_key_file,
                             counter_key_file_path,
                             G_KEY_FILE_NONE,
                             &error);
  g_assert_no_error (error);

  g_assert (g_key_file_get_integer (counter_key_file, "autoinstall", "Progress", NULL) == 1);
}

static void
test_deploy_failure_resume_from_latest (FlatpakDeploymentsFixture *fixture,
                                        gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", "org.test.Test2", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autofree gchar *state_counter_path = g_file_get_path (fixture->counter_file);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_fail_initially_path = g_build_filename (installation_directory_path,
                                                                                  "app",
                                                                                  "org.test.Test2",
                                                                                  NULL);
  g_autoptr(GFile) directory_expected_to_fail_dir = g_file_new_for_path (directory_expected_to_fail_initially_path);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);
  g_autofree gchar *counter_key_file_path = g_file_get_path (fixture->counter_file);
  g_autoptr(GKeyFile) counter_key_file = g_key_file_new ();

  g_assert_no_error (error);

  /* Put a file in the way of where flatpak will want to put a directory */
  g_file_set_contents (directory_expected_to_fail_initially_path, "evil", -1, &error);
  g_assert_no_error (error);

  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_PERFORM,
                                  TRUE,
                                  &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);

  g_clear_error (&error);
  g_file_delete (directory_expected_to_fail_dir, NULL, &error);
  g_assert_no_error (error);

  /* Run the installer again after deleting the file, it should succeed this time */
  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_PERFORM,
                                  TRUE,
                                  &error);

  g_assert_no_error (error);

  g_assert (g_file_test (directory_expected_to_fail_initially_path, G_FILE_TEST_EXISTS));
}

static void
test_flatpak_check_succeeds_if_actions_are_up_to_date (FlatpakDeploymentsFixture *fixture,
                                                       gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autofree gchar *state_counter_path = g_file_get_path (fixture->counter_file);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);
  g_autofree gchar *counter_key_file_path = g_file_get_path (fixture->counter_file);
  g_autoptr(GKeyFile) counter_key_file = g_key_file_new ();

  g_assert_no_error (error);

  g_key_file_set_integer (counter_key_file, "autoinstall", "Progress", 1);
  g_key_file_save_to_file (counter_key_file, counter_key_file_path, &error);
  g_assert_no_error (error);

  eufi_apply_flatpak_ref_actions (installation,
                                  state_counter_path,
                                  actions,
                                  EU_INSTALLER_MODE_PERFORM,
                                  TRUE,
                                  &error);
  g_assert_no_error (error);

  /* Run the checker - it should return true because all actions will be
   * up to date */
  eufi_check_ref_actions_applied (installation,
                                  actions,
                                  &error);
  g_assert_no_error (error);
}

static void
test_flatpak_check_fails_if_installed_flatpak_is_not_installed (FlatpakDeploymentsFixture *fixture,
                                                                gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Test", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions ("autoinstall", flatpaks_to_install);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);
  g_autofree gchar *counter_key_file_path = g_file_get_path (fixture->counter_file);
  g_autoptr(GKeyFile) counter_key_file = g_key_file_new ();

  g_assert_no_error (error);

  g_key_file_set_integer (counter_key_file, "autoinstall", "Progress", 1);
  g_key_file_save_to_file (counter_key_file, counter_key_file_path, &error);
  g_assert_no_error (error);

  /* Run the checker - it should return false because the flatpak that
   * needs to be installed is not yet installed */
  eufi_check_ref_actions_applied (installation,
                                  actions,
                                  &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
}

static void
test_flatpak_check_fails_if_unininstalled_flatpak_is_installed (FlatpakDeploymentsFixture *fixture,
                                                                gconstpointer              user G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  const gchar *flatpaks_to_install[] = { "org.test.Preinstalled", NULL };
  g_autoptr(GPtrArray) actions = sample_flatpak_ref_actions_of_type ("autoinstall",
                                                                     flatpaks_to_install,
                                                                     EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_for_path (fixture->flatpak_installation_directory,
                                                                                   TRUE,
                                                                                   NULL,
                                                                                   &error);
  g_autofree gchar *installation_directory_path = g_file_get_path (fixture->flatpak_installation_directory);
  g_autofree gchar *directory_expected_to_exist_path = g_build_filename (installation_directory_path,
                                                                         "app",
                                                                         "org.test.Test",
                                                                         NULL);
  g_autofree gchar *counter_key_file_path = g_file_get_path (fixture->counter_file);
  g_autoptr(GKeyFile) counter_key_file = g_key_file_new ();

  g_assert_no_error (error);

  g_key_file_set_integer (counter_key_file, "autoinstall", "Progress", 1);
  g_key_file_save_to_file (counter_key_file, counter_key_file_path, &error);
  g_assert_no_error (error);

  /* Run the checker - it should return false because the preinstalled flatpak
   * is still installed */
  eufi_check_ref_actions_applied (installation,
                                  actions,
                                  &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  /* Since we setup a flatpak repo with the architecture being overridden
   * as "arch", we need to override it here too */
  g_setenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", "arch", FALSE);
  g_setenv ("FLATPAK_SYSTEM_HELPER_ON_SESSION", "1", TRUE);
  g_setenv ("GIO_USE_VFS", "local", TRUE);

  /* FIXME: The tests should theoretically be run with a fake D-Bus system bus
   * and a load of mock services. However, those mock services have not been
   * written yet (see https://phabricator.endlessm.com/T25340). In the meantime,
   * to avoid behaviour differences in the tests when running on OBS/Jenkins vs
   * a local VM, disable the system bus for everyone: */
  g_setenv ("DBUS_SYSTEM_BUS_ADDRESS", "unix:/dev/null", TRUE);

  g_test_add ("/flatpak/deploy-flatpak-files-as-expected",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_deploy_flatpak_files_as_expected,
              flatpak_deployments_fixture_teardown);
  g_test_add ("/flatpak/stamp-does-not-deploy-flatpaks",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_stamp_no_deploy_flatpaks,
              flatpak_deployments_fixture_teardown);
  g_test_add ("/flatpak/stamp-counter-file-updated",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_stamp_counter_state_updated,
              flatpak_deployments_fixture_teardown);
  g_test_add ("/flatpak/deploy-flatpak-fail-other-ones-stay-deployed",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_deploy_failure_previous_flatpaks_stay_deployed,
              flatpak_deployments_fixture_teardown);
  g_test_add ("/flatpak/deploy-flatpak-fail-counter-state-updated",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_deploy_failure_counter_state_updated,
              flatpak_deployments_fixture_teardown);
  g_test_add ("/flatpak/deploy-flatpak-fail-resume-from-latest",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_deploy_failure_resume_from_latest,
              flatpak_deployments_fixture_teardown);

  g_test_add ("/flatpak/check-succeeds-if-actions-are-up-to-date",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_flatpak_check_succeeds_if_actions_are_up_to_date,
              flatpak_deployments_fixture_teardown);
  g_test_add ("/flatpak/check-fails-if-installed-flatpak-is-not-installed",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_flatpak_check_fails_if_installed_flatpak_is_not_installed,
              flatpak_deployments_fixture_teardown);
  g_test_add ("/flatpak/check-fails-if-uninstalled-flatpak-is-installed",
              FlatpakDeploymentsFixture,
              NULL,
              flatpak_deployments_fixture_setup,
              test_flatpak_check_fails_if_unininstalled_flatpak_is_installed,
              flatpak_deployments_fixture_teardown);

  return g_test_run ();
}
