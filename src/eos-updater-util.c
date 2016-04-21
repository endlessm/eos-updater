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

  return g_steal_pointer (&repo);
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

static OstreeDeployment *
get_booted_deployment (GError **error)
{
  gs_unref_object OstreeSysroot *sysroot = ostree_sysroot_new_default ();
  OstreeDeployment *booted_deployment = NULL;

  if (!ostree_sysroot_load (sysroot, NULL, error))
    return NULL;

  booted_deployment = ostree_sysroot_get_booted_deployment (sysroot);
  if (booted_deployment == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                           "Not an ostree system");
      return NULL;
    }

  return g_object_ref (booted_deployment);
}

static gchar *
get_booted_checksum (OstreeDeployment *booted_deployment)
{
  return g_strdup (ostree_deployment_get_csum (booted_deployment));
}

static gboolean
get_origin_refspec (OstreeDeployment *booted_deployment,
                    gchar **remote,
                    gchar **ref,
                    GError **error)
{
  GKeyFile *origin;
  gs_free gchar *refspec = NULL;

  origin = ostree_deployment_get_origin (booted_deployment);

  if (origin == NULL)
    {
      const gchar *osname;
      const gchar *booted;

      osname = ostree_deployment_get_osname (booted_deployment);
      booted = ostree_deployment_get_csum (booted_deployment);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No origin found for %s (%s), cannot upgrade",
                   osname, booted);
      return FALSE;
    }

  refspec = g_key_file_get_string (origin, "origin", "refspec", error);
  if (refspec == NULL)
    return FALSE;

  return ostree_parse_refspec (refspec, remote, ref, error);
}

static gchar *
get_baseurl (OstreeDeployment *booted_deployment,
             OstreeRepo *repo,
             GError **error)
{
  const gchar *osname;
  gs_free gchar *url = NULL;

  osname = ostree_deployment_get_osname (booted_deployment);
  if (!ostree_repo_remote_get_url (repo, osname, &url, error))
    return NULL;

  return g_steal_pointer (&url);
}

#define VENDOR_KEY "sys_vendor"
#define PRODUCT_KEY "product_name"

static void
get_arm_hw_descriptors (GHashTable *hw_descriptors)
{
  gs_unref_object GFile *fp = NULL;
  gs_free gchar *fc = NULL;

  fp = g_file_new_for_path (DT_COMPATIBLE);
  if (g_file_load_contents (fp, NULL, &fc, NULL, NULL, NULL))
    {
      gs_strfreev gchar **sv = g_strsplit (fc, ",", -1);

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

static GHashTable *
get_hw_descriptors (void)
{
  GHashTable *hw_descriptors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, g_free);

  if (g_file_test (DT_COMPATIBLE, G_FILE_TEST_EXISTS))
    { /* ARM */
      get_arm_hw_descriptors (hw_descriptors);
    }
  else
    { /* X86 */
      get_x86_hw_descriptors (hw_descriptors);
    }

  if (!g_hash_table_lookup (hw_descriptors, VENDOR_KEY))
    g_hash_table_insert (hw_descriptors, g_strdup (VENDOR_KEY),
                         g_strdup ("EOSUNKNOWN"));

  if (!g_hash_table_lookup (hw_descriptors, PRODUCT_KEY))
    g_hash_table_insert (hw_descriptors, g_strdup (PRODUCT_KEY),
                         g_strdup ("EOSUNKNOWN"));

  return hw_descriptors;
}

static GKeyFile *
download_branch_file (const gchar *baseurl,
                      GHashTable *query_params,
                      GError **error)
{
  gs_free gchar *query = NULL;
  gs_free gchar *uri = NULL;
  gs_unref_object SoupSession *soup = NULL;
  gs_unref_object SoupMessage *msg = NULL;
  guint status = 0;
  gs_unref_keyfile GKeyFile *bkf = NULL;

  query = soup_form_encode_hash (query_params);
  uri = g_strconcat (baseurl, "/", BRANCHES_CONFIG_PATH, "?", query, NULL);
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
      return NULL;
    }

  bkf = g_key_file_new ();
  if (!g_key_file_load_from_data (bkf, msg->response_body->data, -1,
                                  G_KEY_FILE_NONE, error))
    return NULL;
  return g_steal_pointer (&bkf);
}

static gboolean
process_single_group (GKeyFile *bkf,
                      const gchar *group_name,
                      gboolean *on_hold,
                      gchar **p_ref,
                      GError **error)
{
  GError *local_error = NULL;
  gs_free gchar *ref = NULL;

  if (g_key_file_get_boolean (bkf, group_name, ON_HOLD_KEY, &local_error))
    {
      *on_hold = TRUE;
      *p_ref = NULL;
      return TRUE;
    }
  /* The "OnHold" key is optional. */
  if (!g_error_matches (local_error,
                        G_KEY_FILE_ERROR,
                        G_KEY_FILE_ERROR_KEY_NOT_FOUND))
    {
      g_propagate_error (error, local_error);
      return FALSE;
    }
  g_clear_error (&local_error);
  ref = g_key_file_get_string (bkf, group_name, OSTREE_REF_KEY, error);
  if (ref == NULL)
    return FALSE;
  *on_hold = FALSE;
  *p_ref = g_steal_pointer (&ref);
  return TRUE;
}

