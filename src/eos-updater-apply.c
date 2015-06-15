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
#include <ostree.h>

static void
apply_finished (GObject *object,
                GAsyncResult *res,
                gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask *task;
  GError *error = NULL;

  OstreeRepo *repo = OSTREE_REPO (user_data);
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
      eos_updater_set_error_code (updater, 0);
      eos_updater_set_error_message (updater, "");
      eos_updater_set_state_changed (updater, EOS_STATE_UPDATE_APPLIED);
    }

  return;

 invalid_task:
  /* Either the threading or the memory management is shafted. Or both.
   * We're boned. Log an error and activate the self destruct mechanism.
   */
  g_error ("Invalid async task object when returning from Poll() thread!");
  g_assert_not_reached ();
}

static void
apply (GTask *task,
       gpointer object,
       gpointer task_data,
       GCancellable *cancel)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GError *error = NULL;
  GMainContext *task_context = g_main_context_new ();
  const gchar *update_id = eos_updater_get_update_id (updater);
  gint bootversion = 0;
  gint newbootver = 0;
  gs_unref_object OstreeDeployment *merge_deployment = NULL;
  gs_unref_object OstreeDeployment *new_deployment = NULL;
  GKeyFile *origin = NULL;
  gs_unref_object OstreeSysroot *sysroot = NULL;

  g_main_context_push_thread_default (task_context);

  sysroot = ostree_sysroot_new_default ();
  if (!ostree_sysroot_load (sysroot, cancel, &error))
    goto error;

  bootversion = ostree_sysroot_get_bootversion (sysroot);
  merge_deployment = ostree_sysroot_get_merge_deployment (sysroot, NULL);
  origin = ostree_deployment_get_origin (merge_deployment);

  if (!ostree_sysroot_deploy_tree (sysroot,
                                   NULL,
                                   update_id,
                                   origin,
                                   merge_deployment,
                                   NULL,
                                   &new_deployment,
                                   cancel,
                                   &error))
    goto error;

  if (!ostree_sysroot_simple_write_deployment (sysroot,
                                               NULL,
                                               new_deployment,
                                               merge_deployment,
                                               0,
                                               cancel,
                                               &error))
    goto error;

  newbootver = ostree_deployment_get_deployserial (new_deployment);

  g_task_return_boolean (task, bootversion != newbootver);
  goto cleanup;

 error:
  g_task_return_error (task, error);

 cleanup:
  g_main_context_pop_thread_default (task_context);
  g_main_context_unref (task_context);
  return;
}

gboolean
handle_apply (EosUpdater            *updater,
              GDBusMethodInvocation *call,
              gpointer               user_data)
{
  OstreeRepo *repo = OSTREE_REPO (user_data);
  gs_unref_object GTask *task = NULL;
  EosState state = eos_updater_get_state (updater);

  switch (state)
    {
    case EOS_STATE_UPDATE_READY:
      break;
    default:
      g_dbus_method_invocation_return_error (call,
        EOS_ERROR, EOS_ERROR_WRONG_STATE,
        "Can't call Apply() while in state %s", eos_state_to_string (state));
      goto bail;
    }

  eos_updater_set_state_changed (updater, EOS_STATE_APPLYING_UPDATE);
  task = g_task_new (updater, NULL, apply_finished, repo);
  g_task_set_task_data (task, g_object_ref (repo), g_object_unref);
  g_task_run_in_thread (task, apply);

  eos_updater_complete_apply (updater, call);

bail:
  return TRUE;
}
