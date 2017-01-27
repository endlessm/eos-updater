/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Vivek Dasmohapatra <vivek@etla.org>
 */

#include "eos-updater-apply.h"
#include "eos-updater-avahi.h"
#include "eos-updater-data.h"

#include "eos-util.h"

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
    message ("System redeployed same boot version");

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

static gchar *
get_test_osname (void)
{
  return eos_updater_dup_envvar_or ("EOS_UPDATER_TEST_UPDATER_OSTREE_OSNAME",
                                     NULL);
}

static gboolean
apply_internal (EosUpdater *updater,
                EosUpdaterData *data,
                GCancellable *cancel,
                gboolean *out_bootversion_changed,
                GError **error)
{
  OstreeRepo *repo = data->repo;
  const gchar *update_id = eos_updater_get_update_id (updater);
  const gchar *update_refspec = eos_updater_get_update_refspec (updater);
  const gchar *orig_refspec = eos_updater_get_original_refspec (updater);
  gint bootversion = 0;
  gint newbootver = 0;
  g_autoptr(OstreeDeployment) booted_deployment = NULL;
  g_autoptr(OstreeDeployment) new_deployment = NULL;
  g_autoptr(GKeyFile) origin = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autofree gchar *osname = get_test_osname ();

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
  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);
  if (booted_deployment == NULL)
    return FALSE;
  origin = ostree_sysroot_origin_new_from_refspec (sysroot, update_refspec);

  if (!ostree_sysroot_deploy_tree (sysroot,
                                   osname,
                                   update_id,
                                   origin,
                                   booted_deployment,
                                   NULL,
                                   &new_deployment,
                                   cancel,
                                   error))
    return FALSE;

  /* If the original refspec is not the update refspec, then we may have
   * a ref to a no longer needed tree. Delete that remote ref so the
   * cleanup done in simple_write_deployment() really removes that tree
   * if no deployments point to it anymore.
   */
  if (g_strcmp0 (update_refspec, orig_refspec) != 0)
    {
      g_autofree gchar *rev = NULL;

      if (!ostree_repo_resolve_rev (repo, orig_refspec, TRUE, &rev, error))
        return FALSE;

      if (rev)
        {
          if (!ostree_repo_prepare_transaction (repo, NULL, cancel, error))
            return FALSE;

          ostree_repo_transaction_set_refspec (repo, orig_refspec, NULL);

          if (!ostree_repo_commit_transaction (repo, NULL, cancel, error))
            return FALSE;
        }
    }

  if (!ostree_sysroot_simple_write_deployment (sysroot,
                                               osname,
                                               new_deployment,
                                               booted_deployment,
                                               0,
                                               cancel,
                                               error))
    return FALSE;

  newbootver = ostree_deployment_get_deployserial (new_deployment);

  if (!eos_extensions_save (data->extensions,
                            repo,
                            cancel,
                            error))
    return FALSE;
  g_set_object (&data->branch_file, data->extensions->branch_file);
  g_clear_object (&data->extensions);

  if (!eos_avahi_generate_service_file (repo,
                                        data->branch_file,
                                        error))
    return FALSE;

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

  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_APPLYING_UPDATE);
  task = g_task_new (updater, NULL, apply_finished, user_data);
  g_task_set_task_data (task, user_data, NULL);
  g_task_run_in_thread (task, apply);

  eos_updater_complete_apply (updater, call);

  return TRUE;
}
