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

#include "eos-updater-data.h"
#include "eos-updater-fetch.h"
#include "eos-updater-object.h"

#include <libeos-updater-util/util.h>

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
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED))
        {
          g_autofree gchar *old_message = g_strdup (error->message);
          g_clear_error (&error);
          g_set_error (&error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_FETCHING,
                       "Error fetching update: %s", old_message);
        }

      eos_updater_set_error (updater, error);
      g_clear_error (&error);
    }
  else
    {
      eos_updater_clear_error (updater, EOS_UPDATER_STATE_UPDATE_READY);
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

  /* FIXME: Cap to the limit of eos_updater_set_downloaded_bytes(). */
  if (bytes > G_MAXINT64)
    bytes = G_MAXINT64;

  /* Idle could have been scheduled after the fetch completed, make sure we
   * don't override the downloaded bytes */
  if (eos_updater_get_state (updater) == EOS_UPDATER_STATE_FETCHING)
    eos_updater_set_downloaded_bytes (updater, (gint64) bytes);
}

static GVariant *
get_options_for_pull (const gchar *ref,
                      const gchar *url_override,
                      gboolean     disable_static_deltas)
{
  g_auto(GVariantBuilder) builder = { { { 0, } } };

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{s@v}", "refs",
                         g_variant_new_variant (g_variant_new_strv (&ref, 1)));
  if (url_override != NULL)
    g_variant_builder_add (&builder, "{s@v}", "override-url",
                           g_variant_new_variant (g_variant_new_string (url_override)));
  g_variant_builder_add (&builder, "{s@v}", "disable-static-deltas",
                         g_variant_new_variant (g_variant_new_boolean (disable_static_deltas)));

  return g_variant_builder_end (&builder);
}

static gboolean
repo_pull (OstreeRepo *self,
           const gchar *remote_name,
           const gchar *ref,
           const gchar *url_override,
           OstreeAsyncProgress *progress,
           GCancellable *cancellable,
           GError **error)
{
  g_autoptr(GVariant) options = get_options_for_pull (ref, url_override, FALSE);
  g_autoptr(GError) local_error = NULL;

  /* FIXME: progress bar will go crazy here if it fails */
  if (!ostree_repo_pull_with_options (self, remote_name, options,
                                      progress, cancellable, &local_error))
    {
      g_autoptr(GVariant) fallback_options = NULL;

      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_warning ("Pulling %s from %s (%s) failed because some object was not found; "
                 "will try again, this time without static deltas: %s",
                 ref,
                 remote_name,
                 (url_override != NULL) ? url_override : "not overridden",
                 local_error->message);
      fallback_options = get_options_for_pull (ref, url_override, TRUE);

      return ostree_repo_pull_with_options (self, remote_name, fallback_options,
                                            progress, cancellable, error);
    }

  return TRUE;
}

static void
async_result_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **out_result = user_data;
  *out_result = g_object_ref (result);
}

static gboolean
repo_pull_from_remotes (OstreeRepo                            *repo,
                        const OstreeRepoFinderResult * const  *results,
                        GVariant                              *options,
                        OstreeAsyncProgress                   *progress,
                        GMainContext                          *context,
                        GCancellable                          *cancellable,
                        GError                               **error)
{
  g_autoptr(GAsyncResult) pull_result = NULL;
  g_autoptr(GError) local_error = NULL;

  /* FIXME: progress bar will go crazy here if it fails */
  ostree_repo_pull_from_remotes_async (repo, results, options, progress,
                                       cancellable, async_result_cb, &pull_result);

  while (pull_result == NULL)
    g_main_context_iteration (context, TRUE);

  if (!ostree_repo_pull_from_remotes_finish (repo, pull_result, &local_error))
    {
      g_auto(GVariantDict) dict = { 0, };
      g_autoptr(GVariant) fallback_options = NULL;

      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
      g_clear_error (&local_error);

      g_warning ("Pulling results %p failed because some object was not found; "
                 "will try again, this time without static deltas: %s",
                 results, local_error->message);

      g_variant_dict_init (&dict, options);
      g_variant_dict_insert (&dict, "disable-static-deltas", "b", TRUE);
      fallback_options = g_variant_dict_end (&dict);

      g_clear_object (&pull_result);
      ostree_repo_pull_from_remotes_async (repo, results, fallback_options, progress,
                                           cancellable, async_result_cb, &pull_result);

      while (pull_result == NULL)
        g_main_context_iteration (context, TRUE);

      return ostree_repo_pull_from_remotes_finish (repo, pull_result, error);
    }

  return TRUE;
}

