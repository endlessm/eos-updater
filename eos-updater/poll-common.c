/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

#include "config.h"

#include <eos-updater/object.h>
#include <eos-updater/poll-common.h>
#include <gio/gunixmounts.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <libeos-updater-util/metrics-private.h>
#include <libeos-updater-util/ostree-util.h>
#include <libeos-updater-util/util.h>
#include <libsoup/soup.h>
#include <ostree.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef HAS_EOSMETRICS_0
#include <eosmetrics/eosmetrics.h>
#endif /* HAS_EOSMETRICS_0 */

static const gchar *const VENDOR_KEY = "sys_vendor";
static const gchar *const PRODUCT_KEY = "product_name";
static const gchar *const DT_COMPATIBLE = "/proc/device-tree/compatible";
static const gchar *const DMI_PATH = "/sys/class/dmi/id/";
static const gchar *const dmi_attributes[] =
  {
    "bios_date",
    "bios_vendor",
    "bios_version",
    "board_name",
    "board_vendor",
    "board_version",
    "chassis_vendor",
    "chassis_version",
    "product_name",
    "product_version",
    "sys_vendor",
    NULL,
  };

static const gchar *const order_key_str[] = {
  "main",
  "lan",
  "volume"
};

G_STATIC_ASSERT (G_N_ELEMENTS (order_key_str) == EOS_UPDATER_DOWNLOAD_LAST + 1);

static void
async_result_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;

  *result_out = g_object_ref (result);
}

/* Returns a strcmp()-like integer, negative if @version_a < @version_b, zero if
 * they’re equal (or the comparison is invalid), positive if
 * @version_a > @version_b. */
static int
compare_major_versions (const char *version_a,
                        const char *version_b)
{
  guint64 version_a_major, version_b_major;

  if (version_a == NULL || version_b == NULL)
    return 0;

  /* Take the first whole integer off each string, and assume it’s the major
   * version number. This should work regardless of whether the strings are in
   * `X.Y.Z` form or `X.Y` or `X`. Note that this parsing is locale
   * independent. */
  version_a_major = g_ascii_strtoull (version_a, NULL, 10);
  version_b_major = g_ascii_strtoull (version_b, NULL, 10);

  if (version_a_major > version_b_major)
    return 1;
  else if (version_a_major < version_b_major)
    return -1;
  else
    return 0;
}

/**
 * is_checksum_an_update:
 * @repo: the #OstreeRepo
 * @update_checksum: checksum of the commit to potentially update to
 * @booted_ref: ref which is currently booted
 * @update_ref: ref of the branch to potentially update to
 * @out_commit: (not optional) (nullable) (out) (transfer full): return location
 *   for the #GVariant containing the commit identified by @update_checksum *if*
 *   it is an update compared to @booted_ref; %NULL is returned otherwise
 * @out_is_update_user_visible: (not optional) (out): return location for a
 *   boolean indicating whether the update to @update_checksum contains user
 *   visible changes which should be highlighted to the user; always returns
 *   %FALSE when @out_commit returns %NULL
 * @out_booted_version: (optional) (nullable) (out): return location for the
 *   version number of the currently booted commit, or %NULL to not return it;
 *   the returned value may be %NULL if the version is not known
 * @out_update_version: (optional) (nullable) (out): return location for the
 *   version number of the commit to update to, or %NULL to not return it; the
 *   returned value may be %NULL if the version is not known
 * @error: return location for a #GError, or %NULL
 *
 * Checks whether an update from @booted_ref to @update_ref would actually be
 * an update, or would end up switching to an older release. See the block
 * comment below for the details.
 *
 * The return value indicates whether there was an error in the check. It does
 * not indicate whether the checksum is an update. If the checksum is an update,
 * @out_commit returns a non-%NULL value.
 *
 * Returns: %TRUE if @out_commit is defined, %FALSE on error
 */
