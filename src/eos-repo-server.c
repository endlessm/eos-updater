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

#include "eos-repo-server.h"

#include <libeos-updater-util/util.h>

#include <string.h>

/**
 * SECTION:repo-server
 * @title: Bare repository server
 * @short_description: Server for the EOS bare OSTree repository
 * @include: eos-repo-server.h
 *
 * A server that sits on top of the bare repository and lies to
 * clients about the repositories' mode, so it is possible to do pulls
 * from this repository.
 *
 * It currently only supports version 1 of the repository format
 * (`repo_version=1` in the configuration file).
 */

/**
 * EosUpdaterRepoServer:
 *
 * A subclass of #SoupServer.
 */
struct _EosUpdaterRepoServer
{
  SoupServer parent_instance;
  OstreeRepo *repo;
  gchar *remote_name;
  GCancellable *cancellable;
  gchar *cached_repo_root;
  GBytes *cached_config;

  guint pending_requests;
  gint64 last_request_time;
};

static void eos_updater_repo_server_initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (EosUpdaterRepoServer, eos_updater_repo_server, SOUP_TYPE_SERVER,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                eos_updater_repo_server_initable_iface_init))

enum
{
  PROP_0,
  PROP_REPO,
  PROP_SERVED_REMOTE,
  PROP_PENDING_REQUESTS,
  PROP_LAST_REQUEST_TIME,

  PROP_N
};

static GParamSpec *props[PROP_N] = { NULL, };

static gboolean
generate_faked_config (OstreeRepo *repo,
                       GBytes **out_faked_config_contents,
                       GError **error)
{
  GKeyFile *parent_config;
  OstreeRepoMode parent_mode;
  gint parent_repo_version;
  g_autoptr(GKeyFile) config = NULL;
  g_autofree gchar *raw = NULL;
  gsize len = 0;

  /* Check that the repository is in a format we understand. */
  parent_config = ostree_repo_get_config (repo);
  parent_mode = ostree_repo_get_mode (repo);
  parent_repo_version = g_key_file_get_integer (parent_config, "core",
                                                "repo_version", NULL);

  if (parent_mode != OSTREE_REPO_MODE_BARE || parent_repo_version != 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Repository is in the wrong mode (%u) or version (%u).",
                   parent_mode, parent_repo_version);
      return FALSE;
    }

  /* Return a simple configuration file which doesn’t expose any of our own
   * remotes (whose URIs might contain usernames and passwords). The client
   * doesn’t need that information. */
  config = g_key_file_new ();

  g_key_file_set_integer (config, "core", "repo_version", 1);
  g_key_file_set_string (config, "core", "mode", "archive-z2");

  raw = g_key_file_to_data (config, &len, error);
  if (raw == NULL)
    {
      g_debug ("Failed to get raw contents of modified repository config");
      return FALSE;
    }

  *out_faked_config_contents = g_bytes_new_take (g_steal_pointer (&raw), len);
  return TRUE;
}

static void
eos_updater_repo_server_init (EosUpdaterRepoServer *server)
{}

