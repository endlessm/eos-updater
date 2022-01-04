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

#include <eos-updater/apply.h>
#include <eos-updater/data.h>
#include <eos-updater/object.h>
#include <libeos-updater-util/ostree-util.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>
#include <ostree.h>

/* Closure containing the data for the apply worker thread. The
 * worker thread must not access EosUpdater or EosUpdaterData directly,
 * as they are not thread safe. */
typedef struct
{
  gchar *update_id;  /* (owned) */
  gchar *update_refspec;  /* (owned) */
  gchar *orig_refspec;  /* (owned) */
  OstreeRepo *repo;  /* (owned) */
} ApplyData;

static void
apply_data_free (ApplyData *data)
{
  g_free (data->update_id);
  g_free (data->update_refspec);
  g_free (data->orig_refspec);
  g_clear_object (&data->repo);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ApplyData, apply_data_free)

static void
apply_finished (GObject *object,
                GAsyncResult *res,
                gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask *task;
  GError *error = NULL;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  task = G_TASK (res);

  if (!g_task_propagate_boolean (task, &error))
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

static gboolean
update_remote_collection_id (OstreeRepo   *repo,
                             const gchar  *refspec,
                             GError      **error)
{
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *cur_collection_id = NULL;
  g_autofree gchar *checksum = NULL;
  g_autofree gchar *remote_group = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) metadata = NULL;
  g_autoptr(GKeyFile) config = NULL;
  const char *collection_id;

  if (!ostree_parse_refspec (refspec, &remote, &ref, error))
    return FALSE;
  if (remote == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid refspec ‘%s’ in origin: did not contain a remote name",
                   refspec);
      return FALSE;
    }

  /* If the remote already has a collection ID set, there's nothing to do */
  if (!ostree_repo_get_remote_option (repo, remote, "collection-id", NULL,
                                      &cur_collection_id, error))
    return FALSE;
  if (cur_collection_id != NULL && *cur_collection_id != '\0')
    return TRUE;

  if (!ostree_repo_resolve_rev (repo, refspec, FALSE, &checksum, error))
    return FALSE;
  if (!ostree_repo_load_variant (repo,
                                 OSTREE_OBJECT_TYPE_COMMIT,
                                 checksum,
                                 &commit,
                                 error))
    return FALSE;

  metadata = g_variant_get_child_value (commit, 0);
  if (!g_variant_lookup (metadata, OSTREE_COMMIT_META_KEY_COLLECTION_BINDING, "&s", &collection_id))
    {
      g_debug ("No ostree.collection-binding metadata key on commit; not setting a collection ID locally");
      return TRUE;
    }

  /* FIXME: Update the config file directly. If ostree ever gains
   * sane remote modification, use that. */
  config = ostree_repo_copy_config (repo);
  remote_group = g_strdup_printf ("remote \"%s\"", remote);

  /* Make sure that the remote group exists in the config file and
   * the remote isn't defined in a remotes.d conf file. That
   * shouldn't happen, but adding the group (which
   * g_key_file_set_value() will do) would create a duplicate remote
   * that would prevent the repo from opening again. */
  if (!g_key_file_has_group (config, remote_group))
    {
      GFile *repo_file = ostree_repo_get_path (repo);
      g_autofree gchar *repo_path = g_file_get_path (repo_file);

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Remote ‘%s’ does not exist in %s/config",
                   remote, repo_path);
      return FALSE;
    }

  g_key_file_set_value (config, remote_group, "collection-id", collection_id);
  if (!ostree_repo_write_config (repo, config, error))
    return FALSE;

  return TRUE;
}

