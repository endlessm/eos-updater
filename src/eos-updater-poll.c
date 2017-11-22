/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include "eos-updater-data.h"
#include "eos-updater-object.h"
#include "eos-updater-poll-common.h"
#include "eos-updater-poll-main.h"
#include "eos-updater-poll.h"
#include "resources.h"

#include <libeos-updater-util/config.h>
#include <libeos-updater-util/util.h>

static const gchar *const CONFIG_FILE_PATH = SYSCONFDIR "/" PACKAGE "/eos-updater.conf";
static const gchar *const LOCAL_CONFIG_FILE_PATH = PREFIX "/local/share/" PACKAGE "/eos-updater.conf";
static const gchar *const STATIC_CONFIG_FILE_PATH = PKGDATADIR "/eos-updater.conf";
static const gchar *const DOWNLOAD_GROUP = "Download";
static const gchar *const ORDER_KEY = "Order";

static gboolean
strv_to_download_order (gchar **sources,
                        GArray **out_download_order,
                        GError **error)
{
  g_autoptr(GArray) array = g_array_new (FALSE, /* not null terminated */
                                         FALSE, /* no clearing */
                                         sizeof (EosUpdaterDownloadSource));
  g_autoptr(GHashTable) found_sources = g_hash_table_new (NULL, NULL);
  gchar **iter;

  for (iter = sources; *iter != NULL; ++iter)
    {
      EosUpdaterDownloadSource idx;
      const gchar *key = g_strstrip (*iter);

      if (!string_to_download_source (key, &idx, error))
        return FALSE;

      if (!g_hash_table_add (found_sources, GINT_TO_POINTER (idx)))
        {
          g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_CONFIGURATION,
                       "Duplicated download source %s",
                       key);
          return FALSE;
        }
      g_array_append_val (array, idx);
    }

  if (array->len == 0)
    {
      g_set_error_literal (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_CONFIGURATION,
                           "No download sources");
      return FALSE;
    }

  *out_download_order = g_steal_pointer (&array);
  return TRUE;
}

static const gchar *
get_config_file_path (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_CONFIG_FILE_PATH",
                                    CONFIG_FILE_PATH);
}

typedef struct
{
  GArray *download_order;
  /* @override_uris must be non-empty if it’s non-%NULL: */
  gchar **override_uris;  /* (owned) (nullable) (array zero-terminated=1) */
} SourcesConfig;

#define SOURCES_CONFIG_CLEARED { NULL }

