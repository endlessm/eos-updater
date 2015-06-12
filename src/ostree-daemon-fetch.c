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

#include "ostree-daemon-fetch.h"

static void
content_fetch_finished (GObject *object,
                        GAsyncResult *res,
                        gpointer user_data)
{
  OTDOSTree *ostree = OTD_OSTREE (object);
  GTask *task;
  GError *error = NULL;

  gs_unref_object OstreeRepo *repo = OSTREE_REPO (user_data);
  gboolean fetched = FALSE;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  task = G_TASK (res);
  fetched = g_task_propagate_boolean (task, &error);

  if (error)
    {
      ostree_daemon_set_error (ostree, error);
    }
  else if (!fetched) // bizarre, should not happen
    {
      otd_ostree_set_error_code (ostree, G_IO_ERROR_NOT_FOUND);
      otd_ostree_set_error_message (ostree, "Update not found on server");
      ostree_daemon_set_state (ostree, OTD_STATE_ERROR);
    }
  else
    {
      otd_ostree_set_error_code (ostree, 0);
      otd_ostree_set_error_message (ostree, "");
      ostree_daemon_set_state (ostree, OTD_STATE_UPDATE_READY);
    }

  return;

 invalid_task:
  // Either the threading or the memory management is shafted. Or both.
  // We're boned. Log an error and activate the self destruct mechanism:
  g_error ("Invalid async task object when returning from Poll() thread!");
  g_assert_not_reached ();
}

typedef struct {
  OTDOSTree *otd;
  OstreeRepo *repo;
  guint fetched;
  guint requested;
  guint64 bytes;
} ProgressData;

static void
update_progress (OstreeAsyncProgress *progress,
                 gpointer object)
{
  OTDOSTree *ostree = OTD_OSTREE (object);
  guint64 bytes = ostree_async_progress_get_uint64 (progress,
                                                    "bytes-transferred");

  /* Idle could have been scheduled after the fetch completed, make sure we
   * don't override the downloaded bytes */
  if ( otd_ostree_get_state (ostree) == OTD_STATE_FETCHING)
    otd_ostree_set_downloaded_bytes (ostree, bytes);
}

static void
content_fetch (GTask *task,
               gpointer object,
               gpointer task_data,
               GCancellable *cancel)
{
  OTDOSTree *ostree = OTD_OSTREE (object);
  OstreeRepo *repo = OSTREE_REPO (task_data);
  OstreeRepoPullFlags flags = OSTREE_REPO_PULL_FLAGS_NONE;
  OstreeAsyncProgress *progress = NULL;
  GError *error = NULL;
  gs_free gchar *src = NULL;
  gs_free gchar *ref = NULL;
  gs_free gchar *sum = NULL;
  gchar *pullrefs[] = { NULL, NULL };
  GMainContext *task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  if (!ostree_daemon_resolve_upgrade (ostree, repo, &src, &ref, &sum, &error))
    goto error;

  message ("Fetch: %s:%s resolved to: %s", src, ref, sum);
  message ("User asked us for commit: %s", otd_ostree_get_update_id (ostree));

  // rather than re-resolving the update, we get the las ID that the
  // user Poll()ed. We do this because that is the last update for which
  // we had size data: If there's been a new update since, then the
  // system hasn;t seen the download/unpack sizes for that so it cannot
  // be considered to have been approved.
  pullrefs[0] = (gchar *) otd_ostree_get_update_id (ostree);

  progress = ostree_async_progress_new_and_connect (update_progress, ostree);

  // FIXME: upstream ostree_repo_pull had an unbalanced
  // g_main_context_get_thread_default/g_main_context_unref
  // instead of
  // g_main_context_ref_thread_default/g_main_context_unref
  // patch has been accepted upstream, but double check when merging
  if (!ostree_repo_pull (repo, src, pullrefs, flags, progress, cancel, &error))
    goto error;

  message ("Fetch: pull() completed");

  if (!ostree_repo_read_commit (repo, pullrefs[0], NULL, NULL, cancel, &error))
    {
      if (!error)
        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Failed to fetch update %s from %s", ref, src);
      goto error;
    }

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
handle_fetch (OTDOSTree             *ostree,
              GDBusMethodInvocation *call,
              gpointer               user_data)
{
  OstreeRepo *repo = OSTREE_REPO (user_data);
  GTask *task = NULL;
  OTDState state = otd_ostree_get_state (ostree);

  switch (state)
    {
      case OTD_STATE_UPDATE_AVAILABLE:
        break;
      default:
        g_dbus_method_invocation_return_error (call,
          OTD_ERROR, OTD_ERROR_WRONG_STATE,
          "Can't call Fetch() while in state %s", otd_state_to_string (state));
      goto bail;
    }

  ostree_daemon_set_state (ostree, OTD_STATE_FETCHING);
  task = g_task_new (ostree, NULL, content_fetch_finished, g_object_ref (repo));
  g_task_set_task_data (task, g_object_ref (repo), g_object_unref);
  g_task_run_in_thread (task, content_fetch);

  otd_ostree_complete_fetch (ostree, call);

bail:
  return TRUE;
}