static void
eos_updater_repo_server_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *spec)
{
  EosUpdaterRepoServer *server = EOS_UPDATER_REPO_SERVER (object);

  switch (property_id)
    {
    case PROP_REPO:
      g_value_set_object (value, server->repo);
      break;

    case PROP_SERVED_REMOTE:
      g_value_set_string (value, server->remote_name);
      break;

    case PROP_PENDING_REQUESTS:
      g_value_set_uint (value, server->pending_requests);
      break;

    case PROP_LAST_REQUEST_TIME:
      g_value_set_int64 (value, server->last_request_time);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
eos_updater_repo_server_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *spec)
{
  EosUpdaterRepoServer *server = EOS_UPDATER_REPO_SERVER (object);

  switch (property_id)
    {
    case PROP_REPO:
      g_set_object (&server->repo, g_value_get_object (value));
      break;

    case PROP_SERVED_REMOTE:
      server->remote_name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
eos_updater_repo_server_dispose (GObject *object)
{
  EosUpdaterRepoServer *server = EOS_UPDATER_REPO_SERVER (object);

  server->pending_requests = 0;
  server->last_request_time = 0;
  g_clear_object (&server->cancellable);
  g_clear_pointer (&server->cached_config, g_bytes_unref);
  g_clear_object (&server->repo);
}

static void
eos_updater_repo_server_finalize (GObject *object)
{
  EosUpdaterRepoServer *server = EOS_UPDATER_REPO_SERVER (object);

  g_free (server->cached_repo_root);
  g_free (server->remote_name);
}

static void
eos_updater_repo_server_class_init (EosUpdaterRepoServerClass *repo_server_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (repo_server_class);

  gobject_class->dispose = eos_updater_repo_server_dispose;
  gobject_class->finalize = eos_updater_repo_server_finalize;
  gobject_class->get_property = eos_updater_repo_server_get_property;
  gobject_class->set_property = eos_updater_repo_server_set_property;

  /**
   * EosUpdaterRepoServer:repo:
   *
   * An #OstreeRepo this server sits on.
   */
  props[PROP_REPO] = g_param_spec_object ("repo",
                                          "Repo",
                                          "OStree repository this server serves",
                                          OSTREE_TYPE_REPO,
                                          G_PARAM_READWRITE |
                                          G_PARAM_CONSTRUCT_ONLY |
                                          G_PARAM_STATIC_STRINGS);

  /**
   * EosUpdaterRepoServer:served-remote:
   *
   * The name of the remote this server serves.
   */
  props[PROP_SERVED_REMOTE] = g_param_spec_string ("served-remote",
                                                   "Served remote",
                                                   "The name of the OSTree remote this server serves",
                                                   "eos",
                                                   G_PARAM_READWRITE |
                                                   G_PARAM_CONSTRUCT_ONLY |
                                                   G_PARAM_STATIC_STRINGS);

  /**
   * EosUpdaterRepoServer:pending-requests:
   *
   * The number of pending requests. See
   * eos_updater_repo_server_get_pending_requests() for details.
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
   * EosUpdaterRepoServer:last-request-time:
   *
   * Time of the last served valid request. See
   * eos_updater_repo_server_get_last_request_time() for details.
   */
  props[PROP_LAST_REQUEST_TIME] = g_param_spec_int64 ("last-request-time",
                                                      "Last request time",
                                                      "A monotonic time in microseconds when the last valid request was handled",
                                                      0,
                                                      G_MAXINT64,
                                                      0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_EXPLICIT_NOTIFY |
                                                      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_N,
                                     props);
}

#define HEX_CLASS "[a-fA-F0-9]"

static GRegex *
get_filez_regex (void)
{
  static gsize regex_initialized;
  static GRegex *regex;

  if (g_once_init_enter (&regex_initialized))
    {
      regex = g_regex_new ("^/objects/(" HEX_CLASS "{2})/(" HEX_CLASS "{62})\\.filez$", G_REGEX_OPTIMIZE, 0, NULL);
      g_assert (regex);
      g_once_init_leave (&regex_initialized, 1);
    }

  return regex;
}

#undef HEX_CLASS

static gchar *
get_checksum_from_filez (const gchar *filez_path,
                         GError **error)
{
  g_autoptr(GMatchInfo) match = NULL;
  g_autofree gchar *first_two = NULL;
  g_autofree gchar *rest = NULL;
  g_autofree gchar *checksum = NULL;

  if (!g_regex_match (get_filez_regex (), filez_path, 0, &match))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid filez path %s", filez_path);
      return NULL;
    }

  first_two = g_match_info_fetch (match, 1);
  rest = g_match_info_fetch (match, 2);
  checksum = g_strdup_printf ("%s%s", first_two, rest);

  if (!ostree_validate_structureof_checksum_string (checksum, error))
    return NULL;

  return g_steal_pointer (&checksum);
}

static gboolean
load_compressed_file_stream (OstreeRepo *repo,
                             const gchar *checksum,
                             GCancellable *cancellable,
                             GInputStream **out_input,
                             guint64 *out_uncompressed_size,
                             GError **error)
{
  g_autoptr(GInputStream) bare = NULL;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GVariant) xattrs = NULL;
  g_autoptr(GInputStream) content = NULL;

  if (!ostree_repo_load_file (repo,
                              checksum,
                              &bare,
                              &info,
                              &xattrs,
                              cancellable,
                              error))
    return FALSE;

  if (!ostree_raw_file_to_archive_z2_stream (bare,
                                             info,
                                             xattrs,
                                             &content,
                                             cancellable,
                                             error))
      return FALSE;

  *out_input = g_steal_pointer (&content);
  *out_uncompressed_size = g_file_info_get_size (info);
  return TRUE;
}

#define EOS_TYPE_FILEZ_READ_DATA eos_filez_read_data_get_type ()
EOS_DECLARE_REFCOUNTED (EosFilezReadData,
                        eos_filez_read_data,
                        EOS,
                        FILEZ_READ_DATA)

struct _EosFilezReadData
{
  GObject parent_instance;

  EosUpdaterRepoServer *server;
  gpointer buffer;
  gsize buflen;
  SoupMessage *msg;
  gchar *filez_path;

  gulong finished_signal_id;
};

static void
eos_filez_read_data_disconnect_and_clear_msg (EosFilezReadData *read_data)
{
  if (read_data->finished_signal_id > 0)
    g_signal_handler_disconnect (read_data->msg, read_data->finished_signal_id);
  read_data->finished_signal_id = 0;
  g_clear_object (&read_data->msg);
  g_clear_object (&read_data->server);
}

static void
update_pending_requests (EosUpdaterRepoServer *server,
                         guint new_value)
{
  if (server->pending_requests == new_value)
    return;

  server->pending_requests = new_value;
  g_object_notify_by_pspec (G_OBJECT (server), props[PROP_PENDING_REQUESTS]);
}

static void
eos_filez_read_data_dispose_impl (EosFilezReadData *read_data)
{
  update_pending_requests (read_data->server,
                           read_data->server->pending_requests - 1);
  eos_filez_read_data_disconnect_and_clear_msg (read_data);
}

static void
eos_filez_read_data_finalize_impl (EosFilezReadData *read_data)
{
  g_free (read_data->buffer);
  g_free (read_data->filez_path);
}

EOS_DEFINE_REFCOUNTED (EOS_FILEZ_READ_DATA,
                       EosFilezReadData,
                       eos_filez_read_data,
                       eos_filez_read_data_dispose_impl,
                       eos_filez_read_data_finalize_impl)

static void
filez_read_data_finished_cb (SoupMessage *msg,
                             gpointer read_data_ptr)
{
  EosFilezReadData *read_data = EOS_FILEZ_READ_DATA (read_data_ptr);

  g_debug ("Downloading %s cancelled by client", read_data->filez_path);
  eos_filez_read_data_disconnect_and_clear_msg (read_data);
}

static EosFilezReadData *
filez_read_data_new (EosUpdaterRepoServer *server,
                     gsize buflen,
                     SoupMessage *msg,
                     const gchar *filez_path)
{
  EosFilezReadData *read_data;

  /* Small buffer length may happen for empty/small files, but zipping
   * empty/small files may produce larger files, presumably due to
   * some zlib file header or something. Let's allocate a larger
   * buffer, so we send the short data over the socket in an ideally
   * single step. Also, ostree adds its own headers to the stream
   * too. */
  if (buflen < 1024)
    buflen = 1024;
  read_data = g_object_new (EOS_TYPE_FILEZ_READ_DATA, NULL);
  read_data->server = g_object_ref (server);
  read_data->buffer = g_malloc (buflen);
  read_data->buflen = buflen;
  read_data->msg = g_object_ref (msg);
  read_data->filez_path = g_strdup (filez_path);
  read_data->finished_signal_id = g_signal_connect (msg, "finished", G_CALLBACK (filez_read_data_finished_cb), read_data);

  update_pending_requests (read_data->server,
                           read_data->server->pending_requests + 1);

  return read_data;
}

static void
filez_stream_read_chunk_cb (GObject *stream_object,
                            GAsyncResult *result,
                            gpointer read_data_ptr)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EosFilezReadData) read_data = EOS_FILEZ_READ_DATA (read_data_ptr);
  GInputStream *stream = G_INPUT_STREAM (stream_object);
  gssize bytes_read = g_input_stream_read_finish (stream,
                                                  result,
                                                  &error);
  SoupServer *server;

  if (read_data->msg == NULL)
    /* got cancelled */
    return;

  server = SOUP_SERVER (read_data->server);
  if (bytes_read < 0)
    {
      g_debug ("Failed to read the file %s: %s", read_data->filez_path, error->message);
      soup_message_set_status (read_data->msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
      soup_message_body_complete (read_data->msg->response_body);
      soup_server_unpause_message (server, read_data->msg);
      return;
    }
  if (bytes_read > 0)
    {
      gpointer buffer;
      gsize buflen;
      GCancellable *cancellable;

      g_debug ("Read %" G_GSSIZE_FORMAT " bytes of the file %s", bytes_read, read_data->filez_path);
      soup_message_body_append (read_data->msg->response_body,
                                SOUP_MEMORY_COPY,
                                read_data->buffer,
                                bytes_read);
      soup_server_unpause_message (server, read_data->msg);

      buffer = read_data->buffer;
      buflen = read_data->buflen;
      cancellable = read_data->server->cancellable;
      g_input_stream_read_async (stream,
                                 buffer,
                                 buflen,
                                 G_PRIORITY_DEFAULT,
                                 cancellable,
                                 filez_stream_read_chunk_cb,
                                 g_steal_pointer (&read_data));
      return;
    }
  g_debug ("Finished reading file %s", read_data->filez_path);
  soup_message_body_complete (read_data->msg->response_body);
  soup_server_unpause_message (server, read_data->msg);
}

