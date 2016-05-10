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
#include <libsoup/soup.h>
#include <eosmetrics/eosmetrics.h>


/*
 * Records which branch will be used by the updater. The payload is a 4-tuple
 * of 3 strings and boolean: vendor name, product ID, selected OStree ref, and
 * whether the machine is on hold
 */
#define EOS_UPDATER_BRANCH_SELECTED "99f48aac-b5a0-426d-95f4-18af7d081c4e"

static gboolean metric_sent = FALSE;

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

static gchar *
cleanstr (gchar *s)
{
  gchar *i, *j, *k;

  if (!s)
    return s;

  for (i = s; *i; i++)
    /* only allow printable */
    if (*i < 32 || *i > 126)
      for (j = i; *j; j++)
        {
          k = j + 1;
          *j = *k;
        }

  return s;
}

static const gchar *BRANCHES_CONFIG_PATH = "eos-branch";
static const gchar *DEFAULT_GROUP = "Default";
static const gchar *OSTREE_REF_KEY = "OstreeRef";
static const gchar *ON_HOLD_KEY = "OnHold";
static const gchar *DT_COMPATIBLE = "/proc/device-tree/compatible";
static const gchar *DMI_PATH = "/sys/class/dmi/id/";
static const gchar *dmi_attributes[] =
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

