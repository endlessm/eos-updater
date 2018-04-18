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

#include <flatpak.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>
#include <libmogwai-schedule-client/schedule-entry.h>
#include <libmogwai-schedule-client/scheduler.h>

/* Closure containing the data for the fetch worker thread. The
 * worker thread must not access EosUpdater or EosUpdaterData directly,
 * as they are not thread safe. */
typedef struct
{
  /* Update information. */
  gchar *update_id;  /* (owned) */
  gchar *update_refspec;  /* (owned) */
  EosUpdaterData *data;  /* (unowned) */

  /* Scheduling. */
  gboolean force;  /* ignore metered data scheduling */
  guint scheduling_timeout_seconds;  /* 0 for no timeout */
  MwscScheduleEntry *schedule_entry;  /* (nullable) (owned) */

  /* Progress. */
  OstreeAsyncProgress *progress;  /* (owned) */
} FetchData;

static void
fetch_data_free (FetchData *data)
{
  g_free (data->update_id);
  g_free (data->update_refspec);
  g_clear_object (&data->progress);
  g_clear_object (&data->schedule_entry);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FetchData, fetch_data_free)

static void
content_fetch_finished (GObject *object,
                        GAsyncResult *res,
                        gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask *task;
  GError *error = NULL;
  FetchData *fetch_data;
  OstreeAsyncProgress *progress;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  task = G_TASK (res);
  fetch_data = g_task_get_task_data (task);
  progress = fetch_data->progress;

  ostree_async_progress_finish (progress);
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

/* This will be executed in the same thread as handle_fetch(). */
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
content_fetch_new (FetchData     *fetch_data,
                   GMainContext  *context,
                   GCancellable  *cancellable,
                   GError       **error)
{
  EosUpdaterData *data = fetch_data->data;

  g_assert (data->results != NULL);

  return repo_pull_from_remotes (data->repo,
                                 (const OstreeRepoFinderResult * const *) data->results,
                                 NULL  /* options */, fetch_data->progress, context,
                                 cancellable, error);
}

static gboolean
content_fetch_old (FetchData     *fetch_data,
                   GMainContext  *context,
                   GCancellable  *cancellable,
                   GError       **error)
{
  EosUpdaterData *data = fetch_data->data;
  const gchar *refspec = fetch_data->update_refspec;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  const gchar *commit_id = fetch_data->update_id;
  const gchar *url_override = NULL;
  OstreeRepo *repo = data->repo;

  if (refspec == NULL || *refspec == '\0')
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "fetch called with empty update refspec");
      return FALSE;
    }

  if (!ostree_parse_refspec (refspec, &remote, &ref, error))
    return FALSE;

  if (remote == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "fetch called with invalid refspec ‘%s’: did not contain a remote name",
                   refspec);
      return FALSE;
    }

  if (commit_id == NULL || *commit_id == '\0')
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "fetch called with empty update commit");
      return FALSE;
    }

  g_message ("Fetch: %s:%s resolved to: %s", remote, ref, commit_id);

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
  if (!repo_pull (repo, remote, commit_id, url_override, fetch_data->progress, cancellable, error))
    return FALSE;

  g_message ("Fetch: pull() completed");

  if (!ostree_repo_read_commit (repo, commit_id, NULL, NULL, cancellable, error))
    return FALSE;

  g_message ("Fetch: commit %s cached", commit_id);

  return TRUE;
}

/* Given a GPtrArray of #EuuFlatpakRemoteRefAction, set the remote-name property
 * in each action "ref" to the name of the actual remote we will be pulling
 * from as opposed to the collection ID.
 *
 * TODO: This is transitional for now, until flatpak supports pulling
 * from collection refs natively */