gboolean
is_checksum_an_update (OstreeRepo *repo,
                       const gchar *update_checksum,
                       const gchar *booted_ref,
                       const gchar *update_ref,
                       GVariant **out_commit,
                       gboolean *out_is_update_user_visible,
                       gchar **out_booted_version,
                       gchar **out_update_version,
                       GError **error)
{
  g_autoptr(GVariant) current_commit = NULL;
  g_autoptr(GVariant) update_commit = NULL;
  g_autoptr(GVariant) current_commit_metadata = NULL;
  g_autoptr(GVariant) update_commit_metadata = NULL;
  g_autofree gchar *booted_checksum = NULL;
  gboolean is_newer;
  gboolean is_update_user_visible;
  guint64 update_timestamp, current_timestamp;
  const char *current_version = NULL, *update_version = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (update_checksum != NULL, FALSE);
  g_return_val_if_fail (out_commit != NULL, FALSE);
  g_return_val_if_fail (out_is_update_user_visible != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Default output. */
  *out_commit = NULL;
  *out_is_update_user_visible = FALSE;
  if (out_booted_version != NULL)
    *out_booted_version = NULL;
  if (out_update_version != NULL)
    *out_update_version = NULL;

  booted_checksum = eos_updater_get_booted_checksum (error);
  if (booted_checksum == NULL)
    return FALSE;

  /* We need to check if the offered checksum on the server
   * was the same as the booted checksum. It is possible for the timestamp
   * on the server to be newer if the commit was re-generated from an
   * existing tree. */
  if (g_str_equal (booted_checksum, update_checksum))
    return TRUE;

  g_debug ("%s: current: %s, update: %s", G_STRFUNC, booted_checksum, update_checksum);

  if (!ostree_repo_load_commit (repo, booted_checksum, &current_commit, NULL, &local_error))
    {
      g_warning ("Error loading current commit ‘%s’ to check if ‘%s’ is an update (assuming it is): %s",
                 booted_checksum, update_checksum, local_error->message);
      g_clear_error (&local_error);
    }

  if (!ostree_repo_load_commit (repo, update_checksum, &update_commit, NULL, error))
    return FALSE;

  /* If we failed to load the currently deployed commit, it is probably missing
   * from the repository (see T22805). Try and recover by assuming @checksum
   * *is* an update and fetching it. We shouldn’t fail to load the @checksum
   * commit because we should have just pulled its metadata into the repository
   * as part of polling. If we do fail, we can’t proceed further since we need
   * to examine the commit metadata before upgrading to it. */
  if (current_commit == NULL)
    {
      *out_commit = g_steal_pointer (&update_commit);
      *out_is_update_user_visible = FALSE;
      return TRUE;
    }

  /* Look up the versions on the current and update commits, so we can determine
   * if there are meant to be any user visible changes in the update. */
  current_commit_metadata = g_variant_get_child_value (current_commit, 0);
  g_variant_lookup (current_commit_metadata, OSTREE_COMMIT_META_KEY_VERSION, "&s", &current_version);

  update_commit_metadata = g_variant_get_child_value (update_commit, 0);
  g_variant_lookup (update_commit_metadata, OSTREE_COMMIT_META_KEY_VERSION, "&s", &update_version);

  /* Determine if the new commit is newer than the old commit to prevent
   * inadvertent (or malicious) attempts to downgrade the system.
   */
  update_timestamp = ostree_commit_get_timestamp (update_commit);
  current_timestamp = ostree_commit_get_timestamp (current_commit);

  g_debug ("%s: current_timestamp: %" G_GUINT64_FORMAT ", "
           "current_version: %s, update_timestamp: %" G_GUINT64_FORMAT ", "
           "update_version: %s",
           G_STRFUNC, current_timestamp, current_version,
           update_timestamp, update_version);

  /* "Newer" if we are switching branches or the update timestamp
   * is greater than the timestamp of the current commit.
   *
   * Generally speaking the updater is only allowed to go forward
   * but we can go "back in time" if we switched branches. This might
   * happen with checkpoint commits, where we have the following
   * history (numbers indicate commit timestamps):
   *
   * eos3a    -----(1)
   *               /\
   *              /  \
   * eos3  (0)--(2)--(3)
   *
   * It is possible to make a commit on a new refspec
   * with an older timestamp than the redirect commit on the old
   * refspec that redirects to it. So we shouldn't fail to switch branches
   * if the commit on the new branch was older in time.
   */
  is_newer = (g_strcmp0 (booted_ref, update_ref) != 0 ||
              update_timestamp > current_timestamp);

  /* We have explicit semantics on our version numbers, which are of the form
   * `major.minor.micro`. Major versions contain user visible changes, minor
   * versions are generally branch changes, and micro versions are bug fixes. */
  is_update_user_visible = compare_major_versions (current_version, update_version) < 0;

  *out_commit = is_newer ? g_steal_pointer (&update_commit) : NULL;
  *out_is_update_user_visible = is_newer ? is_update_user_visible : FALSE;

  if (out_booted_version != NULL)
    *out_booted_version = g_strdup (current_version);
  if (out_update_version != NULL)
    *out_update_version = g_strdup (update_version);

  return TRUE;
}

G_DEFINE_TYPE (EosMetricsInfo, eos_metrics_info, G_TYPE_OBJECT)

static void
eos_metrics_info_finalize (GObject *object)
{
  EosMetricsInfo *self = EOS_METRICS_INFO (object);

  g_free (self->vendor);
  g_free (self->product);
  g_free (self->ref);

  G_OBJECT_CLASS (eos_metrics_info_parent_class)->finalize (object);
}

static void
eos_metrics_info_class_init (EosMetricsInfoClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->finalize = eos_metrics_info_finalize;
}

static void
eos_metrics_info_init (EosMetricsInfo *self)
{
  /* nothing here */
}

G_DEFINE_TYPE (EosUpdateInfo, eos_update_info, G_TYPE_OBJECT)

static void
eos_update_info_dispose (GObject *object)
{
  EosUpdateInfo *self = EOS_UPDATE_INFO (object);

  g_clear_pointer (&self->commit, g_variant_unref);

  G_OBJECT_CLASS (eos_update_info_parent_class)->dispose (object);
}

static void
eos_update_info_finalize (GObject *object)
{
  EosUpdateInfo *self = EOS_UPDATE_INFO (object);

  g_free (self->checksum);
  g_free (self->new_refspec);
  g_free (self->old_refspec);
  g_free (self->version);
  g_strfreev (self->urls);
  g_clear_pointer (&self->results, ostree_repo_finder_result_freev);

  G_OBJECT_CLASS (eos_update_info_parent_class)->finalize (object);
}

static void
eos_update_info_class_init (EosUpdateInfoClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = eos_update_info_dispose;
  object_class->finalize = eos_update_info_finalize;
}

static void
eos_update_info_init (EosUpdateInfo *self)
{
  /* nothing here */
}

/* Steals @results. */
EosUpdateInfo *
eos_update_info_new (const gchar *checksum,
                     GVariant *commit,
                     const gchar *new_refspec,
                     const gchar *old_refspec,
                     const gchar *version,
                     gboolean is_user_visible,
                     const gchar *release_notes_uri,
                     const gchar * const *urls,
                     gboolean offline_results_only,
                     OstreeRepoFinderResult **results)
{
  EosUpdateInfo *info;

  g_return_val_if_fail (checksum != NULL, NULL);
  g_return_val_if_fail (commit != NULL, NULL);
  g_return_val_if_fail (new_refspec != NULL, NULL);
  g_return_val_if_fail (old_refspec != NULL, NULL);

  info = g_object_new (EOS_TYPE_UPDATE_INFO, NULL);
  info->checksum = g_strdup (checksum);
  info->commit = g_variant_ref (commit);
  info->new_refspec = g_strdup (new_refspec);
  info->old_refspec = g_strdup (old_refspec);
  info->version = g_strdup (version);
  info->is_user_visible = is_user_visible;
  info->release_notes_uri = g_strdup (release_notes_uri);
  info->urls = g_strdupv ((gchar **) urls);
  info->offline_results_only = offline_results_only;
  info->results = g_steal_pointer (&results);

  return info;
}

GDateTime *
eos_update_info_get_commit_timestamp (EosUpdateInfo *info)
{
  g_return_val_if_fail (EOS_IS_UPDATE_INFO (info), NULL);

  return g_date_time_new_from_unix_utc ((gint64) ostree_commit_get_timestamp (info->commit));
}

static gchar *
cleanstr (gchar *s)
{
  gchar *read;
  gchar *write;

  if (s == NULL)
    return s;

  for (read = write = s; *read != '\0'; ++read)
    {
      /* only allow printable */
      if (*read < 32 || *read > 126)
        continue;
      *write = *read;
      ++write;
    }
  *write = '\0';

  return s;
}

EosMetricsInfo *
eos_metrics_info_new (const gchar *booted_ref)
{
  g_autoptr(GHashTable) hw_descriptors = NULL;
  g_autoptr(EosMetricsInfo) info = NULL;

  hw_descriptors = get_hw_descriptors ();

  info = g_object_new (EOS_TYPE_METRICS_INFO, NULL);
  info->vendor = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, VENDOR_KEY)));
  info->product = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, PRODUCT_KEY)));
  info->ref = g_strdup (booted_ref);

  return g_steal_pointer (&info);
}

gboolean
get_booted_refspec (OstreeDeployment     *booted_deployment,
                    gchar               **booted_refspec,
                    gchar               **booted_remote,
                    gchar               **booted_ref,
                    OstreeCollectionRef **booted_collection_ref,
                    GError              **error)
{
  GKeyFile *origin;
  g_autofree gchar *refspec = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *collection_id = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (OSTREE_IS_DEPLOYMENT (booted_deployment), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  origin = ostree_deployment_get_origin (booted_deployment);
  if (origin == NULL)
    {
      const gchar *osname = ostree_deployment_get_osname (booted_deployment);
      const gchar *booted = ostree_deployment_get_csum (booted_deployment);

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No origin found for %s (%s), cannot upgrade",
                   osname, booted);
      return FALSE;
    }

  refspec = g_key_file_get_string (origin, "origin", "refspec", error);
  if (refspec == NULL)
    return FALSE;

  if (!ostree_parse_refspec (refspec, &remote, &ref, error))
    return FALSE;
  if (remote == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid refspec ‘%s’ in origin: did not contain a remote name",
                   refspec);
      return FALSE;
    }

  repo = eos_updater_local_repo (&local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  if (!ostree_repo_get_remote_option (repo, remote, "collection-id", NULL, &collection_id, error))
    return FALSE;

  g_message ("Using product branch %s", ref);

  if (booted_collection_ref != NULL)
    *booted_collection_ref = (collection_id != NULL) ? ostree_collection_ref_new (collection_id, ref) : NULL;
  if (booted_refspec != NULL)
    *booted_refspec = g_steal_pointer (&refspec);
  if (booted_remote != NULL)
    *booted_remote = g_steal_pointer (&remote);
  if (booted_ref != NULL)
    *booted_ref = g_steal_pointer (&ref);

  return TRUE;
}

