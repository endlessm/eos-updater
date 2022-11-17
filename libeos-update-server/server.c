/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>
#include <libsoup/soup.h>

#include <libeos-update-server/repo.h>
#include <libeos-update-server/server.h>

/**
 * SECTION:server
 * @title: HTTP server
 * @short_description: HTTP server for multiple bare OSTree repositories
 * @include: libeos-update-server/server.h
 *
 * A server that sits on top of zero or more bare repositories and lies to
 * clients about the repositories’ mode, so it is possible to do pulls
 * from this repository.
 *
 * Each repository is served under its #EusRepo:root-path prefix.
 *
 * Since: UNRELEASED
 */

/**
 * EusServer:
 *
 * A server which handles serving zero or more #OstreeRepos at specified paths.
 *
 * Since: UNRELEASED
 */
struct _EusServer
{
  GObject parent_instance;

  SoupServer *server;  /* owned */
  GPtrArray *repos;  /* (element-type EusRepo), owned */

  guint pending_requests;
  gint64 last_request_time;
};

G_DEFINE_TYPE (EusServer, eus_server, G_TYPE_OBJECT)

typedef enum
{
  PROP_SERVER = 1,
  PROP_PENDING_REQUESTS,
  PROP_LAST_REQUEST_TIME,
} EusServerProperty;

static GParamSpec *props[PROP_LAST_REQUEST_TIME + 1] = { NULL, };

static void request_read_cb (SoupServer        *soup_server,
                             SoupMessage       *message,
                             SoupClientContext *client,
                             gpointer           user_data);
static void request_finished_cb (SoupServer        *soup_server,
                                 SoupMessage       *message,
                                 SoupClientContext *client,
                                 gpointer           user_data);
static void request_aborted_cb (SoupServer        *soup_server,
                                SoupMessage       *message,
                                SoupClientContext *client,
                                gpointer           user_data);

