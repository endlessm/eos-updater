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

#include <eos-updater/data.h>
#include <eos-updater/object.h>
#include <eos-updater/poll-common.h>
#include <eos-updater/poll.h>
#include <eos-updater/resources.h>
#include <libeos-updater-util/config.h>
#include <libeos-updater-util/ostree.h>
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
        {
          g_message ("Poll: Adding override URI ‘%s’", config->override_uris[i]);
          ostree_repo_finder_override_add_uri (finder_override, config->override_uris[i]);
        }
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

typedef struct {
  gchar *refspec;
  gchar *remote;
  gchar *ref;
  OstreeCollectionRef *collection_ref;
  gchar *new_refspec;
  gchar *checksum;
  gchar *version;
  GVariant *commit;
} UpdateRefInfo;

static void
update_ref_info_init (UpdateRefInfo *update_ref_info)
{
  update_ref_info->refspec = NULL;
  update_ref_info->remote = NULL;
  update_ref_info->ref = NULL;
  update_ref_info->collection_ref = NULL;
  update_ref_info->new_refspec = NULL;
  update_ref_info->checksum = NULL;
  update_ref_info->version = NULL;
  update_ref_info->commit = NULL;
}

static void
update_ref_info_clear (UpdateRefInfo *update_ref_info)
{
  g_clear_pointer (&update_ref_info->refspec, g_free);
  g_clear_pointer (&update_ref_info->remote, g_free);
  g_clear_pointer (&update_ref_info->ref, g_free);
  g_clear_pointer (&update_ref_info->collection_ref, ostree_collection_ref_free);
  g_clear_pointer (&update_ref_info->new_refspec, g_free);
  g_clear_pointer (&update_ref_info->checksum, g_free);
  g_clear_pointer (&update_ref_info->version, g_free);
  g_clear_pointer (&update_ref_info->commit, g_variant_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (UpdateRefInfo, update_ref_info_clear)

static gboolean
check_for_update_using_booted_branch (OstreeRepo           *repo,
                                      gboolean             *out_is_update,
                                      UpdateRefInfo        *out_update_ref_info,
                                      GCancellable         *cancellable,
                                      GError              **error)
{
  g_autofree gchar *booted_refspec = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autoptr(OstreeCollectionRef) collection_ref = NULL;
  g_autofree gchar *new_refspec = NULL;
  g_autofree gchar *new_ref = NULL;
  g_autofree gchar *version = NULL;
  gboolean is_update = FALSE;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(OstreeSysroot) sysroot = ostree_sysroot_new_default ();
  g_autoptr(OstreeDeployment) booted_deployment = NULL;

  g_return_val_if_fail (out_is_update != NULL, FALSE);
  g_return_val_if_fail (out_update_ref_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!ostree_sysroot_load (sysroot, NULL, error))
    return FALSE;

  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);

  if (booted_deployment == NULL)
    return FALSE;

  if (!get_booted_refspec (booted_deployment,
                           &booted_refspec,
                           &remote,
                           &ref,
                           &collection_ref, error))
    return FALSE;

  if (!fetch_latest_commit (repo,
                            cancellable,
                            booted_refspec,
                            NULL,
                            &checksum,
                            &new_refspec,
                            &version,
                            error))
    return FALSE;

  if (!ostree_parse_refspec (new_refspec, NULL, &new_ref, error))
    return FALSE;

  if (!is_checksum_an_update (repo,
                              checksum,
                              ref,
                              new_ref,
                              &commit,
                              error))
    return FALSE;

  is_update = (commit != NULL);
  *out_is_update = is_update;

  if (is_update)
    {
      out_update_ref_info->refspec = g_steal_pointer (&booted_refspec);
      out_update_ref_info->remote = g_steal_pointer (&remote);
      out_update_ref_info->ref = g_steal_pointer (&ref);
      out_update_ref_info->collection_ref = g_steal_pointer (&collection_ref);
      out_update_ref_info->new_refspec = g_steal_pointer (&new_refspec);
      out_update_ref_info->checksum = g_steal_pointer (&checksum);
      out_update_ref_info->version = g_steal_pointer (&version);
      out_update_ref_info->commit = g_steal_pointer (&commit);
    }
  else
    {
      update_ref_info_clear (out_update_ref_info);
    }

  return TRUE;
}

static gboolean
get_booted_refspec_from_default_booted_sysroot_deployment (gchar               **out_refspec,
                                                           gchar               **out_remote,
                                                           gchar               **out_ref,
                                                           OstreeCollectionRef **out_collection_ref,
                                                           GError              **error)
{
  g_autoptr(OstreeSysroot) sysroot = ostree_sysroot_new_default ();
  g_autoptr(OstreeDeployment) booted_deployment = NULL;

  if (!ostree_sysroot_load (sysroot, NULL, error))
    return FALSE;

  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);

  if (booted_deployment == NULL)
    return FALSE;

  return get_booted_refspec (booted_deployment,
                             out_refspec,
                             out_remote,
                             out_ref,
                             out_collection_ref,
                             error);
}

