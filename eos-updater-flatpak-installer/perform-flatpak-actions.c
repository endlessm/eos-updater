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
#include <libeos-updater-util/enums.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>

#include "installer.h"

static const gchar *flatpak_ref_kind_to_str[] = {
  "app", "runtime"
};

static const gchar *
string_for_flatpak_kind (FlatpakRefKind kind)
{
  g_assert (kind >= FLATPAK_REF_KIND_APP && kind <= FLATPAK_REF_KIND_RUNTIME);

  return flatpak_ref_kind_to_str[(gsize) kind];
}

static gboolean
try_update_application (FlatpakInstallation       *installation,
                        FlatpakRefKind             kind,
                        const gchar               *name,
                        EosUpdaterInstallerFlags   flags,
                        GError                   **error)
{
  g_autofree gchar *remote_name = NULL;
  const gchar *formatted_kind = string_for_flatpak_kind (kind);
  g_autoptr(GError) local_error = NULL;

  g_message ("Attempting to update %s/%s", remote_name, formatted_kind, name);

  /* Installation may have failed because we can just update instead,
   * try that. */
  if (!flatpak_installation_update (installation,
                                    FLATPAK_UPDATE_FLAGS_NO_PULL,
                                    kind,
                                    name,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &local_error))
    {
      if (g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED))
        {
          g_message ("%s/%s is not installed, so not updating", formatted_kind, name);
          g_clear_error (&local_error);
          return TRUE;
        }

      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  g_message ("Successfully updated %s/%s", formatted_kind, name);
  return TRUE;
}

static gboolean
try_install_application (FlatpakInstallation       *installation,
                         const gchar               *collection_id,
                         const gchar               *in_remote_name,
                         FlatpakRefKind             kind,
                         const gchar               *name,
                         EosUpdaterInstallerFlags   flags,
                         GError                   **error)
{
  g_autofree gchar *candidate_remote_name = NULL;
  const gchar *remote_name = NULL;
  const gchar *formatted_kind = string_for_flatpak_kind (kind);
  g_autoptr(GError) local_error = NULL;

  if (collection_id != NULL)
    {
      g_message ("Finding remote name for %s", collection_id);

      candidate_remote_name = eos_updater_util_lookup_flatpak_repo_for_collection_id (installation,
                                                                                      collection_id,
                                                                                      error);

      if (in_remote_name != NULL &&
          candidate_remote_name != NULL &&
          g_strcmp0 (in_remote_name, candidate_remote_name) != 0)
        {
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_FLATPAK_REMOTE_CONFLICT,
                       "Specified flatpak remote '%s' conflicts with the remote "
                       "detected for collection ID '%s' ('%s'), cannot continue.",
                       in_remote_name,
                       collection_id,
                       candidate_remote_name);
          return FALSE;
        }

      g_message ("Remote name for %s is %s", collection_id, remote_name);
      remote_name = candidate_remote_name;
    }

  if (remote_name == NULL)
    return FALSE;

  g_message ("Attempting to install %s:%s/%s", remote_name, formatted_kind, name);

  /* Installation may have failed because we can just update instead,
   * try that. */
  if (!flatpak_installation_install_full (installation,
                                          !(flags & EU_INSTALLER_FLAGS_ALSO_PULL) ? FLATPAK_INSTALL_FLAGS_NO_PULL : 0,
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
          g_message ("Failed to install %s:%s/%s", remote_name, formatted_kind, name);
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_message ("%s:%s/%s already installed, updating", remote_name, formatted_kind, name);
      g_clear_error (&local_error);
      if (!flatpak_installation_update (installation,
                                        !(flags & EU_INSTALLER_FLAGS_ALSO_PULL) ? FLATPAK_UPDATE_FLAGS_NO_PULL : 0,
                                        kind,
                                        name,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        error))
        {
          g_message ("Failed to update %s:%s/%s", remote_name, formatted_kind, name);
          return FALSE;
        }
    }

  g_message ("Successfully installed or updated %s:%s/%s", remote_name, formatted_kind, name);
  return TRUE;
}

static gboolean
try_uninstall_application (FlatpakInstallation  *installation,
                           FlatpakRefKind        kind,
                           const gchar          *name,
                           GError              **error)
{
  g_autoptr(GError) local_error = NULL;
  const gchar *formatted_ref_kind = string_for_flatpak_kind (kind);

  g_message ("Attempting to uninstall %s/%s", formatted_ref_kind, name);

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
          g_message ("Could not uninstall %s/%s", formatted_ref_kind, name);
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_message ("%s/%s already uninstalled", formatted_ref_kind, name);
      g_clear_error (&local_error);
      return TRUE;
    }

  g_message ("Successfully uninstalled %s/%s", formatted_ref_kind, name);
  return TRUE;
}