/* On split-disk systems, an additional (bigger, slower) disk is mounted at
 * /var/endless-extra, and the system flatpak repo is configured to be at
 * /var/endless-extra/flatpak rather than /var/lib/flatpak/repo. */
static gboolean
booted_system_is_split_disk (OstreeRepo *os_repo)
{
  g_autoptr(GUnixMountEntry) extra_mount = NULL;

  extra_mount = g_unix_mount_at ("/var/endless-extra", NULL);

  if (g_strcmp0 (g_getenv ("EOS_UPDATER_TEST_IS_SPLIT_DISK"), "1") == 0)
    return TRUE;

  return (extra_mount != NULL);
}

/* Allow overriding various things for the tests. */
static const gchar *
allow_env_override (const gchar *default_value,
                    const gchar *env_key)
{
  const gchar *env_value = g_getenv (env_key);

  if (env_value != NULL && g_strcmp0 (env_value, "") != 0)
    return env_value;
  else
    return default_value;
}

/* ARM64 systems have their architecture listed as `aarch64` on Linux. On other
 * OSs, such as Darwin, it’s listed as `arm64`. */
static gboolean
booted_system_is_arm64 (void)
{
  struct utsname buf;
  const gchar *uname_machine;

  if (uname (&buf) != 0)
    return FALSE;

  uname_machine = allow_env_override (buf.machine, "EOS_UPDATER_TEST_UNAME_MACHINE");

  return (g_strcmp0 (uname_machine, "aarch64") == 0);
}

/* Check for an Intel i-8565U CPU using the info from /proc/cpuinfo. If the
 * system has multiple CPUs, this will match any of them. */
static gboolean
booted_system_has_i8565u_cpu (void)
{
  const gchar *cpuinfo_path;
  g_autofree gchar *cpuinfo = NULL;

  cpuinfo_path = allow_env_override ("/proc/cpuinfo", "EOS_UPDATER_TEST_CPUINFO_PATH");

  if (!g_file_get_contents (cpuinfo_path, &cpuinfo, NULL, NULL))
    return FALSE;

  return g_regex_match_simple ("^model name\\s*:\\s*Intel\\(R\\) Core\\(TM\\) i7-8565U CPU @ 1.80GHz$", cpuinfo,
                               G_REGEX_MULTILINE, 0);
}

/* Check @sys_vendor/@product_name against a list of systems which are no longer
 * supported since EOS 4. */
static gboolean
booted_system_is_unsupported_by_eos4_kernel (const gchar *sys_vendor,
                                             const gchar *product_name)
{
  const struct
    {
      const gchar *sys_vendor;
      const gchar *product_name;
    }
  no_upgrade_systems[] =
    {
      { "Acer", "Aspire ES1-533" },
      { "Acer", "Aspire ES1-732" },
      { "Acer", "Veriton Z4660G" },
      { "Acer", "Veriton Z4860G" },
      { "Acer", "Veriton Z6860G" },
      { "ASUSTeK COMPUTER INC.", "Z550MA" },
      { "Endless", "ELT-JWM" },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (no_upgrade_systems); i++)
    {
      if (g_str_equal (sys_vendor, no_upgrade_systems[i].sys_vendor) &&
          g_str_equal (product_name, no_upgrade_systems[i].product_name))
        return TRUE;
    }

  return FALSE;
}

/* Check @sys_vendor/@product_name against a list of systems which are no longer
 * supported since EOS 5. */
static gboolean
booted_system_is_unsupported_by_eos5_kernel (const gchar *sys_vendor,
                                             const gchar *product_name)
{
  const struct
    {
      const gchar *sys_vendor;
      const gchar *product_name;
    }
  no_upgrade_systems[] =
    {
      { "Endless", "EE-200" },
      { "Standard", "EF20" },
      { "Standard", "EF20EA" },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (no_upgrade_systems); i++)
    {
      if (g_str_equal (sys_vendor, no_upgrade_systems[i].sys_vendor) &&
          g_str_equal (product_name, no_upgrade_systems[i].product_name))
        return TRUE;
    }

  return FALSE;
}

/* Check if /proc/cmdline contains the given @needle, surrounded by word boundaries. */
static gboolean
boot_args_contain (const gchar *needle)
{
  const gchar *cmdline_path;
  g_autofree gchar *cmdline = NULL;
  g_autofree gchar *regex = NULL;

  cmdline_path = allow_env_override ("/proc/cmdline", "EOS_UPDATER_TEST_CMDLINE_PATH");

  if (!g_file_get_contents (cmdline_path, &cmdline, NULL, NULL))
    return FALSE;

  regex = g_strconcat ("\\b", needle, "\\b", NULL);
  return g_regex_match_simple (regex, cmdline, 0, 0);
}

/* Check if /var/lib/flatpak/repo has been split from /ostree/repo. A
 * simple symlink check is used since it would be very unlikely that
 * would occur in any other scenario.
 */
static gboolean
flatpak_repo_is_split (void)
{
  const gchar *dir_path;
  g_autofree gchar *repo_path = NULL;
  struct stat buf;

  dir_path = allow_env_override ("/var/lib/flatpak", "EOS_UPDATER_TEST_FLATPAK_INSTALLATION_DIR");
  repo_path = g_build_filename (dir_path, "repo", NULL);
  if (lstat (repo_path, &buf) == -1)
    {
      if (errno == ENOENT)
        return TRUE;

      g_warning ("Could not determine %s status: %s", repo_path, g_strerror (errno));
      return FALSE;
    }
  if ((buf.st_mode & S_IFMT) == S_IFLNK)
    return FALSE;

  return TRUE;
}

/* Check whether the ostree repo option "sysroot.bootloader" is set. */
static gboolean
ostree_bootloader_is_configured (OstreeRepo *repo)
{
  GKeyFile *config;
  g_autofree gchar *value = NULL;
  g_autoptr(GError) error = NULL;

  config = ostree_repo_get_config (repo);
  value = g_key_file_get_string (config, "sysroot", "bootloader", &error);

  /* Note that we don't care what the value is, only that it's set. This
   * matches the logic in the eos-ostree-bootloader-setup migration
   * script.
   */
  if (value == NULL)
    {
      if (!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND) &&
          !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND))
        {
          g_autofree gchar *repo_path = g_file_get_path (ostree_repo_get_path (repo));
          g_warning ("Error reading %s sysroot.bootloader option: %s", repo_path, error->message);
        }
      return FALSE;
    }

  return TRUE;
}

/* Whether the upgrade should follow the given checkpoint and move to the given
 * @target_ref for the upgrade deployment. The default for this is %TRUE, but
 * there are various systems for which support has been withdrawn, which need
 * to stay on old branches. In those cases, this function will return %FALSE
 * and will set a human-readable reason for this in @out_reason. */