static void
handle_objects_filez (EosUpdaterRepoServer *server,
                      SoupMessage *msg,
                      const gchar *requested_path)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GInputStream) stream = NULL;
  gpointer buffer;
  guint64 uncompressed_size;
  gsize buflen;
  g_autoptr(EosFilezReadData) read_data = NULL;

  checksum = get_checksum_from_filez (requested_path,
                                      &error);
  if (checksum == NULL)
    {
      g_debug ("Failed to get checksum of the filez object %s: %s", requested_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return;
    }
  message("Got checksum: %s", checksum);

  if (!load_compressed_file_stream (server->repo,
                                    checksum,
                                    server->cancellable,
                                    &stream,
                                    &uncompressed_size,
                                    &error))
    {
      g_debug ("Failed to get stream to the filez object %s: %s", requested_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return;
    }

  g_debug ("Sending %s", requested_path);
  soup_message_headers_set_encoding (msg->response_headers,
                                     SOUP_ENCODING_CHUNKED);
  soup_message_set_status (msg, SOUP_STATUS_OK);
  read_data = filez_read_data_new (server,
                                   MIN(2 * 1024 * 1024, uncompressed_size + 1),
                                   msg,
                                   requested_path);
  buffer = read_data->buffer;
  buflen = read_data->buflen;
  g_input_stream_read_async (stream,
                             buffer,
                             buflen,
                             G_PRIORITY_DEFAULT,
                             server->cancellable,
                             filez_stream_read_chunk_cb,
                             g_steal_pointer (&read_data));
  soup_server_pause_message (SOUP_SERVER (server),
                             msg);
  return;
}

static const gchar *const as_is_allowed_object_suffices[] =
  {
    ".commit",
    ".commitmeta",
    ".dirmeta",
    ".dirtree",
    ".sig",
    ".sizes2",
    NULL
  };

static gboolean
path_is_handled_as_is (const gchar *requested_path)
{
  guint idx;

  if (g_str_has_prefix (requested_path, "/objects/"))
    {
      for (idx = 0; as_is_allowed_object_suffices[idx]; ++idx)
        if (g_str_has_suffix (requested_path,
                              as_is_allowed_object_suffices[idx]))
          return TRUE;

      return FALSE;
    }

  if (g_str_has_prefix (requested_path, "/deltas/") ||
      g_str_has_prefix (requested_path, "/extensions/"))
    return TRUE;

  return FALSE;
}

static gboolean
serve_file_if_exists (SoupMessage *msg,
                      const gchar *root,
                      const gchar *raw_path,
                      GCancellable *cancellable,
                      gboolean *served)
{
  g_autoptr(GFile) path = g_file_new_for_path (raw_path);
  g_autoptr(GFile) root_path = g_file_new_for_path (root);
  g_autoptr(GMappedFile) mapping = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(SoupBuffer) buffer = NULL;

  /* Security check to ensure we don’t get tricked into serving files which
   * are outside the document root. This canonicalises the paths but does not
   * follow symlinks.
   *
   * FIXME: Do we also want to resolve symlinks to ensure a malicious symlink
   * inside the root can’t cause us to serve a file from outside the root
   * (for example, /etc/shadow)? */
  if (!g_file_has_prefix (path, root_path))
    {
      g_debug ("File ‘%s’ not within root ‘%s’", raw_path, root);
      *served = FALSE;
      return TRUE;
    }

  if (!g_file_query_exists (path, cancellable))
    {
      *served = FALSE;
      return TRUE;
    }

  mapping = g_mapped_file_new (raw_path, FALSE, &error);
  if (mapping == NULL)
    {
      g_debug ("Failed to map %s: %s", raw_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
      return FALSE;
    }

  g_debug ("Serving %s", raw_path);
  buffer = soup_buffer_new_with_owner (g_mapped_file_get_contents (mapping),
                                       g_mapped_file_get_length (mapping),
                                       g_mapped_file_ref (mapping),
                                       (GDestroyNotify)g_mapped_file_unref);
  soup_message_body_append_buffer (msg->response_body, buffer);
  soup_message_set_status (msg, SOUP_STATUS_OK);
  *served = TRUE;

  return TRUE;
}

static void
serve_file (SoupMessage *msg,
            const gchar *root,
            const gchar *raw_path,
            GCancellable *cancellable)
{
  gboolean served = FALSE;

  if (!serve_file_if_exists (msg, root, raw_path, cancellable, &served))
    return;

  if (!served)
    {
      g_debug ("File %s not found", raw_path);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    }
}

static void
handle_as_is (EosUpdaterRepoServer *server,
              SoupMessage *msg,
              const gchar *requested_path)
{
  g_autofree gchar *raw_path = g_build_filename (server->cached_repo_root, requested_path, NULL);

  serve_file (msg, server->cached_repo_root, raw_path, server->cancellable);
}

static SoupBuffer *
buffer_from_bytes (GBytes *bytes)
{
  gconstpointer raw;
  gsize len;

  raw = g_bytes_get_data (bytes, &len);
  return soup_buffer_new_with_owner (raw,
                                     len,
                                     g_bytes_ref (bytes),
                                     (GDestroyNotify)g_bytes_unref);
}

static void
send_bytes (SoupMessage *msg,
            GBytes *bytes)
{
  g_autoptr(SoupBuffer) buffer = buffer_from_bytes (bytes);

  soup_message_body_append_buffer (msg->response_body, buffer);
  soup_message_set_status (msg, SOUP_STATUS_OK);
}

static void
handle_config (EosUpdaterRepoServer *server,
               SoupMessage *msg)
{
  send_bytes (msg, server->cached_config);
}

static void
handle_refs_heads (EosUpdaterRepoServer *server,
                   SoupMessage *msg,
                   const gchar *requested_path)
{
  gsize prefix_len = strlen ("/refs/heads/");
  size_t len = strlen (requested_path);
  g_autofree gchar *raw_path = NULL;
  gboolean served = FALSE;
  const gchar *head;

  if (len <= prefix_len)
    {
      g_debug ("Invalid request for /refs/heads/");
      soup_message_set_status (msg, SOUP_STATUS_BAD_REQUEST);
      return;
    }

  /* Pass through requests to things like /refs/heads/ostree/1/1/0 if they
   * exist. */
  raw_path = g_build_filename (server->cached_repo_root, requested_path, NULL);
  if (!serve_file_if_exists (msg, server->cached_repo_root, raw_path, server->cancellable, &served))
    return;

  if (served)
    return;

  /* If not, this is probably a request for a head which is only available on
   * the server — and hence available in our repository as a remote ref.
   * Transparently redirect to /refs/remotes/$remote_name. For example, map
   * /refs/heads/os/eos/amd64/master to
   * /refs/remotes/eos/os/eos/amd64/master. */
  head = requested_path + prefix_len; /* e.g eos2/i386 */
  g_clear_pointer (&raw_path, g_free);
  raw_path = g_build_filename (server->cached_repo_root,
                               "refs",
                               "remotes",
                               server->remote_name,
                               head,
                               NULL);

  serve_file (msg, server->cached_repo_root, raw_path, server->cancellable);
}

static void
handle_path (EosUpdaterRepoServer *server,
             SoupMessage *msg,
             const gchar *path)
{
  if (g_cancellable_is_cancelled (server->cancellable))
    {
      soup_message_set_status (msg, SOUP_STATUS_SERVICE_UNAVAILABLE);
      return;
    }

  g_debug ("Requested %s", path);
  if (strstr (path, "..") != NULL)
    {
      soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);
      return;
    }

  if (g_str_has_prefix (path, "/objects/") && g_str_has_suffix (path, ".filez"))
    handle_objects_filez (server, msg, path);
  else if (path_is_handled_as_is (path))
    handle_as_is (server, msg, path);
  else if (g_strcmp0 (path, "/config") == 0)
    handle_config (server, msg);
  else if (g_str_has_prefix (path, "/refs/heads/"))
    handle_refs_heads (server, msg, path);
  else
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
}

static void
update_last_request_time (EosUpdaterRepoServer *server)
{
  server->last_request_time = g_get_monotonic_time ();
  g_object_notify_by_pspec (G_OBJECT (server), props[PROP_LAST_REQUEST_TIME]);
}

static void
server_cb (SoupServer *soup_server,
           SoupMessage *msg,
           const gchar *path,
           GHashTable *query,
           SoupClientContext *context,
           gpointer user_data)
{
  EosUpdaterRepoServer *server = EOS_UPDATER_REPO_SERVER (soup_server);
  guint status_code;

  handle_path (server, msg, path);

  g_object_get (msg,
                "status-code", &status_code,
                NULL);

  if (SOUP_STATUS_IS_SUCCESSFUL (status_code))
    update_last_request_time (server);
}

static gboolean
eos_updater_repo_server_initable_init (GInitable     *initable,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
  EosUpdaterRepoServer *server = EOS_UPDATER_REPO_SERVER (initable);

  if (!generate_faked_config (server->repo,
                              &server->cached_config,
                              error))
    return FALSE;

  g_set_object (&server->cancellable, cancellable);
  server->cached_repo_root = g_file_get_path (ostree_repo_get_path (server->repo));
  soup_server_add_handler (SOUP_SERVER (server),
                           NULL,
                           server_cb,
                           NULL,
                           NULL);
  return TRUE;
}

static void
eos_updater_repo_server_initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = eos_updater_repo_server_initable_init;
}

