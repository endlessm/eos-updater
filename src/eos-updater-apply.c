/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 *  - Vivek Dasmohapatra <vivek@etla.org>
 */

#include "eos-updater-apply.h"
#include "eos-updater-data.h"
#include "eos-updater-object.h"

#include <string.h>
#include <errno.h>

#include <libeos-updater-util/types.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/util.h>

#include <flatpak.h>
#include <ostree.h>

static void
apply_finished (GObject *object,
                GAsyncResult *res,
                gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask *task;
  GError *error = NULL;
  gboolean bootver_changed = FALSE;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  task = G_TASK (res);
  bootver_changed = g_task_propagate_boolean (task, &error);

  if (!bootver_changed)
    g_message ("System redeployed same boot version");

  if (error)
    {
      eos_updater_set_error (updater, error);
      g_clear_error (&error);
    }
  else
    {
      eos_updater_clear_error (updater, EOS_UPDATER_STATE_UPDATE_APPLIED);
    }

  return;

 invalid_task:
  /* Either the threading or the memory management is shafted. Or both.
   * We're boned. Log an error and activate the self destruct mechanism.
   */
  g_error ("Invalid async task object when returning from Poll() thread!");
  g_assert_not_reached ();
}

static const gchar *
get_test_osname (void)
{
  return g_getenv ("EOS_UPDATER_TEST_UPDATER_OSTREE_OSNAME");
}

static GFile *
get_temporary_directory_to_checkout_in (GError **error)
{
  g_autofree gchar *temp_dir = g_dir_make_tmp ("ostree-checkout-XXXXXX", error);
  g_autofree gchar *path = NULL;

  if (!temp_dir)
    return NULL;

  path = g_build_filename (temp_dir, "checkout", NULL);
  return g_file_new_for_path (path);
}

static GHashTable *
flatpak_ref_actions_for_commit (OstreeRepo    *repo,
                                const gchar   *checksum,
                                GCancellable  *cancellable,
                                GError       **error)
{
  g_autofree gchar *checkout_directory_path = NULL;
  g_autoptr(GFile) checkout_directory = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_table = NULL;
  OstreeRepoCheckoutAtOptions options = { 0, };

  checkout_directory = get_temporary_directory_to_checkout_in (error);
  checkout_directory_path = g_file_get_path (checkout_directory);

  /* Now that we have a temporary directory, checkout the OSTree in it
   * at the /usr/share/eos-application-tools path. If it fails, there's nothing to
   * read, otherwise we can read in the list of flatpaks to be auto-installed
   * for this commit. */
  options.subpath = "usr/share/eos-application-tools/flatpak-autoinstall.d";

  if (!ostree_repo_checkout_at (repo,
                                &options,
                                -1,
                                checkout_directory_path,
                                checksum,
                                cancellable,
                                NULL))
    {
      eos_updater_remove_recursive (checkout_directory, NULL);

      /* In this case we return an empty hashtable */
      return g_hash_table_new (NULL, NULL);
    }

  flatpak_ref_actions_table = eos_updater_util_flatpak_ref_actions_from_directory (checkout_directory,
                                                                                   cancellable,
                                                                                   error);

  /* Regardless of whether there was an error, we always want to remove
   * the checkout directory at this point */
  eos_updater_remove_recursive (checkout_directory, NULL);

  if (!flatpak_ref_actions_table)
    return NULL;

  if (!ostree_repo_checkout_gc (repo, cancellable, error))
    return NULL;

  return g_steal_pointer (&flatpak_ref_actions_table);
}

/* Clean up any flatpaks in flatpaks_to_deploy up until right_bound. This
 * function is called in cases where one of the flatpaks failed to pull
 * for some reason and we need to roll back the other pulled flatpaks. This
 * function "never fails", in the sense that if a rollback fails
 * the best we can do is just warn about it and move on. */
