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
#include <libeos-updater-util/config-util.h>
#include <libeos-updater-util/ostree-util.h>
#include <libeos-updater-util/util.h>

static const gchar *const CONFIG_FILE_PATH = SYSCONFDIR "/eos-updater/eos-updater.conf";
static const gchar *const LOCAL_CONFIG_FILE_PATH = PREFIX "/local/share/eos-updater/eos-updater.conf";
static const gchar *const STATIC_CONFIG_FILE_PATH = DATADIR "/eos-updater/eos-updater.conf";
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

#define SOURCES_CONFIG_CLEARED { NULL, NULL }

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

static void
get_finders (SourcesConfig          *config,
             GMainContext           *context,
             GPtrArray             **out_offline_finders,
             GPtrArray             **out_online_finders,
             OstreeRepoFinderAvahi **out_finder_avahi)
{
  g_autoptr(OstreeRepoFinderAvahi) finder_avahi = NULL;
  g_autoptr(GPtrArray) offline_finders = g_ptr_array_new_full (0, object_unref0);
  g_autoptr(GPtrArray) online_finders = g_ptr_array_new_full (0, object_unref0);
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
          g_ptr_array_add (online_finders, ostree_repo_finder_config_new ());
          break;

        case EOS_UPDATER_DOWNLOAD_LAN:
          /* strv_to_download_order() already checks for duplicated download_order entries */
          g_assert (finder_avahi == NULL);
          finder_avahi = ostree_repo_finder_avahi_new (context);
          g_ptr_array_add (offline_finders, g_object_ref (finder_avahi));
          break;

        case EOS_UPDATER_DOWNLOAD_VOLUME:
          /* TODO: How to make this one testable? */
          g_ptr_array_add (offline_finders, ostree_repo_finder_mount_new (NULL));
          break;

        default:
          g_assert_not_reached ();
        }
    }

  if (config->override_uris != NULL)
    {
      g_autoptr(OstreeRepoFinderOverride) finder_override = ostree_repo_finder_override_new ();

      g_ptr_array_set_size (offline_finders, 0);  /* override everything */
      g_ptr_array_set_size (online_finders, 0);  /* override everything */

      /* We don't know if the URIs are online or offline; assume online so we
       * don't accidentally bypass the scheduler */
      g_ptr_array_add (online_finders, g_object_ref (finder_override));

      g_clear_object (&finder_avahi);

      for (i = 0; config->override_uris[i] != NULL; i++)
        {
          g_message ("Poll: Adding override URI ‘%s’", config->override_uris[i]);
          ostree_repo_finder_override_add_uri (finder_override, config->override_uris[i]);
        }
    }

  if (offline_finders->len > 0)
    g_ptr_array_add (offline_finders, NULL);  /* NULL terminator */
  if (online_finders->len > 0)
    g_ptr_array_add (online_finders, NULL);  /* NULL terminator */

  /* TODO: Stop this at some point; think of a better way to store it and
   * control its lifecycle. */
  if (finder_avahi != NULL)
    ostree_repo_finder_avahi_start (OSTREE_REPO_FINDER_AVAHI (finder_avahi),
                                    &local_error);

  if (local_error != NULL)
    {
      g_warning ("Avahi finder failed; removing it: %s", local_error->message);
      g_ptr_array_remove (offline_finders, finder_avahi);
      g_clear_object (&finder_avahi);
      g_clear_error (&local_error);
    }

  if (out_finder_avahi != NULL)
    *out_finder_avahi = g_steal_pointer (&finder_avahi);

  if (out_offline_finders != NULL)
    *out_offline_finders = g_steal_pointer (&offline_finders);
  if (out_online_finders != NULL)
    *out_online_finders = g_steal_pointer (&online_finders);
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

typedef struct {
  gchar *refspec;
  gchar *remote;
  gchar *ref;
  OstreeCollectionRef *collection_ref;
  OstreeRepoFinderResult **results;
  gchar *new_refspec;
  gchar *checksum;
  gchar *version;
  gboolean is_user_visible;
  gchar *release_notes_uri;
  GVariant *commit;
} UpdateRefInfo;

static void
update_ref_info_init (UpdateRefInfo *update_ref_info)
{
  update_ref_info->refspec = NULL;
  update_ref_info->remote = NULL;
  update_ref_info->ref = NULL;
  update_ref_info->collection_ref = NULL;
  update_ref_info->results = NULL;
  update_ref_info->new_refspec = NULL;
  update_ref_info->checksum = NULL;
  update_ref_info->version = NULL;
  update_ref_info->is_user_visible = FALSE;
  update_ref_info->release_notes_uri = NULL;
  update_ref_info->commit = NULL;
}