static gboolean
process_branch_file (GKeyFile *bkf,
                     const gchar *group_name,
                     gboolean *on_hold,
                     gchar **p_ref,
                     GError **error)
{
  /* Check for product-specific entry */
  if (g_key_file_has_group (bkf, group_name))
    {
      message ("Product-specific branch configuration found");
      if (!process_single_group (bkf, group_name, on_hold, p_ref, error))
        return FALSE;
      if (*on_hold)
        message ("Product is on hold, nothing to upgrade here");
      return TRUE;
    }
  /* Check for a DEFAULT_GROUP entry */
  if (g_key_file_has_group (bkf, DEFAULT_GROUP))
    {
      message ("No product-specific branch configuration found, following %s",
               DEFAULT_GROUP);
      if (!process_single_group (bkf, DEFAULT_GROUP, on_hold, p_ref, error))
        return FALSE;
      if (*on_hold)
        message ("No product-specific configuration and %s is on hold, "
                 "nothing to upgrade here", DEFAULT_GROUP);
      return TRUE;
    }

  *on_hold = FALSE;
  *p_ref = NULL;
  return TRUE;
}

static void
maybe_send_metric (const gchar *vendor,
                   const gchar *product,
                   const gchar *ref,
                   gboolean on_hold)
{
  static gboolean metric_sent = FALSE;

  if (metric_sent)
    return;

  message ("Recording metric event %s: (%s, %s, %s, %d)",
           EOS_UPDATER_BRANCH_SELECTED, vendor, product,
           ref, on_hold);
  emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                         EOS_UPDATER_BRANCH_SELECTED,
                                         g_variant_new ("(sssb)", vendor,
                                                        product,
                                                        ref,
                                                        on_hold));
  metric_sent = TRUE;
}

static gboolean
get_upgrade_info (OstreeRepo *repo,
                  OstreeDeployment *booted_deployment,
                  gchar **upgrade_refspec,
                  gchar **original_refspec,
                  GError **error)
{
  gboolean on_hold = FALSE;
  gs_free gchar *booted_remote = NULL;
  gs_free gchar *booted_ref = NULL;
  gs_free gchar *ref = NULL;
  gs_free gchar *vendor = NULL;
  gs_free gchar *product = NULL;
  gs_free gchar *product_group = NULL;
  gs_free gchar *baseurl = NULL;
  gs_unref_hashtable GHashTable *hw_descriptors = NULL;
  gs_unref_keyfile GKeyFile *bkf = NULL;

  if (!get_origin_refspec (booted_deployment, &booted_remote, &booted_ref, error))
    return FALSE;

  baseurl = get_baseurl (booted_deployment, repo, error);
  if (!baseurl)
    return FALSE;

  hw_descriptors = get_hw_descriptors ();
  vendor = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, VENDOR_KEY)));
  product = cleanstr (g_strdup (g_hash_table_lookup (hw_descriptors, PRODUCT_KEY)));
  product_group = g_strdup_printf ("%s %s", vendor, product);
  message ("Product group: %s", product_group);

  g_hash_table_insert (hw_descriptors, g_strdup ("ref"), g_strdup (booted_ref));
  g_hash_table_insert (hw_descriptors, g_strdup ("commit"), get_booted_checksum (booted_deployment));
  bkf = download_branch_file (baseurl, hw_descriptors, error);
  if (bkf == NULL)
    return FALSE;

  if (!process_branch_file (bkf, product_group, &on_hold, &ref, error))
    return FALSE;

  if (on_hold)
    {
      ref = g_strdup (booted_ref);
      *upgrade_refspec = NULL;
      *original_refspec = NULL;
    }
  else
    {
      if (ref == NULL)
        {
          message ("No product-specific branch configuration or %s found, "
                   "following the origin file", DEFAULT_GROUP);
          ref = g_strdup (booted_ref);
        }

      message ("Using product branch %s", ref);
      *upgrade_refspec = g_strdup_printf ("%s:%s", booted_remote, ref);
      *original_refspec = g_strdup_printf ("%s:%s", booted_remote, booted_ref);
    }

  maybe_send_metric (vendor, product, ref, on_hold);
  return TRUE;
}

gboolean
eos_updater_get_upgrade_info (OstreeRepo *repo,
                              gchar **upgrade_refspec,
                              gchar **original_refspec,
                              GError **error)
{
  gs_unref_object OstreeDeployment *booted_deployment = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (upgrade_refspec != NULL, FALSE);
  g_return_val_if_fail (original_refspec != NULL, FALSE);

  booted_deployment = get_booted_deployment (error);
  if (booted_deployment == NULL)
    return FALSE;

  return get_upgrade_info (repo,
                           booted_deployment,
                           upgrade_refspec,
                           original_refspec,
                           error);
}

gchar *
eos_updater_get_booted_checksum (GError **error)
{
  gs_unref_object OstreeDeployment *booted_deployment = NULL;

  booted_deployment = get_booted_deployment (error);
  if (booted_deployment == NULL)
    return NULL;

  return get_booted_checksum (booted_deployment);
}
