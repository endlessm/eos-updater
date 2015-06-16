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

#include "eos-updater-util.h"
#include <ostree.h>

static const GDBusErrorEntry eos_updater_error_entries[] = {
  { EOS_UPDATER_ERROR_WRONG_STATE, "com.endlessm.Updater.Error.WrongState" }
};

/* Ensure that every error code has an associated D-Bus error name */
G_STATIC_ASSERT (G_N_ELEMENTS (eos_updater_error_entries) == EOS_UPDATER_N_ERRORS);

GQuark
eos_updater_error_quark (void)
{
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("eos-updater-error-quark",
                                      &quark_volatile,
                                      eos_updater_error_entries,
                                      G_N_ELEMENTS (eos_updater_error_entries));
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

G_STATIC_ASSERT (G_N_ELEMENTS (state_str) == EOS_UPDATER_N_STATES);

const gchar * eos_updater_state_to_string (EosUpdaterState state)
{
  g_assert (state < EOS_UPDATER_N_STATES);

  return state_str[state];
};


void
eos_updater_set_state_changed (EosUpdater *updater, EosUpdaterState state)
{
  eos_updater_set_state (updater, state);
  eos_updater_emit_state_changed (updater, state);
}

void
eos_updater_set_error (EosUpdater *updater, GError *error)
{
  gint code = error ? error->code : -1;
  const gchar *msg = (error && error->message) ? error->message : "Unspecified";

  eos_updater_set_error_code (updater, code);
  eos_updater_set_error_message (updater, msg);
  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_ERROR);
}

OstreeRepo *
eos_updater_local_repo (void)
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
eos_updater_resolve_upgrade (EosUpdater  *updater,
                             OstreeRepo *repo,
                             gchar     **upgrade_remote,
                             gchar     **upgrade_ref,
                             gchar     **booted_checksum,
                             GError    **error)
{
  gboolean ret = FALSE;
  gs_free gchar *o_refspec = NULL;
  gs_free gchar *o_remote = NULL;
  gs_free gchar *o_ref = NULL;
  gs_unref_ptrarray GPtrArray *cur_deployments = NULL;
  gs_unref_object OstreeDeployment *merge_deployment = NULL;
  const gchar *osname;
  const gchar *booted;
  GKeyFile *origin;

  OstreeSysroot *sysroot = ostree_sysroot_new_default ();

  if (!ostree_sysroot_load (sysroot, NULL, error))
    goto out;

  if (!ostree_sysroot_get_booted_deployment (sysroot))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                           "Not an ostree system");
      goto out;
    }

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
