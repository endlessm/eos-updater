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

#include "eos-updater-fetch.h"
#include "eos-updater-data.h"

static void
content_fetch_finished (GObject *object,
                        GAsyncResult *res,
                        gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask *task;
  GError *error = NULL;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  task = G_TASK (res);
  g_task_propagate_boolean (task, &error);

  if (error)
    {
      eos_updater_set_error (updater, error);
    }
  else
    {
      eos_updater_set_error_code (updater, 0);
      eos_updater_set_error_message (updater, "");
      eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_UPDATE_READY);
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
update_progress (OstreeAsyncProgress *progress,
                 gpointer object)
{
  EosUpdater *updater = EOS_UPDATER (object);
  guint64 bytes = ostree_async_progress_get_uint64 (progress,
                                                    "bytes-transferred");

  /* Idle could have been scheduled after the fetch completed, make sure we
   * don't override the downloaded bytes */
  if (eos_updater_get_state (updater) == EOS_UPDATER_STATE_FETCHING)
    eos_updater_set_downloaded_bytes (updater, bytes);
}

static void
content_fetch (GTask *task,
               gpointer object,
               gpointer task_data,
               GCancellable *cancel)
{
  EosUpdater *updater = EOS_UPDATER (object);
  EosUpdaterData *data = task_data;
  OstreeRepo *repo = data->repo;
  OstreeRepoPullFlags flags = OSTREE_REPO_PULL_FLAGS_NONE;
  gs_unref_object OstreeAsyncProgress *progress = NULL;
  GError *error = NULL;
  const gchar *refspec;
  gs_free gchar *src = NULL;
  gs_free gchar *ref = NULL;
  const gchar *sum;
  gchar *pullrefs[] = { NULL, NULL };
  GMainContext *task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  refspec = eos_updater_get_update_refspec (updater);
  if (refspec == NULL || *refspec == '\0')
    {
      g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "fetch called with empty update refspec");
      goto error;
    }

  if (!ostree_parse_refspec (refspec, &src, &ref, &error))
    goto error;

  sum = eos_updater_get_update_id (updater);
  if (sum == NULL || *sum == '\0')
    {
      g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "fetch called with empty update commit");
      goto error;
    }

  message ("Fetch: %s:%s resolved to: %s", src, ref, sum);

  /* rather than re-resolving the update, we get the last ID that the
   * user Poll()ed. We do this because that is the last update for which
   * we had size data: If there's been a new update since, then the
   * system hasn;t seen the download/unpack sizes for that so it cannot
   * be considered to have been approved.
   */
  pullrefs[0] = (gchar *) sum;

  progress = ostree_async_progress_new_and_connect (update_progress, updater);

  if (!ostree_repo_pull (repo, src, pullrefs, flags, progress, cancel, &error))
    goto error;

  message ("Fetch: pull() completed");

  if (!ostree_repo_read_commit (repo, pullrefs[0], NULL, NULL, cancel, &error))
    goto error;

  message ("Fetch: commit %s cached", pullrefs[0]);
  g_task_return_boolean (task, TRUE);
  goto cleanup;

 error:
  message ("Fetch returning ERROR");
  g_task_return_error (task, error);

 cleanup:
  if (progress)
    ostree_async_progress_finish(progress);
  g_main_context_pop_thread_default (task_context);
  g_main_context_unref (task_context);
  return;
}

gboolean
handle_fetch (EosUpdater            *updater,
              GDBusMethodInvocation *call,
              gpointer               user_data)
{
  gs_unref_object GTask *task = NULL;
  EosUpdaterState state = eos_updater_get_state (updater);

  switch (state)
    {
      case EOS_UPDATER_STATE_UPDATE_AVAILABLE:
        break;
      default:
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call Fetch() while in state %s",
          eos_updater_state_to_string (state));
      goto bail;
    }

  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_FETCHING);
  task = g_task_new (updater, NULL, content_fetch_finished, user_data);
  g_task_set_task_data (task, user_data, NULL);
  g_task_run_in_thread (task, content_fetch);

  eos_updater_complete_fetch (updater, call);

bail:
  return TRUE;
}