static gboolean
update_remote_branches (OstreeRepo   *repo,
                        const gchar  *refspec,
                        GError      **error)
{
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *cur_branches = NULL;
  g_autofree gchar *new_branches = NULL;

  if (!ostree_parse_refspec (refspec, &remote, &ref, error))
    return FALSE;
  if (remote == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid refspec ‘%s’ in origin: did not contain a remote name",
                   refspec);
      return FALSE;
    }

  /* Update the remote branches option to include the origin ref.
   * Ideally this would operate on a list of branches and add the new
   * branch, but to keep things simple just set the option to the single
   * origin ref. */
  if (!ostree_repo_get_remote_option (repo, remote, "branches", NULL,
                                      &cur_branches, error))
    return FALSE;
  new_branches = g_strdup_printf ("%s;", ref);
  if (g_strcmp0 (cur_branches, new_branches) != 0)
    {
      /* FIXME: Update the config file directly. If ostree ever gains
       * sane remote modification, use that. */
      g_autoptr(GKeyFile) config = ostree_repo_copy_config (repo);
      g_autofree gchar *remote_group = g_strdup_printf ("remote \"%s\"",
                                                        remote);

      /* Make sure that the remote group exists in the config file and
       * the remote isn't defined in a remotes.d conf file. That
       * shouldn't happen, but adding the group (which
       * g_key_file_set_value() will do) would create a duplicate remote
       * that would prevent the repo from opening again. */
      if (!g_key_file_has_group (config, remote_group))
        {
          GFile *repo_file = ostree_repo_get_path (repo);
          g_autofree gchar *repo_path = g_file_get_path (repo_file);

          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Remote ‘%s’ does not exist in %s/config",
                       remote, repo_path);
          return FALSE;
        }

      g_key_file_set_value (config, remote_group, "branches", new_branches);
      if (!ostree_repo_write_config (repo, config, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
apply_internal (ApplyData     *apply_data,
                GCancellable  *cancellable,
                GError       **error)
{
  OstreeRepo *repo = apply_data->repo;
  const gchar *update_id = apply_data->update_id;
  const gchar *update_refspec = apply_data->update_refspec;
  const gchar *orig_refspec = apply_data->orig_refspec;
  g_autoptr(OstreeDeployment) booted_deployment = NULL;
  g_autoptr(OstreeDeployment) new_deployment = NULL;
  g_autoptr(GKeyFile) origin = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  const gchar *osname = get_test_osname ();
  gboolean staged_deploy;
  g_autoptr(GError) local_error = NULL;

  sysroot = ostree_sysroot_new_default ();
  /* The sysroot lock must be taken to prevent multiple processes (like this
   * and ostree admin upgrade) from deploying simultaneously, which will fail.
   * The lock will be unlocked automatically when sysroot is deallocated.
   */
  if (!ostree_sysroot_lock (sysroot, error))
    return FALSE;
  if (!ostree_sysroot_load (sysroot, cancellable, error))
    return FALSE;

  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);
  if (booted_deployment == NULL)
    return FALSE;

  g_message ("Booted (old) deployment: index: %d, OS name: %s, deploy serial: %d, "
             "checksum: %s, boot checksum: %s, boot serial: %d",
             ostree_deployment_get_index (booted_deployment),
             ostree_deployment_get_osname (booted_deployment),
             ostree_deployment_get_deployserial (booted_deployment),
             ostree_deployment_get_csum (booted_deployment),
             ostree_deployment_get_bootcsum (booted_deployment),
             ostree_deployment_get_bootserial (booted_deployment));

  origin = ostree_sysroot_origin_new_from_refspec (sysroot, update_refspec);

  /* When booted into an OSTree system, stage the deployment so that the
   * /etc merge happens during shutdown. Otherwise (primarily the test
   * suite), deploy the finalized tree immediately.
   */
  staged_deploy = ostree_sysroot_is_booted (sysroot);
  if (staged_deploy)
    {
      g_message ("Creating staged deployment for revision %s", update_id);
      if (!ostree_sysroot_stage_tree (sysroot,
                                      osname,
                                      update_id,
                                      origin,
                                      booted_deployment,
                                      NULL,
                                      &new_deployment,
                                      cancellable,
                                      error))
        return FALSE;
    }
  else
    {
      g_message ("Creating finalized deployment for revision %s", update_id);
      if (!ostree_sysroot_deploy_tree (sysroot,
                                       osname,
                                       update_id,
                                       origin,
                                       booted_deployment,
                                       NULL,
                                       &new_deployment,
                                       cancellable,
                                       error))
        return FALSE;

      if (!ostree_sysroot_simple_write_deployment (sysroot,
                                                   osname,
                                                   new_deployment,
                                                   booted_deployment,
                                                   OSTREE_SYSROOT_SIMPLE_WRITE_DEPLOYMENT_FLAGS_NO_CLEAN,
                                                   cancellable,
                                                   error))
        return FALSE;
    }

  g_message ("New deployment: index: %d, OS name: %s, deploy serial: %d, "
             "checksum: %s, boot checksum: %s, boot serial: %d",
             ostree_deployment_get_index (new_deployment),
             ostree_deployment_get_osname (new_deployment),
             ostree_deployment_get_deployserial (new_deployment),
             ostree_deployment_get_csum (new_deployment),
             ostree_deployment_get_bootcsum (new_deployment),
             ostree_deployment_get_bootserial (new_deployment));

  /* If the original refspec is not the update refspec, then we may have
   * a ref to a no longer needed tree. Delete that remote ref so the
   * sysroot cleanup below really removes that tree if no deployments
   * point to it anymore.
   */
  if (g_strcmp0 (update_refspec, orig_refspec) != 0)
    {
      g_autofree gchar *rev = NULL;

      if (!ostree_repo_resolve_rev (repo, orig_refspec, TRUE, &rev, error))
        return FALSE;

      if (rev)
        {
          if (!ostree_repo_prepare_transaction (repo, NULL, cancellable, error))
            return FALSE;

          ostree_repo_transaction_set_refspec (repo, orig_refspec, NULL);

          if (!ostree_repo_commit_transaction (repo, NULL, cancellable, error))
            return FALSE;
        }
    }

  /* FIXME: Cleaning up after update should be non-fatal, since we've
   * already successfully deployed the new OS. This clearly is a
   * workaround for a more serious issue, likely related to concurrent
   * prunes (https://phabricator.endlessm.com/T16736). */
  if (!ostree_sysroot_cleanup (sysroot, cancellable, &local_error))
    g_warning ("Failed to clean up the sysroot after successful deployment: %s",
               local_error->message);
  g_clear_error (&local_error);

  /* Try to update the remote branches option to use the new refspec.
   * This option is almost never used and has no impact on future
   * upgrades, so ignore any errors. */
  if (!update_remote_branches (repo, update_refspec, &local_error))
    g_warning ("Failed to set remote branches option: %s",
               local_error->message);
  g_clear_error (&local_error);

  /* Try to add a collection ID to the OS remote, which enables LAN/USB OS
   * updates when combined with eos-updater.conf changes. Don't treat any
   * errors as fatal to the update. */
  /* FIXME: Remove this function call after the next checkpoint after 3.4.8 */
  if (!update_remote_collection_id (repo, update_refspec, &local_error))
    g_warning ("Failed to set collection-id on remote: %s",
               local_error->message);
  g_clear_error (&local_error);

  return TRUE;
}

static void
apply (GTask *task,
       gpointer object,
       gpointer task_data,
       GCancellable *cancellable)
{
  g_autoptr(GError) local_error = NULL;
  ApplyData *apply_data = task_data;
  g_autoptr(GMainContext) task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  if (!apply_internal (apply_data,
                       cancellable,
                       &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_boolean (task, TRUE);

  g_main_context_pop_thread_default (task_context);
}

gboolean
handle_apply (EosUpdater            *updater,
              GDBusMethodInvocation *call,
              gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(ApplyData) apply_data = NULL;
  EosUpdaterData *data = user_data;
  EosUpdaterState state = eos_updater_get_state (updater);

  if (state != EOS_UPDATER_STATE_UPDATE_READY)
    {
      g_dbus_method_invocation_return_error (call,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
        "Can't call Apply() while in state %s",
        eos_updater_state_to_string (state));
      return TRUE;
    }

  apply_data = g_new0 (ApplyData, 1);
  apply_data->update_id = g_strdup (eos_updater_get_update_id (updater));
  apply_data->update_refspec = g_strdup (eos_updater_get_update_refspec (updater));
  apply_data->orig_refspec = g_strdup (eos_updater_get_original_refspec (updater));
  apply_data->repo = g_object_ref (data->repo);

  eos_updater_data_reset_cancellable (data);
  eos_updater_clear_error (updater, EOS_UPDATER_STATE_APPLYING_UPDATE);
  task = g_task_new (updater, data->cancellable, apply_finished, NULL);
  g_task_set_task_data (task, g_steal_pointer (&apply_data), (GDestroyNotify) apply_data_free);
  g_task_run_in_thread (task, apply);

  eos_updater_complete_apply (updater, call);

  return TRUE;
}
