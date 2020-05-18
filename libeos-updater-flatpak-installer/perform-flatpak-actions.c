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

#include <flatpak.h>
#include <glib.h>
#include <libeos-updater-flatpak-installer/installer.h>
#include <libeos-updater-util/enums.h>
#include <libeos-updater-util/flatpak-util.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

static gboolean
try_update_application (FlatpakInstallation       *installation,
                        FlatpakRef                *ref,
                        EosUpdaterInstallerFlags   flags,
                        GError                   **error)
{
  FlatpakRefKind kind = flatpak_ref_get_kind (ref);
  const gchar *name = flatpak_ref_get_name (ref);
  const gchar *arch = flatpak_ref_get_arch (ref);
  const gchar *branch = flatpak_ref_get_branch (ref);
  g_autofree gchar *formatted_ref = flatpak_ref_format_ref (ref);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(FlatpakInstalledRef) updated_ref = NULL;

  g_message ("Attempting to update %s", formatted_ref);

  /* Installation may have failed because we can just update instead,
   * try that. */
  updated_ref = flatpak_installation_update (installation,
                                             FLATPAK_UPDATE_FLAGS_NO_PRUNE |
                                             FLATPAK_UPDATE_FLAGS_NO_PULL,
                                             kind,
                                             name,
                                             arch,
                                             branch,
                                             NULL,
                                             NULL,
                                             NULL,
                                             &local_error);

  if (updated_ref == NULL)
    {
      if (g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED))
        {
          g_message ("%s is not installed, so not updating", formatted_ref);
          g_clear_error (&local_error);
          return TRUE;
        }

     /* We also have to check for FLATPAK_ERROR_ALREADY_INSTALLED since this
      * is thrown if there are no updates to complete. Arguably a design flaw
      * in Flatpak itself. */
     if (g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED))
        {
          g_message ("%s is already up to date, so not updating", formatted_ref);
          g_clear_error (&local_error);
          return TRUE;
        }

      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  g_message ("Successfully updated %s", formatted_ref);
  return TRUE;
}

static gboolean
try_install_application (FlatpakInstallation       *installation,
                         const gchar               *collection_id,
                         const gchar               *in_remote_name,
                         FlatpakRef                *ref,
                         EosUpdaterInstallerFlags   flags,
                         GError                   **error)
{
  FlatpakRefKind kind = flatpak_ref_get_kind (ref);
  const gchar *name = flatpak_ref_get_name (ref);
  const gchar *arch = flatpak_ref_get_arch (ref);
  const gchar *branch = flatpak_ref_get_branch (ref);
  g_autofree gchar *formatted_ref = flatpak_ref_format_ref (ref);
  g_autofree gchar *candidate_remote_name = NULL;
  const gchar *remote_name = in_remote_name;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(FlatpakInstalledRef) installed_ref = NULL;

  g_assert (in_remote_name != NULL);

  if (collection_id != NULL)
    {
      g_message ("Finding remote name for %s", collection_id);

      /* Ignore errors here. We always have the @in_remote_name to use. */
      candidate_remote_name = euu_lookup_flatpak_remote_for_collection_id (installation,
                                                                           collection_id,
                                                                           NULL);

      if (candidate_remote_name != NULL &&
          g_strcmp0 (in_remote_name, candidate_remote_name) != 0)
        {
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_FLATPAK_REMOTE_CONFLICT,
                       "Specified flatpak remote ‘%s’ conflicts with the remote "
                       "detected for collection ID ‘%s’ (‘%s’), cannot continue.",
                       in_remote_name,
                       collection_id,
                       candidate_remote_name);
          return FALSE;
        }

      g_message ("Remote name for %s is %s", collection_id, remote_name);
    }

  g_message ("Attempting to install %s:%s", remote_name, formatted_ref);

  /* Installation may have failed because we can just update instead,
   * try that. */
  installed_ref = flatpak_installation_install_full (installation,
                                                     !(flags & EU_INSTALLER_FLAGS_ALSO_PULL) ? FLATPAK_INSTALL_FLAGS_NO_PULL : 0,
                                                     remote_name,
                                                     kind,
                                                     name,
                                                     arch,
                                                     branch,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     &local_error);

  if (installed_ref == NULL)
    {
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED))
        {
          g_message ("Failed to install %s:%s: %s", remote_name, formatted_ref,
                     local_error->message);
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_message ("%s:%s already installed, updating", remote_name, formatted_ref);
      g_clear_error (&local_error);

      installed_ref = flatpak_installation_update (installation,
                                                   FLATPAK_UPDATE_FLAGS_NO_PRUNE |
                                                   (!(flags & EU_INSTALLER_FLAGS_ALSO_PULL) ? FLATPAK_UPDATE_FLAGS_NO_PULL : 0),
                                                   kind,
                                                   name,
                                                   arch,
                                                   branch,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   &local_error);

      if (installed_ref == NULL)
        {
          if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED))
            {
              g_message ("Failed to update %s:%s", remote_name, formatted_ref);
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }

          g_clear_error (&local_error);
        }
    }

  g_message ("Successfully installed or updated %s:%s", remote_name, formatted_ref);
  return TRUE;
}

