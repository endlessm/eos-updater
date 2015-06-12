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

#include "ostree-daemon-util.h"
#include "ostree-daemon.h"
#include <ostree.h>

static const GDBusErrorEntry otd_error_entries[] = {
  { OTD_ERROR_WRONG_STATE, "org.gnome.OSTree.Error.WrongState" }
};

/* Ensure that every error code has an associated D-Bus error name */
G_STATIC_ASSERT (G_N_ELEMENTS (otd_error_entries) == OTD_N_ERRORS);

GQuark
otd_error_quark (void)
{
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("otd-error-quark",
                                      &quark_volatile,
                                      otd_error_entries,
                                      G_N_ELEMENTS (otd_error_entries));
  return (GQuark) quark_volatile;
}

static const gchar * state_str[] = {
   "None",
   "Ready",
   "Error",
   "Polling",
   "UpdateAvailable",
   "Fetching",
   "UpdateReady",
   "ApplyUpdate",
   "UpdateApplied" };

G_STATIC_ASSERT (G_N_ELEMENTS (state_str) == OTD_N_STATES);

const gchar * otd_state_to_string (OTDState state)
{
  g_assert (state < OTD_N_STATES);

  return state_str[state];
};


void
ostree_daemon_set_state (OTDOSTree *ostree, OTDState state)
{
  otd_ostree_set_state (ostree, state);
  otd_ostree_emit_state_changed (ostree, state);
}

void
ostree_daemon_set_error (OTDOSTree *ostree, GError *error)
{
  gint code = error ? error->code : -1;
  const gchar *msg = (error && error->message) ? error->message : "Unspecified";

  otd_ostree_set_error_code (ostree, code);
  otd_ostree_set_error_message (ostree, msg);
  ostree_daemon_set_state (ostree, OTD_STATE_ERROR);
}

OstreeRepo *
ostree_daemon_local_repo (void)
{
  OstreeRepo *ret = NULL;
  GError *error = NULL;

  gs_unref_object OstreeRepo *repo = ostree_repo_new_default ();

  if (!ostree_repo_open (repo, NULL, &error))
    {
      GFile *file = ostree_repo_get_path (repo);
      gs_free gchar *path = g_file_get_path (file);

      g_warning ("Repo at '%s' is not Ok (%s)",
                 path ? path : "", error->message);

      g_clear_error (&error);
      g_assert_not_reached ();
    }

  ret = repo;
  repo = NULL;

  return ret;
}

gboolean
ostree_daemon_resolve_upgrade (OTDOSTree  *ostree,
                               OstreeRepo *repo,
                               gchar     **upgrade_remote,
                               gchar     **upgrade_ref,
                               gchar     **booted_checksum,
                               GError    **error)
{
  gboolean ret = FALSE;
  // gint cur_bootversion = -1;
  gs_free gchar *o_refspec = NULL;
  gs_free gchar *o_remote = NULL;
  gs_free gchar *o_ref = NULL;
  gs_unref_ptrarray GPtrArray *cur_deployments = NULL;
  // gs_unref_object OstreeDeployment *old_deployment = NULL;
  gs_unref_object OstreeDeployment *merge_deployment = NULL;
  // gs_unref_object GFile *sysroot = g_file_new_for_path ("/");
  const gchar *osname;
  const gchar *booted;
  GKeyFile *origin;

  OstreeSysroot *sysroot = ostree_sysroot_new_default ();

  if (!ostree_sysroot_load (sysroot, NULL, error))
    goto out;

  merge_deployment = ostree_sysroot_get_merge_deployment (sysroot, NULL);
  osname = ostree_deployment_get_osname (merge_deployment);
  origin = ostree_deployment_get_origin (merge_deployment);
  booted = ostree_deployment_get_csum (merge_deployment);

  if (!origin)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No origin found for %s (%s), cannot upgrade",
                   osname, booted);
      goto out;
    }

  o_refspec = g_key_file_get_string (origin, "origin", "refspec", NULL);

  if (!ostree_parse_refspec (o_refspec, &o_remote, &o_ref, error))
    goto out;

  ret = (o_remote && o_ref && *o_remote && *o_ref);

  shuffle_out_values (upgrade_remote, o_remote, NULL);
  shuffle_out_values (upgrade_ref, o_ref, NULL);

  if (booted_checksum)
    *booted_checksum = g_strdup (booted);

  out:
    return ret;
}