static void
update_ref_info_clear (UpdateRefInfo *update_ref_info)
{
  g_clear_pointer (&update_ref_info->refspec, g_free);
  g_clear_pointer (&update_ref_info->remote, g_free);
  g_clear_pointer (&update_ref_info->ref, g_free);
  g_clear_pointer (&update_ref_info->collection_ref, ostree_collection_ref_free);
  g_clear_pointer (&update_ref_info->results, ostree_repo_finder_result_freev);
  g_clear_pointer (&update_ref_info->new_refspec, g_free);
  g_clear_pointer (&update_ref_info->checksum, g_free);
  g_clear_pointer (&update_ref_info->version, g_free);
  g_clear_pointer (&update_ref_info->release_notes_uri, g_free);
  g_clear_pointer (&update_ref_info->commit, g_variant_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (UpdateRefInfo, update_ref_info_clear)

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

#if !GLIB_CHECK_VERSION(2, 68, 0)
/* From gstring.c:
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
static guint
g_string_replace (GString     *string,
                  const gchar *find,
                  const gchar *replace,
                  guint        limit)
{
  gsize f_len, r_len, pos;
  gchar *cur, *next;
  guint n = 0;

  g_return_val_if_fail (string != NULL, 0);
  g_return_val_if_fail (find != NULL, 0);
  g_return_val_if_fail (replace != NULL, 0);

  f_len = strlen (find);
  r_len = strlen (replace);
  cur = string->str;

  while ((next = strstr (cur, find)) != NULL)
    {
      pos = next - string->str;
      g_string_erase (string, pos, f_len);
      g_string_insert (string, pos, replace);
      cur = string->str + pos + r_len;
      n++;
      /* Only match the empty string once at any given position, to
       * avoid infinite loops */
      if (f_len == 0)
        {
          if (cur[0] == '\0')
            break;
          else
            cur++;
        }
      if (n == limit)
        break;
    }

  return n;
}
#endif

/* Replace any placeholders in the given @template release notes URI with the
 * appropriate values, which depend on the update path being taken, and return
 * the resulting release notes URI. */
static gchar *
format_release_notes_uri (const gchar *template,
                          const gchar *booted_version,
                          const gchar *update_version)
{
  g_autoptr(GString) str = g_string_new (template);

  g_string_replace (str, "${booted_version}", (booted_version != NULL) ? booted_version : "-", 0);
  g_string_replace (str, "${update_version}", (update_version != NULL) ? update_version : "-", 0);

  return g_string_free (g_steal_pointer (&str), FALSE);
}

