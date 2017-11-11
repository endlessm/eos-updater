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

#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/util.h>
#include <libeos-updater-util/types.h>

#include <flatpak.h>

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

static GFile *
inspect_directory_in_ostree_repo (OstreeRepo    *repo,
                                  const gchar   *checksum,
                                  const gchar   *subpath,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
  g_autofree gchar *checkout_directory_path = NULL;
  g_autoptr(GFile) checkout_directory = NULL;
  OstreeRepoCheckoutAtOptions options = { 0, };

  checkout_directory = get_temporary_directory_to_checkout_in (error);

  if (!checkout_directory)
    return NULL;

  checkout_directory_path = g_file_get_path (checkout_directory);

  /* Now that we have a temporary directory, checkout the OSTree in it
   * at the nominated path */
  options.subpath = subpath;

  if (!ostree_repo_checkout_at (repo,
                                &options,
                                -1,
                                checkout_directory_path,
                                checksum,
                                cancellable,
                                error))
    {
      eos_updater_remove_recursive (checkout_directory, NULL);
      return NULL;
    }

  return g_steal_pointer (&checkout_directory);
}

static GHashTable *
flatpak_ref_actions_for_commit (OstreeRepo    *repo,
                                const gchar   *checksum,
                                GCancellable  *cancellable,
                                GError       **error)
{
  g_autofree gchar *checkout_directory_path = NULL;
  const gchar *path_relative_to_deployment = "usr/share/eos-application-tools/flatpak-autoinstall.d";
  g_autoptr(GFile) checkout_directory = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_table = NULL;
  g_autoptr(GError) local_error = NULL;

  checkout_directory = get_temporary_directory_to_checkout_in (error);
  checkout_directory_path = g_file_get_path (checkout_directory);

  /* Now that we have a temporary directory, checkout the OSTree in it
   * at the /usr/share/eos-application-tools path. If it fails, there's nothing to
   * read, otherwise we can read in the list of flatpaks to be auto-installed
   * for this commit. */
  checkout_directory = inspect_directory_in_ostree_repo (repo,
                                                         checksum,
                                                         path_relative_to_deployment,
                                                         cancellable,
                                                         &local_error);

  if (!checkout_directory)
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return NULL;
        }

      return g_hash_table_new (NULL, NULL);
    }

  flatpak_ref_actions_table = eos_updater_util_flatpak_ref_actions_from_directory (path_relative_to_deployment,
                                                                                   checkout_directory,
                                                                                   FLATPAKS_IN_OSTREE_PRIORITY,
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


/* Given a GPtrArray of FlatpakRemoteRefAction, set the remote-name property
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
      FlatpakRemoteRefAction *action = g_ptr_array_index (pending_flatpak_ref_actions, i);
      FlatpakLocationRef *to_install = action->ref;
      const gchar *collection_id = to_install->collection_id;
      const gchar *candidate_remote_name = NULL;
      g_autoptr(GError) local_error = NULL;

      if (action->type != EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL)
        continue;

      /* Invariant - must have either a remote name or a collection ID
       * by this point */
      g_assert (collection_id || to_install->remote);

      /* If a collection ID was not specified, then we already have the
       * remote, so we can just continue here */
      if (collection_id == NULL)
        continue;

      /* If we didn't hit the remote name cache, figure it out now. */
      if (candidate_remote_name == NULL)
        {
          g_autofree gchar *formatted_ref = flatpak_ref_format_ref (to_install->ref);
          g_autofree gchar *found_remote_name = NULL;
          OstreeCollectionRef collection_ref =
          {
            (gchar *) to_install->collection_id,
            formatted_ref
          };

          g_message ("Looking up flatpak remote name for collection ID %s", collection_id);

          /* FlatpakLocationRef supports specifying both a collection ID and/or
           * a remote name.
           *
           * Once flatpak_installation_install_full gains support for passing
           * collection ID's as opposed to remotes, then we can drop this code */
          found_remote_name = eos_updater_util_lookup_flatpak_repo_for_collection_id (installation,
                                                                                      collection_ref.collection_id,
                                                                                      &local_error);
          candidate_remote_name = found_remote_name;

          /* Should be able to find a remote name for all collection IDs */
          if (candidate_remote_name == NULL &&
              to_install->remote == NULL)
            {
              g_message ("Failed to find a remote name for collection ID %s: %s",
                         collection_id,
                         local_error->message);
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }

          g_message ("Found remote name %s for collection ID %s",
                     candidate_remote_name,
                     collection_id);

          g_hash_table_insert (collection_ids_to_remote_names,
                               g_strdup (collection_id),
                               g_steal_pointer (&found_remote_name));
        }

      /* If we had a remote name from before, check to make sure the two
       * match, otherwise bail out */
      if (to_install->remote != NULL)
        {
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
                           collection_id,
                           candidate_remote_name);
              return FALSE;
            }

           /* The detected remote name and candidate remote name will be
            * the same here, so no need to do anything */
        }
      else
        {
          /* Set the remote name now */
          to_install->remote = g_strdup (candidate_remote_name);
        }
    }

  return TRUE;
}