static gboolean
check_for_update_following_checkpoint_commits (OstreeRepo     *repo,
                                               UpdateRefInfo  *out_update_ref_info,
                                               GCancellable   *cancellable,
                                               GError        **error)
{
  g_autofree gchar *upgrade_refspec = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *booted_ref = NULL;
  g_autofree gchar *ref_after_following_rebases = NULL;
  g_autoptr(OstreeCollectionRef) collection_ref = NULL;
  g_autofree gchar *new_refspec = NULL;
  g_autofree gchar *version = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;

  g_return_val_if_fail (out_update_ref_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!get_booted_refspec_from_default_booted_sysroot_deployment (NULL,
                                                                  NULL,
                                                                  &booted_ref,
                                                                  NULL,
                                                                  error))
    return FALSE;

  if (!get_refspec_to_upgrade_on (&upgrade_refspec, &remote, &ref, &collection_ref, error))
    return FALSE;

  if (!fetch_latest_commit (repo,
                            cancellable,
                            upgrade_refspec,
                            NULL,
                            &checksum,
                            &new_refspec,
                            &version,
                            error))
    return FALSE;

  if (!ostree_parse_refspec (new_refspec, NULL, &ref_after_following_rebases, error))
    return FALSE;

  if (!is_checksum_an_update (repo,
                              checksum,
                              booted_ref,
                              ref_after_following_rebases,
                              &commit,
                              error))
    return FALSE;

  out_update_ref_info->refspec = g_steal_pointer (&upgrade_refspec);
  out_update_ref_info->remote = g_steal_pointer (&remote);
  out_update_ref_info->ref = g_steal_pointer (&ref);
  out_update_ref_info->collection_ref = g_steal_pointer (&collection_ref);
  out_update_ref_info->new_refspec = g_steal_pointer (&new_refspec);
  out_update_ref_info->checksum = g_steal_pointer (&checksum);
  out_update_ref_info->version = g_steal_pointer (&version);
  out_update_ref_info->commit = g_steal_pointer (&commit);

  return TRUE;
}