static gboolean
should_follow_checkpoint (OstreeSysroot     *sysroot,
                          OstreeRepo        *repo,
                          OstreeDeployment  *booted_deployment,
                          const gchar       *booted_ref,
                          const gchar       *target_ref,
                          gchar            **out_reason)
{
  g_autoptr(GHashTable) hw_descriptors = NULL;
  const gchar *sys_vendor, *product_name;
  /* https://phabricator.endlessm.com/T32542,
   * https://phabricator.endlessm.com/T32552 */
  gboolean is_eos3_conditional_upgrade_path =
    (g_str_has_suffix (booted_ref, "/eos3a") ||
     g_str_has_suffix (booted_ref, "nexthw/eos3.9"));
  /* https://phabricator.endlessm.com/T33311 */
  gboolean is_eos4_conditional_upgrade_path = g_str_has_suffix (booted_ref, "/latest1");

  /* Simplifies the code below. */
  g_assert (out_reason != NULL);

  /* Allow an override in case the logic below is incorrect or doesn’t age well. */
  if (g_strcmp0 (g_getenv ("EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT"), "1") == 0)
    {
      g_message ("Forcing checkpoint target ‘%s’ to be used as EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT is set",
                  target_ref);
      return TRUE;
    }

  /* https://phabricator.endlessm.com/T30922 */
  if (is_eos3_conditional_upgrade_path &&
      booted_system_is_split_disk (repo))
    {
      *out_reason = g_strdup (_("Split disk systems are not supported in EOS 4."));
      return FALSE;
    }

  /* https://phabricator.endlessm.com/T31726 */
  if (is_eos3_conditional_upgrade_path &&
      booted_system_is_arm64 ())
    {
      *out_reason = g_strdup (_("ARM64 system upgrades are not supported in EOS 4. Please reinstall."));
      return FALSE;
    }

  /* These support being overridden by tests inside get_hw_descriptors(). */
  hw_descriptors = get_hw_descriptors ();
  sys_vendor = g_hash_table_lookup (hw_descriptors, VENDOR_KEY);
  product_name = g_hash_table_lookup (hw_descriptors, PRODUCT_KEY);

  /* https://phabricator.endlessm.com/T31777 */
  if (is_eos3_conditional_upgrade_path &&
      g_strcmp0 (sys_vendor, "Asus") == 0 &&
      booted_system_has_i8565u_cpu ())
    {
      /* Translators: The first placeholder is a system vendor name (such as
       * Acer). The second placeholder is a computer model (such as
       * Aspire ES1-533). */
      *out_reason = g_strdup_printf (_("%s %s systems are not supported in EOS 4."), "Asus", "i-8565U");
      return FALSE;
    }

  /* https://phabricator.endlessm.com/T31772 */
  if (is_eos3_conditional_upgrade_path &&
      sys_vendor != NULL && product_name != NULL &&
      booted_system_is_unsupported_by_eos4_kernel (sys_vendor, product_name))
    {
      *out_reason = g_strdup_printf (_("%s %s systems are not supported in EOS 4."), sys_vendor, product_name);
      return FALSE;
    }

  /* https://phabricator.endlessm.com/T31776 */
  if (is_eos3_conditional_upgrade_path &&
      boot_args_contain ("ro"))
    {
      *out_reason = g_strdup (_("Read-only systems are not supported in EOS 4."));
      return FALSE;
    }

  /* https://phabricator.endlessm.com/T33311 */
  if (is_eos4_conditional_upgrade_path &&
      sys_vendor != NULL && product_name != NULL &&
      booted_system_is_unsupported_by_eos5_kernel (sys_vendor, product_name))
    {
      *out_reason = g_strdup_printf (_("%s %s systems are not supported in EOS 5."), sys_vendor, product_name);
      return FALSE;
    }

  /* https://phabricator.endlessm.com/T34110 */
  if (is_eos4_conditional_upgrade_path &&
      !flatpak_repo_is_split ())
    {
      *out_reason = g_strdup (_("Merged OSTree and Flatpak repos are not supported in EOS 5."));
      return FALSE;
    }

  if (is_eos4_conditional_upgrade_path &&
      !ostree_bootloader_is_configured (repo))
    {
      *out_reason = g_strdup (_("OSTree automatic bootloader detection is not supported in EOS 5."));
      return FALSE;
    }

  /* Checkpoint can be followed. */
  g_assert (*out_reason == NULL);
  return TRUE;
}

static gboolean
get_ref_to_upgrade_on_from_deployment (OstreeSysroot     *sysroot,
                                       OstreeDeployment  *booted_deployment,
                                       const gchar       *booted_ref,
                                       gchar            **out_ref_to_upgrade_from_deployment,
                                       GError           **error)
{
  const gchar *checksum = ostree_deployment_get_csum (booted_deployment);
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) metadata = NULL;
  g_autoptr(GVariant) ref_for_deployment_variant = NULL;
  const gchar *refspec_for_deployment = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *reason = NULL;

  g_return_val_if_fail (out_ref_to_upgrade_from_deployment != NULL, FALSE);

  if (!ostree_sysroot_get_repo (sysroot, &repo, NULL, error))
   return FALSE;

  /* We need to be resilient if the $checksum.commit object is missing from the
   * local repository (for some reason). */
  if (!ostree_repo_load_variant (repo,
                                 OSTREE_OBJECT_TYPE_COMMIT,
                                 checksum,
                                 &commit,
                                 &local_error))
    {
      g_warning ("Error loading commit ‘%s’ to find checkpoint (assuming none): %s",
                 checksum, local_error->message);
      g_clear_error (&local_error);
    }

  /* Look up the checkpoint target to see if there is one on this commit. */
  if (commit != NULL)
    {
      metadata = g_variant_get_child_value (commit, 0);
      ref_for_deployment_variant = g_variant_lookup_value (metadata,
                                                           "eos.checkpoint-target",
                                                           G_VARIANT_TYPE_STRING);
    }

  /* No metadata tag on this commit, just return TRUE with no value */
  if (ref_for_deployment_variant == NULL)
    {
      *out_ref_to_upgrade_from_deployment = NULL;
      return TRUE;
    }

  refspec_for_deployment = g_variant_get_string (ref_for_deployment_variant, NULL);

  if (!ostree_parse_refspec (refspec_for_deployment, &remote, &ref, &local_error))
    {
      g_warning ("Failed to parse eos.checkpoint-target ref '%s', ignoring it",
                 refspec_for_deployment);
      *out_ref_to_upgrade_from_deployment = NULL;
      return TRUE;
    }

  if (remote != NULL)
    g_warning ("Ignoring remote '%s' in eos.checkpoint-target metadata '%s'",
               remote, refspec_for_deployment);

  /* Should we take this checkpoint? */
  if (!should_follow_checkpoint (sysroot, repo, booted_deployment, booted_ref, ref, &reason))
    {
      g_message ("Ignoring eos.checkpoint-target metadata ‘%s’ as following "
                 "the checkpoint is disabled for this system: %s",
                 refspec_for_deployment, reason);
      *out_ref_to_upgrade_from_deployment = NULL;
      return TRUE;
    }

  *out_ref_to_upgrade_from_deployment = g_steal_pointer (&ref);
  return TRUE;
}