static gboolean
check_for_update_using_booted_branch (OstreeRepo           *repo,
                                      gboolean             *out_is_update,
                                      UpdateRefInfo        *out_update_ref_info,
                                      GPtrArray            *finders, /* (element-type OstreeRepoFinder) */
                                      GMainContext         *context,
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
  g_autofree gchar *release_notes_uri_template = NULL;
  gboolean is_update = FALSE;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  gboolean is_update_user_visible = FALSE;
  g_autoptr(OstreeSysroot) sysroot = ostree_sysroot_new_default ();
  g_autoptr(OstreeDeployment) booted_deployment = NULL;
  g_auto(OstreeRepoFinderResultv) results = NULL;
  g_autofree gchar *booted_version = NULL;
  g_autofree gchar *update_version = NULL;

  g_return_val_if_fail (out_is_update != NULL, FALSE);
  g_return_val_if_fail (out_update_ref_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!get_booted_refspec_from_default_booted_sysroot_deployment (&booted_refspec,
                                                                  &remote,
                                                                  &ref,
                                                                  &collection_ref,
                                                                  error))
    return FALSE;

  if (!fetch_latest_commit (repo,
                            cancellable,
                            context,
                            booted_refspec,
                            NULL,
                            finders,
                            collection_ref,
                            &results,
                            &checksum,
                            &new_refspec,
                            &version,
                            &release_notes_uri_template,
                            error))
    return FALSE;

  if (!ostree_parse_refspec (new_refspec, NULL, &new_ref, error))
    return FALSE;

  if (!is_checksum_an_update (repo,
                              checksum,
                              ref,
                              new_ref,
                              &commit,
                              &is_update_user_visible,
                              &booted_version,
                              &update_version,
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
      out_update_ref_info->results = g_steal_pointer (&results);
      out_update_ref_info->new_refspec = g_steal_pointer (&new_refspec);
      out_update_ref_info->checksum = g_steal_pointer (&checksum);
      out_update_ref_info->version = g_steal_pointer (&version);
      out_update_ref_info->release_notes_uri = format_release_notes_uri (release_notes_uri_template, booted_version, update_version);
      out_update_ref_info->is_user_visible = is_update_user_visible;
      out_update_ref_info->commit = g_steal_pointer (&commit);
    }
  else
    {
      update_ref_info_clear (out_update_ref_info);
    }

  return TRUE;
}

static gboolean
check_for_update_following_checkpoint_commits (OstreeRepo     *repo,
                                               UpdateRefInfo  *out_update_ref_info,
                                               GPtrArray      *finders, /* (element-type OstreeRepoFinder) */
                                               GMainContext   *context,
                                               GCancellable   *cancellable,
                                               GError        **error)
{
  g_autofree gchar *upgrade_refspec = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *booted_refspec = NULL;
  g_autofree gchar *booted_ref = NULL;
  g_autofree gchar *ref_after_following_rebases = NULL;
  g_autoptr(OstreeCollectionRef) collection_ref = NULL;
  g_autofree gchar *new_refspec = NULL;
  g_autofree gchar *version = NULL;
  g_autofree gchar *release_notes_uri_template = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  gboolean is_update_user_visible = FALSE;
  g_auto(OstreeRepoFinderResultv) results = NULL;
  g_autofree gchar *booted_version = NULL;
  g_autofree gchar *update_version = NULL;

  g_return_val_if_fail (out_update_ref_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Get the booted refspec. We'll use this to work out whether
   * we are pulling from a different refspec than the one we booted
   * on, which has implications for cleanup later. */
  if (!get_booted_refspec_from_default_booted_sysroot_deployment (&booted_refspec,
                                                                  NULL,
                                                                  &booted_ref,
                                                                  NULL,
                                                                  error))
    return FALSE;

  /* Get the refspec to upgrade on. This typically the "checkpoint commit"
   * refspec contained in the metadata of the currently booted refspec. It
   * tells us which refspec we should be looking at for future upgrades
   * if we are booted in a given commit. This is used to ensure that the updater
   * or its dependencies supports a particular feature that we'll need in order
   * to be able to upgrade properly to newer versions. */
  if (!get_refspec_to_upgrade_on (&upgrade_refspec, &remote, &ref, &collection_ref, error))
    return FALSE;

  /* Fetch the latest commit on the upgrade refspec, potentially following
   * eol-rebase refspec metadata on commits. We always unconditionally follow
   * the eol-rebase metadata until we reach the end of a series - this is
   * different to checkpoint commits where we can only follow the new
   * refspec once booted into that commit. */
  if (!fetch_latest_commit (repo,
                            cancellable,
                            context,
                            upgrade_refspec,
                            NULL,
                            finders,
                            collection_ref,
                            &results,
                            &checksum,
                            &new_refspec,
                            &version,
                            &release_notes_uri_template,
                            error))
    return FALSE;

  if (!ostree_parse_refspec (new_refspec, NULL, &ref_after_following_rebases, error))
    return FALSE;

  /* Work out whether the most recently available checksum
   * on ref_after_following_rebases represents an update to
   * whatever we currently have booted. If it isn't, abort. */
  if (!is_checksum_an_update (repo,
                              checksum,
                              booted_ref,
                              ref_after_following_rebases,
                              &commit,
                              &is_update_user_visible,
                              &booted_version,
                              &update_version,
                              error))
    return FALSE;

  if (commit != NULL)
    {
      /* The "refspec" member is the *currently booted* refspec
       * which may get cleaned up later if we change away from it. */
      out_update_ref_info->refspec = g_steal_pointer (&booted_refspec);

      /* The "remote", "ref" and "collection_ref" refer here to the
       * ref and remote that we should be following given checkpoints. */
      out_update_ref_info->remote = g_steal_pointer (&remote);
      out_update_ref_info->ref = g_steal_pointer (&ref);

      /* "collection_ref", "new_refspec" and "checksum" refer to the collection
       * ref and refspec of the checksum that we will be pulling and updating to */
      out_update_ref_info->collection_ref = g_steal_pointer (&collection_ref);
      out_update_ref_info->new_refspec = g_steal_pointer (&new_refspec);
      out_update_ref_info->checksum = g_steal_pointer (&checksum);

      out_update_ref_info->results = g_steal_pointer (&results);
      out_update_ref_info->version = g_steal_pointer (&version);
      out_update_ref_info->is_user_visible = is_update_user_visible;
      out_update_ref_info->release_notes_uri = format_release_notes_uri (release_notes_uri_template, booted_version, update_version);
      out_update_ref_info->commit = g_steal_pointer (&commit);
    }
  else
    {
      update_ref_info_clear (out_update_ref_info);
    }

  return TRUE;
}

static gboolean
check_for_update_following_checkpoint_if_allowed (OstreeRepo     *repo,
                                                  UpdateRefInfo  *out_update_ref_info,
                                                  GPtrArray      *finders, /* (element-type OstreeRepoFinder) */
                                                  GMainContext   *context,
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
                                             finders,
                                             context,
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
                                                          finders,
                                                          context,
                                                          cancellable,
                                                          error))
        return FALSE;
    }

  return TRUE;
}