static gboolean
check_for_update_following_checkpoint_if_allowed (OstreeRepo     *repo,
                                                  UpdateRefInfo  *out_update_ref_info,
                                                  GCancellable   *cancellable,
                                                  GError        **error)
{
  gboolean had_update_on_branch = FALSE;

  g_return_val_if_fail (out_update_ref_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* First, check for an update on the booted refspec. If one exists,
   * use that, since it may mean that we did some emergency fixes
   * on the booted refspec after the checkpoint and we don't want
   * to transition users on to the new branch just yet */
  if (!check_for_update_using_booted_branch (repo,
                                             &had_update_on_branch,
                                             out_update_ref_info,
                                             cancellable,
                                             error))
    return FALSE;

  /* Did we have an update? If not, we can follow the checkpoint */
  if (!had_update_on_branch)
    {
      /* Make sure to clear update_ref_info if we're going to
       * reassign its values here */
      update_ref_info_clear (out_update_ref_info);

      if (!check_for_update_following_checkpoint_commits (repo,
                                                          out_update_ref_info,
                                                          cancellable,
                                                          error))
        return FALSE;
    }

  return TRUE;
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
  g_autofree gchar *version = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autofree gchar *new_refspec = NULL;
  g_autoptr(OstreeCollectionRef) new_collection_ref = NULL;
  g_autoptr(OstreeCollectionRef) upgrade_collection_ref = NULL;
  g_autofree gchar *upgrade_refspec = NULL;
  const OstreeCollectionRef *booted_collection_ref;
  g_auto(UpdateRefInfo) update_ref_info;
  g_autoptr(GPtrArray) finders = NULL;  /* (element-type OstreeRepoFinder) */
  g_autoptr(RepoFinderAvahiRunning) finder_avahi = NULL;
  gboolean redirect_followed = FALSE;

  update_ref_info_init (&update_ref_info);

  /* The upgrade refspec here is either the booted refspec if
   * there were new commits on the branch of the booted refspec, or
   * the checkpoint refspec. */
  if (!check_for_update_following_checkpoint_if_allowed (repo,
                                                         &update_ref_info,
                                                         cancellable,
                                                         error))
    return FALSE;

  if (update_ref_info.collection_ref == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "No collection ID set for currently booted deployment.");
      return NULL;
    }

  booted_collection_ref = update_ref_info.collection_ref;
  upgrade_collection_ref = ostree_collection_ref_dup (booted_collection_ref);
  upgrade_refspec = g_strdup (update_ref_info.refspec);

  finders = get_finders (config, context, &finder_avahi);
  if (finders->len == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "All configured update sources failed to initialize.");
      return NULL;
    }

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

      g_clear_pointer (&results, ostree_repo_finder_result_freev);

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
      g_clear_pointer (&version, g_free);
      g_clear_pointer (&new_refspec, g_free);
      g_clear_pointer (&new_collection_ref, ostree_collection_ref_free);

      /* Parse the commit and check there’s no redirection to a new ref. */
      if (!parse_latest_commit (repo, upgrade_refspec, &redirect_followed,
                                &checksum, &new_refspec, &new_collection_ref, &version,
                                cancellable, error))
        return NULL;

      if (new_refspec != NULL)
        {
          g_clear_pointer (&upgrade_refspec, g_free);
          upgrade_refspec = g_strdup (new_refspec);
        }
      if (new_collection_ref != NULL)
        {
          g_clear_pointer (&upgrade_collection_ref, ostree_collection_ref_free);
          upgrade_collection_ref = g_steal_pointer (&new_collection_ref);
        }
    }
  while (redirect_followed);

  /* Final checks on the commit we found. */
  if (!is_checksum_an_update (repo,
                              checksum,
                              booted_collection_ref->ref_name,
                              upgrade_collection_ref->ref_name,
                              &commit,
                              error))
    return NULL;

  if (commit == NULL)
    return NULL;

  info = eos_update_info_new (checksum, commit,
                              upgrade_refspec,
                              update_ref_info.refspec,
                              version,
                              NULL, g_steal_pointer (&results));
  metrics_report_successful_poll (info);

  return g_steal_pointer (&info);
}

/* Fetch metadata such as commit checksums from OSTree repositories that may be
 * found on the Internet, the local network, or a removable drive. */
static gboolean
metadata_fetch_from_main (OstreeRepo     *repo,
                          GMainContext   *context,
                          EosUpdateInfo **out_info,
                          GCancellable   *cancellable,
                          GError        **error)
{
  g_auto(UpdateRefInfo) update_ref_info;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *new_ref = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;

  update_ref_info_init (&update_ref_info);

  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!check_for_update_following_checkpoint_if_allowed (repo,
                                                         &update_ref_info,
                                                         cancellable,
                                                         error))
    return FALSE;

  if (!get_booted_refspec_from_default_booted_sysroot_deployment (NULL,
                                                                  NULL,
                                                                  &ref,
                                                                  NULL,
                                                                  error))
    return FALSE;

  if (!ostree_parse_refspec (update_ref_info.new_refspec, NULL, &new_ref, error))
    return FALSE;

  /* The checksum that is being checked here is either the
   * most recent commit on the branch (following
   * eol-rebase redirects) or the checkpoint commit if we have
   * that. */
  if (!is_checksum_an_update (repo,
                              update_ref_info.checksum,
                              ref,
                              new_ref,
                              &commit,
                              error))
    return FALSE;

  if (commit != NULL)
    info = eos_update_info_new (update_ref_info.checksum,
                                commit,
                                update_ref_info.new_refspec,
                                update_ref_info.refspec,
                                update_ref_info.version,
                                NULL,
                                NULL);

  *out_info = g_steal_pointer (&info);

  return TRUE;
}

