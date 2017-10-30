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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <locale.h>
#include <flatpak.h>
#include <ostree.h>
#include <sysexits.h>
#include <libeos-updater-util/enums.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/util.h>

#include "installer.h"

static int
usage (GOptionContext *context,
       const gchar    *error_message,
       ...) G_GNUC_PRINTF (2, 3);

static int
usage (GOptionContext *context,
       const gchar    *error_message,
       ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;
  g_autofree gchar *help = NULL;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  help = g_option_context_get_help (context, TRUE, NULL);
  g_printerr ("%s: %s\n\n%s\n", g_get_prgname (), formatted_message, help);

  return 128;
}

static const gchar *
get_datadir (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_OSTREE_DATADIR",
                                    DATADIR);
}

static GHashTable *
incoming_flatpak_ref_actions (GError **error)
{
  g_autofree gchar *ref_actions_path = g_build_filename (get_datadir (),
                                                         "eos-application-tools",
                                                         "flatpak-autoinstall.d",
                                                         NULL);
  g_autoptr(GFile) ref_actions_directory = g_file_new_for_path (ref_actions_path);

  return eos_updater_util_flatpak_ref_actions_from_directory (ref_actions_directory,
                                                              NULL,
                                                              error);
}

static gboolean
try_install_application (FlatpakInstallation  *installation,
                         const gchar          *remote_name,
                         FlatpakRefKind        kind,
                         const gchar          *name,
                         GError              **error)
{
  g_autoptr(GError) local_error = NULL;

  /* Installation may have failed because we can just update instead,
   * try that. */
  if (!flatpak_installation_install_full (installation,
                                          FLATPAK_INSTALL_FLAGS_NO_PULL,
                                          remote_name,
                                          kind,
                                          name,
                                          NULL,
                                          NULL,
                                          NULL,
                                          NULL,
                                          NULL,
                                          NULL,
                                          &local_error))
    {
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
      if (!flatpak_installation_update (installation,
                                        FLATPAK_UPDATE_FLAGS_NO_PULL,
                                        kind,
                                        name,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
try_uninstall_application (FlatpakInstallation  *installation,
                           FlatpakRefKind        kind,
                           const gchar          *name,
                           GError              **error)
{
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_installation_uninstall (installation,
                                       kind,
                                       name,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &local_error))
    {
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
      return TRUE;
    }

  return TRUE;
}

static gboolean
perform_action (FlatpakInstallation     *installation,
                FlatpakRemoteRefAction  *action,
                GError                 **error)
{
  const gchar *remote_name = flatpak_remote_ref_get_remote_name (action->ref);
  FlatpakRefKind kind = flatpak_ref_get_kind (FLATPAK_REF (action->ref));
  const gchar *name = flatpak_ref_get_name (FLATPAK_REF (action->ref));

  switch (action->type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return try_install_application (installation, remote_name, kind, name, error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
        return try_uninstall_application (installation, kind, name, error);
      default:
        g_assert_not_reached ();
    }
}

static void
complain_about_failure_to_update_system_installation_counter (const gchar *failing_name,
                                                              GError      *error)
{
  g_autofree gchar *counter_path = g_build_filename (eos_updater_util_pending_flatpak_deployments_state_path (),
                                                     failing_name,
                                                     NULL);
  g_autofree gchar *incoming_actions_path = g_build_filename (DATADIR,
                                                              "eos-application-tools",
                                                              "flatpak-autoinstall.d",
                                                              failing_name,
                                                              NULL);

  g_warning ("Failed to update flatpak autoinstallations counter, "
             "it is likely that the system will be in an inconsistent "
             "state from this point forward. Consider examining "
             "%s and %s to determine what actions should be manually "
             "applied: %s.",
             counter_path,
             incoming_actions_path,
             error->message);
}

static gboolean
update_counter (GFile                   *counter_file,
                FlatpakRemoteRefAction  *action,
                GError                 **error)
{
  g_autoptr(GFile) parent = g_file_get_parent (counter_file);
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *counter_as_string = g_strdup_printf ("%lu", action->serial);

  if (!g_file_make_directory_with_parents (parent, NULL, &local_error))
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
    }

  return g_file_replace_contents (counter_file,
                                  counter_as_string,
                                  strlen (counter_as_string),
                                  NULL,
                                  FALSE,
                                  G_FILE_CREATE_NONE,
                                  NULL,
                                  NULL,
                                  error);
}

static void
update_counter_complain_on_error (GFile                   *counter_file,
                                  FlatpakRemoteRefAction  *action,
                                  const gchar             *name)
{
  g_autoptr(GError) error = NULL;

  if (!update_counter (counter_file, action, &error))
    complain_about_failure_to_update_system_installation_counter (name,
                                                                  error);
}

static gboolean
apply_flatpak_ref_actions (GHashTable               *table,
                           EosUpdaterInstallerMode   mode,
                           GError                  **error)
{
  g_autoptr(FlatpakInstallation) installation = eos_updater_get_flatpak_installation (NULL, error);
  gpointer key;
  gpointer value;
  GHashTableIter hash_iter;

  if (!installation)
    return FALSE;

  g_hash_table_iter_init (&hash_iter, table);

  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    {
      const gchar *name = key;
      GPtrArray *pending_actions = value;
      g_autofree gchar *counter_path = g_build_filename (eos_updater_util_pending_flatpak_deployments_state_path (),
                                                         name,
                                                         NULL);
      g_autoptr(GFile) counter_file = g_file_new_for_path (counter_path);
      gsize i;
      FlatpakRemoteRefAction *last_successful_action = NULL;

      for (i = 0 ; i < pending_actions->len; ++i)
        {
          FlatpakRemoteRefAction *pending_action = g_ptr_array_index (pending_actions, i);

          /* Only perform actions if we're in the "perform" mode. Otherwise
           * we just pretend to perform actions and update the counter
           * accordingly */
          if (mode == EU_INSTALLER_MODE_PERFORM &&
              !perform_action (installation, pending_action, error))
            {
              /* If we fail, we should still update the state of the counter
               * to the last successful before we get out, this is to ensure
               * that we don't perform the same action again next time */
              if (last_successful_action)
                update_counter_complain_on_error (counter_file,
                                                  last_successful_action,
                                                  source_path);
              return FALSE;
            }

          last_successful_action = pending_action;
        }

      /* Once we're done, update the state of the counter, but bail out
       * if it fails */
      if (last_successful_action &&
          !update_counter (counter_file, last_successful_action, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
check_if_flatpak_is_installed (FlatpakInstallation     *installation,
                               FlatpakRemoteRefAction  *action,
                               gboolean                *out_is_installed,
                               GError                 **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(FlatpakInstalledRef) installed_ref = NULL;
  FlatpakRef *ref = FLATPAK_REF (action->ref);

  g_return_val_if_fail (out_is_installed != NULL, FALSE);

  installed_ref = flatpak_installation_get_installed_ref (installation,
                                                          flatpak_ref_get_kind (ref),
                                                          flatpak_ref_get_name (ref),
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          &local_error);

  if (!installed_ref &&
      !g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  *out_is_installed = (installed_ref != NULL);

  return TRUE;
}

static gboolean
check_flatpak_ref_actions_applied (GHashTable  *table,
                                   GError     **error)
{
  g_autoptr(FlatpakInstallation) installation = eos_updater_get_flatpak_installation (NULL, error);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) deltas = g_string_new ("");
  gpointer key;
  gpointer value;
  GHashTableIter hash_iter;

  if (!installation)
    return FALSE;

  g_hash_table_iter_init (&hash_iter, table);

  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    {
      const gchar *name = key;
      GPtrArray *pending_actions = value;
      g_autofree gchar *counter_path = g_build_filename (eos_updater_util_pending_flatpak_deployments_state_path (),
                                                         name,
                                                         NULL);
      g_autoptr(GFile) counter_file = g_file_new_for_path (counter_path);
      gsize i;

      for (i = 0 ; i < pending_actions->len; ++i)
        {
          FlatpakRemoteRefAction *pending_action = g_ptr_array_index (pending_actions, i);
          gboolean is_installed;

          switch (pending_action->type)
            {
              case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
                if (!check_if_flatpak_is_installed (installation,
                                                    pending_action,
                                                    &is_installed,
                                                    error))
                  return FALSE;

                if (!is_installed)
                  {
                    g_autofree gchar *formatted_ref = flatpak_ref_format_ref (FLATPAK_REF (pending_action->ref));
                    g_autofree gchar *msg = g_strdup_printf ("Flatpak %s should have been installed by "
                                                             "%s but was not installed\n",
                                                             formatted_ref,
                                                             counter_path);
                    g_string_append (deltas, msg);
                  }
                break;
              case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
                if (!check_if_flatpak_is_installed (installation,
                                                    pending_action,
                                                    &is_installed,
                                                    error))
                  return FALSE;

                if (is_installed)
                  {
                    g_autofree gchar *formatted_ref = flatpak_ref_format_ref (FLATPAK_REF (pending_action->ref));
                    g_autofree gchar *msg = g_strdup_printf ("Flatpak %s should have been uninstalled by "
                                                             "%s but was installed",
                                                             formatted_ref,
                                                             counter_path);
                    g_string_append (deltas, msg);
                  }
                break;
              default:
                g_assert_not_reached ();
            }
        }
    }

  if (deltas->len)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Deltas were detected: %s",
                   deltas->str);

      return FALSE;
    }

  return TRUE;
}

static gboolean
parse_installer_mode (const gchar              *mode,
                      EosUpdaterInstallerMode  *out_mode,
                      GError                  **error)
{
  GEnumClass *enum_class = g_type_class_ref (EOS_TYPE_UPDATER_INSTALLER_MODE);
  GEnumValue *enum_value = g_enum_get_value_by_nick (enum_class, mode);

  g_type_class_unref (enum_class);

  if (!enum_value)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Invalid installer mode %s",
                   mode);
      return FALSE;
    }

  *out_mode = (EosUpdaterInstallerMode) enum_value->value;

  return TRUE;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GFile) flatpaks_to_export_file = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_for_this_boot = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_progress = NULL;
  EosUpdaterInstallerMode parsed_mode;

  gchar *mode = NULL;
  GOptionEntry entries[] =
  {
    { "mode", 'm', 0, G_OPTION_ARG_STRING, &mode, "Mode to use (perform, stamp, check)", NULL },
    { NULL }
  };

  setlocale (LC_ALL, "");

  context = g_option_context_new ("— Endless OS Updater - Flatpak Installer");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Install flatpak packages on system boot");

  if (!g_option_context_parse (context, &argc, &argv, &error))
    return usage (context, "Failed to parse options: %s", error->message);

  if (!parse_installer_mode (mode != NULL ? mode : "perform",
                             &parsed_mode,
                             &error))
    {
      g_message ("%s", error->message);
      return EXIT_FAILURE;
    }

  flatpak_ref_actions_for_this_boot = incoming_flatpak_ref_actions (&error);

  if (!flatpak_ref_actions_for_this_boot)
    {
      g_message ("Could get flatpak ref actons for this OSTree deployment: %s",
                 error->message);
      return EXIT_FAILURE;
    }

  flatpak_ref_actions_progress = eos_updater_util_flatpak_ref_action_application_progress_in_state_path (NULL, &error);

  if (!flatpak_ref_actions_progress)
    {
      g_message ("Could not get information on which flatpak ref actions have been applied: %s",
                 error->message);
      return EXIT_FAILURE;
    }

  /* Check mode is completely different - we need to read in the action
   * application state and check if there's a delta between what we expect
   * and what we have.
   *
   * Note that on a user system it might be perfectly legitimate for there
   * to be a delta, because the user might have uninstalled or installed
   * an app we marked as auto-install or auto-uninstall. Generally speaking
   * you would use this mode on the image builder to catch situations where
   * the apps list is out of sync.
   */
  switch (parsed_mode)
    {
      case EU_INSTALLER_MODE_CHECK:
        {
          g_autoptr(GHashTable) flatpak_ref_actions_to_check =
            eos_updater_util_filter_for_existing_flatpak_ref_actions (flatpak_ref_actions_for_this_boot,
                                                                      flatpak_ref_actions_progress);
          if (!check_flatpak_ref_actions_applied (flatpak_ref_actions_to_check,
                                                  &error))
            {
              g_message ("Flatpak ref actions are not up to date: %s",
                         error->message);
              return EXIT_FAILURE;
            }

          break;
        }
      case EU_INSTALLER_MODE_PERFORM:
      case EU_INSTALLER_MODE_STAMP:
        {
          g_autoptr(GHashTable) new_flatpak_ref_actions_to_apply =
            eos_updater_util_filter_for_new_flatpak_ref_actions (flatpak_ref_actions_for_this_boot,
                                                                 flatpak_ref_actions_progress);

          if (!apply_flatpak_ref_actions (new_flatpak_ref_actions_to_apply,
                                          parsed_mode,
                                          &error))
            {
               g_message ("Couldn't apply some flatpak update actions for this boot: %s",
                          error->message);
               return EXIT_FAILURE;
            }

          break;
        }
      default:
        g_assert_not_reached ();
    }

  return EXIT_SUCCESS;
}