/* Fetch metadata such as commit checksums from OSTree repositories that may be
 * found on the Internet, the local network, or a removable drive. May return
 * NULL without setting an error if no updates were found. */
static EosUpdateInfo *
metadata_fetch_new (OstreeRepo    *repo,
                    SourcesConfig *config,
                    GMainContext  *context,
                    GCancellable  *cancellable,
                    GError       **error)
{
  g_auto(OstreeRepoFinderResultv) results = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_auto(UpdateRefInfo) update_ref_info;
  g_autoptr(GPtrArray) offline_finders = NULL;  /* (element-type OstreeRepoFinder) */
  g_autoptr(GPtrArray) online_finders = NULL;  /* (element-type OstreeRepoFinder) */
  g_autoptr(RepoFinderAvahiRunning) finder_avahi = NULL;
  gboolean offline_results_only = TRUE;

  get_finders (config, context, &offline_finders, &online_finders, &finder_avahi);
  if (offline_finders->len == 0 && online_finders->len == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "All configured update sources failed to initialize.");
      return NULL;
    }

  update_ref_info_init (&update_ref_info);

  /* The upgrade refspec here is either the booted refspec if
   * there were new commits on the branch of the booted refspec, or
   * the checkpoint refspec. */
  if (offline_finders->len > 0 &&
      !check_for_update_following_checkpoint_if_allowed (repo,
                                                         &update_ref_info,
                                                         offline_finders,
                                                         context,
                                                         cancellable,
                                                         error))
    return NULL;

  /* If checking for updates offline failed, check online */
  if (update_ref_info.commit == NULL ||
      update_ref_info.results == NULL ||
      update_ref_info.results[0] == NULL)
    {
      offline_results_only = FALSE;

      update_ref_info_clear (&update_ref_info);

      if (online_finders->len > 0 &&
          !check_for_update_following_checkpoint_if_allowed (repo,
                                                             &update_ref_info,
                                                             online_finders,
                                                             context,
                                                             cancellable,
                                                             error))
        return NULL;
    }

  if (update_ref_info.commit != NULL &&
      update_ref_info.results != NULL &&
      update_ref_info.results[0] != NULL)
    {
      info = eos_update_info_new (update_ref_info.checksum,
                                  update_ref_info.commit,
                                  update_ref_info.new_refspec,
                                  update_ref_info.refspec,
                                  update_ref_info.version,
                                  update_ref_info.is_user_visible,
                                  update_ref_info.release_notes_uri,
                                  NULL,
                                  offline_results_only,
                                  g_steal_pointer (&update_ref_info.results));
      metrics_report_successful_poll (info);
      return g_steal_pointer (&info);
    }
  else
    {
      g_message ("Poll: Couldn’t find any updates");
      return NULL;
    }
}

/* Fetch metadata such as commit checksums from OSTree repositories, only
 * checking the Internet not peer sources. May return NULL without setting an
 * error if no updates were found. */
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
                                                         NULL,
                                                         context,
                                                         cancellable,
                                                         error))
    return FALSE;

  if (update_ref_info.commit != NULL)
    info = eos_update_info_new (update_ref_info.checksum,
                                update_ref_info.commit,
                                update_ref_info.new_refspec,
                                update_ref_info.refspec,
                                update_ref_info.version,
                                update_ref_info.is_user_visible,
                                update_ref_info.release_notes_uri,
                                NULL,
                                FALSE,
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
  gboolean use_new_code = TRUE;
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