static gboolean
metadata_fetch_internal (OstreeRepo     *repo,
                         EosUpdateInfo **out_info,
                         GCancellable   *cancellable,
                         GError        **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_ref_thread_default ();
  g_auto(SourcesConfig) config = SOURCES_CONFIG_CLEARED;
  g_autoptr(OstreeDeployment) deployment = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  static gboolean use_new_code = TRUE;
  /* TODO: link this --^ to failure of the fetch or apply stages?
   * Add environment variables or something else to force it one way or the other?
   * Make it clear in the logging which code path is being used. */
  gboolean disable_old_code = (g_getenv ("EOS_UPDATER_DISABLE_FALLBACK_FETCHERS") != NULL);

  /* Check we’re not on a dev-converted system. */
  deployment = eos_updater_get_booted_deployment (&local_error);
  if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
      g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_FAILED))
    {
      g_set_error (error, EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM,
                   "Not an OSTree-based system: cannot update it.");
      return FALSE;
    }

  g_clear_error (&local_error);

  /* Work out which sources to poll. */
  if (!read_config (get_config_file_path (), &config, error))
    return FALSE;

  /* Do we want to use the new libostree code for P2P, or fall back on the old
   * eos-updater code?
   * FIXME: Eventually drop the old code. See:
   * https://phabricator.endlessm.com/T19606 */
  if (use_new_code)
    {
      info = metadata_fetch_new (repo, &config, task_context, cancellable, &local_error);

      if (local_error != NULL)
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }

          use_new_code = FALSE;

          if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
            g_warning ("Error polling for updates using libostree P2P code; falling back to old code: %s",
                       local_error->message);
          else
            g_message ("Failed to poll for updates using libostree P2P code as it is not supported; falling back to old code: %s",
                       local_error->message);
          g_clear_error (&local_error);
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
          g_autoptr(GArray) order = g_array_sized_new (FALSE, /* not null terminated */
                                                       FALSE, /* no clearing */
                                                       sizeof (EosUpdaterDownloadSource),
                                                       1);
          EosUpdaterDownloadSource main_source = EOS_UPDATER_DOWNLOAD_MAIN;

          g_ptr_array_add (fetchers, metadata_fetch_from_main);
          g_array_append_val (order, main_source);

          info = run_fetchers (repo,
                               task_context,
                               cancellable,
                               fetchers,
                               order,
                               &local_error);
        }
      else
        {
          g_debug ("%s: Not polling for updates on old code path as main source is not enabled",
                   G_STRFUNC);
          info = NULL;
        }
    }

  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (out_info != NULL)
    *out_info = g_steal_pointer (&info);

  return TRUE;
}

