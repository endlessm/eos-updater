/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <libeos-updater-util/ostree.h>
#include <libeos-updater-util/util.h>
#include <ostree.h>

/**
 * eos_sysroot_get_advertisable_commit:
 * @sysroot: loaded OSTree sysroot to use
 * @commit_checksum: (out callee-allocates) (transfer full) (nullable) (optional):
 *    return location for the checksum of an advertisable commit
 * @commit_ostree_path: (out callee-allocates) (transfer full) (nullable) (optional):
 *    return location for the OSTree path of an advertisable commit
 * @commit_timestamp: (out caller-allocates) (optional): return location for
 *    the timestamp of an advertisable commit
 * @error: return location for a #GError
 *
 * Get the details of the most suitable OSTree commit to advertise over the
 * local network as being available to download from this machine. Note that
 * this does not check whether advertisements are enabled.
 *
 * The commit is the latest deployed commit in @sysroot for the same OS as
 * the current booted deployment. If running on a non-OSTree system,
 * %G_IO_ERROR_NOT_FOUND will be returned. Otherwise, %TRUE will be returned,
 * and the commit details will be put in @commit_checksum, @commit_ostree_path
 * and @commit_timestamp.
 *
 * @commit_ostree_path has the same format as returned by
 * eos_updater_get_ostree_path().
 *
 * @sysroot must have been loaded before calling this function, using
 * ostree_sysroot_load(). This function does not lock the sysroot.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
eos_sysroot_get_advertisable_commit (OstreeSysroot  *sysroot,
                                     gchar         **commit_checksum,
                                     gchar         **commit_ostree_path,
                                     guint64        *commit_timestamp,
                                     GError        **error)
{
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(GPtrArray/*<OstreeDeployment>*/) deployments = NULL;
  OstreeDeployment *booted_deployment;
  const gchar *booted_osname;
  gsize i;
  guint64 latest_commit_timestamp = 0;
  const gchar *latest_commit_checksum = NULL;
  g_autofree gchar *latest_commit_ostree_path = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GError) load_commit_error = NULL;

  g_return_val_if_fail (OSTREE_IS_SYSROOT (sysroot), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!ostree_sysroot_get_repo (sysroot, &repo, NULL, error))
    return FALSE;

  /* Get the commit timestamps for the available deployments of the same OS as
   * is currently booted, then advertise the most up-to-date one. This might
   * not be the booted deployment, as we can start advertising a refspec as
   * soon as it’s deployed (i.e. after the ‘apply’ stage of the update). */
  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);
  if (booted_deployment == NULL)
    return FALSE;
  booted_osname = ostree_deployment_get_osname (booted_deployment);

  if (!eos_updater_get_ostree_path (repo, booted_osname,
                                    &latest_commit_ostree_path, error))
    return FALSE;

  deployments = ostree_sysroot_get_deployments (sysroot);

  for (i = 0; i < deployments->len; i++)
    {
      OstreeDeployment *deployment = OSTREE_DEPLOYMENT (deployments->pdata[i]);
      const gchar *checksum;
      g_autoptr(GVariant) update_commit = NULL;
      guint64 update_commit_timestamp;

      /* Right OS? */
      if (g_strcmp0 (ostree_deployment_get_osname (deployment), booted_osname) != 0)
        {
          g_debug ("%s: Skipping deployment ‘%s’ because its OS (%s) does not "
                   "match the booted OS (%s).",
                   G_STRFUNC, ostree_deployment_get_origin_relpath (deployment),
                   ostree_deployment_get_osname (deployment), booted_osname);
          continue;
        }

      checksum = ostree_deployment_get_csum (deployment);

      g_debug ("%s: deployment %s: %s", G_STRFUNC,
               ostree_deployment_get_origin_relpath (deployment), checksum);

      if (!ostree_repo_load_commit (repo, checksum, &update_commit, NULL,
                                    &local_error))
        {
          GFile *repo_path = ostree_repo_get_path (repo);
          g_autofree gchar *repo_path_str = g_file_get_path (repo_path);

          g_warning ("Deployment ‘%s’ uses checksum ‘%s’ which does not "
                     "correspond to a commit in repository ‘%s’. Ignoring.",
                     ostree_deployment_get_origin_relpath (deployment),
                     checksum, repo_path_str);

          /* If we fail to load commits from all the deployments, we will end
           * up with no latest commit. In that case, return this error. */
          if (load_commit_error == NULL)
            load_commit_error = g_steal_pointer (&local_error);
          else
            g_clear_error (&local_error);

          continue;
        }

      update_commit_timestamp = ostree_commit_get_timestamp (update_commit);
      if (latest_commit_timestamp == 0 ||
          update_commit_timestamp > latest_commit_timestamp)
        {
          latest_commit_checksum = ostree_deployment_get_csum (deployment);
          latest_commit_timestamp = update_commit_timestamp;
        }
    }

  /* If we have a booted deployment (which we must have to get to this point),
   * then we must have found a commit; unless we failed to load any of them. */
  if (latest_commit_timestamp == 0)
    {
      g_assert (load_commit_error != NULL);
      g_propagate_error (error, g_steal_pointer (&load_commit_error));
      return FALSE;
    }

  g_assert (latest_commit_checksum != NULL);
  g_assert (latest_commit_ostree_path != NULL);
  g_assert (latest_commit_timestamp > 0);

  if (commit_checksum != NULL)
    *commit_checksum = g_strdup (latest_commit_checksum);
  if (commit_ostree_path != NULL)
    *commit_ostree_path = g_steal_pointer (&latest_commit_ostree_path);
  if (commit_timestamp != NULL)
    *commit_timestamp = latest_commit_timestamp;

  return TRUE;
}