static void
cleanup_undeployed_flatpaks_in_installation (FlatpakInstallation *installation,
                                             GPtrArray           *pending_flatpak_ref_actions,
                                             gsize                right_bound)
{
  g_autoptr(GError) error = NULL;
  gsize i = 0;

  g_return_if_fail (right_bound <= pending_flatpak_ref_actions->len);

  for (; i < right_bound; ++i)
    {
      FlatpakRemoteRefAction *action = g_ptr_array_index (pending_flatpak_ref_actions, i);
      FlatpakRemoteRef *to_deploy = action->ref;
      const char *remote_name = flatpak_remote_ref_get_remote_name (to_deploy);
      g_autofree gchar *ref = NULL;

      if (action->type != EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL)
        continue;

      ref = flatpak_ref_format_ref (FLATPAK_REF (to_deploy));

      if (!flatpak_installation_remove_local_ref_sync (installation,
                                                       remote_name,
                                                       ref,
                                                       NULL,
                                                       &error))
        {
          g_warning ("Couldn't clean up undeployed ref %s:%s: %s",
                     remote_name,
                     ref,
                     error->message);
          g_clear_error (&error);
        }
    }

  if (!flatpak_installation_prune_local_repo (installation, NULL, &error))
    {
      g_warning ("Could not prune orphaned objects in local repo: %s",
                 error->message);
      g_clear_error (&error);
    }
}

static gboolean
cleanup_undeployed_flatpaks (GPtrArray *pending_flatpak_ref_actions,
                             gsize      right_bound,
                             GError   **error)
{
  g_autoptr(FlatpakInstallation) installation = eos_updater_get_flatpak_installation (NULL,
                                                                                      error);

  if (!installation)
    return FALSE;

  cleanup_undeployed_flatpaks_in_installation (installation,
                                               pending_flatpak_ref_actions,
                                               right_bound);

  return TRUE;
}

static gboolean
pull_flatpaks (GPtrArray     *pending_flatpak_ref_actions,
               GCancellable  *cancellable,
               GError       **error)
{
  g_autoptr(FlatpakInstallation) installation = eos_updater_get_flatpak_installation (cancellable,
                                                                                      error);
  gsize i = 0;

  if (!installation)
    return FALSE;

  for (; i < pending_flatpak_ref_actions->len; ++i)
    {
      FlatpakRemoteRefAction *action = g_ptr_array_index (pending_flatpak_ref_actions, i);
      FlatpakRemoteRef *to_install = action->ref;
      g_autoptr(GError) local_error = NULL;

      if (action->type != EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL)
        continue;

      /* We have to pass in a local_error instance here and check to see
       * if it was FLATPAK_ERROR_ONLY_PULLED - this is what will be
       * thrown if we succeeded at pulling the flatpak into the local
       * repository but did not deploy it (since no FlatpakInstalledRef
       * will be returned). */
      flatpak_installation_install_full (installation,
                                         FLATPAK_INSTALL_FLAGS_NO_DEPLOY,
                                         flatpak_remote_ref_get_remote_name (to_install),
                                         flatpak_ref_get_kind (FLATPAK_REF (to_install)),
                                         flatpak_ref_get_name (FLATPAK_REF (to_install)),
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL,
                                         cancellable,
                                         &local_error);

      /* This is highly highly unlikely to happen and should usually only
       * occurr in cases of deployment or programmer error,
       * FLATPAK_INSTALL_FLAGS_NO_DEPLOY is documented to always return
       * the error FLATPAK_ERROR_ONLY_PULLED */
      if (!local_error)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Flatpak installation should not have succeeded!");
          cleanup_undeployed_flatpaks_in_installation (installation, pending_flatpak_ref_actions, i);
          return FALSE;
        }

      /* Something unexpected failed, return early now.
       *
       * We are not able to meaningfully clean up here - the refs will remain
       * in the flatpak ostree repo for the next time we want to install them
       * but there's nothing in the public API that will allow us to get rid of
       * them. */
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ONLY_PULLED))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          cleanup_undeployed_flatpaks_in_installation (installation, pending_flatpak_ref_actions, i);
          return FALSE;
        }
    }

  return TRUE;
}