static gboolean
try_uninstall_application (FlatpakInstallation  *installation,
                           FlatpakRef           *ref,
                           GError              **error)
{
  FlatpakRefKind kind = flatpak_ref_get_kind (ref);
  const gchar *name = flatpak_ref_get_name (ref);
  const gchar *arch = flatpak_ref_get_arch (ref);
  const gchar *branch = flatpak_ref_get_branch (ref);
  g_autofree gchar *formatted_ref = flatpak_ref_format_ref (ref);
  g_autoptr(GError) local_error = NULL;

  g_message ("Attempting to uninstall %s", formatted_ref);

  if (!flatpak_installation_uninstall_full (installation,
                                            FLATPAK_UNINSTALL_FLAGS_NO_PRUNE,
                                            kind,
                                            name,
                                            arch,
                                            branch,
                                            NULL,
                                            NULL,
                                            NULL,
                                            &local_error))
    {
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED))
        {
          g_message ("Could not uninstall %s", formatted_ref);
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_message ("%s already uninstalled", formatted_ref);
      g_clear_error (&local_error);
      return TRUE;
    }

  g_message ("Successfully uninstalled %s", formatted_ref);
  return TRUE;
}

static gboolean
perform_action (FlatpakInstallation        *installation,
                EuuFlatpakRemoteRefAction  *action,
                EosUpdaterInstallerFlags    flags,
                GError                    **error)
{
  const gchar *collection_id = action->ref->collection_id;
  const gchar *remote_name = action->ref->remote;

  switch (action->type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return try_install_application (installation,
                                        collection_id,
                                        remote_name,
                                        action->ref->ref,
                                        flags,
                                        error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE:
        return try_update_application (installation,
                                       action->ref->ref,
                                       flags,
                                       error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
        return try_uninstall_application (installation,
                                          action->ref->ref,
                                          error);
      default:
        g_assert_not_reached ();
    }
}

static void
complain_about_failure_to_update_system_installation_counter (const gchar  *failing_name,
                                                              const gchar  *counter_path,
                                                              const GError *error)
{
  g_autofree gchar *incoming_actions_path = g_build_filename (DATADIR,
                                                              "eos-application-tools",
                                                              "flatpak-autoinstall.d",
                                                              failing_name,
                                                              NULL);

  g_warning ("Failed to update flatpak autoinstall counter: "
             "it is likely that the system will be in an inconsistent "
             "state from this point forward. Consider examining "
             "%s and %s to determine what actions should be manually "
             "applied: %s.",
             counter_path,
             incoming_actions_path,
             error->message);
}

static gboolean
update_counter (const gchar  *counter_path,
                GHashTable   *new_progresses  /* (element-type filename gint32) */,
                GError      **error)
{
  g_autoptr(GFile) counter_file = g_file_new_for_path (counter_path);
  g_autoptr(GFile) parent = g_file_get_parent (counter_file);
  g_autoptr(GKeyFile) counter_keyfile = g_key_file_new ();
  GHashTableIter iter;
  gpointer key, value;
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
                                  counter_path,
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

  g_hash_table_iter_init (&iter, new_progresses);

  while (g_hash_table_iter_next (&iter, &key, &value))
    g_key_file_set_int64 (counter_keyfile, key, "Progress", GPOINTER_TO_INT (value));

  if (!g_key_file_save_to_file (counter_keyfile, counter_path, error))
    return FALSE;

  return TRUE;
}

static void
update_counter_complain_on_error (const gchar *failing_name,
                                  const gchar *counter_path,
                                  GHashTable  *new_progresses  /* (element-type filename gint32) */)
{
  g_autoptr(GError) error = NULL;

  if (!update_counter (counter_path, new_progresses, &error))
    complain_about_failure_to_update_system_installation_counter (failing_name,
                                                                  counter_path,
                                                                  error);
}

/**
 * eufi_apply_flatpak_ref_actions:
 * @installation: a #FlatpakInstallation
 * @state_counter_path: (type filename): path to the counter that records what
 *    actions have been applied
 * @actions: (element-type EuuFlatpakRemoteRefAction): actions to apply
 * @mode: the #EosUpdaterInstallerMode
 * @flags: any #EosUpdaterInstallerFlags
 * @error: return location for a #GError, or %NULL
 *
 * Apply the actions @actions, and update the state counter at
 * @state_counter_path to the last successfully applied action. The actions are
 * only actually performed if @mode is set to %EU_INSTALLER_MODE_PERFORM.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
eufi_apply_flatpak_ref_actions (FlatpakInstallation       *installation,
                                const gchar               *state_counter_path,
                                GPtrArray                 *actions,
                                EosUpdaterInstallerMode    mode,
                                EosUpdaterInstallerFlags   flags,
                                GError                   **error)
{
  gsize i;
  g_autoptr(GHashTable) new_progresses = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  g_return_val_if_fail (FLATPAK_IS_INSTALLATION (installation), FALSE);
  g_return_val_if_fail (state_counter_path != NULL, FALSE);
  g_return_val_if_fail (actions != NULL, FALSE);
  g_return_val_if_fail (mode != EU_INSTALLER_MODE_CHECK, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  for (i = 0; i < actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *pending_action = g_ptr_array_index (actions, i);
      const gchar *source = pending_action->source;
      gboolean is_dependency = (pending_action->flags & EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY) != 0;

      /* Dependencies should not be passed through this function - they
       * were meant to be deployed earlier. Uninstall dependencies will
       * be handled implicitly. Allow them if we’re running
       * `eos-updater-flatpak-installer -mode deploy --pull` manually though. */
      g_assert (!is_dependency || flags & EU_INSTALLER_FLAGS_ALSO_PULL);

      /* Only perform actions if we’re in the "perform" mode. Otherwise
       * we just pretend to perform actions and update the counter
       * accordingly */
      if (mode == EU_INSTALLER_MODE_PERFORM &&
          !perform_action (installation, pending_action, flags, error))
        {
          /* If we fail, we should still update the state of the counter
           * to the last successful one before we get out. This is to ensure
           * that we don’t perform the same action again next time. */
          update_counter_complain_on_error (source,
                                            state_counter_path,
                                            new_progresses);
          return FALSE;
        }

      g_hash_table_replace (new_progresses,
                            (gpointer) source,
                            GINT_TO_POINTER (pending_action->serial));
    }

    /* Once we’re done, update the state of the counter, but bail out
     * if it fails */
    if (!update_counter (state_counter_path,
                         new_progresses,
                         error))
      return FALSE;

  return TRUE;
}