static gboolean
perform_install_preparation (FlatpakInstallation  *installation,
                             FlatpakLocationRef   *ref,
                             GCancellable         *cancellable,
                             GError              **error)
{
  FlatpakRefKind kind = flatpak_ref_get_kind (ref->ref);
  const gchar *remote = ref->remote;
  const gchar *name = flatpak_ref_get_name (ref->ref);
  g_autoptr(GError) local_error = NULL;

  /* We have to pass in a local_error instance here and check to see
   * if it was FLATPAK_ERROR_ONLY_PULLED - this is what will be
   * thrown if we succeeded at pulling the flatpak into the local
   * repository but did not deploy it (since no FlatpakInstalledRef
   * will be returned). */
  flatpak_installation_install_full (installation,
                                     FLATPAK_INSTALL_FLAGS_NO_DEPLOY,
                                     remote,
                                     kind,
                                     name,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     cancellable,
                                     &local_error);

  /* This is highly highly unlikely to happen and should usually only
   * occur in cases of deployment or programmer error,
   * FLATPAK_INSTALL_FLAGS_NO_DEPLOY is documented to always return
   * the error FLATPAK_ERROR_ONLY_PULLED */
  if (local_error == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Flatpak installation should not have succeeded!");
      return FALSE;
    }

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
                                        FLATPAK_UPDATE_FLAGS_NO_DEPLOY,
                                        kind,
                                        name,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        error))
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
      g_autofree gchar *formatted_ref = flatpak_ref_format_ref (ref->ref);
      g_message ("Error occurred whilst pulling flatpak %s:%s: %s",
                 remote,
                 formatted_ref,
                 local_error->message);
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

static gboolean
perform_update_preparation (FlatpakInstallation  *installation,
                            FlatpakLocationRef   *ref,
                            GCancellable         *cancellable,
                            GError              **error)
{
  FlatpakRefKind kind = flatpak_ref_get_kind (ref->ref);
  const gchar *name = flatpak_ref_get_name (ref->ref);
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_installation_update (installation,
                                    FLATPAK_UPDATE_FLAGS_NO_DEPLOY,
                                    kind,
                                    name,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &local_error))
    {
      if (!g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      g_clear_error (&local_error);
    }

  return TRUE;
}

static gboolean
perform_action_preparation (FlatpakInstallation      *installation,
                            FlatpakRemoteRefAction   *action,
                            GCancellable             *cancellable,
                            GError                  **error)
{
  FlatpakLocationRef *ref = action->ref;

  switch (action->type)
    {
      case EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL:
        return perform_install_preparation (installation, ref, cancellable, error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE:
        return perform_update_preparation (installation, ref, cancellable, error);
      case EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL:
        return TRUE;
      default:
        g_assert_not_reached();
        return FALSE;
    }

}

static gboolean
pull_flatpaks (GPtrArray     *pending_flatpak_ref_actions,
               GCancellable  *cancellable,
               GError       **error)
{
  g_autoptr(FlatpakInstallation) installation = eos_updater_get_flatpak_installation (cancellable,
                                                                                      error);
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
      FlatpakRemoteRefAction *action = g_ptr_array_index (pending_flatpak_ref_actions, i);

      if (!(action->type == EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL ||
            action->type == EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE))
        continue;

      if (!perform_action_preparation (installation, action, cancellable, error))
        return FALSE;
    }

  return TRUE;
}