static GPtrArray *
prepare_flatpaks_to_deploy (OstreeRepo    *repo,
                            const gchar   *update_id,
                            GCancellable  *cancellable,
                            GError       **error)
{
  g_autoptr(GHashTable) flatpak_ref_actions_this_commit_wants = NULL;
  g_autoptr(GHashTable) flatpak_ref_action_progresses = NULL;
  g_autoptr(GHashTable) relevant_flatpak_ref_actions = NULL;
  g_autoptr(GPtrArray) flatpaks_to_deploy = NULL;

  flatpak_ref_actions_this_commit_wants = flatpak_ref_actions_for_commit (repo,
                                                                          update_id,
                                                                          cancellable,
                                                                          error);

  if (!flatpak_ref_actions_this_commit_wants)
    return NULL;

  flatpak_ref_action_progresses =
    eos_updater_util_flatpak_ref_action_application_progress_in_state_path (cancellable,
                                                                            error);

  if (!flatpak_ref_action_progresses)
    return NULL;

  /* Filter the flatpak ref actions for the ones which are actually relevant
   * to this system and figure out which flatpaks need to be pulled from
   * there */
  relevant_flatpak_ref_actions = eos_updater_util_filter_for_new_flatpak_ref_actions (flatpak_ref_actions_this_commit_wants,
                                                                                      flatpak_ref_action_progresses);

  /* Convert the hash table into a single linear array of flatpaks to pull. The
   * reason we need a linear array is that on failure, we need to roll back
   * any flatpaks which were deployed */
  flatpaks_to_deploy = eos_updater_util_flatten_flatpak_ref_actions_table (relevant_flatpak_ref_actions);

  if (!pull_flatpaks (flatpaks_to_deploy, cancellable, error))
    return NULL;

  return g_steal_pointer (&flatpaks_to_deploy);
}

static OstreeDeployment *
deploy_new_sysroot (EosUpdater     *updater,
                    OstreeRepo     *repo,
                    OstreeSysroot  *sysroot,
                    const gchar    *update_id,
                    GCancellable   *cancellable,
                    GError        **error)
{
  const gchar *update_refspec = eos_updater_get_update_refspec (updater);
  const gchar *orig_refspec = eos_updater_get_original_refspec (updater);
  g_autoptr(OstreeDeployment) booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                                                         error);
  g_autoptr(OstreeDeployment) new_deployment = NULL;
  g_autoptr(GKeyFile) origin = NULL;
  const gchar *osname = get_test_osname ();

  if (booted_deployment == NULL)
    return NULL;

  origin = ostree_sysroot_origin_new_from_refspec (sysroot, update_refspec);

  if (!ostree_sysroot_deploy_tree (sysroot,
                                   osname,
                                   update_id,
                                   origin,
                                   booted_deployment,
                                   NULL,
                                   &new_deployment,
                                   cancellable,
                                   error))
    return NULL;

  /* If the original refspec is not the update refspec, then we may have
   * a ref to a no longer needed tree. Delete that remote ref so the
   * cleanup done in simple_write_deployment() really removes that tree
   * if no deployments point to it anymore.
   */
  if (g_strcmp0 (update_refspec, orig_refspec) != 0)
    {
      g_autofree gchar *rev = NULL;

      if (!ostree_repo_resolve_rev (repo, orig_refspec, TRUE, &rev, error))
        return NULL;

      if (rev)
        {
          if (!ostree_repo_prepare_transaction (repo, NULL, cancellable, error))
            return NULL;

          ostree_repo_transaction_set_refspec (repo, orig_refspec, NULL);

          if (!ostree_repo_commit_transaction (repo, NULL, cancellable, error))
            return NULL;
        }
    }

  if (!ostree_sysroot_simple_write_deployment (sysroot,
                                               osname,
                                               new_deployment,
                                               booted_deployment,
                                               OSTREE_SYSROOT_SIMPLE_WRITE_DEPLOYMENT_FLAGS_NO_CLEAN,
                                               cancellable,
                                               error))
    return NULL;

  return g_steal_pointer (&new_deployment);
}

