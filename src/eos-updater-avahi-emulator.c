/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2016 Kinvolk GmbH
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
 * Author: Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include "eos-updater-avahi-emulator.h"
#include "eos-updater-avahi.h"

#include "eos-util.h"

#include <gio/gio.h>

static gboolean
must_get_env (const gchar *env_var,
              gchar **out_value,
              GError **error)
{
  const gchar *value;

  value = g_getenv (env_var);
  if (value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid environment for avahi emulator, missing %s env var",
                   env_var);
      return FALSE;
    }

  *out_value = g_strdup (value);
  return TRUE;
}

static gboolean
get_avahi_emulator_definitions_dir (gchar **definitions_dir,
                                    GError **error)
{
  return must_get_env ("EOS_UPDATER_TEST_UPDATER_AVAHI_EMULATOR_DEFINITIONS_DIR",
                       definitions_dir,
                       error);
}

static gboolean
fill_service_from_key_file (EosAvahiService *service,
                            GKeyFile *keyfile,
                            GError **error)
{
  g_autoptr(GError) local_error = NULL;
  gint port;

  service->name = g_key_file_get_string (keyfile,
                                         "service",
                                         "name",
                                         error);
  if (service->name == NULL)
    return FALSE;

  service->domain = g_key_file_get_string (keyfile,
                                           "service",
                                           "domain",
                                           error);
  if (service->domain == NULL)
    return FALSE;

  service->address = g_key_file_get_string (keyfile,
                                            "service",
                                            "address",
                                            error);
  if (service->address == NULL)
    return FALSE;

  port = g_key_file_get_integer (keyfile,
                                 "service",
                                 "port",
                                 &local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  if (port < 1 || port > G_MAXUINT16)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "port number %d is invalid", port);
      return FALSE;
    }
  service->port = port;

  service->txt = g_key_file_get_string_list (keyfile,
                                             "service",
                                             "txt",
                                             NULL,
                                             error);
  if (service->txt == NULL)
    return FALSE;

  return TRUE;
}

static gboolean
parse_definition_file (GFile *file,
                       EosAvahiService **service,
                       GError **error)
{
  g_autofree gchar *contents = NULL;
  gsize len = 0;
  g_autoptr(GKeyFile) keyfile = NULL;
  g_autoptr(EosAvahiService) parsed_service = NULL;

  if (!g_file_load_contents (file,
                             NULL,
                             &contents,
                             &len,
                             NULL,
                             error))
    return FALSE;

  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_data (keyfile,
                                  contents,
                                  len,
                                  G_KEY_FILE_NONE,
                                  error))
    return FALSE;

  parsed_service = g_object_new (EOS_TYPE_AVAHI_SERVICE, NULL);
  if (!fill_service_from_key_file (parsed_service,
                                   keyfile,
                                   error))
    return FALSE;

  *service = g_steal_pointer (&parsed_service);
  return TRUE;
}

static gboolean
walk_definitions_directory (GFileEnumerator *enumerator,
                            GPtrArray **services,
                            GError **error)
{
  g_autoptr(GPtrArray) found_services = object_array_new ();

  for (;;)
    {
      GFileInfo *info;
      GFile *file;
      g_autoptr(EosAvahiService) service = NULL;

      if (!g_file_enumerator_iterate (enumerator,
                                      &info,
                                      &file,
                                      NULL,
                                      error))
        return FALSE;

      if (info == NULL || file == NULL)
        break;

      if ((g_file_info_get_file_type (info) != G_FILE_TYPE_REGULAR) ||
          (!g_str_has_suffix (g_file_info_get_name (info), ".ini")))
        continue;

      if (!parse_definition_file (file,
                                  &service,
                                  error))
        return FALSE;

      g_ptr_array_add (found_services, g_steal_pointer (&service));
    }

  *services = g_steal_pointer (&found_services);
  return TRUE;
}

gboolean
eos_updater_avahi_emulator_get_services (GPtrArray **services,
                                         GError **error)
{
  g_autofree gchar *definitions_dir = NULL;
  g_autoptr(GFile) dir = NULL;
  g_autoptr(GFileEnumerator) enumerator = NULL;

  if (!get_avahi_emulator_definitions_dir (&definitions_dir, error))
    return FALSE;

  dir = g_file_new_for_path (definitions_dir);
  enumerator = g_file_enumerate_children (dir,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          error);
  if (enumerator == NULL)
    return FALSE;

  return walk_definitions_directory (enumerator, services, error);
}
