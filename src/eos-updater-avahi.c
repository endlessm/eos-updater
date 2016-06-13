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

#include "config.h"

#include "eos-updater-avahi.h"

#include "eos-util.h"

#include "eos-updater-types.h"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/address.h>
#include <avahi-common/defs.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/strlst.h>
#include <avahi-glib/glib-malloc.h>
#include <avahi-glib/glib-watch.h>

#ifndef EOS_AVAHI_PORT
#error "EOS_AVAHI_PORT is not defined"
#endif

static const gchar *const EOS_UPDATER_AVAHI_SERVICE_TYPE = "_eos_updater._tcp";
static const gchar *const EOS_AVAHI_SERVICE_FILE_TEMPLATE =
  "<service-group>\n"
  "  <name replace-wildcards=\"yes\">EOS update service on %h</name>\n"
  "  <service>\n"
  "    <type>@TYPE@</type>\n"
  "    <port>@PORT@</port>\n"
  "    <txt-record>eos_txt_version=@TXT_VERSION@</txt-record>\n"
  "    @MORE_TXT_RECORDS@\n"
  "  </service>\n"
  "</service-group>\n";

static void
eos_avahi_service_finalize_impl (EosAvahiService *service)
{
  g_free (service->name);
  g_free (service->domain);
  g_free (service->address);
  g_strfreev (service->txt);
}

EOS_DEFINE_REFCOUNTED (EOS_AVAHI_SERVICE,
                       EosAvahiService,
                       eos_avahi_service,
                       NULL,
                       eos_avahi_service_finalize_impl)

typedef enum
{
  EOS_AVAHI_DISCOVERING_AND_RESOLVING,
  EOS_AVAHI_RESOLVING_ONLY,
  EOS_AVAHI_FINISHED
} EosAvahiState;

struct _EosAvahiDiscoverer
{
  GObject parent_instance;

  AvahiGLibPoll *poll;
  AvahiClient *client;
  AvahiServiceBrowser *browser;

  EosAvahiDiscovererCallback callback;
  gpointer user_data;
  GDestroyNotify notify;
  GMainContext *context;

  GHashTable *discovered_services;
  GPtrArray *found_services;
  GError *error;
  EosAvahiState state;
  guint callback_id;
};

static void
eos_avahi_discoverer_dispose_impl (EosAvahiDiscoverer *discoverer)
{
  guint callback_id = discoverer->callback_id;
  gpointer user_data = discoverer->user_data;
  GDestroyNotify notify = discoverer->notify;

  discoverer->callback_id = 0;
  if (callback_id > 0)
    {
      GSource* source;

      source = g_main_context_find_source_by_id (discoverer->context,
                                                 callback_id);
      if (source != NULL)
        g_source_destroy (source);
    }

  discoverer->user_data = NULL;
  discoverer->notify = NULL;
  if (user_data != NULL && notify != NULL)
    notify (user_data);

  g_clear_pointer (&discoverer->context, g_main_context_unref);
  g_clear_pointer (&discoverer->found_services, g_ptr_array_unref);
  g_clear_pointer (&discoverer->discovered_services, g_hash_table_unref);
}

static void
eos_avahi_discoverer_finalize_impl (EosAvahiDiscoverer *discoverer)
{
  g_clear_error (&discoverer->error);
  g_clear_pointer (&discoverer->browser, avahi_service_browser_free);
  g_clear_pointer (&discoverer->client, avahi_client_free);
  g_clear_pointer (&discoverer->poll, avahi_glib_poll_free);
}

EOS_DEFINE_REFCOUNTED (EOS_AVAHI_DISCOVERER,
                       EosAvahiDiscoverer,
                       eos_avahi_discoverer,
                       eos_avahi_discoverer_dispose_impl,
                       eos_avahi_discoverer_finalize_impl)

static gboolean
run_callback (gpointer discoverer_ptr)
{
  g_autoptr(EosAvahiDiscoverer) discoverer = g_object_ref (EOS_AVAHI_DISCOVERER (discoverer_ptr));
  g_autoptr(GPtrArray) found_services = discoverer->found_services;

  discoverer->callback_id = 0;
  discoverer->found_services = object_array_new ();
  if (discoverer->error != NULL)
    {
      GError *error = g_steal_pointer (&discoverer->error);

      discoverer->callback (discoverer, NULL, discoverer->user_data, error);
    }
  else
    discoverer->callback (discoverer, found_services, discoverer->user_data, NULL);

  return G_SOURCE_REMOVE;
}