/* @refspec_to_upgrade_on is guaranteed to include a remote and a ref name. */
gboolean
get_refspec_to_upgrade_on (gchar               **refspec_to_upgrade_on,
                           gchar               **remote_to_upgrade_on,
                           gchar               **ref_to_upgrade_on,
                           OstreeCollectionRef **collection_ref_to_upgrade_on,
                           GError              **error)
{
  g_autofree gchar *booted_refspec = NULL;
  g_autofree gchar *booted_remote = NULL;
  g_autofree gchar *booted_ref = NULL;
  g_autoptr(OstreeCollectionRef) booted_collection_ref = NULL;
  g_autofree gchar *checkpoint_ref_for_deployment = NULL;
  g_autoptr(OstreeSysroot) sysroot = ostree_sysroot_new_default ();
  g_autoptr(OstreeDeployment) booted_deployment = NULL;

  if (!ostree_sysroot_load (sysroot, NULL, error))
    return FALSE;

  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);

  if (booted_deployment == NULL)
    return FALSE;

  if (!get_booted_refspec (booted_deployment,
                           &booted_refspec,
                           &booted_remote,
                           &booted_ref,
                           &booted_collection_ref,
                           error))
    return FALSE;

  if (!get_ref_to_upgrade_on_from_deployment (sysroot,
                                              booted_deployment,
                                              booted_ref,
                                              &checkpoint_ref_for_deployment,
                                              error))
    return FALSE;

  /* Handle the ref from the commit's metadata */
  if (checkpoint_ref_for_deployment != NULL)
    {
      /* Set outparams from the checkpoint ref instead */
      if (collection_ref_to_upgrade_on != NULL)
        {
          g_autoptr(OstreeCollectionRef) collection_ref = NULL;

          if (booted_collection_ref != NULL)
            collection_ref = ostree_collection_ref_new (booted_collection_ref->collection_id,
                                                        checkpoint_ref_for_deployment);

          *collection_ref_to_upgrade_on = g_steal_pointer (&collection_ref);
        }
      if (refspec_to_upgrade_on != NULL)
        *refspec_to_upgrade_on = g_strdup_printf ("%s:%s",
                                                  booted_remote,
                                                  checkpoint_ref_for_deployment);
      if (remote_to_upgrade_on != NULL)
        *remote_to_upgrade_on = g_steal_pointer (&booted_remote);
      if (ref_to_upgrade_on != NULL)
        *ref_to_upgrade_on = g_steal_pointer (&checkpoint_ref_for_deployment);

      return TRUE;
    }

  /* Just use the booted refspec */
  if (collection_ref_to_upgrade_on != NULL)
    *collection_ref_to_upgrade_on = g_steal_pointer (&booted_collection_ref);
  if (refspec_to_upgrade_on != NULL)
    *refspec_to_upgrade_on = g_steal_pointer (&booted_refspec);
  if (remote_to_upgrade_on != NULL)
    *remote_to_upgrade_on = g_steal_pointer (&booted_remote);
  if (ref_to_upgrade_on != NULL)
    *ref_to_upgrade_on = g_steal_pointer (&booted_ref);

  return TRUE;
}

static GVariant *
get_repo_pull_options (const gchar *url_override,
                       const gchar *ref)
{
  g_auto(GVariantBuilder) builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (url_override != NULL)
    g_variant_builder_add (&builder, "{s@v}", "override-url",
                           g_variant_new_variant (g_variant_new_string (url_override)));
  g_variant_builder_add (&builder, "{s@v}", "flags",
                         g_variant_new_variant (g_variant_new_int32 (OSTREE_REPO_PULL_FLAGS_COMMIT_ONLY)));
  g_variant_builder_add (&builder, "{s@v}", "refs",
                         g_variant_new_variant (g_variant_new_strv (&ref, 1)));

  return g_variant_ref_sink (g_variant_builder_end (&builder));
};

/* @refspec *must* contain a remote and ref name (not just a ref name).
 * @out_new_refspec is guaranteed to include a remote and a ref name. */
gboolean
fetch_latest_commit (OstreeRepo *repo,
                     GCancellable *cancellable,
                     GMainContext *context,
                     const gchar *refspec,
                     const gchar *url_override,
                     GPtrArray *finders, /* (element-type OstreeRepoFinder) */
                     OstreeCollectionRef *collection_ref,
                     OstreeRepoFinderResult ***out_results,
                     gchar **out_checksum,
                     gchar **out_new_refspec,
                     gchar **out_version,
                     gchar **out_release_notes_uri_template,
                     GError **error)
{
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) options = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) rebase = NULL;
  g_autoptr(GVariant) metadata = NULL;
  g_autofree gchar *remote_name = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *upgrade_refspec = NULL;
  g_autofree gchar *new_refspec = NULL;
  g_autofree gchar *version = NULL;
  g_autofree gchar *release_notes_uri_template = NULL;
  gboolean redirect_followed = FALSE;
  g_auto(OstreeRepoFinderResultv) results = NULL;
  g_autoptr(OstreeCollectionRef) upgrade_collection_ref = NULL;
  g_autoptr(OstreeCollectionRef) new_collection_ref = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (refspec != NULL, FALSE);
  g_return_val_if_fail (out_checksum != NULL, FALSE);
  g_return_val_if_fail (out_new_refspec != NULL, FALSE);
  g_return_val_if_fail (out_version != NULL, FALSE);
  g_return_val_if_fail (out_release_notes_uri_template != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (finders != NULL && collection_ref == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "No collection ID set for currently booted deployment.");
      return FALSE;
    }

  upgrade_refspec = g_strdup (refspec);
  if (collection_ref != NULL)
    upgrade_collection_ref = ostree_collection_ref_dup (collection_ref);

  /* Check whether the commit is a redirection; if so, fetch the new ref and
   * check again. */
  do
    {
      if (finders == NULL)
        {
          g_clear_pointer (&remote_name, g_free);
          g_clear_pointer (&ref, g_free);

          if (!ostree_parse_refspec (upgrade_refspec, &remote_name, &ref, error))
            return FALSE;
          g_assert (remote_name != NULL);  /* caller must guarantee this */

          options = get_repo_pull_options (url_override, ref);
          if (!ostree_repo_pull_with_options (repo,
                                              remote_name,
                                              options,
                                              NULL,
                                              cancellable,
                                              error))
            return FALSE;
        }
      else
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
            return FALSE;

          /* Only pull commit metadata if there's an update available. */
          if (results[0] != NULL)
            {
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
                return FALSE;
            }
        }

      g_clear_pointer (&checksum, g_free);
      g_clear_pointer (&version, g_free);
      g_clear_pointer (&release_notes_uri_template, g_free);
      g_clear_pointer (&new_refspec, g_free);
      g_clear_pointer (&new_collection_ref, ostree_collection_ref_free);

      /* Parse the commit and check there’s no redirection to a new ref. */
      if (!parse_latest_commit (repo, upgrade_refspec, &redirect_followed,
                                &checksum, &new_refspec,
                                finders == NULL ? NULL : &new_collection_ref,
                                &version, &release_notes_uri_template, cancellable, error))
        return FALSE;

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

  *out_version = g_steal_pointer (&version);
  *out_release_notes_uri_template = g_steal_pointer (&release_notes_uri_template);
  *out_checksum = g_steal_pointer (&checksum);
  if (new_refspec != NULL)
    *out_new_refspec = g_steal_pointer (&new_refspec);
  else
    *out_new_refspec = g_strdup (refspec);

  if (out_results != NULL)
    *out_results = g_steal_pointer (&results);

  return TRUE;
}

