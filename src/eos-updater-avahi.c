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

#include "eos-updater-avahi-emulator.h"
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
#include <string.h>

#ifndef EOS_AVAHI_PORT
#error "EOS_AVAHI_PORT is not defined"
#endif

static const gchar *const EOS_UPDATER_AVAHI_SERVICE_TYPE = "_eos_updater._tcp";

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

  /* Map of service name (typically human readable) to the number of
   * #AvahiServiceResolver instances we have running against that name. We
   * could end up with more than one resolver if the same name is advertised to
   * us over multiple interfaces or protocols (for example, IPv4 and IPv6).
   * Resolve all of them just in case one doesn’t work. */
  GHashTable *discovered_services;  /* (element-type (owned) utf8 uint) */
  GPtrArray *found_services;
  GError *error;
  EosAvahiState state;
  guint callback_id;
};

static void
eos_avahi_discoverer_dispose_impl (EosAvahiDiscoverer *discoverer)
{
  gpointer user_data = discoverer->user_data;
  GDestroyNotify notify = discoverer->notify;

  if (discoverer->callback_id > 0)
    {
      GSource* source = g_main_context_find_source_by_id (discoverer->context,
                                                          discoverer->callback_id);

      discoverer->callback_id = 0;
      if (source != NULL)
        g_source_destroy (source);
    }

  discoverer->user_data = NULL;
  discoverer->notify = NULL;
  if (notify != NULL)
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

    default:
      g_assert_not_reached ();
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
  guint n_resolvers;

  if (discoverer->state == EOS_AVAHI_FINISHED)
    return;

  /* Track the number of resolvers active for this @name. There may be several,
   * as @name might appear to us over several interfaces or protocols. Most
   * commonly this happens when both hosts are connected via IPv4 and IPv6. */
  n_resolvers = GPOINTER_TO_UINT (g_hash_table_lookup (discoverer->discovered_services,
                                                       name));
  if (n_resolvers == 0)
    {
      /* maybe it was removed in the meantime */
      g_hash_table_remove (discoverer->discovered_services, name);
      return;
    }
  else if (n_resolvers == 1)
    {
      g_hash_table_remove (discoverer->discovered_services, name);
    }
  else
    {
      g_hash_table_insert (discoverer->discovered_services, g_strdup (name),
                           GUINT_TO_POINTER (n_resolvers - 1));
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
  guint n_resolvers;

  if (discoverer->state == EOS_AVAHI_RESOLVING_ONLY)
    return;

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
      queue_error_callback (discoverer,
                            "Failed to resolve service %s: %s",
                            name,
                            avahi_strerror (avahi_client_errno (discoverer->client)));
      return;
    }

  message ("Found name service %s on the network; type: %s, domain: %s, "
           "protocol: %u, interface: %u", name, type, domain, protocol,
           interface);

  /* Increment (or start) the counter for the number of resolvers for this
   * @name. */
  n_resolvers = GPOINTER_TO_UINT (g_hash_table_lookup (discoverer->discovered_services,
                                                       name));
  g_hash_table_insert (discoverer->discovered_services, g_strdup (name),
                       GUINT_TO_POINTER (n_resolvers + 1));
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

    default:
      g_assert_not_reached ();
    }
}

static gboolean
setup_real_avahi_discoverer (EosAvahiDiscoverer *discoverer,
                             GMainContext *context,
                             GError **error)
{
  int failure = 0;

  avahi_set_allocator (avahi_glib_allocator ());

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
      return FALSE;
    }

  return TRUE;
}

static gboolean
setup_emulated_avahi_discoverer (EosAvahiDiscoverer *discoverer,
                                 GError **error)
{
  g_autoptr(GPtrArray) services = NULL;

  if (!eos_updater_avahi_emulator_get_services (&services, error))
    return FALSE;
  g_clear_pointer (&discoverer->found_services, g_ptr_array_unref);
  discoverer->found_services = g_steal_pointer (&services);
  queue_callback (discoverer);
  return TRUE;
}

static gboolean
use_avahi_emulator (void)
{
  const gchar *value = NULL;

  value = g_getenv ("EOS_UPDATER_TEST_UPDATER_USE_AVAHI_EMULATOR");

  return value != NULL;
}

EosAvahiDiscoverer *
eos_avahi_discoverer_new (GMainContext *context,
                          EosAvahiDiscovererCallback callback,
                          gpointer user_data,
                          GDestroyNotify notify,
                          GError **error)
{
  g_autoptr(EosAvahiDiscoverer) discoverer = NULL;

  g_return_val_if_fail (callback != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

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

  if (use_avahi_emulator ())
    {
      if (!setup_emulated_avahi_discoverer (discoverer, error))
        return NULL;
    }
  else if (!setup_real_avahi_discoverer (discoverer, context, error))
    return NULL;

  return g_steal_pointer (&discoverer);
}

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

static gchar *
get_avahi_services_dir (void)
{
  return eos_updater_dup_envvar_or ("EOS_UPDATER_TEST_UPDATER_AVAHI_SERVICES_DIR",
                                    SYSCONFDIR "/avahi/services");
}

gboolean
eos_avahi_generate_service_file (OstreeRepo *repo,
                                 GDateTime *head_commit_timestamp,
                                 GError **error)
{
  g_autoptr(GFile) service_file = NULL;
  g_autofree gchar *services_dir = NULL;
  g_autofree gchar *service_file_path = NULL;

  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (head_commit_timestamp != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  services_dir = get_avahi_services_dir ();
  service_file_path = g_build_filename (services_dir, "eos-updater.service", NULL);
  service_file = g_file_new_for_path (service_file_path);

  return generate_v1_service_file (repo, head_commit_timestamp, service_file, error);
}

const gchar *const eos_avahi_v1_ostree_path = "eos_ostree_path";
const gchar *const eos_avahi_v1_head_commit_timestamp = "eos_head_commit_timestamp";