static gboolean
apply_internal (EosUpdater     *updater,
                EosUpdaterData *data,
                GCancellable   *cancel,
                gboolean       *out_bootversion_changed,
                GError        **error)
{
  OstreeRepo *repo = data->repo;
  const gchar *update_id = eos_updater_get_update_id (updater);
  gint bootversion = -1;
  gint newbootver = -1;
  g_autoptr(OstreeDeployment) new_deployment = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autoptr(GPtrArray) flatpaks_to_deploy = NULL;
  g_autoptr(GFile) flatpaks_to_deploy_file = NULL;
  g_autoptr(GError) local_error = NULL;

  sysroot = ostree_sysroot_new_default ();
  /* The sysroot lock must be taken to prevent multiple processes (like this
   * and ostree admin upgrade) from deploying simultaneously, which will fail.
   * The lock will be unlocked automatically when sysroot is deallocated.
   */
  if (!ostree_sysroot_lock (sysroot, error))
    return FALSE;
  if (!ostree_sysroot_load (sysroot, cancel, error))
    return FALSE;

  bootversion = ostree_sysroot_get_bootversion (sysroot);

  /* Empty array just means that there were no flatpaks to deploy, otherwise
   * there was an error pulling flatpaks and we need to return early */
  flatpaks_to_deploy = prepare_flatpaks_to_deploy (repo, update_id, cancel, error);
  if (!flatpaks_to_deploy)
    return FALSE;

  /* Deploy the new system, but roll back pulled flatpaks if that fails */
  new_deployment = deploy_new_sysroot (updater,
                                       repo,
                                       sysroot,
                                       update_id,
                                       cancel,
                                       error);

  if (!new_deployment)
    {
      if (flatpaks_to_deploy &&
          !cleanup_undeployed_flatpaks (flatpaks_to_deploy,
                                        flatpaks_to_deploy->len,
                                        &local_error))
        {
          g_warning ("Failed to clean up undeployed flatpaks: %s", local_error->message);
          g_clear_error (&local_error);
        }

      g_file_delete (flatpaks_to_deploy_file, NULL, NULL);
      return FALSE;
    }

  newbootver = ostree_deployment_get_deployserial (new_deployment);

  /* FIXME: Cleaning up after update should be non-fatal, since we've
   * already successfully deployed the new OS. This clearly is a
   * workaround for a more serious issue, likely related to concurrent
   * prunes (https://phabricator.endlessm.com/T16736). */
  if (!ostree_sysroot_cleanup (sysroot, cancel, &local_error))
    g_warning ("Failed to clean up the sysroot after successful deployment: %s",
               local_error->message);
  g_clear_error (&local_error);

  *out_bootversion_changed = bootversion != newbootver;
  return TRUE;
}

static void
apply (GTask *task,
       gpointer object,
       gpointer task_data,
       GCancellable *cancel)
{
  g_autoptr(GError) local_error = NULL;
  EosUpdater *updater = EOS_UPDATER (object);
  EosUpdaterData *data = task_data;
  gboolean bootversion_changed;
  g_autoptr(GMainContext) task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  if (!apply_internal (updater,
                       data,
                       cancel,
                       &bootversion_changed,
                       &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_boolean (task, bootversion_changed);

  g_main_context_pop_thread_default (task_context);
}

gboolean
handle_apply (EosUpdater            *updater,
              GDBusMethodInvocation *call,
              gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  EosUpdaterState state = eos_updater_get_state (updater);

  if (state != EOS_UPDATER_STATE_UPDATE_READY)
    {
      g_dbus_method_invocation_return_error (call,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
        "Can't call Apply() while in state %s",
        eos_updater_state_to_string (state));
      return TRUE;
    }

  eos_updater_clear_error (updater, EOS_UPDATER_STATE_APPLYING_UPDATE);
  task = g_task_new (updater, NULL, apply_finished, user_data);
  g_task_set_task_data (task, user_data, NULL);
  g_task_run_in_thread (task, apply);

  eos_updater_complete_apply (updater, call);

  return TRUE;
}