static void
queue_callback (EosAvahiDiscoverer *discoverer)
{
  discoverer->state = EOS_AVAHI_FINISHED;

  discoverer->callback_id = eos_updater_queue_callback (discoverer->context,
                                                        run_callback,
                                                        discoverer,
                                                        "eos updater avahi callback");
}

static void
queue_error_callback (EosAvahiDiscoverer *discoverer,
                      const gchar *format,
                      ...)
{
  va_list args;

  va_start (args, format);
  discoverer->error = g_error_new_valist (EOS_UPDATER_ERROR,
                                          EOS_UPDATER_ERROR_LAN_DISCOVERY_ERROR,
                                          format,
                                          args);
  va_end (args);
  queue_callback (discoverer);
}

static void
client_cb (AvahiClient *client,
           AvahiClientState state,
           void* discoverer_ptr)
{
  EosAvahiDiscoverer *discoverer = discoverer_ptr;

  if (discoverer->state == EOS_AVAHI_FINISHED)
    return;

  switch (state)
    {
    case AVAHI_CLIENT_S_REGISTERING:
    case AVAHI_CLIENT_S_RUNNING:
    case AVAHI_CLIENT_S_COLLISION:
    case AVAHI_CLIENT_CONNECTING:
      /* we do not care about these states */
      break;

    case AVAHI_CLIENT_FAILURE:
      queue_error_callback (discoverer,
                            "Avahi client error: %s",
                            avahi_strerror (avahi_client_errno (client)));
      break;
    }
}

static void
maybe_queue_success_callback (EosAvahiDiscoverer *discoverer)
{
  if (discoverer->state != EOS_AVAHI_RESOLVING_ONLY)
    return;

  if (discoverer->callback_id > 0)
    return;

  if (g_hash_table_size (discoverer->discovered_services) > 0)
    return;

  queue_callback (discoverer);
}

static void
resolve_cb (AvahiServiceResolver *r,
            AvahiIfIndex interface,
            AvahiProtocol protocol,
            AvahiResolverEvent event,
            const char *name,
            const char *type,
            const char *domain,
            const char *host_name,
            const AvahiAddress *address,
            uint16_t port,
            AvahiStringList *txt,
            AvahiLookupResultFlags flags,
            void* discoverer_ptr)
{
  EosAvahiDiscoverer *discoverer = discoverer_ptr;
  char address_string[AVAHI_ADDRESS_STR_MAX];
  GPtrArray *gtxt;
  EosAvahiService *service;
  AvahiStringList* iter;

  if (discoverer->state == EOS_AVAHI_FINISHED)
    return;

  if (!g_hash_table_remove (discoverer->discovered_services, name))
    {
      /* maybe it was removed in the meantime */
      return;
    }

  gtxt = g_ptr_array_new ();
  for (iter = txt; iter != NULL; iter = avahi_string_list_get_next (iter))
    {
      const gchar* text = (const gchar*)avahi_string_list_get_text (iter);
      gsize size = avahi_string_list_get_size (iter);

      g_ptr_array_add (gtxt, g_strndup (text, size));
    }
  g_ptr_array_add (gtxt, NULL);

  service = g_object_new (EOS_TYPE_AVAHI_SERVICE, NULL);
  service->name = g_strdup (name);
  service->domain = g_strdup (domain);
  service->address = g_strdup (avahi_address_snprint (address_string, sizeof address_string, address));
  service->port = port;
  service->txt = (gchar**)g_ptr_array_free (gtxt, FALSE);
  g_ptr_array_add (discoverer->found_services, service);

  maybe_queue_success_callback (discoverer);
}

static void
browse_new (EosAvahiDiscoverer *discoverer,
            AvahiIfIndex interface,
            AvahiProtocol protocol,
            const char *name,
            const char *type,
            const char *domain)
{
  AvahiServiceResolver *resolver;

  if (discoverer->state == EOS_AVAHI_RESOLVING_ONLY)
    {
      return;
    }

  if (g_hash_table_contains (discoverer->discovered_services, name))
    {
      message("name service %s was already found on the network", name);
      return;
    }

  resolver = avahi_service_resolver_new (discoverer->client,
                                         interface,
                                         protocol,
                                         name,
                                         type,
                                         domain,
                                         AVAHI_PROTO_UNSPEC,
                                         0,
                                         resolve_cb,
                                         discoverer);
  if (resolver == NULL)
    {
      int failure = avahi_client_errno (discoverer->client);

      queue_error_callback (discoverer,
                            "Failed to resolve service %s: %s",
                            name,
                            avahi_strerror (failure));
      return;
    }

  g_hash_table_add (discoverer->discovered_services, g_strdup (name));
}