static gboolean
check_if_flatpak_is_installed (FlatpakInstallation        *installation,
                               EuuFlatpakRemoteRefAction  *action,
                               gboolean                   *out_is_installed,
                               GError                    **error)
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
                                                          flatpak_ref_get_arch (ref),
                                                          flatpak_ref_get_branch (ref),
                                                          NULL,
                                                          &local_error);

  if (installed_ref == NULL &&
      !g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  g_clear_error (&local_error);

  *out_is_installed = (installed_ref != NULL);
  g_message ("Flatpak described by ref %s is %s",
             formatted_ref,
             *out_is_installed ? "installed": "not installed");

  return TRUE;
}

/**
 * eufi_check_ref_actions_applied:
 * @installation: a #FlatpakInstallation
 * @actions: (element-type EuuFlatpakRemoteRefAction): actions to apply
 * @error: return location for a #GError, or %NULL
 *
 * Check each action in @actions to see if its operation has been applied. In
 * truth only installs and updates are checked; there's not currently a way to
 * check update operations. If some of the actions haven't been successfully
 * applied, @error will be set with a helpful message.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
eufi_check_ref_actions_applied (FlatpakInstallation  *installation,
                                GPtrArray            *actions,
                                GError              **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) deltas = g_string_new ("");
  gsize i;

  g_return_val_if_fail (installation != NULL, FALSE);

  for (i = 0; i < actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *pending_action = g_ptr_array_index (actions, i);
      const gchar *name = pending_action->source;
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
                                                         name);
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
                                                         "%s but was installed\n",
                                                         formatted_ref,
                                                         name);
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

  if (deltas->len > 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Deltas were detected: %s", deltas->str);

      return FALSE;
    }

  return TRUE;
}