static gboolean
transition_pending_ref_action_collection_ids_to_remote_names (FlatpakInstallation  *installation,
                                                              GPtrArray            *pending_flatpak_ref_actions,
                                                              GCancellable         *cancellable,
                                                              GError             **error)
{
  g_autoptr(GHashTable) collection_ids_to_remote_names = g_hash_table_new_full (g_str_hash,
                                                                                g_str_equal,
                                                                                g_free,
                                                                                g_free);
  gsize i;

  for (i = 0; i < pending_flatpak_ref_actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *action = g_ptr_array_index (pending_flatpak_ref_actions, i);
      EuuFlatpakLocationRef *to_install = action->ref;
      const gchar *candidate_remote_name = NULL;
      g_autoptr(GError) local_error = NULL;

      if (action->type != EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL)
        continue;

      /* Invariant - must have both a remote name or a collection ID
       * by this point */
      g_assert (to_install->collection_id != NULL && to_install->remote != NULL);

      /* If don’t hit the remote name cache, figure it out now. */
      if (!g_hash_table_lookup_extended (collection_ids_to_remote_names,
                                         to_install->collection_id,
                                         NULL, (gpointer *) &candidate_remote_name))
        {
          g_autofree gchar *found_remote_name = NULL;

          g_message ("Looking up flatpak remote name for collection ID %s", to_install->collection_id);

          /* #EuuFlatpakLocationRef supports specifying both a collection ID
           * and/or a remote name.
           *
           * Once flatpak_installation_install_full gains support for passing
           * collection ID's as opposed to remotes, then we can drop this code */
          found_remote_name = euu_lookup_flatpak_remote_for_collection_id (installation,
                                                                           to_install->collection_id,
                                                                           &local_error);
          candidate_remote_name = found_remote_name;

          /* The user’s repo config might not yet contain the collection IDs, so
           * lookup might fail. */
          if (found_remote_name == NULL)
            {
              g_message ("Failed to find a remote name for collection ID %s: %s",
                         to_install->collection_id,
                         local_error->message);
              g_clear_error (&local_error);
            }
          else
            {
              g_message ("Found remote name %s for collection ID %s",
                         found_remote_name, to_install->collection_id);
            }

          /* @found_remote_name might be %NULL on failure here. */
          g_hash_table_insert (collection_ids_to_remote_names,
                               g_strdup (to_install->collection_id),
                               g_steal_pointer (&found_remote_name));
        }

      /* Cross check the remote name from before with the one we have now */
      if (candidate_remote_name != NULL &&
          g_strcmp0 (to_install->remote,
                     candidate_remote_name) != 0)
        {
          g_set_error (error,
                       EOS_UPDATER_ERROR,
                       EOS_UPDATER_ERROR_FLATPAK_REMOTE_CONFLICT,
                       "Specified flatpak remote '%s' conflicts with the remote "
                       "detected for collection ID '%s' ('%s'), cannot continue.",
                       to_install->remote,
                       to_install->collection_id,
                       candidate_remote_name);
          return FALSE;
        }

       /* The detected remote name and candidate remote name will be
        * the same here, so no need to do anything further */
    }

  return TRUE;
}

static FlatpakInstallFlags
install_flags_for_action_flags (EuuFlatpakRemoteRefActionFlags action_flags)
{
  return !(action_flags & EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY) ?
      FLATPAK_INSTALL_FLAGS_NO_DEPLOY :
      FLATPAK_INSTALL_FLAGS_NONE;
}

static FlatpakInstallFlags
update_flags_for_action_flags (EuuFlatpakRemoteRefActionFlags action_flags)
{
  return !(action_flags & EUU_FLATPAK_REMOTE_REF_ACTION_FLAG_IS_DEPENDENCY) ?
      FLATPAK_UPDATE_FLAGS_NO_DEPLOY :
      FLATPAK_UPDATE_FLAGS_NONE;
}

