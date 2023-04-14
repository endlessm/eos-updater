/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2023 Endless OS Foundation, LLC
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "config.h"

#include <test-common/httpd.h>

#include <gio/gio.h>
#include <libsoup/soup.h>

typedef struct {
  GFile        *root;  /* (owned) */
  GMainContext *context;  /* (owned) */
  gint          running;
  gchar        *url;  /* (owned) */
} HttpdData;

struct _Httpd {
  HttpdData *data;  /* (owned) */
  GThread   *thread;  /* (owned) */
};

static HttpdData *
httpd_data_new (GFile *root)
{
  HttpdData *data = g_new0 (HttpdData, 1);
  data->root = g_object_ref (root);
  data->context = g_main_context_new ();
  return data;
}

static void
httpd_data_free (HttpdData *data)
{
  g_clear_object (&data->root);
  g_clear_pointer (&data->context, g_main_context_unref);
  g_clear_pointer (&data->url, g_free);
  g_free (data);
}

static void
log_httpd_message (SoupMessage *msg,
                   const char  *path)
{
  g_autoptr(GDateTime) now = NULL;
  g_autofree gchar *now_str = NULL;

  g_assert_cmpuint (msg->status_code, !=, 0);
  g_assert_nonnull (msg->reason_phrase);
  now = g_date_time_new_now_local ();
  now_str = g_date_time_format_iso8601 (now);
  g_test_message ("%s %s /%s: %u %s",
                  now_str, msg->method, path, msg->status_code, msg->reason_phrase);
}

static void
httpd_handler (SoupServer        *server,
               SoupMessage       *msg,
               const char        *path,
               GHashTable        *query,
               SoupClientContext *client,
               gpointer           user_data)
{
  GFile *root = G_FILE (user_data);
  g_autoptr(GFile) child = NULL;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GDateTime) modified_dt = NULL;
  g_autoptr(SoupDate) modified_st = NULL;
  g_autofree char *last_modified = NULL;
  const char *content_type;
  const char *etag_value;
  g_autofree char *etag = NULL;
  const char *if_none_match;
  const char *if_modified_since;
  gboolean not_modified = FALSE;

  g_debug ("Received %s %s", msg->method, path);

  if (msg->method != SOUP_METHOD_HEAD && msg->method != SOUP_METHOD_GET)
    {
      soup_message_set_status (msg, SOUP_STATUS_METHOD_NOT_ALLOWED);
      return log_httpd_message (msg, path);
    }

  while (path[0] == '/')
    path++;

  child = g_file_get_child (root, path);
  if (!g_file_equal (child, root) && !g_file_has_prefix (child, root))
    {
      soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);
      return log_httpd_message (msg, path);
    }

  info = g_file_query_info (child,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                            G_FILE_ATTRIBUTE_ETAG_VALUE ","
                            G_FILE_ATTRIBUTE_TIME_MODIFIED,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  if (!info)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
        }
      else
        {
          g_autofree gchar *child_path = g_file_get_path (child);

          g_error ("Could not query file %s: %s", child_path, error->message);
          soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
        }
      return log_httpd_message (msg, path);
    }

  if (g_file_info_get_file_type (info) != G_FILE_TYPE_REGULAR)
    {
      soup_message_set_status (msg, SOUP_STATUS_BAD_REQUEST);
      return log_httpd_message (msg, path);
    }

  modified_dt = g_file_info_get_modification_date_time (info);
  modified_st = soup_date_new_from_time_t (g_date_time_to_unix (modified_dt));
  last_modified = soup_date_to_string (modified_st, SOUP_DATE_HTTP);
  soup_message_headers_append (msg->response_headers, "Last-Modified", last_modified);

  content_type = g_file_info_get_content_type (info);
  if (!content_type)
    content_type = "application/octet-stream";

  etag_value = g_file_info_get_etag (info);
  if (etag_value != NULL)
    {
      etag = g_strdup_printf ("\"%s\"", etag_value);
      soup_message_headers_append (msg->response_headers, "ETag", etag);
    }

  /* Handle If-None-Match and If-Modified-Since. Per
   * https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/If-Modified-Since,
   * If-Modified-Since is ignored if If-None-Match is sent and is supported by
   * the server.
   */
  if_none_match = soup_message_headers_get_one (msg->request_headers, "If-None-Match");
  if_modified_since = soup_message_headers_get_one (msg->request_headers, "If-Modified-Since");
  if (etag != NULL && if_none_match != NULL)
    {
      not_modified = (g_strcmp0 (etag, if_none_match) == 0);
    }
  else if (modified_dt != NULL && if_modified_since != NULL)
    {
      g_autoptr(SoupDate) if_modified_since_sd = NULL;
      g_autoptr(GDateTime) if_modified_since_dt = NULL;

      if_modified_since_sd = soup_date_new_from_string (if_modified_since);
      if (if_modified_since_sd != NULL)
        if_modified_since_dt =
          g_date_time_new_from_unix_utc (soup_date_to_time_t (if_modified_since_sd));
      if (if_modified_since_dt != NULL)
        not_modified = (g_date_time_compare (modified_dt, if_modified_since_dt) <= 0);
    }

  if (not_modified || msg->method == SOUP_METHOD_HEAD)
    {
      g_autofree gchar *length = g_strdup_printf ("%" G_GOFFSET_FORMAT, g_file_info_get_size (info));
      guint status = not_modified ? SOUP_STATUS_NOT_MODIFIED : SOUP_STATUS_OK;

      soup_message_headers_append (msg->response_headers, "Content-Length", length);
      soup_message_headers_append (msg->response_headers, "Content-Type", content_type);
      soup_message_set_status (msg, status);
    }
  else
    {
      g_autofree char *contents = NULL;
      gsize length = 0;

      if (!g_file_load_contents (child, NULL, &contents, &length, NULL, &error))
        {
          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
              soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
            }
          else
            {
              g_autofree gchar *child_path = g_file_get_path (child);

              g_error ("Could not load file %s: %s", child_path, error->message);
              soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
            }
          return log_httpd_message (msg, path);
        }

      soup_message_set_response (msg, content_type, SOUP_MEMORY_TAKE, g_steal_pointer (&contents), length);
      soup_message_set_status (msg, SOUP_STATUS_OK);
    }

  return log_httpd_message (msg, path);
}

