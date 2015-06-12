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

#include "ostree-daemon-poll.h"

static void
metadata_fetch_finished (GObject *object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  OTDOSTree *ostree = OTD_OSTREE (object);
  GTask     *task;
  GError    *error = NULL;

  gs_free gchar *csum;
  gs_unref_object OstreeRepo *repo = OSTREE_REPO (user_data);

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  // get the sha256 of the fetched update:
  task = G_TASK (res);
  csum = g_task_propagate_pointer (task, &error);

  if (csum)
    {
      gint64 archived = -1;
      gint64 unpacked = -1;
      gint64 new_archived = 0;
      gint64 new_unpacked = 0;
      gs_unref_variant GVariant *commit = NULL;
      gs_free gchar *cur = NULL;
      const gchar *label;
      const gchar *message;

      // get the sha256 sum uf the currently booted image:
      if (!ostree_daemon_resolve_upgrade (ostree, repo, NULL, NULL, &cur, &error))
        goto out;

      // Everything is happy thusfar
      otd_ostree_set_error_code (ostree, 0);
      otd_ostree_set_error_message (ostree, "");
      // if we have a checksum for the remote upgrade candidate
      // and it's ≠ what we're currently booted into, advertise it as such:
      if (g_strcmp0 (cur, csum) != 0) {
        ostree_daemon_set_state (ostree, OTD_STATE_UPDATE_AVAILABLE);
        }
      else
        {
          ostree_daemon_set_state (ostree, OTD_STATE_READY);
          goto out;
        }

      otd_ostree_set_update_id (ostree, csum);

      if (!ostree_repo_load_variant (repo, OSTREE_OBJECT_TYPE_COMMIT,
                                     csum, &commit, &error))
        goto out;

      g_variant_get_child (commit, 3, "&s", &label);
      g_variant_get_child (commit, 4, "&s", &message);
      otd_ostree_set_update_label (ostree, label ? label : "");
      otd_ostree_set_update_message (ostree, message ? message : "");

      if (ostree_repo_get_commit_sizes (repo, csum,
                                        &new_archived, &new_unpacked,
                                        NULL,
                                        &archived, &unpacked,
                                        NULL,
                                        g_task_get_cancellable (task),
                                        &error))
        {
          otd_ostree_set_full_download_size (ostree, archived);
          otd_ostree_set_full_unpacked_size (ostree, unpacked);
          otd_ostree_set_download_size (ostree, new_archived);
          otd_ostree_set_unpacked_size (ostree, new_unpacked);
          otd_ostree_set_downloaded_bytes (ostree, 0);
        }
      else // no size data available (may or may not be an error):
        {
          otd_ostree_set_full_download_size (ostree, -1);
          otd_ostree_set_full_unpacked_size (ostree, -1);
          otd_ostree_set_download_size (ostree, -1);
          otd_ostree_set_unpacked_size (ostree, -1);
          otd_ostree_set_downloaded_bytes (ostree, -1);

          // shouldn't actually stop us offering an update, as long
          // as the branch itself is resolvable in the next step,
          // but log it anyway:
          if (error)
            {
              message ("No size summary data: %s", error->message);
              g_clear_error (&error);
            }
        }

      // get the sha256 sum uf the currently booted image:
      if (!ostree_daemon_resolve_upgrade (ostree, repo, NULL, NULL, &cur, &error))
        goto out;
    }
  else if (!error) // this should never happen, but check for it anyway:
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Update not found on server");
      goto out;
    }

 out:
  if (error)
    {
      ostree_daemon_set_error (ostree, error);
      g_clear_error (&error);
    }
  return;

 invalid_task:
  // Either the threading or the memory management is shafted. Or both.
  // We're boned. Log an error and activate the self destruct mechanism:
  g_error ("Invalid async task object when returning from Poll() thread!");
  g_assert_not_reached ();
}

static void
metadata_fetch (GTask *task,
                gpointer object,
                gpointer task_data,
                GCancellable *cancel)
{
  OTDOSTree *ostree = OTD_OSTREE (object);
  OstreeRepo *repo = OSTREE_REPO (task_data);
  OstreeRepoPullFlags flags = (OSTREE_REPO_PULL_FLAGS_COMMIT_ONLY);
  GError *error = NULL;
  gs_free gchar *remote = NULL;
  gs_free gchar *branch = NULL;
  gs_free gchar *refspec = NULL;
  gchar *pullrefs[] = { NULL, NULL };
  gchar *csum = NULL;
  GMainContext *task_context = g_main_context_new ();
  gs_unref_variant GVariant *commit = NULL;

  g_main_context_push_thread_default (task_context);

  if (!ostree_daemon_resolve_upgrade (ostree, repo,
                                      &remote, &branch, NULL, &error))
    goto error;

  pullrefs[0] = branch;

  // FIXME: upstream ostree_repo_pull has an unbalanced
  // g_main_context_get_thread_default/g_main_context_unref
  // instead of
  // g_main_context_ref_thread_default/g_main_context_unref
  // which breaks our g_main_context_unref down in cleanup:
  if (!ostree_repo_pull (repo, remote, pullrefs, flags, NULL, cancel, &error))
    goto error;

  refspec = g_strdup_printf ("%s:%s", remote, branch);

  if (!ostree_repo_resolve_rev (repo, refspec, TRUE, &csum, &error))
    goto error;

  if (!csum)
    {
      if (!error)
        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Server does not have image '%s'", refspec);
      goto error;
    }

  if (!ostree_repo_load_variant (repo, OSTREE_OBJECT_TYPE_COMMIT,
                                 csum, &commit, &error))
    goto error;

  // returning the sha256 sum of the just-fetched rev:
  g_task_return_pointer (task, csum, g_free);
  goto cleanup;

 error:
  g_task_return_error (task, error);

 cleanup:
  g_main_context_pop_thread_default (task_context);
  g_main_context_unref (task_context);
  return;
}

gboolean
handle_poll (OTDOSTree             *ostree,
             GDBusMethodInvocation *call,
             gpointer               user_data)
{
  OstreeRepo *repo = OSTREE_REPO (user_data);
  GTask *task = NULL;
  OTDState state = otd_ostree_get_state (ostree);
  // gboolean poll_ok = FALSE;

  switch (state)
    {
      case OTD_STATE_READY:
      case OTD_STATE_UPDATE_AVAILABLE:
      case OTD_STATE_UPDATE_READY:
      case OTD_STATE_ERROR:
        break;
      default:
        g_dbus_method_invocation_return_error (call,
          OTD_ERROR, OTD_ERROR_WRONG_STATE,
          "Can't call Poll() while in state %s", otd_state_to_string (state));
        goto bail;
    }

  ostree_daemon_set_state (ostree, OTD_STATE_POLLING);
  task = g_task_new (ostree, NULL, metadata_fetch_finished, g_object_ref (repo));
  g_task_set_task_data (task, g_object_ref (repo), g_object_unref);
  g_task_run_in_thread (task, metadata_fetch);

  otd_ostree_complete_poll (ostree, call);

bail:
  return TRUE;
}