static void
sources_config_clear (SourcesConfig *config)
{
  g_clear_pointer (&config->download_order, g_array_unref);
  g_clear_pointer (&config->override_uris, g_strfreev);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (SourcesConfig, sources_config_clear)

static gboolean
read_config (const gchar *config_file_path,
             SourcesConfig *sources_config,
             GError **error)
{
  g_autoptr(EuuConfigFile) config = NULL;
  g_auto(GStrv) download_order_strv = NULL;
  g_autofree gchar *group_name = NULL;
  const gchar * const paths[] =
    {
      config_file_path,  /* typically CONFIG_FILE_PATH unless testing */
      LOCAL_CONFIG_FILE_PATH,
      STATIC_CONFIG_FILE_PATH,
      NULL
    };

  /* Load the config file. */
  config = euu_config_file_new (paths, eos_updater_resources_get_resource (),
                                "/com/endlessm/Updater/config/eos-updater.conf");

  /* Parse the options. */
  download_order_strv = euu_config_file_get_strv (config,
                                                  DOWNLOAD_GROUP, ORDER_KEY,
                                                  NULL, error);
  if (download_order_strv == NULL)
    return FALSE;

  if (!strv_to_download_order (download_order_strv,
                               &sources_config->download_order,
                               error))
    return FALSE;

  /* FIXME: For the moment, this is undocumented and hidden. It can also be set
   * via the PollVolume() D-Bus method. It must be non-empty if set. */
  sources_config->override_uris = euu_config_file_get_strv (config,
                                                            DOWNLOAD_GROUP,
                                                            "OverrideUris", NULL,
                                                            error);
  /* Normalise empty arrays to NULL. */
  if (sources_config->override_uris != NULL && sources_config->override_uris[0] == NULL)
    g_clear_pointer (&sources_config->override_uris, g_strfreev);

  return TRUE;
}

static void
object_unref0 (gpointer obj)
{
  if (obj == NULL)
    return;
  g_object_unref (obj);
}

static GPtrArray *
get_finders (SourcesConfig          *config,
             GMainContext           *context,
             OstreeRepoFinderAvahi **out_finder_avahi)
{
  g_autoptr(OstreeRepoFinderAvahi) finder_avahi = NULL;
  g_autoptr(GPtrArray) finders = g_ptr_array_new_full (config->download_order->len,
                                                       object_unref0);
  g_autoptr(GError) local_error = NULL;
  gsize i;

  /* FIXME: Refactor the download_order handling once the old code paths have
   * been dropped, since we no longer care about the *order* of entries in
   * download_order. */
  g_assert (config->download_order->len > 0);

  for (i = 0; i < config->download_order->len; i++)
    {
      switch (g_array_index (config->download_order, EosUpdaterDownloadSource, i))
        {
        case EOS_UPDATER_DOWNLOAD_MAIN:
          g_ptr_array_add (finders, ostree_repo_finder_config_new ());
          break;

        case EOS_UPDATER_DOWNLOAD_LAN:
          /* strv_to_download_order() already checks for duplicated download_order entries */
          g_assert (finder_avahi == NULL);
          finder_avahi = ostree_repo_finder_avahi_new (context);
          g_ptr_array_add (finders, g_object_ref (finder_avahi));
          break;

        case EOS_UPDATER_DOWNLOAD_VOLUME:
          /* TODO: How to make this one testable? */
          g_ptr_array_add (finders, ostree_repo_finder_mount_new (NULL));
          break;

        default:
          g_assert_not_reached ();
        }
    }

  if (config->override_uris != NULL)
    {
      g_autoptr(OstreeRepoFinderOverride) finder_override = ostree_repo_finder_override_new ();

      g_ptr_array_set_size (finders, 0);  /* override everything */
      g_ptr_array_add (finders, g_object_ref (finder_override));
      g_clear_object (&finder_avahi);

      for (i = 0; config->override_uris[i] != NULL; i++)
        ostree_repo_finder_override_add_uri (finder_override, config->override_uris[i]);
    }

  g_ptr_array_add (finders, NULL);  /* NULL terminator */

  /* TODO: Stop this at some point; think of a better way to store it and
   * control its lifecycle. */
  if (finder_avahi != NULL)
    ostree_repo_finder_avahi_start (OSTREE_REPO_FINDER_AVAHI (finder_avahi),
                                    &local_error);

  if (local_error != NULL)
    {
      g_warning ("Avahi finder failed; removing it: %s", local_error->message);
      g_ptr_array_remove (finders, finder_avahi);
      g_clear_object (&finder_avahi);
      g_clear_error (&local_error);
    }

  if (out_finder_avahi != NULL)
    *out_finder_avahi = g_steal_pointer (&finder_avahi);

  return g_steal_pointer (&finders);
}

typedef OstreeRepoFinderAvahi RepoFinderAvahiRunning;

static void
repo_finder_avahi_stop_and_unref (RepoFinderAvahiRunning *finder)
{
  if (finder == NULL)
    return;

  ostree_repo_finder_avahi_stop (finder);
  g_object_unref (finder);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RepoFinderAvahiRunning, repo_finder_avahi_stop_and_unref)

static void
async_result_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;

  *result_out = g_object_ref (result);
}

