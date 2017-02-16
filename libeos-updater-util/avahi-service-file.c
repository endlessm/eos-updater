/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
 * Copyright © 2017 Endless Mobile, Inc.
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
 * Authors:
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <libeos-updater-util/avahi-service-file.h>
#include <libeos-updater-util/util.h>
#include <ostree.h>
#include <string.h>

#ifndef EOS_AVAHI_PORT
#error "EOS_AVAHI_PORT is not defined"
#endif

const gchar * const EOS_UPDATER_AVAHI_SERVICE_TYPE = "_eos_updater._tcp";

const gchar * const eos_avahi_v1_ostree_path = "eos_ostree_path";
const gchar * const eos_avahi_v1_head_commit_timestamp = "eos_head_commit_timestamp";

static gchar *
txt_records_to_string (const gchar **txt_records)
{
  g_autoptr(GString) str = NULL;
  gsize idx;

  str = g_string_new ("");

  for (idx = 0; txt_records[idx] != NULL; idx++)
    {
      g_autofree gchar *record_escaped = g_markup_escape_text (txt_records[idx], -1);
      g_string_append_printf (str, "    <txt-record>%s</txt-record>\n",
                              record_escaped);
    }

  return g_string_free (g_steal_pointer (&str), FALSE);
}

static GBytes *
generate_from_avahi_service_template (const gchar *type,
                                      guint16 port,
                                      const gchar *txt_version,
                                      const gchar **txt_records)
{
  g_autofree gchar *service_group = NULL;
  gsize service_group_len;  /* bytes, not including trailing nul */
  g_autofree gchar *txt_records_str = txt_records_to_string (txt_records);
  g_autofree gchar *type_escaped = g_markup_escape_text (type, -1);
  g_autofree gchar *txt_version_escaped = g_markup_escape_text (txt_version, -1);

  service_group = g_strdup_printf (
      "<service-group>\n"
      "  <name replace-wildcards=\"yes\">EOS update service on %%h</name>\n"
      "  <service>\n"
      "    <type>%s</type>\n"
      "    <port>%" G_GUINT16_FORMAT "</port>\n"
      "    <txt-record>eos_txt_version=%s</txt-record>\n"
      "%s"
      "  </service>\n"
      "</service-group>\n",
      type_escaped, port, txt_version_escaped, txt_records_str);
  service_group_len = strlen (service_group);

  return g_bytes_new_take (g_steal_pointer (&service_group), service_group_len);
}

static gboolean
generate_avahi_service_template_to_file (GFile *path,
                                         const gchar *txt_version,
                                         const gchar **txt_records,
                                         GError **error)
{
  g_autoptr(GBytes) contents = NULL;
  gconstpointer raw;
  gsize raw_len;

  contents = generate_from_avahi_service_template (EOS_UPDATER_AVAHI_SERVICE_TYPE,
                                                   EOS_AVAHI_PORT,
                                                   txt_version,
                                                   txt_records);
  raw = g_bytes_get_data (contents, &raw_len);
  return g_file_replace_contents (path,
                                  raw,
                                  raw_len,
                                  NULL,
                                  FALSE,
                                  G_FILE_CREATE_NONE,
                                  NULL,
                                  NULL,
                                  error);
}

static gchar *
txt_record (const gchar *key,
            const gchar *value)
{
  return g_strdup_printf ("%s=%s", key, value);
}

static gboolean
generate_v1_service_file (OstreeRepo *repo,
                          GDateTime *head_commit_timestamp,
                          GFile *service_file,
                          GError **error)
{
  g_autoptr(GPtrArray) txt_records = NULL;
  g_autofree gchar *ostree_path = NULL;
  g_autofree gchar *timestamp_str = NULL;

  if (!eos_updater_get_ostree_path (repo, &ostree_path, error))
    return FALSE;

  timestamp_str = g_date_time_format (head_commit_timestamp, "%s");
  txt_records = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (txt_records, txt_record (eos_avahi_v1_ostree_path,
                                            ostree_path));
  g_ptr_array_add (txt_records, txt_record (eos_avahi_v1_head_commit_timestamp,
                                            timestamp_str));

  g_ptr_array_add (txt_records, NULL);
  return generate_avahi_service_template_to_file (service_file,
                                                  "1",
                                                  (const gchar **)txt_records->pdata,
                                                  error);
}

static const gchar *
get_avahi_services_dir (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_AVAHI_SERVICES_DIR",
                                    SYSCONFDIR "/avahi/services");
}

gboolean
eos_avahi_service_file_generate (OstreeRepo *repo,
                                 GDateTime *head_commit_timestamp,
                                 GError **error)
{
  g_autoptr(GFile) service_file = NULL;
  const gchar *services_dir;
  g_autofree gchar *service_file_path = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (head_commit_timestamp != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  services_dir = get_avahi_services_dir ();
  service_file_path = g_build_filename (services_dir, "eos-updater.service", NULL);
  service_file = g_file_new_for_path (service_file_path);

  return generate_v1_service_file (repo, head_commit_timestamp, service_file, error);
}