static gboolean
perform_install_preparation (FlatpakInstallation             *installation,
                             EuuFlatpakLocationRef           *ref,
                             EuuFlatpakRemoteRefActionFlags   action_flags,
                             GCancellable                    *cancellable,
                             GError                         **error)
{
  FlatpakRefKind kind = flatpak_ref_get_kind (ref->ref);
  const gchar *remote = ref->remote;
  const gchar *name = flatpak_ref_get_name (ref->ref);
  const gchar *arch = flatpak_ref_get_arch (ref->ref);
  const gchar *branch = flatpak_ref_get_branch (ref->ref);
  FlatpakInstallFlags install_flags = install_flags_for_action_flags (action_flags);
  g_autoptr(GError) local_error = NULL;

  /* We have to pass in a local_error instance here and check to see
   * if it was FLATPAK_ERROR_ONLY_PULLED - this is what will be
   * thrown if we succeeded at pulling the flatpak into the local
   * repository but did not deploy it (since no FlatpakInstalledRef
   * will be returned).
   *
   * Also note here the call to install_flags_for_action_flags(). Dependency
   * ref actions are immediately deployed upon the fetch() stage as opposed
   * to waiting for eos-updater-flatpak-installer to handle them. This is
   * because we can deploy them "safely" as they are "invisible" to the user. This
   * saves us from having to maintain dependency state in the ostree repo
   * across reboots.
   *
   */
  flatpak_installation_install_full (installation,
                                     install_flags,
                                     remote,
                                     kind,
                                     name,
                                     arch,
                                     branch,
                                     NULL,
                                     NULL,
                                     NULL,
                                     cancellable,
                                     &local_error);

  if (local_error != NULL)
    {
      if (g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED))
        {
          g_autofree gchar *formatted_ref = flatpak_ref_format_ref (ref->ref);
          g_message ("%s:%s already installed, updating to most recent version instead",
                     remote,
                     formatted_ref);
          g_clear_error (&local_error);

          /* flatpak_installation_update will not throw
           * FLATPAK_ERROR_ONLY_PULLED when specifying NO_DEPLOY (since
           * the ref was already available on the system if the flatpak
           * was already installed. So we can directly pass error. */
          if (!flatpak_installation_update (installation,
                                            update_flags_for_action_flags (action_flags),
                                            kind,
                                            name,
                                            arch,
                                            branch,
                                            NULL,
                                            NULL,
                                            NULL,
                                            &local_error))
            {
              /* We'll get FLATPAK_ERROR_ALREADY_INSTALLED again if there
               * were no updates to pull. This is probably a design bug
               * in Flatpak but we have to live with it. */
              if (g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED))
                {
                  g_message ("%s:%s has no updates either, nothing to do",
                             remote,
                             formatted_ref);
                  g_clear_error (&local_error);
                  return TRUE;
                }

              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }

          return TRUE;
        }

      /* Something unexpected failed, return early now.
       *
       * We are not able to meaningfully clean up here - the refs will remain
       * in the flatpak ostree repo for the next time we want to install them
       * but there's nothing in the public API that will allow us to get rid of
       * them. */
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ONLY_PULLED))
        {
          g_autofree gchar *formatted_ref = flatpak_ref_format_ref (ref->ref);
          g_message ("Error occurred whilst pulling flatpak %s:%s: %s",
                     remote,
                     formatted_ref,
                     local_error->message);
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }
  else
    {
      if (install_flags & FLATPAK_INSTALL_FLAGS_NO_DEPLOY)
        {
          /* This is highly highly unlikely to happen and should usually only
           * occur in cases of deployment or programmer error,
           * FLATPAK_INSTALL_FLAGS_NO_DEPLOY is documented to always return
           * the error FLATPAK_ERROR_ONLY_PULLED */
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Flatpak installation should not have succeeded!");
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
perform_update_preparation (FlatpakInstallation             *installation,
                            EuuFlatpakLocationRef           *ref,
                            EuuFlatpakRemoteRefActionFlags   action_flags,
                            GCancellable                    *cancellable,
                            GError                         **error)
{
  FlatpakRefKind kind = flatpak_ref_get_kind (ref->ref);
  const gchar *name = flatpak_ref_get_name (ref->ref);
  const gchar *arch = flatpak_ref_get_arch (ref->ref);
  const gchar *branch = flatpak_ref_get_branch (ref->ref);
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_installation_update (installation,
                                    update_flags_for_action_flags (action_flags),
                                    kind,
                                    name,
                                    arch,
                                    branch,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &local_error))
    {
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED) &&
          !g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
    }

  return TRUE;
}