/* @refspec *must* contain a remote and ref name (not just a ref name).
 * @out_new_refspec is guaranteed to include a remote and a ref name. */
gboolean
parse_latest_commit (OstreeRepo           *repo,
                     const gchar          *refspec,
                     gboolean             *out_redirect_followed,
                     gchar               **out_checksum,
                     gchar               **out_new_refspec,
                     OstreeCollectionRef **out_new_collection_ref,
                     gchar               **out_version,
                     gchar               **out_release_notes_uri_template,
                     GCancellable         *cancellable,
                     GError              **error)
{
  g_autofree gchar *ref = NULL;
  g_autofree gchar *remote_name = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) rebase = NULL;
  g_autoptr(GVariant) version = NULL;
  g_autoptr(GVariant) metadata = NULL;
  g_autofree gchar *collection_id = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (refspec != NULL, FALSE);
  g_return_val_if_fail (out_redirect_followed != NULL, FALSE);
  g_return_val_if_fail (out_checksum != NULL, FALSE);
  g_return_val_if_fail (out_new_refspec != NULL, FALSE);
  g_return_val_if_fail (out_version != NULL, FALSE);
  g_return_val_if_fail (out_release_notes_uri_template != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!ostree_parse_refspec (refspec, &remote_name, &ref, error))
    return FALSE;
  g_assert (remote_name != NULL);  /* caller must guarantee this */

  if (!ostree_repo_resolve_rev (repo, refspec, FALSE, &checksum, error))
    return FALSE;
  if (!ostree_repo_get_remote_option (repo, remote_name, "collection-id", NULL, &collection_id, error))
    return FALSE;

  /* We need to be resilient if the $checksum.commit object is missing from the
   * local repository (for some reason). */
  if (!ostree_repo_load_variant (repo,
                                 OSTREE_OBJECT_TYPE_COMMIT,
                                 checksum,
                                 &commit,
                                 &local_error))
    {
      g_warning ("Error loading commit ‘%s’ to find redirect (assuming none): %s",
                 checksum, local_error->message);
      g_clear_error (&local_error);
    }

  /* If this is a redirect commit, follow it and fetch the new ref instead
   * (unless the rebase is a loop; ignore that). */
  if (commit != NULL)
    {
      metadata = g_variant_get_child_value (commit, 0);
      rebase = g_variant_lookup_value (metadata, "ostree.endoflife-rebase", G_VARIANT_TYPE_STRING);
    }

  if (rebase != NULL &&
      g_strcmp0 (g_variant_get_string (rebase, NULL), ref) != 0)
    {
      g_clear_pointer (&ref, g_free);
      ref = g_variant_dup_string (rebase, NULL);

      *out_redirect_followed = TRUE;
    }
  else
    *out_redirect_followed = FALSE;

  if (metadata != NULL)
    version = g_variant_lookup_value (metadata, "version", G_VARIANT_TYPE_STRING);
  if (version == NULL)
    *out_version = NULL;
  else
    *out_version = g_variant_dup_string (version, NULL);

  if (metadata == NULL ||
      !g_variant_lookup (metadata, "eos-updater.release-notes-uri", "s", out_release_notes_uri_template))
    *out_release_notes_uri_template = NULL;

  *out_checksum = g_steal_pointer (&checksum);
  *out_new_refspec = g_strconcat (remote_name, ":", ref, NULL);
  if (out_new_collection_ref != NULL && collection_id != NULL)
    *out_new_collection_ref = ostree_collection_ref_new (collection_id, ref);
  else if (out_new_collection_ref != NULL)
    *out_new_collection_ref = NULL;

  return TRUE;
}

static void
get_custom_hw_descriptors (GHashTable *hw_descriptors,
                           const gchar *path)
{
  g_autoptr(GKeyFile) keyfile = NULL;
  g_auto(GStrv) keys = NULL;
  gchar **iter;
  const gchar *group = "descriptors";

  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_file (keyfile,
                                  path,
                                  G_KEY_FILE_NONE,
                                  NULL))
    return;

  keys = g_key_file_get_keys (keyfile,
                              group,
                              NULL,
                              NULL);
  if (keys == NULL)
    return;

  for (iter = keys; *iter != NULL; ++iter)
    {
      const gchar *key = *iter;
      gchar *value = g_key_file_get_string (keyfile,
                                            group,
                                            key,
                                            NULL);

      if (value == NULL)
        continue;

      g_hash_table_insert (hw_descriptors, g_strdup (key), value);
    }
}

static void
get_arm_hw_descriptors (GHashTable *hw_descriptors)
{
  g_autoptr(GFile) fp = g_file_new_for_path (DT_COMPATIBLE);
  g_autofree gchar *fc = NULL;

  if (g_file_load_contents (fp, NULL, &fc, NULL, NULL, NULL))
    {
      g_auto(GStrv) sv = g_strsplit (fc, ",", -1);

      if (sv && sv[0])
        g_hash_table_insert (hw_descriptors, g_strdup (VENDOR_KEY),
                             g_strdup (g_strstrip (sv[0])));
      if (sv && sv[1])
        g_hash_table_insert (hw_descriptors, g_strdup (PRODUCT_KEY),
                             g_strdup (g_strstrip (sv[1])));
    }
}

static void
get_x86_hw_descriptors (GHashTable *hw_descriptors)
{
  guint i;

  for (i = 0; dmi_attributes[i]; i++)
    {
      g_autofree gchar *path = NULL;
      g_autoptr(GFile) fp = NULL;
      g_autofree gchar *fc = NULL;
      gsize len;

      path = g_build_filename (DMI_PATH, dmi_attributes[i], NULL);
      fp = g_file_new_for_path (path);
      if (g_file_load_contents (fp, NULL, &fc, &len, NULL, NULL))
        {
          if (len > 128)
            fc[128] = '\0';
          g_hash_table_insert (hw_descriptors, g_strdup (dmi_attributes[i]),
                               g_strdup (g_strstrip (fc)));
        }
    }
}

static const gchar *
get_custom_descriptors_path (void)
{
  return g_getenv ("EOS_UPDATER_TEST_UPDATER_CUSTOM_DESCRIPTORS_PATH");
}

GHashTable *
get_hw_descriptors (void)
{
  GHashTable *hw_descriptors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, g_free);
  const gchar *custom_descriptors = get_custom_descriptors_path ();

  if (custom_descriptors != NULL)
    get_custom_hw_descriptors (hw_descriptors,
                               custom_descriptors);
  else if (g_file_test (DT_COMPATIBLE, G_FILE_TEST_EXISTS))
    get_arm_hw_descriptors (hw_descriptors);
  else
    get_x86_hw_descriptors (hw_descriptors);

  if (!g_hash_table_lookup (hw_descriptors, VENDOR_KEY))
    g_hash_table_insert (hw_descriptors, g_strdup (VENDOR_KEY),
                         g_strdup ("EOSUNKNOWN"));

  if (!g_hash_table_lookup (hw_descriptors, PRODUCT_KEY))
    g_hash_table_insert (hw_descriptors, g_strdup (PRODUCT_KEY),
                         g_strdup ("EOSUNKNOWN"));

  return hw_descriptors;
}