/* May return NULL without setting an error if no updates were found. */
static EosUpdateInfo *
metadata_fetch_new (OstreeRepo    *repo,
                    SourcesConfig *config,
                    GMainContext  *context,
                    GCancellable  *cancellable,
                    GError       **error)
{
  g_auto(OstreeRepoFinderResultv) results = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autofree gchar *booted_refspec = NULL, *new_refspec = NULL;
  g_autoptr(OstreeCollectionRef) collection_ref_to_upgrade_on_for_booted_deployment = NULL, new_collection_ref = NULL;
  const gchar *upgrade_refspec;
  const OstreeCollectionRef *upgrade_collection_ref;
  g_autoptr(GPtrArray) finders = NULL;  /* (element-type OstreeRepoFinder) */
  g_autoptr(RepoFinderAvahiRunning) finder_avahi = NULL;
  gboolean redirect_followed = FALSE;

  if (!get_refspec_to_upgrade_on (&booted_refspec,
                                  NULL,
                                  NULL,
                                  &collection_ref_to_upgrade_on_for_booted_deployment,
                                  error))
    return NULL;

  if (collection_ref_to_upgrade_on_for_booted_deployment == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "No collection ID set for currently booted deployment.");
      return NULL;
    }

  upgrade_collection_ref = collection_ref_to_upgrade_on_for_booted_deployment;
  upgrade_refspec = booted_refspec;

  finders = get_finders (config, context, &finder_avahi);

  /* Check whether the commit is a redirection; if so, fetch the new ref and
   * check again. */
  do
    {
      const OstreeCollectionRef *refs[] = { upgrade_collection_ref, NULL };
      g_autoptr(GVariant) pull_options = NULL;
      g_autoptr(GAsyncResult) find_result = NULL, pull_result = NULL;
      g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{sv}"));

      g_debug ("%s: Finding remotes advertising upgrade_collection_ref: (%s, %s)",
               G_STRFUNC, upgrade_collection_ref->collection_id, upgrade_collection_ref->ref_name);

      ostree_repo_find_remotes_async (repo, refs, NULL  /* options */,
                                      (OstreeRepoFinder **) finders->pdata,
                                      NULL  /* progress */,
                                      cancellable, async_result_cb, &find_result);

      while (find_result == NULL)
        g_main_context_iteration (context, TRUE);

      results = ostree_repo_find_remotes_finish (repo, find_result, error);
      if (results == NULL)
        return NULL;

      /* No updates available. */
      if (results[0] == NULL)
        {
          g_message ("Poll: Couldn’t find any updates");
          return NULL;
        }

      g_variant_builder_add (&builder, "{s@v}", "flags",
                             g_variant_new_variant (g_variant_new_int32 (OSTREE_REPO_PULL_FLAGS_COMMIT_ONLY)));
      pull_options = g_variant_ref_sink (g_variant_builder_end (&builder));

      ostree_repo_pull_from_remotes_async (repo,
                                           (const OstreeRepoFinderResult * const *) results,
                                           pull_options, NULL  /* progress */, cancellable,
                                           async_result_cb, &pull_result);

      while (pull_result == NULL)
        g_main_context_iteration (context, TRUE);

      if (!ostree_repo_pull_from_remotes_finish (repo, pull_result, error))
        return NULL;

      g_clear_pointer (&checksum, g_free);
      g_clear_pointer (&new_refspec, g_free);
      g_clear_pointer (&new_collection_ref, ostree_collection_ref_free);

      /* Parse the commit and check there’s no redirection to a new ref. */
      if (!parse_latest_commit (repo, upgrade_refspec, &redirect_followed,
                                &checksum, &new_refspec, NULL, cancellable, error))
        return NULL;

      if (new_refspec != NULL)
        upgrade_refspec = new_refspec;
      if (new_collection_ref != NULL)
        upgrade_collection_ref = new_collection_ref;
    }
  while (redirect_followed);

  /* Final checks on the commit we found. */
  if (!is_checksum_an_update (repo, checksum, &commit, error))
    return NULL;

  if (commit == NULL)
    return NULL;

  info = eos_update_info_new (checksum, commit,
                              upgrade_refspec, booted_refspec,
                              NULL, g_steal_pointer (&results));
  metrics_report_successful_poll (info);

  return g_steal_pointer (&info);
}