static gpointer
httpd_thread (gpointer thread_data)
{
  g_autoptr(SoupServer) server = NULL;
  HttpdData *data = thread_data;
  GMainContext *context = data->context;
  g_autoptr(GMainContextPusher) pusher = g_main_context_pusher_new (context);
  g_autoptr(GSList) uris = NULL;
  g_autofree gchar *url = NULL;
  g_autoptr(GError) error = NULL;

  server = soup_server_new (NULL, NULL);
  soup_server_add_handler (server, NULL, httpd_handler, data->root, NULL);
  if (!soup_server_listen_local (server, 0, 0, &error))
    {
      g_prefix_error_literal (&error, "HTTP server could not listen for connections: ");
      return g_steal_pointer (&error);
    }

  uris = soup_server_get_uris (server);
  if (uris == NULL)
    {
      g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "HTTP server does not have any URLs");
      return g_steal_pointer (&error);
    }
  url = soup_uri_to_string ((SoupURI *) uris->data, FALSE);

  g_test_message ("Starting HTTP server on %s", url);
  g_atomic_pointer_set (&data->url, g_steal_pointer (&url));
  g_atomic_int_set (&data->running, 1);
  while (g_atomic_int_get (&data->running) > 0)
    g_main_context_iteration (context, TRUE);

  return NULL;
}

Httpd *
httpd_new (GFile *root)
{
  Httpd *httpd = g_new0 (Httpd, 1);
  httpd->data = httpd_data_new (root);
  return httpd;
}

void
httpd_free (Httpd *httpd)
{
  g_autoptr(GError) local_error = NULL;

  if (!httpd_stop (httpd, &local_error))
    {
      g_error ("%s", local_error->message);
      g_clear_error (&local_error);
    }
  g_clear_pointer (&httpd->thread, g_thread_unref);
  g_clear_pointer (&httpd->data, httpd_data_free);
  g_free (httpd);
}

gboolean
httpd_start (Httpd   *httpd,
             GError **error)
{
  gint64 deadline;

  g_assert_null (httpd->thread);
  g_debug ("Starting HTTP server thread");
  httpd->thread = g_thread_new ("httpd", httpd_thread, httpd->data);

  deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
  while (g_atomic_int_get (&httpd->data->running) == 0)
    {
      if (g_get_monotonic_time () >= deadline)
        {
          g_critical ("HTTP server did not start within 5 seconds");
          (void) httpd_stop (httpd, error);
          return FALSE;
        }
      g_thread_yield ();
    }

  return TRUE;
}

gboolean
httpd_stop (Httpd   *httpd,
            GError **error)
{
  if (httpd->thread != NULL)
    {
      gpointer ret;

      g_test_message ("Stopping HTTP server");
      g_atomic_int_set (&httpd->data->running, 0);
      g_main_context_wakeup (httpd->data->context);

      /* Joining the thread consumes the reference. */
      ret = g_thread_join (g_steal_pointer (&httpd->thread));
      g_debug ("Stopped HTTP server thread");

      if (ret != NULL)
        {
          g_propagate_error (error, g_steal_pointer (&ret));
          return FALSE;
        }
    }

  return TRUE;
}

const gchar *
httpd_get_url (Httpd *httpd)
{
  return g_atomic_pointer_get (&httpd->data->url);
}