static void
maybe_send_metric (EosMetricsInfo *metrics)
{
#ifdef HAS_EOSMETRICS_0
  static gboolean metric_sent = FALSE;

  if (metric_sent)
    return;

  if (euu_get_metrics_enabled ())
    {
      g_message ("Recording metric event %s: (%s, %s, %s)",
                 EOS_UPDATER_METRIC_BRANCH_SELECTED, metrics->vendor, metrics->product,
                 metrics->ref);
      emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                             EOS_UPDATER_METRIC_BRANCH_SELECTED,
                                             g_variant_new ("(sssb)", metrics->vendor,
                                                            metrics->product,
                                                            metrics->ref,
                                                            (gboolean) FALSE  /* on-hold */));
    }
  else
    {
      g_debug ("Skipping metric event %s: (%s, %s, %s) (metrics disabled)",
               EOS_UPDATER_METRIC_BRANCH_SELECTED, metrics->vendor, metrics->product,
               metrics->ref);
    }

  metric_sent = TRUE;
#endif
}

void
metrics_report_successful_poll (EosUpdateInfo *update)
{
  g_autofree gchar *new_ref = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(EosMetricsInfo) metrics = NULL;

  if (!ostree_parse_refspec (update->new_refspec, NULL, &new_ref, &error))
    {
      g_message ("Failed to get metrics: %s", error->message);
      return;
    }

  /* Send metrics about our ref: this is the ref we’re going to upgrade to,
   * and that’s not always the same as the one we’re currently on. */
  metrics = eos_metrics_info_new (new_ref);
  maybe_send_metric (metrics);
}

gchar *
eos_update_info_to_string (EosUpdateInfo *update)
{
  g_autofree gchar *update_urls = NULL;
  g_autoptr(GDateTime) timestamp = NULL;
  g_autofree gchar *timestamp_str = NULL;
  g_autoptr(GString) results_string = NULL;
  g_autofree gchar *results = NULL;
  const gchar *version = update->version;
  const gchar *is_user_visible_str = NULL;
  const gchar *release_notes_uri = update->release_notes_uri;
  gsize i;

  if (update->urls != NULL)
    update_urls = g_strjoinv ("\n   ", update->urls);
  else
    update_urls = g_strdup ("");

  timestamp = eos_update_info_get_commit_timestamp (update);
  timestamp_str = g_date_time_format (timestamp, "%FT%T%:z");

  results_string = g_string_new ("");
  for (i = 0; update->results != NULL && update->results[i] != NULL; i++)
    {
      const OstreeRepoFinderResult *result = update->results[i];

      g_string_append_printf (results_string, "\n   %s, priority %d, %u refs",
                              ostree_remote_get_name (result->remote),
                              result->priority,
                              g_hash_table_size (result->ref_to_checksum));
    }
  if (update->results == NULL)
    g_string_append (results_string, "(no repo finder results)");

  results = g_string_free (g_steal_pointer (&results_string), FALSE);

  if (version == NULL)
    version = "(no version information)";

  is_user_visible_str = update->is_user_visible ? "user visible" : "not user visible";

  if (release_notes_uri == NULL)
    release_notes_uri = "(no release notes URI)";

  return g_strdup_printf ("%s, %s, %s, %s, %s, %s, %s\n   %s%s",
                          update->checksum,
                          update->new_refspec,
                          update->old_refspec,
                          version,
                          is_user_visible_str,
                          release_notes_uri,
                          timestamp_str,
                          update_urls,
                          results);
}

static EosUpdateInfo *
get_latest_update (GArray *sources,
                   GHashTable *source_to_update)
{
  g_autoptr(GHashTable) latest = g_hash_table_new (NULL, NULL);
  GHashTableIter iter;
  gpointer name_ptr;
  gpointer update_ptr;
  g_autoptr(GDateTime) latest_timestamp = NULL;
  gsize idx;

  g_debug ("%s: source_to_update mapping:", G_STRFUNC);

  g_hash_table_iter_init (&iter, source_to_update);
  while (g_hash_table_iter_next (&iter, &name_ptr, &update_ptr))
    {
      EosUpdateInfo *update = update_ptr;
      g_autofree gchar *update_string = eos_update_info_to_string (update);
      gint compare_value = 1;
      g_autoptr(GDateTime) update_timestamp = NULL;

      g_debug ("%s: - %s: %s", G_STRFUNC, (const gchar *) name_ptr, update_string);

      update_timestamp = eos_update_info_get_commit_timestamp (update);

      if (latest_timestamp != NULL)
        compare_value = g_date_time_compare (update_timestamp,
                                             latest_timestamp);
      if (compare_value > 0)
        {
          g_clear_pointer (&latest_timestamp, g_date_time_unref);
          latest_timestamp = g_date_time_ref (update_timestamp);
          g_hash_table_remove_all (latest);
          compare_value = 0;
        }

      if (compare_value == 0)
        g_hash_table_insert (latest, name_ptr, update_ptr);
    }

  g_debug ("%s: sources list:", G_STRFUNC);

  for (idx = 0; idx < sources->len; ++idx)
    {
      EosUpdaterDownloadSource source = g_array_index (sources,
                                                       EosUpdaterDownloadSource,
                                                       idx);
      const gchar *name = download_source_to_string (source);
      EosUpdateInfo *update = g_hash_table_lookup (latest, name);

      if (update != NULL)
        {
          g_debug ("%s: - %s (matched)", G_STRFUNC, name);
          return update;
        }
      else
        {
          g_debug ("%s: - %s", G_STRFUNC, name);
        }
    }

  return NULL;
}

EosUpdateInfo *
run_fetchers (OstreeRepo   *repo,
              GMainContext *context,
              GCancellable *cancellable,
              GPtrArray    *fetchers,
              GArray       *sources,
              GError      **error)
{
  guint idx;
  g_autoptr(GHashTable) source_to_update = g_hash_table_new_full (NULL,
                                                                  NULL,
                                                                  NULL,
                                                                  (GDestroyNotify) g_object_unref);

  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);
  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (fetchers != NULL, NULL);
  g_return_val_if_fail (sources != NULL, NULL);
  g_return_val_if_fail (fetchers->len == sources->len, NULL);

  for (idx = 0; idx < fetchers->len; ++idx)
    {
      MetadataFetcher fetcher = g_ptr_array_index (fetchers, idx);
      g_autoptr(EosUpdateInfo) info = NULL;
      EosUpdaterDownloadSource source = g_array_index (sources,
                                                       EosUpdaterDownloadSource,
                                                       idx);
      const gchar *name = download_source_to_string (source);
      g_autoptr(GError) local_error = NULL;

      if (!fetcher (repo, context, &info, cancellable, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return NULL;
            }

          g_message ("Failed to poll metadata from source %s: %s",
                     name, local_error->message);
          continue;
        }

      if (info != NULL)
        {
          g_hash_table_insert (source_to_update,
                               (gpointer) name, g_object_ref (info));
        }
    }

  if (g_hash_table_size (source_to_update) > 0)
    {
      EosUpdateInfo *latest_update = NULL;

      latest_update = get_latest_update (sources, source_to_update);
      if (latest_update != NULL)
        {
          metrics_report_successful_poll (latest_update);
          return g_object_ref (latest_update);
        }
    }

  return NULL;
}