static void
eus_server_init (EusServer *self)
{
  self->repos = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
eus_server_constructed (GObject *object)
{
  EusServer *self = EUS_SERVER (object);

  G_OBJECT_CLASS (eus_server_parent_class)->constructed (object);

  g_assert (self->server != NULL);

  g_signal_connect (self->server, "request-read", (GCallback) request_read_cb, self);
  g_signal_connect (self->server, "request-finished", (GCallback) request_finished_cb, self);
  g_signal_connect (self->server, "request-aborted", (GCallback) request_aborted_cb, self);
}

static void
eus_server_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *spec)
{
  EusServer *self = EUS_SERVER (object);

  switch ((EusServerProperty) property_id)
    {
    case PROP_SERVER:
      g_value_set_object (value, self->server);
      break;

    case PROP_PENDING_REQUESTS:
      g_value_set_uint (value, self->pending_requests);
      break;

    case PROP_LAST_REQUEST_TIME:
      g_value_set_int64 (value, self->last_request_time);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
eus_server_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *spec)
{
  EusServer *self = EUS_SERVER (object);

  switch ((EusServerProperty) property_id)
    {
    case PROP_SERVER:
      g_set_object (&self->server, g_value_get_object (value));
      break;

    case PROP_PENDING_REQUESTS:
    case PROP_LAST_REQUEST_TIME:
      /* Read only. */

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
eus_server_dispose (GObject *object)
{
  EusServer *self = EUS_SERVER (object);

  if (self->repos != NULL)
    eus_server_disconnect (self);

  self->pending_requests = 0;
  self->last_request_time = 0;
  g_clear_pointer (&self->repos, g_ptr_array_unref);

  if (self->server != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->server, request_aborted_cb, self);
      g_signal_handlers_disconnect_by_func (self->server, request_finished_cb, self);
      g_signal_handlers_disconnect_by_func (self->server, request_read_cb, self);

      g_clear_object (&self->server);
    }

  G_OBJECT_CLASS (eus_server_parent_class)->dispose (object);
}

static void
eus_server_class_init (EusServerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = eus_server_constructed;
  object_class->dispose = eus_server_dispose;
  object_class->get_property = eus_server_get_property;
  object_class->set_property = eus_server_set_property;

  /**
   * EusServer:server:
   *
   * The #SoupServer to handle requests from.
   *
   * Since: UNRELEASED
   */
  props[PROP_SERVER] = g_param_spec_object ("server",
                                            "Server",
                                            "The #SoupServer to handle requests from.",
                                            SOUP_TYPE_SERVER,
                                            G_PARAM_READWRITE |
                                            G_PARAM_CONSTRUCT_ONLY |
                                            G_PARAM_STATIC_STRINGS);

  /**
   * EusServer:pending-requests:
   *
   * Pending requests are usually requests for file objects that happen
   * asynchronously, mostly due to their larger size. Use this property
   * together with #EusServer:last-request-time if you want to stop the server
   * after the timeout.
   */
  props[PROP_PENDING_REQUESTS] = g_param_spec_uint ("pending-requests",
                                                    "Pending requests",
                                                    "A number of pending requests this server has at the moment",
                                                    0,
                                                    G_MAXUINT,
                                                    0,
                                                    G_PARAM_READABLE |
                                                    G_PARAM_EXPLICIT_NOTIFY |
                                                    G_PARAM_STATIC_STRINGS);

  /**
   * EusServer:last-request-time:
   *
   * The result of storing g_get_monotonic_time() at the end of the request and
   * response handlers. It is updated once at the start of each request, and
   * once at the end (regardless of whether the request was successful). Use
   * this property together with #EusServer:pending-requests if you want to stop
   * the server after the timeout.
   */
  props[PROP_LAST_REQUEST_TIME] = g_param_spec_int64 ("last-request-time",
                                                      "Last request time",
                                                      "A monotonic time in microseconds when the last request or response was handled",
                                                      0,
                                                      G_MAXINT64,
                                                      0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_EXPLICIT_NOTIFY |
                                                      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class,
                                     G_N_ELEMENTS (props),
                                     props);
}

static void
update_pending_requests (EusServer *self,
                         gboolean   increment)
{
  GObject *obj = G_OBJECT (self);

  g_assert (increment ? self->pending_requests < G_MAXUINT : self->pending_requests > 0);

  g_debug ("%s: Updating from %u by %d", G_STRFUNC, self->pending_requests,
           increment ? 1 : -1);

  if (increment)
    self->pending_requests++;
  else
    self->pending_requests--;
  self->last_request_time = g_get_monotonic_time ();

  g_object_freeze_notify (obj);
  g_object_notify_by_pspec (obj, props[PROP_PENDING_REQUESTS]);
  g_object_notify_by_pspec (obj, props[PROP_LAST_REQUEST_TIME]);
  g_object_thaw_notify (obj);
}

static void
request_read_cb (SoupServer        *soup_server,
                 SoupMessage       *message,
                 SoupClientContext *client,
                 gpointer           user_data)
{
  EusServer *self = EUS_SERVER (user_data);

  update_pending_requests (self, TRUE);
}

static void
request_finished_cb (SoupServer        *soup_server,
                     SoupMessage       *message,
                     SoupClientContext *client,
                     gpointer           user_data)
{
  EusServer *self = EUS_SERVER (user_data);

  update_pending_requests (self, FALSE);
}

static void
request_aborted_cb (SoupServer        *soup_server,
                    SoupMessage       *message,
                    SoupClientContext *client,
                    gpointer           user_data)
{
  EusServer *self = EUS_SERVER (user_data);

  update_pending_requests (self, FALSE);
}

/**
 * eus_server_new:
 * @server: #SoupServer to handle requests from
 *
 * Create a new #EusServer to handle requests from @server.
 *
 * Returns: (transfer full): The server.
 */
EusServer *
eus_server_new (SoupServer *server)
{
  g_return_val_if_fail (SOUP_IS_SERVER (server), NULL);

  return g_object_new (EUS_TYPE_SERVER,
                       "server", server,
                       NULL);
}

/**
 * eus_server_add_repo:
 * @self: an #EusServer
 * @repo: repository to start serving
 *
 * Add an #EusRepo to the server, and immediately make its contents available
 * to clients of the server.
 *
 * The repository will be available until eus_server_disconnect() is called.
 *
 * Since: UNRELEASED
 */
void
eus_server_add_repo (EusServer *self,
                     EusRepo   *repo)
{
  g_return_if_fail (EUS_IS_SERVER (self));
  g_return_if_fail (EUS_IS_REPO (repo));

  g_ptr_array_add (self->repos, g_object_ref (repo));
  eus_repo_connect (repo, self->server);
}

/**
 * eus_server_disconnect:
 * @self: an #EusServer
 *
 * Disconnect the server and all its repositories from the underlying
 * #SoupServer and its socket. Cancel all pending requests and stop handling
 * any new ones.
 *
 * This does not call soup_server_disconnect() on the underlying #SoupServer.
 *
 * This is called automatically when the #EusServer is disposed.
 *
 * Since: UNRELEASED
 */
void
eus_server_disconnect (EusServer *self)
{
  gsize i;

  g_return_if_fail (EUS_IS_SERVER (self));

  for (i = 0; i < self->repos->len; i++)
    {
      EusRepo *repo = g_ptr_array_index (self->repos, i);

      eus_repo_disconnect (repo);
    }

  g_ptr_array_set_size (self->repos, 0);
}

/**
 * eus_server_get_pending_requests:
 * @self: The #EusServer
 *
 * Get the value of #EusServer:pending-requests.
 *
 * Returns: Number of pending remotes.
 */
guint
eus_server_get_pending_requests (EusServer *self)
{
  return self->pending_requests;
}

/**
 * eus_server_get_last_request_time:
 * @self: The #EusServer
 *
 * Get the value of #EusServer:last-request-time.
 *
 * Returns: When was the last request handled
 */
gint64
eus_server_get_last_request_time (EusServer *self)
{
  return self->last_request_time;
}