static gboolean
perform_action_preparation (FlatpakInstallation        *installation,
                            EuuFlatpakRemoteRefAction  *action,
                            GCancellable               *cancellable,
                            GError                    **error)
{
  EuuFlatpakLocationRef *ref = action->ref;

  switch (action->type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return perform_install_preparation (installation,
                                            ref,
                                            action->flags,
                                            cancellable,
                                            error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE:
        return perform_update_preparation (installation,
                                           ref,
                                           action->flags,
                                           cancellable,
                                           error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
        return TRUE;
      default:
        g_assert_not_reached();
        return FALSE;
    }

}

static gboolean
pull_flatpaks (FlatpakInstallation  *installation,
               GPtrArray            *pending_flatpak_ref_actions,
               GCancellable         *cancellable,
               GError              **error)
{
  gsize i;

  if (!installation)
    return FALSE;

  /* From this point onwards, remote-name properties in the action refs are
   * "resolved" - they refer to actual remotes as opposed to collections. We
   * do this here once for N actions as opposed to inline below because
   * (1) we only need to resolve each collection ID once and
   * (2) in case deployment fails, we need to revert the pulled flatpaks which
   *     requires knowing the remote names for all of them */
  if (!transition_pending_ref_action_collection_ids_to_remote_names (installation,
                                                                     pending_flatpak_ref_actions,
                                                                     cancellable,
                                                                     error))
    return FALSE;

  for (i = 0; i < pending_flatpak_ref_actions->len; ++i)
    {
      EuuFlatpakRemoteRefAction *action = g_ptr_array_index (pending_flatpak_ref_actions, i);

      if (!(action->type == EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL ||
            action->type == EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE))
        continue;

      if (!perform_action_preparation (installation, action, cancellable, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
prepare_flatpaks_to_deploy (OstreeRepo    *repo,
                            const gchar   *update_id,
                            GCancellable  *cancellable,
                            GError       **error)
{
  g_autoptr(FlatpakInstallation) installation = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_this_commit_wants = NULL;
  g_autoptr(GHashTable) flatpak_ref_action_progresses = NULL;
  g_autoptr(GHashTable) relevant_flatpak_ref_actions = NULL;
  g_autoptr(GPtrArray) flatpaks_to_deploy = NULL;
  g_autoptr(GPtrArray) flatpaks_to_deploy_with_dependencies = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_this_commit_wants = NULL;
  g_autofree gchar *formatted_relevant_flatpak_ref_actions = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_progress = NULL;

  flatpak_ref_actions_this_commit_wants =
      euu_flatpak_ref_actions_from_ostree_commit (repo,
                                                  update_id,
                                                  cancellable,
                                                  error);

  if (!flatpak_ref_actions_this_commit_wants)
    return FALSE;

  formatted_flatpak_ref_actions_this_commit_wants =
    euu_format_all_flatpak_ref_actions ("All flatpak ref actions that this commit wants to have applied on deployment",
                                        flatpak_ref_actions_this_commit_wants);
  g_message ("%s", formatted_flatpak_ref_actions_this_commit_wants);

  /* Note: This will only fetch applied progresses for actions that
   * have actually been executed by eos-updater-flatpak-installer.
   *
   * The effect of this is that if we fetch commit N, which causes us to
   * prepare actions P to Q, then commit N + 1 adds action Q + 1, without
   * having run eos-updater-flatpak-installer in between, then actions
   * P to Q + 1 will be prepared again when commit N + 1 is pulled.
   *
   * For now, this is relatively benign since it would only cause all
   * the pulled refs to be updated to their most recent version in the
   * remote, which is what would have happened anyway if commits N
   * and N + 1 were pulled at the same time, but it may prove to be
   * problematic if we do any sort of destructive "preparation" in
   * a fetch operation in future. That would require tracking
   * the preparation that has been done in a separate state file
   * to the one here. */
  flatpak_ref_action_progresses =
    euu_flatpak_ref_action_application_progress_in_state_path (cancellable,
                                                               error);

  if (!flatpak_ref_action_progresses)
    return FALSE;

  formatted_flatpak_ref_actions_progress = euu_format_all_flatpak_ref_actions_progresses (flatpak_ref_action_progresses);
  g_message ("%s", formatted_flatpak_ref_actions_progress);

  /* Filter the flatpak ref actions for the ones which are actually relevant
   * to this system and figure out which flatpaks need to be pulled from
   * there */
  relevant_flatpak_ref_actions = euu_filter_for_new_flatpak_ref_actions (flatpak_ref_actions_this_commit_wants,
                                                                         flatpak_ref_action_progresses);

  formatted_relevant_flatpak_ref_actions =
    euu_format_all_flatpak_ref_actions ("Flatpak ref actions that need to be prepared while fetching this commit",
                                        relevant_flatpak_ref_actions);
  g_message ("%s", formatted_relevant_flatpak_ref_actions);

  /* Convert the hash table into a single linear array of flatpaks to pull. */
  flatpaks_to_deploy = euu_flatten_flatpak_ref_actions_table (relevant_flatpak_ref_actions);

  installation = eos_updater_get_flatpak_installation (cancellable, error);

  if (installation == NULL)
    return FALSE;

  flatpaks_to_deploy_with_dependencies =
    euu_add_dependency_ref_actions_for_installation (installation,
                                                     flatpaks_to_deploy,
                                                     cancellable,
                                                     error);

  if (flatpaks_to_deploy_with_dependencies == NULL)
    return FALSE;

  return pull_flatpaks (installation,
                        flatpaks_to_deploy_with_dependencies,
                        cancellable,
                        error);
}

static void
download_now_cb (GObject    *obj,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
  MwscScheduleEntry *schedule_entry = MWSC_SCHEDULE_ENTRY (obj);
  gboolean *download_now_out = user_data;
  *download_now_out = mwsc_schedule_entry_get_download_now (schedule_entry);
}

static void
invalidated_cb (MwscScheduleEntry *entry,
                const GError      *error,
                gpointer           user_data)
{
  GError **error_out = user_data;
  *error_out = g_error_copy (error);
}

static gboolean
timeout_cb (gpointer user_data)
{
  gboolean *timed_out_out = user_data;
  *timed_out_out = TRUE;
  return G_SOURCE_CONTINUE;
}

/* Create a schedule entry for this download in the download scheduler. */
static gboolean
schedule_download (FetchData     *fetch_data,
                   GMainContext  *context,
                   GCancellable  *cancellable,
                   GError       **error)
{
  g_autoptr(MwscScheduler) scheduler = NULL;
  g_autoptr(GAsyncResult) new_result = NULL;
  g_autoptr(GAsyncResult) schedule_result = NULL;
  g_auto(GVariantDict) parameters_dict = G_VARIANT_DICT_INIT (NULL);
  g_autoptr(GVariant) parameters = NULL;

  /* Connect to the download scheduler. If we can’t connect, fail
   * safe, and don’t download on a potentially metered connection.
   * Don’t check the user-specified @scheduling_timeout_seconds here; instead
   * rely on the D-Bus call timeout to exit the loop if it takes too long. */
  mwsc_scheduler_new_async (cancellable, async_result_cb, &new_result);

  while (new_result == NULL)
    g_main_context_iteration (context, TRUE);

  scheduler = mwsc_scheduler_new_finish (new_result, error);

  if (scheduler == NULL)
    return FALSE;

  /* Schedule the download. Similar reasoning applies to the timeout as above. */
  g_variant_dict_insert (&parameters_dict, "resumable", "b", FALSE);
  parameters = g_variant_ref_sink (g_variant_dict_end (&parameters_dict));

  mwsc_scheduler_schedule_async (scheduler, parameters, cancellable,
                                 async_result_cb, &schedule_result);

  while (schedule_result == NULL)
    g_main_context_iteration (context, TRUE);

  fetch_data->schedule_entry = mwsc_scheduler_schedule_finish (scheduler,
                                                               schedule_result, error);

  if (fetch_data->schedule_entry == NULL)
    return FALSE;

  return TRUE;
}

/* Remove the schedule entry for this download from the download scheduler,
 * if one exists. Otherwise, return %TRUE immediately. */
static gboolean
unschedule_download (FetchData     *fetch_data,
                     GMainContext  *context,
                     GCancellable  *cancellable,
                     GError       **error)
{
  g_autoptr(GAsyncResult) remove_result = NULL;
  g_autoptr(MwscScheduleEntry) entry = NULL;
  g_autoptr(GError) local_error = NULL;

  entry = g_steal_pointer (&fetch_data->schedule_entry);

  if (entry == NULL)
    return TRUE;

  /* Remove the schedule entry. Similar reasoning as in schedule_download()
   * applies to the timeout. */
  mwsc_schedule_entry_remove_async (entry, NULL, async_result_cb, &remove_result);

  while (remove_result == NULL)
    g_main_context_iteration (context, TRUE);

  /* Don’t bother propagating errors from invalidated entries, since the entry
   * has already been removed. */
  if (!mwsc_schedule_entry_remove_finish (entry, remove_result, &local_error))
    {
      if (g_error_matches (local_error, MWSC_SCHEDULE_ENTRY_ERROR, MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED))
        g_clear_error (&local_error);
    }

  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

/* This must be run in the same worker thread as content_fetch(), with a
 * #GMainContext used only in that thread. */
static gboolean
check_scheduler (FetchData     *fetch_data,
                 GMainContext  *context,
                 GCancellable  *cancellable,
                 GError       **error)
{
  gboolean download_now;
  g_autoptr(GError) invalidated_error = NULL;
  g_autoptr(GError) local_error = NULL;
  gboolean timed_out;
  g_autoptr(GSource) timeout_source = NULL;
  gulong notify_id, invalidated_id;

  /* If we are forcing updates, don’t check the scheduler at all. */
  if (fetch_data->force)
    {
      g_message ("Fetch: not checking with download scheduler due to force option being set");
      return TRUE;
    }

  /* Otherwise, connect to the download scheduler. If we can’t connect, fail
   * safe, and don’t download on a potentially metered connection. */
  if (!schedule_download (fetch_data, context, cancellable, error))
    return FALSE;

  /* Wait for the scheduler to say to start downloading. If the caller has
   * specified a timeout, only block for that long and then return
   * %EOS_UPDATER_ERROR_METERED_CONNECTION if the download hasn’t been permitted
   * by then. If the #GCancellable is triggered at any point, immediately return
   * %G_IO_ERROR_CANCELLED. */
  download_now = mwsc_schedule_entry_get_download_now (fetch_data->schedule_entry);
  notify_id = g_signal_connect (fetch_data->schedule_entry, "notify::download-now",
                                (GCallback) download_now_cb, &download_now);

  invalidated_error = NULL;
  invalidated_id = g_signal_connect (fetch_data->schedule_entry, "invalidated",
                                     (GCallback) invalidated_cb, &invalidated_error);

  timed_out = FALSE;

  if (fetch_data->scheduling_timeout_seconds > 0)
    {
      timeout_source = g_timeout_source_new_seconds (fetch_data->scheduling_timeout_seconds);
      g_source_set_callback (timeout_source, timeout_cb, &timed_out, NULL);
      g_source_attach (timeout_source, context);
    }

  if (fetch_data->scheduling_timeout_seconds == 0)
    g_message ("Fetch: waiting indefinitely for response from download scheduler");
  else
    g_message ("Fetch: waiting for %u seconds for response from download scheduler",
               fetch_data->scheduling_timeout_seconds);

  while (!download_now && !g_cancellable_is_cancelled (cancellable) &&
         !timed_out && invalidated_error == NULL)
    g_main_context_iteration (context, TRUE);

  if (timeout_source != NULL)
    g_source_destroy (timeout_source);
  g_signal_handler_disconnect (fetch_data->schedule_entry, invalidated_id);
  g_signal_handler_disconnect (fetch_data->schedule_entry, notify_id);

  /* Get downloading!
   * FIXME: In future, we will want to monitor notify::download-now and cancel
   * the ongoing download if the scheduler tells us to. For now, just download
   * through from start to finish. */
  if (download_now)
    {
      g_message ("Fetch: got positive response from download scheduler");
      return TRUE;
    }

  /* Handle the possible error paths. */
  if (timed_out)
    g_set_error (error,
                 EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_METERED_CONNECTION,
                 "Error fetching update: %s",
                 "Timed out waiting for download to be scheduled");
  else if (invalidated_error != NULL)
    {
      g_autofree gchar *message = g_strdup_printf ("Download scheduler disappeared unexpectedly: %s",
                                                   invalidated_error->message);
      g_set_error (error,
                   EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_METERED_CONNECTION,
                   "Error fetching update: %s", message);
    }
  else
    g_cancellable_set_error_if_cancelled (cancellable, error);

  g_message ("Fetch: removing download scheduler entry");

  if (!unschedule_download (fetch_data, context, cancellable, &local_error))
    {
      g_warning ("Failed to remove schedule entry: %s", local_error->message);
      g_clear_error (&local_error);
    }

  g_assert (error == NULL || *error != NULL);

  return FALSE;
}

static gboolean
content_fetch (FetchData     *fetch_data,
               GMainContext  *context,
               GCancellable  *cancellable,
               GError       **error)
{
  g_autoptr(GError) local_error = NULL;
  const gchar *update_id = fetch_data->update_id;
  EosUpdaterData *data = fetch_data->data;

  /* Query the scheduler here. Just fail if downloads aren’t allowed; the
   * updater will return a D-Bus error which the caller can interpret. */
  if (!check_scheduler (fetch_data, context, cancellable, error))
    {
      g_message ("Fetch: not fetching due to download scheduler decision");
      return FALSE;
    }

  /* Do we want to use the new libostree code for P2P, or fall back on the old
   * eos-updater code?
   * FIXME: Eventually drop the old code. See:
   * https://phabricator.endlessm.com/T19606 */
  if (data->results != NULL)
    {
      g_message ("Fetch: using results %p", data->results);

      if (content_fetch_new (fetch_data, context, cancellable, &local_error))
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

      if (content_fetch_old (fetch_data, context, cancellable, &local_error))
        g_message ("Fetch: finished pulling using old code");
      else
        {
          g_message ("Fetch: error pulling using old code: %s", local_error->message);
          goto error;
        }
    }

  g_message ("Fetch: pulling any necessary new flatpaks for this update");
  if (!prepare_flatpaks_to_deploy (data->repo, update_id, cancellable, &local_error))
    {
      g_message ("Fetch: failed to pull necessary new flatpaks for update: %s", local_error->message);
      goto error;
    }

  return TRUE;

error:
  g_propagate_error (error, g_steal_pointer (&local_error));

  if (!unschedule_download (fetch_data, context, cancellable, &local_error))
    {
      g_warning ("Fetch: failed to remove download schedule entry: %s", local_error->message);
      g_clear_error (&local_error);
    }

  return FALSE;
}

static void
content_fetch_task (GTask        *task,
                    gpointer      object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  FetchData *fetch_data = task_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  if (!content_fetch (fetch_data, task_context, cancellable, &error))
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
  return handle_fetch_full (updater, call, NULL, user_data);
}

gboolean
handle_fetch_full (EosUpdater            *updater,
                   GDBusMethodInvocation *call,
                   GVariant              *options,
                   gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  EosUpdaterState state = eos_updater_get_state (updater);
  EosUpdaterData *data = user_data;
  g_autoptr(FetchData) fetch_data = NULL;
  gboolean force = FALSE;
  guint scheduling_timeout_seconds = 0;  /* default to an infinite timeout */

  if (state != EOS_UPDATER_STATE_UPDATE_AVAILABLE)
    {
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call Fetch() while in state %s",
          eos_updater_state_to_string (state));
      return TRUE;
    }

  /* Handle the options, if passed. */
  if (options != NULL)
    {
      g_variant_lookup (options, "force", "b", &force);
      g_variant_lookup (options, "scheduling-timeout-seconds", "u",
                        &scheduling_timeout_seconds);
    }

  fetch_data = g_new0 (FetchData, 1);
  fetch_data->force = force;
  fetch_data->scheduling_timeout_seconds = scheduling_timeout_seconds;
  fetch_data->update_id = g_strdup (eos_updater_get_update_id (updater));
  fetch_data->update_refspec = g_strdup (eos_updater_get_update_refspec (updater));
  fetch_data->progress = ostree_async_progress_new_and_connect (update_progress, updater);

  /* FIXME: Passing the EosUpdaterData to the worker thread is not thread safe.
   * See: https://phabricator.endlessm.com/T15923 */
  fetch_data->data = data;

  eos_updater_data_reset_cancellable (data);
  eos_updater_clear_error (updater, EOS_UPDATER_STATE_FETCHING);
  task = g_task_new (updater, data->cancellable, content_fetch_finished, NULL);
  g_task_set_task_data (task, g_steal_pointer (&fetch_data), (GDestroyNotify) fetch_data_free);
  g_task_run_in_thread (task, content_fetch_task);

  eos_updater_complete_fetch (updater, call);

  return TRUE;
}