static void
browse_remove (EosAvahiDiscoverer *discoverer,
               const char *name)
{
  guint iter;

  if (g_hash_table_remove (discoverer->discovered_services, name))
    {
      maybe_queue_success_callback (discoverer);
      return;
    }
  for (iter = 0; iter < discoverer->found_services->len; ++iter)
    {
      EosAvahiService *service = g_ptr_array_index (discoverer->found_services, iter);

      if (g_strcmp0(service->name, name) == 0)
        {
          g_ptr_array_remove_index_fast (discoverer->found_services, iter);
          break;
        }
    }
}

static void
browse_discovery_finished (EosAvahiDiscoverer *discoverer)
{
  if (discoverer->state == EOS_AVAHI_RESOLVING_ONLY)
    return;

  discoverer->state = EOS_AVAHI_RESOLVING_ONLY;
  maybe_queue_success_callback (discoverer);
}

static void
browse_cb (AvahiServiceBrowser *browser,
           AvahiIfIndex interface,
           AvahiProtocol protocol,
           AvahiBrowserEvent event,
           const char *name,
           const char *type,
           const char *domain,
           AvahiLookupResultFlags flags,
           void* discoverer_ptr)
{
  EosAvahiDiscoverer *discoverer = discoverer_ptr;

  if (discoverer->state == EOS_AVAHI_FINISHED)
    return;

  switch (event)
    {
    case AVAHI_BROWSER_NEW:
      browse_new (discoverer, interface, protocol, name, type, domain);
      break;

    case AVAHI_BROWSER_REMOVE:
      browse_remove (discoverer, name);
      break;

    case AVAHI_BROWSER_CACHE_EXHAUSTED:
    case AVAHI_BROWSER_ALL_FOR_NOW:
      browse_discovery_finished (discoverer);
      break;

    case AVAHI_BROWSER_FAILURE:
      queue_error_callback (discoverer,
                            "Avahi browser error: %s",
                            avahi_strerror (avahi_client_errno (discoverer->client)));
      break;
    }
}

EosAvahiDiscoverer *
eos_avahi_discoverer_new (GMainContext *context,
                          EosAvahiDiscovererCallback callback,
                          gpointer user_data,
                          GDestroyNotify notify,
                          GError **error)
{
  g_autoptr(EosAvahiDiscoverer) discoverer = NULL;
  int failure = 0;

  g_return_val_if_fail (callback != NULL, NULL);

  avahi_set_allocator (avahi_glib_allocator ());

  discoverer = g_object_new (EOS_TYPE_AVAHI_DISCOVERER, NULL);
  discoverer->callback = callback;
  discoverer->user_data = user_data;
  discoverer->notify = notify;
  if (context == NULL)
    discoverer->context = g_main_context_ref_thread_default ();
  else
    discoverer->context = g_main_context_ref (context);
  discoverer->discovered_services = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      NULL);
  discoverer->found_services = object_array_new ();

  discoverer->poll = avahi_glib_poll_new (context, G_PRIORITY_DEFAULT);
  discoverer->client = avahi_client_new (avahi_glib_poll_get (discoverer->poll),
                                         0,
                                         client_cb,
                                         discoverer,
                                         &failure);

  if (discoverer->client == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_LAN_DISCOVERY_ERROR,
                   "Failed to create discoverer client: %s",
                   avahi_strerror (failure));
      return FALSE;
    }

  discoverer->browser = avahi_service_browser_new (discoverer->client,
                                                   AVAHI_IF_UNSPEC,
                                                   AVAHI_PROTO_UNSPEC,
                                                   EOS_UPDATER_AVAHI_SERVICE_TYPE,
                                                   NULL,
                                                   0,
                                                   browse_cb,
                                                   discoverer);
  if (discoverer->browser == NULL)
    {
      g_set_error (error,
                   EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_LAN_DISCOVERY_ERROR,
                   "Failed to create service browser: %s",
                   avahi_strerror (avahi_client_errno (discoverer->client)));
      return NULL;
    }
  return g_steal_pointer (&discoverer);
}

static GBytes *
generate_from_template (const gchar *tmpl,
                        GHashTable *values,
                        GError **error)
{
  g_auto(GStrv) splitted = g_strsplit (tmpl, "@", -1);
  gchar **iter;
  gboolean special = FALSE;
  g_autoptr(GString) xml = g_string_new (NULL);

  for (iter = splitted; *iter != NULL; ++iter)
    {
      if (special)
        {
          const gchar *value = g_hash_table_lookup (values, *iter);

          if (value == NULL)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "No value provided for token %s", *iter);
              return NULL;
            }
          g_string_append (xml, value);
        }
      else
        g_string_append (xml, *iter);

      special = !special;
    }

  /* if special is false then it means that the last token was special
   * and the next one would be a normal token, but it never came */
  if (!special)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Badly formed template, uneven number of @s");
      return NULL;
    }

  return g_string_free_to_bytes (g_steal_pointer (&xml));
}