static void
metadata_fetch (GTask *task,
                gpointer object,
                gpointer task_data,
                GCancellable *cancellable)
{
  g_autoptr(GError) local_error = NULL;
  OstreeRepo *repo = task_data;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  if (!metadata_fetch_internal (repo,
                                &info,
                                cancellable,
                                &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_pointer (task, g_steal_pointer (&info), g_object_unref);

  g_main_context_pop_thread_default (task_context);
}

gboolean
handle_poll (EosUpdater            *updater,
             GDBusMethodInvocation *call,
             gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  EosUpdaterData *data = user_data;
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
        return TRUE;
    }

  /* FIXME: Passing the #OstreeRepo to the worker thread here is not thread safe.
   * See: https://phabricator.endlessm.com/T15923 */
  eos_updater_data_reset_cancellable (data);
  eos_updater_clear_error (updater, EOS_UPDATER_STATE_POLLING);
  task = g_task_new (updater, data->cancellable, metadata_fetch_finished, data);
  g_task_set_task_data (task, g_object_ref (data->repo), g_object_unref);
  g_task_run_in_thread (task, metadata_fetch);

  eos_updater_complete_poll (updater, call);

  return TRUE;
}

typedef struct
{
  OstreeRepo *repo;  /* (owned) */
  gchar *volume_path;  /* (owned) */
} PollVolumeData;

static void
poll_volume_data_free (PollVolumeData *data)
{
  g_free (data->volume_path);
  g_clear_object (&data->repo);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PollVolumeData, poll_volume_data_free)

static PollVolumeData *
poll_volume_data_new (OstreeRepo  *repo,
                      const gchar *path)
{
  g_autoptr(PollVolumeData) data = NULL;

  data = g_new (PollVolumeData, 1);
  data->repo = g_object_ref (repo);
  data->volume_path = g_strdup (path);

  return g_steal_pointer (&data);
}

static gboolean
poll_volume_internal (PollVolumeData  *poll_volume_data,
                      EosUpdateInfo  **out_info,
                      GCancellable    *cancellable,
                      GError         **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_ref_thread_default ();
  g_auto(SourcesConfig) config = SOURCES_CONFIG_CLEARED;
  g_autoptr(OstreeDeployment) deployment = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  EosUpdaterDownloadSource idx;
  g_autofree gchar *repo_path = NULL;

  /* Check we’re not on a dev-converted system. */
  deployment = eos_updater_get_booted_deployment (&local_error);
  if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
      g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_FAILED))
    {
      g_set_error (error, EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM,
                   "Not an OSTree-based system: cannot update it.");
      return FALSE;
    }

  g_clear_error (&local_error);

  config.download_order = g_array_new (FALSE, /* not null terminated */
                                       FALSE, /* no clearing */
                                       sizeof (EosUpdaterDownloadSource));
  idx = EOS_UPDATER_DOWNLOAD_MAIN;
  g_array_append_val (config.download_order, idx);

  repo_path = g_build_filename (poll_volume_data->volume_path, ".ostree", "repo", NULL);
  config.override_uris = g_new0 (gchar *, 2);
  config.override_uris[0] = g_strconcat ("file://", repo_path, NULL);

  info = metadata_fetch_new (poll_volume_data->repo, &config, task_context, cancellable, &local_error);

  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (out_info != NULL)
    *out_info = g_steal_pointer (&info);

  return TRUE;
}

static void
poll_volume (GTask        *task,
             gpointer      object,
             gpointer      task_data,
             GCancellable *cancellable)
{
  g_autoptr(GError) local_error = NULL;
  PollVolumeData *poll_volume_data = task_data;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_autoptr(GMainContext) task_context = g_main_context_new ();

  g_main_context_push_thread_default (task_context);

  if (!poll_volume_internal (poll_volume_data,
                             &info,
                             cancellable,
                             &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_pointer (task, g_steal_pointer (&info), g_object_unref);

  g_main_context_pop_thread_default (task_context);
}

gboolean
handle_poll_volume (EosUpdater            *updater,
                    GDBusMethodInvocation *call,
                    const gchar           *path,
                    gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(PollVolumeData) poll_volume_data = NULL;
  EosUpdaterData *data = user_data;
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

  /* FIXME: The #OstreeRepo instance here is not thread safe. */
  poll_volume_data = poll_volume_data_new (data->repo, path);

  eos_updater_data_reset_cancellable (data);
  eos_updater_clear_error (updater, EOS_UPDATER_STATE_POLLING);
  task = g_task_new (updater, data->cancellable, metadata_fetch_finished, data);
  g_task_set_task_data (task, g_steal_pointer (&poll_volume_data),
                        (GDestroyNotify) poll_volume_data_free);
  g_task_run_in_thread (task, poll_volume);

  eos_updater_complete_poll_volume (updater, call);

  return TRUE;
}