static void
metadata_fetch (GTask *task,
                gpointer object,
                gpointer task_data,
                GCancellable *cancellable)
{
  EosUpdaterData *data = task_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_new ();
  g_autoptr(EosMetadataFetchData) fetch_data = NULL;
  g_auto(SourcesConfig) config = SOURCES_CONFIG_CLEARED;
  g_autoptr(OstreeDeployment) deployment = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  static gboolean use_new_code = TRUE;
  /* TODO: link this --^ to failure of the fetch or apply stages?
   * Add environment variables or something else to force it one way or the other?
   * Make it clear in the logging which code path is being used. */
  gboolean disable_old_code = (g_getenv ("EOS_UPDATER_DISABLE_FALLBACK_FETCHERS") != NULL);

  fetch_data = eos_metadata_fetch_data_new (task, data, task_context);

  /* Check we’re not on a dev-converted system. */
  deployment = eos_updater_get_booted_deployment (&error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED))
    {
      g_task_return_new_error (task, EOS_UPDATER_ERROR,
                               EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM,
                               "Not an OSTree-based system: cannot update it.");
      return;
    }

  g_clear_error (&error);

  /* Work out which sources to poll. */
  if (!read_config (get_config_file_path (), &config, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Do we want to use the new libostree code for P2P, or fall back on the old
   * eos-updater code?
   * FIXME: Eventually drop the old code. See:
   * https://phabricator.endlessm.com/T19606 */
  if (use_new_code)
    {
      info = metadata_fetch_new (data->repo, &config, task_context, cancellable, &error);

      if (error != NULL)
        {
          use_new_code = FALSE;

          g_warning ("Error polling for updates using libostree P2P code; falling back to old code: %s",
                     error->message);
          g_clear_error (&error);
        }

      if (info != NULL)
        {
          g_autofree gchar *update_string = eos_update_info_to_string (info);
          g_debug ("%s: Got update results %p from new P2P code: %s",
                   G_STRFUNC, info->results, update_string);
        }
    }

  /* Fall back to the old code path. */
  if (info == NULL && !disable_old_code)
    {
      gsize i;
      gboolean main_enabled = FALSE;

      for (i = 0; i < config.download_order->len; i++)
        {
          if (g_array_index (config.download_order,
                             EosUpdaterDownloadSource, i) == EOS_UPDATER_DOWNLOAD_MAIN)
            {
              main_enabled = TRUE;
              break;
            }
        }

      if (main_enabled)
        {
          g_autoptr(GPtrArray) fetchers = g_ptr_array_sized_new (1);
          g_assert (config.download_order->len > 0);
          g_ptr_array_add (fetchers, metadata_fetch_from_main);

          info = run_fetchers (fetch_data,
                               fetchers,
                               config.download_order);
        }
      else
        {
          g_debug ("%s: Not polling for updates on old code path as main source is not enabled",
                   G_STRFUNC);
          info = NULL;
        }
    }

  if (error != NULL)
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task,
                           (info != NULL) ? g_object_ref (info) : NULL,
                           g_object_unref);
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
      case EOS_UPDATER_STATE_NONE:
      case EOS_UPDATER_STATE_POLLING:
      case EOS_UPDATER_STATE_FETCHING:
      case EOS_UPDATER_STATE_APPLYING_UPDATE:
      case EOS_UPDATER_STATE_UPDATE_APPLIED:
      default:
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call Poll() while in state %s",
          eos_updater_state_to_string (state));
        goto bail;
    }

  eos_updater_clear_error (updater, EOS_UPDATER_STATE_POLLING);
  task = g_task_new (updater, NULL, metadata_fetch_finished, user_data);
  g_task_set_task_data (task, user_data, NULL);
  g_task_run_in_thread (task, metadata_fetch);

  eos_updater_complete_poll (updater, call);