gboolean
eos_updater_resolve_upgrade (EosUpdater  *updater,
                             OstreeRepo *repo,
                             gchar     **upgrade_refspec,
                             gchar     **original_refspec,
                             gchar     **booted_checksum,
                             GError    **error)
{
  gboolean ret = FALSE;
  gboolean on_hold = FALSE;
  guint status = 0;
  gs_free gchar *o_refspec = NULL;
  gs_free gchar *o_remote = NULL;
  gs_free gchar *o_ref = NULL;
  gs_free gchar *vendor = NULL;
  gs_free gchar *product = NULL;
  gs_free gchar *p_id = NULL;
  gs_free gchar *p_ref = NULL;
  gs_free gchar *osgroup = NULL;
  gs_free gchar *baseurl = NULL;
  gs_free gchar *uri = NULL;
  gs_free gchar *query = NULL;
  gs_unref_object OstreeDeployment *merge_deployment = NULL;
  const gchar *osname;
  const gchar *booted;
  GKeyFile *origin;
  GKeyFile *repo_config;
  GHashTable *hw_descriptors = NULL;
  gs_unref_object SoupSession *soup = NULL;
  gs_unref_object SoupMessage *msg = NULL;
  gs_unref_keyfile GKeyFile *bkf = NULL;

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

  if (booted_checksum)
    *booted_checksum = g_strdup (booted);

  if (!upgrade_refspec)
    {
      /* Nothing left to do */
      ret = TRUE;
      goto out;
    }

  /* Get branch configuration data baseurl */
  repo_config = ostree_repo_get_config (repo);
  osgroup = g_strdup_printf ("remote \"%s\"", osname);
  baseurl = g_key_file_get_string (repo_config, osgroup, "url", error);
  if (!baseurl)
    goto out;

  /* Get product identifier */
  hw_descriptors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, g_free);
  if (g_file_test (DT_COMPATIBLE, G_FILE_TEST_EXISTS))
    { /* ARM */
      gs_unref_object GFile *fp = NULL;
      gs_free gchar *fc = NULL;

      fp = g_file_new_for_path (DT_COMPATIBLE);
      if (g_file_load_contents (fp, NULL, &fc, NULL, NULL, NULL))
        {
          gs_strfreev gchar **sv = g_strsplit (fc, ",", -1);

          if (sv && sv[0])
            g_hash_table_insert (hw_descriptors, g_strdup ("sys_vendor"),
                                 g_strdup (g_strstrip (sv[0])));
          if (sv && sv[1])
            g_hash_table_insert (hw_descriptors, g_strdup ("product_name"),
                                 g_strdup (g_strstrip (sv[1])));
        }
    }
  else
    { /* X86 */
      guint i;

      for (i = 0; dmi_attributes[i]; i++)
        {
          gs_free gchar *path = NULL;
          gs_unref_object GFile *fp = NULL;
          gs_free gchar *fc = NULL;
          gsize len;

          path = g_strconcat (DMI_PATH, dmi_attributes[i], NULL);
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

  if (!g_hash_table_lookup (hw_descriptors, "sys_vendor"))
    g_hash_table_insert (hw_descriptors, g_strdup ("sys_vendor"),
                         g_strdup ("EOSUNKNOWN"));

  if (!g_hash_table_lookup (hw_descriptors, "product_name"))
    g_hash_table_insert (hw_descriptors, g_strdup ("product_name"),
                         g_strdup ("EOSUNKNOWN"));

  vendor = g_strdup (g_hash_table_lookup (hw_descriptors, "sys_vendor"));
  product = g_strdup (g_hash_table_lookup (hw_descriptors, "product_name"));
  p_id = g_strconcat (cleanstr (vendor), " ", cleanstr (product), NULL);
  message ("Product group: %s", p_id);

  /* Build the HTTP URI */
  g_hash_table_insert (hw_descriptors, g_strdup ("ref"), g_strdup (o_ref));
  g_hash_table_insert (hw_descriptors, g_strdup ("commit"), g_strdup (booted));
  query = soup_form_encode_hash (hw_descriptors);
  uri = g_strconcat (baseurl, "/", BRANCHES_CONFIG_PATH, "?", query, NULL);
  g_hash_table_destroy (hw_descriptors);
  message ("Branches configuration URI: %s", uri);

  /* Download branch configuration data */
  soup = soup_session_new ();
  msg = soup_message_new ("GET", uri);
  status = soup_session_send_message (soup, msg);
  if (!SOUP_STATUS_IS_SUCCESSFUL (status))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to download branch config data (HTTP %d),"
                   " cannot upgrade", status);
      goto out;
    }

  bkf = g_key_file_new ();
  if (!g_key_file_load_from_data (bkf, msg->response_body->data, -1,
                                  G_KEY_FILE_NONE, error))
    goto out;

  /* Check for product-specific entry */
  if (g_key_file_has_group (bkf, p_id))
    {
      message ("Product-specific branch configuration found");
      if (g_key_file_get_boolean (bkf, p_id, ON_HOLD_KEY, NULL))
        {
          message ("Product is on hold, nothing to upgrade here");
          on_hold = TRUE;
          ret = TRUE;
          goto out;
        }
      p_ref = g_key_file_get_string (bkf, p_id, OSTREE_REF_KEY, error);
      if (!p_ref)
        goto out;
    }
  /* Check for a DEFAULT_GROUP entry */
  else if (g_key_file_has_group (bkf, DEFAULT_GROUP))
    {
      message ("No product-specific branch configuration found, following %s",
               DEFAULT_GROUP);
      if (g_key_file_get_boolean (bkf, DEFAULT_GROUP, ON_HOLD_KEY, NULL))
        {
          message ("No product-specific configuration and %s is on hold, "
                   "nothing to upgrade here", DEFAULT_GROUP);
          on_hold = TRUE;
          ret = TRUE;
          goto out;
        }
      p_ref = g_key_file_get_string (bkf, DEFAULT_GROUP, OSTREE_REF_KEY, error);
      if (!p_ref)
        goto out;
    }
  else /* fallback to the the origin file ref */
    {
      message ("No product-specific branch configuration or %s found, "
               "following the origin file", DEFAULT_GROUP);
      p_ref = o_ref;
      o_ref = NULL;
    }

  message ("Using product branch %s", p_ref);
  ret = TRUE;
  *upgrade_refspec = g_strdup_printf ("%s:%s", o_remote, p_ref);
  shuffle_out_values (original_refspec, o_refspec, NULL);

out:
  if ((p_ref || on_hold) && !metric_sent)
    {
      message ("Recording metric event %s: (%s, %s, %s, %d)",
               EOS_UPDATER_BRANCH_SELECTED, vendor, product,
               on_hold ? o_ref : p_ref, on_hold);
      emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                             EOS_UPDATER_BRANCH_SELECTED,
                                             g_variant_new ("(sssb)", vendor,
                                                            product,
                                                            on_hold ? o_ref :
                                                              p_ref,
                                                            on_hold));
      metric_sent = TRUE;
    }
  return ret;
}