const gchar *
download_source_to_string (EosUpdaterDownloadSource source)
{
  switch (source)
    {
    case EOS_UPDATER_DOWNLOAD_MAIN:
    case EOS_UPDATER_DOWNLOAD_LAN:
    case EOS_UPDATER_DOWNLOAD_VOLUME:
      return order_key_str[source];

    default:
      g_assert_not_reached ();
    }
}

gboolean
string_to_download_source (const gchar *str,
                           EosUpdaterDownloadSource *source,
                           GError **error)
{
  EosUpdaterDownloadSource idx;

  g_return_val_if_fail (str != NULL, FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  for (idx = EOS_UPDATER_DOWNLOAD_FIRST;
       idx <= EOS_UPDATER_DOWNLOAD_LAST;
       ++idx)
    if (g_str_equal (str, order_key_str[idx]))
      break;

  if (idx > EOS_UPDATER_DOWNLOAD_LAST)
    {
      g_set_error (error, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_WRONG_CONFIGURATION, "Unknown download source %s", str);
      return FALSE;
    }
  *source = idx;
  return TRUE;
}

/* Macros for handling saturated incrementing and clamping */
#define SATURATED_INCREMENT_GUINT64(a, b)       \
  G_STMT_START {                                \
    if ((a) < G_MAXUINT64 - (b))                \
      (a) += (b);                               \
    else                                        \
      (a) = G_MAXUINT64;                        \
  } G_STMT_END
#define CLAMP_GUINT64_TO_GINT64(a) \
  G_STMT_START {                   \
    if ((a) > G_MAXINT64)          \
      (a) = G_MAXINT64;            \
  } G_STMT_END

static gboolean
get_commit_sizes (OstreeRepo    *repo,
                  const gchar   *checksum,
                  guint64       *new_archived,
                  guint64       *new_unpacked,
                  guint64       *archived,
                  guint64       *unpacked,
                  GCancellable  *cancellable,
                  GError       **error)
{
  g_return_val_if_fail (new_archived != NULL, FALSE);
  g_return_val_if_fail (new_unpacked != NULL, FALSE);
  g_return_val_if_fail (archived != NULL, FALSE);
  g_return_val_if_fail (unpacked != NULL, FALSE);

#ifdef HAVE_OSTREE_COMMIT_GET_OBJECT_SIZES
  g_autoptr(GVariant) commit = NULL;
  if (!ostree_repo_load_variant (repo, OSTREE_OBJECT_TYPE_COMMIT, checksum,
                                 &commit, error))
    {
      g_prefix_error (error, "Failed to read commit: ");
      return FALSE;
    }

  g_autoptr(GPtrArray) sizes = NULL;
  if (!ostree_commit_get_object_sizes (commit, &sizes, error))
    return FALSE;

  for (guint i = 0; i < sizes->len; i++)
    {
      OstreeCommitSizesEntry *entry = sizes->pdata[i];
      gboolean exists;

      SATURATED_INCREMENT_GUINT64 (*archived, entry->archived);
      SATURATED_INCREMENT_GUINT64 (*unpacked, entry->unpacked);

      if (!ostree_repo_has_object (repo, entry->objtype, entry->checksum,
                                   &exists, cancellable, error))
        return FALSE;

      if (!exists)
        {
          /* Object not in local repo */
          SATURATED_INCREMENT_GUINT64 (*new_archived, entry->archived);
          SATURATED_INCREMENT_GUINT64 (*new_unpacked, entry->unpacked);
        }
    }

  return TRUE;
#else
  /* API not available, just pretend as if sizes could not be found */
  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                       "OSTree does not support parsing ostree.sizes metadata");
  return FALSE;
#endif
}

void
metadata_fetch_finished (GObject *object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  EosUpdater *updater = EOS_UPDATER (object);
  GTask *task;
  GError *error = NULL;
  EosUpdaterData *data = user_data;
  OstreeRepo *repo = data->repo;
  g_autoptr(EosUpdateInfo) info = NULL;

  if (!g_task_is_valid (res, object))
    goto invalid_task;

  /* get the info about the fetched update */
  task = G_TASK (res);
  info = g_task_propagate_pointer (task, &error);

  if (info != NULL)
    {
      guint64 archived = 0;
      guint64 unpacked = 0;
      guint64 new_archived = 0;
      guint64 new_unpacked = 0;
      const gchar *label;
      const gchar *message;

      g_strfreev (data->overridden_urls);
      data->overridden_urls = g_steal_pointer (&info->urls);

      g_clear_pointer (&data->results, ostree_repo_finder_result_freev);
      data->results = g_steal_pointer (&info->results);

      data->offline_results_only = info->offline_results_only;

      /* Everything is happy thusfar */
      /* if we have a checksum for the remote upgrade candidate
       * and it's ≠ what we're currently booted into, advertise it as such.
       */
      eos_updater_clear_error (updater, EOS_UPDATER_STATE_UPDATE_AVAILABLE);
      eos_updater_set_update_id (updater, info->checksum);
      eos_updater_set_update_refspec (updater, info->new_refspec);
      eos_updater_set_original_refspec (updater, info->old_refspec);
      eos_updater_set_version (updater, info->version);
      eos_updater_set_update_is_user_visible (updater, info->is_user_visible);
      eos_updater_set_release_notes_uri (updater, info->release_notes_uri);

      g_variant_get_child (info->commit, 3, "&s", &label);
      g_variant_get_child (info->commit, 4, "&s", &message);
      eos_updater_set_update_label (updater, label ? label : "");
      eos_updater_set_update_message (updater, message ? message : "");

      if (get_commit_sizes (repo, info->checksum,
                            &new_archived, &new_unpacked,
                            &archived, &unpacked,
                            g_task_get_cancellable (task),
                            &error))
        {
          /* Clamp to signed 64 bit max */
          CLAMP_GUINT64_TO_GINT64 (archived);
          CLAMP_GUINT64_TO_GINT64 (unpacked);
          CLAMP_GUINT64_TO_GINT64 (new_archived);
          CLAMP_GUINT64_TO_GINT64 (new_unpacked);
          eos_updater_set_full_download_size (updater, (gint64) archived);
          eos_updater_set_full_unpacked_size (updater, (gint64) unpacked);
          eos_updater_set_download_size (updater, (gint64) new_archived);
          eos_updater_set_unpacked_size (updater, (gint64) new_unpacked);
          eos_updater_set_downloaded_bytes (updater, 0);
        }
      else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          /* no size data available or no size parsing API available */
          eos_updater_set_full_download_size (updater, -1);
          eos_updater_set_full_unpacked_size (updater, -1);
          eos_updater_set_download_size (updater, -1);
          eos_updater_set_unpacked_size (updater, -1);
          eos_updater_set_downloaded_bytes (updater, -1);

          /* shouldn't actually stop us offering an update, as long
           * as the branch itself is resolvable in the next step,
           * but log it anyway.
           */
          g_message ("No size summary data: %s", error->message);
          g_clear_error (&error);
        }
    }
  else /* info == NULL means OnHold=true, nothing to do here */
    eos_updater_clear_error (updater, EOS_UPDATER_STATE_READY);

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