/**
 * eos_updater_repo_server_new:
 * @repo: A repo
 * @served_remote: The name of the remote
 * @cancellable: (nullable): A #GCancellable
 * @error: A location for an error
 *
 * Creates an #EosUpdaterRepoServer, which will serve the contents of
 * the @repo from the remote @served_remote.
 *
 * Returns: (transfer full): The server.
 */
EosUpdaterRepoServer *
eos_updater_repo_server_new (OstreeRepo *repo,
                             const gchar *served_remote,
                             GCancellable *cancellable,
                             GError **error)
{
  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);
  g_return_val_if_fail (served_remote != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_initable_new (EOS_UPDATER_TYPE_REPO_SERVER,
                         cancellable,
                         error,
                         "repo", repo,
                         "served-remote", served_remote,
                         NULL);
}

/**
 * eos_updater_repo_server_get_pending_requests:
 * @repo_server: The #EosUpdaterRepoServer
 *
 * Pending requests are usually requests for file objects that happen
 * asynchronously, mostly due to their larger size. Use this function
 * together with eos_updater_repo_server_get_last_request_time() if
 * you want to stop the server after the timeout.
 *
 * Returns: Number of pending remotes.
 */
guint
eos_updater_repo_server_get_pending_requests (EosUpdaterRepoServer *repo_server)
{
  return repo_server->pending_requests;
}

/**
 * eos_updater_repo_server_get_last_request_time:
 * @repo_server: The #EosUpdaterRepoServer
 *
 * The result of this function is basically a result of
 * g_get_monotonic_time() at the end request handler. Note that this
 * property is updated only when the request was valid (returned 2xx
 * HTTP status). Use this function together with
 * eos_updater_repo_server_get_pending_requests() if you want to stop
 * the server after the timeout.
 *
 * Returns: When was the last request handled
 */
gint64
eos_updater_repo_server_get_last_request_time (EosUpdaterRepoServer *repo_server)
{
  return repo_server->last_request_time;
}