static gboolean
perform_action (FlatpakInstallation      *installation,
                FlatpakRemoteRefAction   *action,
                EosUpdaterInstallerFlags  flags,
                GError                   **error)
{
  const gchar *collection_id = action->ref->collection_id;
  const gchar *remote_name = action->ref->remote;
  FlatpakRefKind kind = flatpak_ref_get_kind (action->ref->ref);
  const gchar *name = flatpak_ref_get_name (action->ref->ref);

  switch (action->type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return try_install_application (installation,
                                        collection_id,
                                        remote_name,
                                        kind,
                                        name,
                                        flags,
                                        error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE:
        return try_update_application (installation,
                                       kind,
                                       name,
                                       flags,
                                       error);
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
update_counter (FlatpakRemoteRefAction  *action,
                const gchar             *source_path,
                GError                 **error)
{
  const gchar *counter_file_path = eos_updater_util_pending_flatpak_deployments_state_path ();
  g_autoptr(GFile) counter_file = g_file_new_for_path (counter_file_path);
  g_autoptr(GFile) parent = g_file_get_parent (counter_file);
  g_autoptr(GKeyFile) counter_keyfile = g_key_file_new ();
  g_autoptr(GError) local_error = NULL;

  /* Ensure that the directory and the key file are created */
  if (!g_file_make_directory_with_parents (parent, NULL, &local_error))
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
    }

  if (!g_key_file_load_from_file (counter_keyfile, 
                                  counter_file_path,
                                  G_KEY_FILE_NONE,
                                  &local_error))
    {
      if (!g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
    }

  g_key_file_set_int64 (counter_keyfile, source_path, "Progress", action->serial);

  if (!g_key_file_save_to_file (counter_keyfile, counter_file_path, error))
    return FALSE;

  return TRUE;
}

static void
update_counter_complain_on_error (FlatpakRemoteRefAction  *action,
                                  const gchar             *source_path)
{
  g_autoptr(GError) error = NULL;

  if (!update_counter (action, source_path, &error))
    complain_about_failure_to_update_system_installation_counter (source_path,
                                                                  error);
}

gboolean
eos_updater_flatpak_installer_apply_flatpak_ref_actions (FlatpakInstallation      *installation,
                                                         GHashTable               *table,
                                                         EosUpdaterInstallerMode   mode,
                                                         EosUpdaterInstallerFlags  pull,
                                                         GError                  **error)
{
  gpointer key;
  gpointer value;
  GHashTableIter hash_iter;

  if (!installation)
    return FALSE;

  g_hash_table_iter_init (&hash_iter, table);

  while (g_hash_table_iter_next (&hash_iter, &key, &value))
    {
      const gchar *source_path = key;
      GPtrArray *pending_actions = value;
      gsize i;
      FlatpakRemoteRefAction *last_successful_action = NULL;

      for (i = 0 ; i < pending_actions->len; ++i)
        {
          FlatpakRemoteRefAction *pending_action = g_ptr_array_index (pending_actions, i);

          /* Only perform actions if we're in the "perform" mode. Otherwise
           * we just pretend to perform actions and update the counter
           * accordingly */
          if (mode == EU_INSTALLER_MODE_PERFORM &&
              !perform_action (installation, pending_action, pull, error))
            {
              /* If we fail, we should still update the state of the counter
               * to the last successful before we get out, this is to ensure
               * that we don't perform the same action again next time */
              if (last_successful_action)
                update_counter_complain_on_error (last_successful_action,
                                                  source_path);
              return FALSE;
            }

          last_successful_action = pending_action;
        }

      /* Once we're done, update the state of the counter, but bail out
       * if it fails */
      if (last_successful_action &&
          !update_counter (last_successful_action, source_path, error))
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
  FlatpakRef *ref = action->ref->ref;
  g_autofree gchar *formatted_ref = flatpak_ref_format_ref (action->ref->ref);

  g_return_val_if_fail (out_is_installed != NULL, FALSE);

  g_message ("Checking if flatpak described by ref %s is installed",
             formatted_ref);

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
  g_message ("Flatpak described by ref %s is installed",
             formatted_ref,
             *out_is_installed ? "installed": "not installed");

  return TRUE;
}

gboolean
eos_updater_flatpak_installer_check_ref_actions_applied (FlatpakInstallation  *installation,
                                                         const gchar          *pending_flatpak_deployments_state_path,
                                                         GHashTable           *table,
                                                         GError              **error)
{
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
      g_autofree gchar *counter_path = g_build_filename (pending_flatpak_deployments_state_path,
                                                         name,
                                                         NULL);
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
                    g_autofree gchar *formatted_ref = flatpak_ref_format_ref (pending_action->ref->ref);
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
                    g_autofree gchar *formatted_ref = flatpak_ref_format_ref (pending_action->ref->ref);
                    g_autofree gchar *msg = g_strdup_printf ("Flatpak %s should have been uninstalled by "
                                                             "%s but was installed",
                                                             formatted_ref,
                                                             counter_path);
                    g_string_append (deltas, msg);
                  }
                break;
              case EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE:
                /* Nothing meaningful we can do here - the flatpak is meant
                 * to be installed if it would have been installed before
                 * otherwise it stays uninstalled */
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