bail:
  return TRUE;
}

typedef struct
{
  EosUpdaterData *data;

  gchar *volume_path;
} VolumeMetadataFetchData;

static VolumeMetadataFetchData *
volume_metadata_fetch_data_new (EosUpdaterData        *data,
                                GDBusMethodInvocation *call)
{
  GVariant *parameters = g_dbus_method_invocation_get_parameters (call);
  const gchar *path;
  VolumeMetadataFetchData *volume_fetch_data;

  g_variant_get (parameters, "(s)", &path);
  volume_fetch_data = g_new (VolumeMetadataFetchData, 1);
  volume_fetch_data->data = data;
  volume_fetch_data->volume_path = g_strdup (path);

  return volume_fetch_data;
}

static void
volume_metadata_fetch_data_free (gpointer volume_fetch_data_ptr)
{
  VolumeMetadataFetchData *volume_fetch_data = volume_fetch_data_ptr;

  g_free (volume_fetch_data->volume_path);
  g_free (volume_fetch_data);
}

static void
volume_metadata_fetch (GTask        *task,
                       gpointer      object,
                       gpointer      task_data,
                       GCancellable *cancellable)
{
  VolumeMetadataFetchData *volume_fetch_data = task_data;
  EosUpdaterData *data = volume_fetch_data->data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_new ();
  g_auto(SourcesConfig) config = SOURCES_CONFIG_CLEARED;
  g_autoptr(OstreeDeployment) deployment = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  EosUpdaterDownloadSource idx;
  g_autofree gchar *repo_path = NULL;

  /* Check we’re not on a dev-converted system. */
  deployment = eos_updater_get_booted_deployment (&error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED))
    {
      g_task_return_new_error (task, EOS_UPDATER_ERROR,
                               EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM,
                               "Not an OSTree-based system: cannot update it.");
      return;
    }

  g_clear_error (&error);

  config.download_order = g_array_new (FALSE, /* not null terminated */
                                       FALSE, /* no clearing */
                                       sizeof (EosUpdaterDownloadSource));
  idx = EOS_UPDATER_DOWNLOAD_MAIN;
  g_array_append_val (config.download_order, idx);

  repo_path = g_build_filename (volume_fetch_data->volume_path, ".ostree", "repo", NULL);
  config.override_uris = g_new0 (gchar *, 2);
  config.override_uris[0] = g_strconcat ("file://", repo_path, NULL);

  info = metadata_fetch_new (data->repo, &config, task_context, cancellable, &error);

  if (error != NULL)
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task,
                           (info != NULL) ? g_object_ref (info) : NULL,
                           g_object_unref);
}

gboolean
handle_poll_volume (EosUpdater            *updater,
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
      case EOS_UPDATER_STATE_NONE:
      case EOS_UPDATER_STATE_POLLING:
      case EOS_UPDATER_STATE_FETCHING:
      case EOS_UPDATER_STATE_APPLYING_UPDATE:
      case EOS_UPDATER_STATE_UPDATE_APPLIED:
      default:
        g_dbus_method_invocation_return_error (call,
          EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_STATE,
          "Can't call PollVolume() while in state %s",
          eos_updater_state_to_string (state));
        return TRUE;
    }

  eos_updater_clear_error (updater, EOS_UPDATER_STATE_POLLING);
  task = g_task_new (updater, NULL, metadata_fetch_finished, user_data);
  g_task_set_task_data (task,
                        volume_metadata_fetch_data_new (user_data, call),
                        volume_metadata_fetch_data_free);
  g_task_run_in_thread (task, volume_metadata_fetch);

  eos_updater_complete_poll_volume (updater, call);
  return TRUE;
}