static gboolean
content_fetch_new (EosUpdater      *updater,
                   EosUpdaterData  *data,
                   GMainContext    *context,
                   GCancellable    *cancellable,
                   GError         **error)
{
  g_autoptr(OstreeAsyncProgress) progress = NULL;
  gboolean retval;

  g_assert (data->results != NULL);

  progress = ostree_async_progress_new_and_connect (update_progress, updater);
  retval = repo_pull_from_remotes (data->repo,
                                   (const OstreeRepoFinderResult * const *) data->results,
                                   NULL  /* options */, progress, context,
                                   cancellable, error);
  ostree_async_progress_finish (progress);

  return retval;
}

static gboolean
content_fetch_old (EosUpdater      *updater,
                   EosUpdaterData  *data,
                   GMainContext    *context,
                   GCancellable    *cancellable,
                   GError         **error)
{
  const gchar *refspec;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  const gchar *commit_id;
  const gchar *url_override = NULL;
  OstreeRepo *repo = data->repo;
  g_autoptr(OstreeAsyncProgress) progress = NULL;

  refspec = eos_updater_get_update_refspec (updater);
  if (refspec == NULL || *refspec == '\0')
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "fetch called with empty update refspec");
      return FALSE;
    }

  if (!ostree_parse_refspec (refspec, &remote, &ref, error))
    return FALSE;

  commit_id = eos_updater_get_update_id (updater);
  if (commit_id == NULL || *commit_id == '\0')
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "fetch called with empty update commit");
      return FALSE;
    }

  g_message ("Fetch: %s:%s resolved to: %s", remote, ref, commit_id);
  progress = ostree_async_progress_new_and_connect (update_progress, updater);

  if (data->overridden_urls != NULL && data->overridden_urls[0] != NULL)
    {
      guint idx = (guint) g_random_int_range (0, (gint32) g_strv_length (data->overridden_urls));

      url_override = data->overridden_urls[idx];
    }
  /* rather than re-resolving the update, we get the last ID that the
   * user Poll()ed. We do this because that is the last update for which
   * we had size data: If there's been a new update since, then the
   * system hasn;t seen the download/unpack sizes for that so it cannot
   * be considered to have been approved.
   */
  if (!repo_pull (repo, remote, commit_id, url_override, progress, cancellable, error))
    {
      ostree_async_progress_finish (progress);
      return FALSE;
    }

  ostree_async_progress_finish (progress);

  g_message ("Fetch: pull() completed");

  if (!ostree_repo_read_commit (repo, commit_id, NULL, NULL, cancellable, error))
    return FALSE;

  g_message ("Fetch: commit %s cached", commit_id);

  return TRUE;
}

static gboolean
content_fetch (EosUpdater      *updater,
               EosUpdaterData  *data,
               GMainContext    *context,
               GCancellable    *cancellable,
               GError         **error)
{
  g_autoptr(GError) local_error = NULL;

  /* Do we want to use the new libostree code for P2P, or fall back on the old
   * eos-updater code?
   * FIXME: Eventually drop the old code. See:
   * https://phabricator.endlessm.com/T19606 */
  if (data->results != NULL)
    {
      g_message ("Fetch: using results %p", data->results);

      if (content_fetch_new (updater, data, context, cancellable, &local_error))
        g_message ("Fetch: finished pulling using libostree P2P code");
      else
        g_warning ("Error fetching updates using libostree P2P code; falling back to old code: %s",
                   local_error->message);
    }

  if (local_error != NULL || data->results == NULL)
    {
      g_clear_error (&local_error);

      if (data->results == NULL)
        g_message ("Fetch: using old code due to lack of repo finder results");

      if (content_fetch_old (updater, data, context, cancellable, &local_error))
        g_message ("Fetch: finished pulling using old code");
      else
        {
          g_message ("Fetch: error pulling using old code: %s", local_error->message);
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }

  return TRUE;
}

static void
content_fetch_task (GTask        *task,
                    gpointer      object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  EosUpdater *updater = EOS_UPDATER (object);
  EosUpdaterData *data = task_data;
  g_autoptr(GPtrArray) flatpaks_to_deploy = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  if (!content_fetch (updater, data, task_context, cancellable, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);

  g_main_context_pop_thread_default (task_context);
}

gboolean
handle_fetch (EosUpdater            *updater,
              GDBusMethodInvocation *call,
              gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  EosUpdaterState state = eos_updater_get_state (updater);

  if (state != EOS_UPDATER_STATE_UPDATE_AVAILABLE)
    {
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call Fetch() while in state %s",
          eos_updater_state_to_string (state));
      return TRUE;
    }

  eos_updater_clear_error (updater, EOS_UPDATER_STATE_FETCHING);
  task = g_task_new (updater, NULL, content_fetch_finished, user_data);
  g_task_set_task_data (task, user_data, NULL);
  g_task_run_in_thread (task, content_fetch_task);

  eos_updater_complete_fetch (updater, call);

  return TRUE;
}