static gchar *
txt_records_to_string (const gchar **txt_records)
{
  guint len = g_strv_length ((gchar **)txt_records);
  guint idx;
  g_auto(GStrv) xmled_txt_records = g_new0 (gchar *, len + 1);

  for (idx = 0; idx < len; ++idx)
    xmled_txt_records[idx] = g_strdup_printf ("<txt-record>%s</txt-record>",
                                                txt_records[idx]);

  return g_strjoinv ("\n    ", xmled_txt_records);
}

static GBytes *
generate_from_avahi_service_template (const gchar *type,
                                      guint16 port,
                                      const gchar *txt_version,
                                      const gchar **txt_records,
                                      GError **error)
{
  g_autoptr(GHashTable) values = g_hash_table_new (g_str_hash, g_str_equal);
  g_autofree gchar *port_str = g_strdup_printf ("%" G_GUINT16_FORMAT, port);
  g_autofree gchar *txt_records_str = txt_records_to_string (txt_records);

  g_hash_table_insert (values, "TYPE", (gpointer)type);
  g_hash_table_insert (values, "PORT", port_str);
  g_hash_table_insert (values, "TXT_VERSION", (gpointer)txt_version);
  g_hash_table_insert (values, "MORE_TXT_RECORDS", txt_records_str);

  return generate_from_template (EOS_AVAHI_SERVICE_FILE_TEMPLATE,
                                 values,
                                 error);
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
                                                   txt_records,
                                                   error);

  if (contents == NULL)
    return FALSE;

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
                          EosBranchFile *branch_file,
                          GFile *service_file,
                          GError **error)
{
  g_autoptr(GPtrArray) txt_records = NULL;
  g_autofree gchar *ostree_path = NULL;
  g_autofree gchar *dl_time = NULL;

  if (!eos_updater_get_ostree_path (repo, &ostree_path, error))
    return FALSE;

  txt_records = g_ptr_array_new_with_free_func (g_free);
  dl_time = g_date_time_format (branch_file->download_time, "%s");

  g_ptr_array_add (txt_records, txt_record (eos_avahi_v1_ostree_path (),
                                            ostree_path));
  g_ptr_array_add (txt_records, txt_record (eos_avahi_v1_branch_file_dl_time (),
                                            dl_time));
  g_ptr_array_add (txt_records, txt_record (eos_avahi_v1_branch_file_sha512sum (),
                                            branch_file->contents_sha512sum));

  g_ptr_array_add (txt_records, NULL);
  return generate_avahi_service_template_to_file (service_file,
                                                  "1",
                                                  (const gchar **)txt_records->pdata,
                                                  error);
}

static gboolean
generate_v2_service_file (OstreeRepo *repo,
                          GFile *service_file,
                          GError **error)
{
  g_autoptr(GPtrArray) txt_records = NULL;
  g_autofree gchar *ostree_path = NULL;

  if (!eos_updater_get_ostree_path (repo, &ostree_path, error))
    return FALSE;

  txt_records = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (txt_records, txt_record (eos_avahi_v2_ostree_path (),
                                            ostree_path));

  g_ptr_array_add (txt_records, NULL);
  return generate_avahi_service_template_to_file (service_file,
                                                  "2",
                                                  (const gchar **)txt_records->pdata,
                                                  error);
}

gboolean
eos_avahi_generate_service_file (OstreeRepo *repo,
                                 EosBranchFile *branch_file,
                                 GError **error)
{
  g_autoptr(GFile) service_file = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (EOS_IS_BRANCH_FILE (branch_file), FALSE);

  service_file = g_file_new_for_path ("/etc/avahi/services/eos-updater.service");

  if (branch_file->raw_signature != NULL)
    return generate_v2_service_file (repo, service_file, error);

  return generate_v1_service_file (repo, branch_file, service_file, error);
}

const gchar *
eos_avahi_v1_ostree_path (void)
{
  return "eos_ostree_path";
}

const gchar *
eos_avahi_v1_branch_file_dl_time (void)
{
  return "eos_branch_file_dl_time";
}

const gchar *
eos_avahi_v1_branch_file_sha512sum (void)
{
  return "eos_branch_file_sha512sum";
}

const gchar *
eos_avahi_v2_ostree_path (void)
{
  return "eos_ostree_path";
}

const gchar *
eos_avahi_v2_branch_file_timestamp (void)
{
  return "eos_branch_file_timestamp";
}