static GHashTable *
compute_flatpak_ref_actions_tables (OstreeRepo    *repo,
                                    const gchar   *update_id,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
  g_auto(GStrv) override_dirs = g_strsplit (eos_updater_util_flatpak_autoinstall_override_paths (), ";", -1);
  GStrv iter = NULL;
  gint priority_counter = 0;
  g_autoptr(GHashTable) ref_actions = flatpak_ref_actions_for_commit (repo,
                                                                      update_id,
                                                                      cancellable,
                                                                      error);

  if (!ref_actions)
    return NULL;

  for (iter = override_dirs; *iter != NULL; ++iter, ++priority_counter)
    {
      if (!eos_updater_util_flatpak_ref_actions_maybe_append_from_directory (*iter,
                                                                             ref_actions,
                                                                             FLATPAKS_IN_OVERRIDE_DIR_PRIORITY + priority_counter,
                                                                             NULL,
                                                                             error))
        return NULL;
    }

  return eos_updater_util_hoist_flatpak_remote_ref_actions (ref_actions);
}

static gboolean
prepare_flatpaks_to_deploy (OstreeRepo    *repo,
                            const gchar   *update_id,
                            GCancellable  *cancellable,
                            GError       **error)
{
  g_autoptr(GHashTable) flatpak_ref_actions_this_commit_wants = NULL;
  g_autoptr(GHashTable) flatpak_ref_action_progresses = NULL;
  g_autoptr(GHashTable) relevant_flatpak_ref_actions = NULL;
  g_autoptr(GPtrArray) flatpaks_to_deploy = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_this_commit_wants = NULL;
  g_autofree gchar *formatted_relevant_flatpak_ref_actions = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_progress = NULL;

  flatpak_ref_actions_this_commit_wants = compute_flatpak_ref_actions_tables (repo,
                                                                              update_id,
                                                                              cancellable,
                                                                              error);

  if (!flatpak_ref_actions_this_commit_wants)
    return FALSE;

  formatted_flatpak_ref_actions_this_commit_wants =
    eos_updater_util_format_all_flatpak_ref_actions ("All flatpak ref actions that this commit wants to have applied on deployment",
                                                     flatpak_ref_actions_this_commit_wants);
  g_message ("%s", formatted_flatpak_ref_actions_this_commit_wants);

  flatpak_ref_action_progresses =
    eos_updater_util_flatpak_ref_action_application_progress_in_state_path (cancellable,
                                                                            error);

  if (!flatpak_ref_action_progresses)
    return FALSE;

  formatted_flatpak_ref_actions_progress = eos_updater_util_format_all_flatpak_ref_actions_progresses (flatpak_ref_action_progresses);
  g_message ("%s", formatted_flatpak_ref_actions_progress);

  /* Filter the flatpak ref actions for the ones which are actually relevant
   * to this system and figure out which flatpaks need to be pulled from
   * there */
  relevant_flatpak_ref_actions = eos_updater_util_filter_for_new_flatpak_ref_actions (flatpak_ref_actions_this_commit_wants,
                                                                                      flatpak_ref_action_progresses);

  formatted_relevant_flatpak_ref_actions =
    eos_updater_util_format_all_flatpak_ref_actions ("Flatpak ref actions that need to be prepared while fetching this commit",
                                                     relevant_flatpak_ref_actions);
  g_message ("%s", formatted_relevant_flatpak_ref_actions);

  /* Convert the hash table into a single linear array of flatpaks to pull. */
  flatpaks_to_deploy = eos_updater_util_flatten_flatpak_ref_actions_table (relevant_flatpak_ref_actions);

  if (!pull_flatpaks (flatpaks_to_deploy, cancellable, error))
    return FALSE;

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
  const gchar *update_id = eos_updater_get_update_id (updater);

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

  g_message ("Fetch: pulling any necessary new flatpaks for this update");
  if (!prepare_flatpaks_to_deploy (data->repo, update_id, cancellable, &local_error))
    {
      g_message ("Fetch: failed to pull necessary new flatpaks for update: %s", local_error->message);
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

static void
content_fetch_task (GTask *task,
                    gpointer object,
                    gpointer task_data,
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
