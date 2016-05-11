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

#include "eos-updater-poll.h"
#include "eos-updater-data.h"

static void
metadata_fetch_finished (GObject *object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask     *task;
  GError    *error = NULL;
  EosUpdaterData *data = user_data;
  g_autofree gchar *csum = NULL;
  OstreeRepo *repo = data->repo;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  /* get the sha256 of the fetched update */
  task = G_TASK (res);
  csum = g_task_propagate_pointer (task, &error);

  if (csum)
    {
      gint64 archived = -1;
      gint64 unpacked = -1;
      gint64 new_archived = 0;
      gint64 new_unpacked = 0;
      g_autoptr(GVariant) current_commit = NULL;
      g_autoptr(GVariant) commit = NULL;
      const gchar *cur;
      gboolean is_newer = FALSE;
      const gchar *label;
      const gchar *message;

      /* get the sha256 sum of the currently booted image */
      cur = eos_updater_get_current_id (updater);
      if (cur == NULL || *cur == '\0')
        {
          g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                               "Could not determine booted checksum");
          goto out;
        }

      if (!ostree_repo_load_variant (repo, OSTREE_OBJECT_TYPE_COMMIT,
                                     cur, &current_commit, &error))
        goto out;

      if (!ostree_repo_load_variant (repo, OSTREE_OBJECT_TYPE_COMMIT,
                                     csum, &commit, &error))
        goto out;

      /* Determine if the new commit is newer than the old commit to prevent
       * inadvertant (or malicious) attempts to downgrade the system.
       */
      is_newer = ostree_commit_get_timestamp (commit) > ostree_commit_get_timestamp (current_commit);

      /* Everything is happy thusfar */
      eos_updater_set_error_code (updater, 0);
      eos_updater_set_error_message (updater, "");
      /* if we have a checksum for the remote upgrade candidate
       * and it's ≠ what we're currently booted into, advertise it as such.
       */
      if (is_newer && g_strcmp0 (cur, csum) != 0)
        {
          eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_UPDATE_AVAILABLE);
        }
      else
        {
          eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_READY);
          goto out;
        }

      eos_updater_set_update_id (updater, csum);

      g_variant_get_child (commit, 3, "&s", &label);
      g_variant_get_child (commit, 4, "&s", &message);
      eos_updater_set_update_label (updater, label ? label : "");
      eos_updater_set_update_message (updater, message ? message : "");

      if (ostree_repo_get_commit_sizes (repo, csum,
                                        &new_archived, &new_unpacked,
                                        NULL,
                                        &archived, &unpacked,
                                        NULL,
                                        g_task_get_cancellable (task),
                                        &error))
        {
          eos_updater_set_full_download_size (updater, archived);
          eos_updater_set_full_unpacked_size (updater, unpacked);
          eos_updater_set_download_size (updater, new_archived);
          eos_updater_set_unpacked_size (updater, new_unpacked);
          eos_updater_set_downloaded_bytes (updater, 0);
        }
      else /* no size data available (may or may not be an error) */
        {
          eos_updater_set_full_download_size (updater, -1);
          eos_updater_set_full_unpacked_size (updater, -1);
          eos_updater_set_download_size (updater, -1);
          eos_updater_set_unpacked_size (updater, -1);
          eos_updater_set_downloaded_bytes (updater, -1);

          /* shouldn't actually stop us offering an update, as long
           * as the branch itself is resolvable in the next step,
           * but log it anyway.
           */
          if (error)
            {
              message ("No size summary data: %s", error->message);
              g_clear_error (&error);
            }
        }
    }
  else /* csum == NULL means OnHold=true, nothing to do here */
    eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_READY);

 out:
  if (error)
    {
      eos_updater_set_error (updater, error);
      g_clear_error (&error);
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
metadata_fetch (GTask *task,
                gpointer object,
                gpointer task_data,
                GCancellable *cancel)
{
  EosUpdater *updater = EOS_UPDATER (object);
  EosUpdaterData *data = task_data;
  OstreeRepo *repo = data->repo;
  OstreeRepoPullFlags flags = (OSTREE_REPO_PULL_FLAGS_COMMIT_ONLY);
  GError *error = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *branch = NULL;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *orig_refspec = NULL;
  gchar *pullrefs[] = { NULL, NULL };
  gchar *csum = NULL;
  GMainContext *task_context = g_main_context_new ();
  g_autoptr(GVariant) commit = NULL;

  g_main_context_push_thread_default (task_context);

  if (!eos_updater_get_upgrade_info (repo, &refspec, &orig_refspec, &error))
    goto error;

  if (!refspec) /* this means OnHold=true */
    {
      g_task_return_pointer (task, NULL, NULL);
      goto cleanup;
    }

  if (!ostree_parse_refspec (refspec, &remote, &branch, &error))
    goto error;

  pullrefs[0] = branch;

  if (!ostree_repo_pull (repo, remote, pullrefs, flags, NULL, cancel, &error))
    goto error;

  if (!ostree_repo_resolve_rev (repo, refspec, TRUE, &csum, &error))
    goto error;

  if (!ostree_repo_load_variant (repo, OSTREE_OBJECT_TYPE_COMMIT,
                                 csum, &commit, &error))
    goto error;

  eos_updater_set_update_refspec (updater, refspec);
  eos_updater_set_original_refspec (updater, orig_refspec);

  /* returning the sha256 sum of the just-fetched rev */
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
handle_poll (EosUpdater            *updater,
             GDBusMethodInvocation *call,
             gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  EosUpdaterState state = eos_updater_get_state (updater);

  switch (state)
    {
      case EOS_UPDATER_STATE_READY:
      case EOS_UPDATER_STATE_UPDATE_AVAILABLE:
      case EOS_UPDATER_STATE_UPDATE_READY:
      case EOS_UPDATER_STATE_ERROR:
        break;
      default:
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call Poll() while in state %s",
          eos_updater_state_to_string (state));
        goto bail;
    }

  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_POLLING);
  task = g_task_new (updater, NULL, metadata_fetch_finished, user_data);
  g_task_set_task_data (task, user_data, NULL);
  g_task_run_in_thread (task, metadata_fetch);

  eos_updater_complete_poll (updater, call);

bail:
  return TRUE;
}
