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

#include "eos-refcounted.h"
#include "eos-util.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include <libsoup/soup.h>

#include <ostree.h>

#include <systemd/sd-daemon.h>

#include <errno.h>
#include <string.h>

typedef struct
{
  guint16 local_port;
  gchar *raw_port_path;
  gint timeout_seconds;
  gchar *served_remote;
} Options;

#define OPTIONS_CLEARED { 0u, NULL, 0, NULL }

static gboolean
check_option_is (const gchar *option_name,
                 const gchar *long_name,
                 const gchar *short_name,
                 GError **error)
{
  if (g_strcmp0 (option_name, long_name) != 0 && g_strcmp0 (option_name, short_name) != 0)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Wrong option %s to parse, expected either %s or %s, should not happen",
                   option_name,
                   long_name,
                   short_name);
      return FALSE;
    }

  return TRUE;
}

static gboolean
local_port_goption (const gchar *option_name,
                    const gchar *value,
                    gpointer options_ptr,
                    GError **error)
{
  guint64 number;
  const gchar *endptr = NULL;
  int saved_errno;
  Options* options = options_ptr;

  if (!check_option_is (option_name, "--local-port", "-p", error))
    return FALSE;

  errno = 0;
  number = g_ascii_strtoull (value, (gchar **)&endptr, 10);
  saved_errno = errno;
  if (saved_errno != 0 || number == 0 || *endptr != '\0' || number > G_MAXUINT16)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Invalid port number %s", value);
      return FALSE;
    }
  options->local_port = number;
  return TRUE;
}

static gboolean
serve_remote_goption (const gchar *option_name,
                      const gchar *value,
                      gpointer options_ptr,
                      GError **error)
{
  Options* options = options_ptr;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *test_refspec = g_strdup_printf ("%s:test", value);

  if (!check_option_is (option_name, "--serve-remote", "-r", error))
    return FALSE;

  if (!ostree_parse_refspec (test_refspec, &remote, NULL, NULL) ||
      g_strcmp0 (value, remote) != 0)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Badly-formed remote name %s", value);
      return FALSE;
    }

  g_free (options->served_remote);
  options->served_remote = g_steal_pointer (&remote);
  return TRUE;
}

static gboolean
options_init (Options *options,
              int *argc,
              gchar ***argv,
              GError **error)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GOptionGroup) group = NULL;
  GOptionEntry entries[] = {
    { "local-port", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, local_port_goption, "Local port number (0 < N < 65536)", "N" },
    { "timeout", 't', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &options->timeout_seconds, "Time in seconds of inactivity allowed before quitting (zero or less means no timeout), default 5 seconds", "N" },
    { "serve-remote", 'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, serve_remote_goption, "Which remote should be served, default eos", "NAME" },
    { "port-file", 'f', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options->raw_port_path, "Where to write the port number, default NULL", "PATH" },
    { NULL }
  };

  context = g_option_context_new ("Endless OSTree server");
  group = g_option_group_new (NULL, NULL, NULL, options, NULL);
  g_option_group_add_entries (group, entries);
  g_option_context_set_main_group (context, g_steal_pointer (&group));

  memset (options, 0, sizeof (*options));
  options->timeout_seconds = 5;
  options->served_remote = g_strdup ("eos");

  return g_option_context_parse (context, argc, argv, error);
};

static void
options_clear (Options *options)
{
  options->local_port = 0;
  options->timeout_seconds = 0;
  g_clear_pointer (&options->served_remote, g_free);
  g_clear_pointer (&options->raw_port_path, g_free);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (Options, options_clear)

static void
clear_source (guint *id)
{
  if (id == NULL || *id == 0)
    return;
  g_source_remove (*id);
  *id = 0;
}

typedef struct
{
  OstreeRepo *repo;
  GMainLoop *loop;
  SoupServer *server;

  gint timeout_seconds;
  guint timeout_id;

  gchar *remote_name;

  gchar *cached_repo_root;
  GBytes *cached_config;

  guint async_requests_pending;
  EosQuitFile *quit_file;
} Data;

#define DATA_CLEARED { NULL, NULL, NULL, 0, 0u, NULL, NULL, NULL, 0, NULL }

static void
data_setup_timeout (Data *data);

static gboolean
timeout_cb (gpointer data_ptr)
{
  Data *data = data_ptr;

  if (data->async_requests_pending > 0)
    {
      message ("%u asynchronous requests pending, resetting timeout", data->async_requests_pending);
      data_setup_timeout (data);
    }
  else
    {
      message ("Timeout passed, quitting");
      g_main_loop_quit (data->loop);
      data->timeout_id = 0;
    }

  return G_SOURCE_REMOVE;
}

static void
data_setup_timeout (Data *data)
{
  clear_source (&data->timeout_id);
  if (data->timeout_seconds > 0)
    data->timeout_id = g_timeout_add_seconds (data->timeout_seconds, timeout_cb, data);
}

static GKeyFile *
key_file_copy (GKeyFile *key_file,
               GError **error)
{
  g_autoptr(GKeyFile) dup = NULL;
  gsize len = 0;
  g_autofree gchar *raw = NULL;

  raw = g_key_file_to_data (key_file, &len, error);
  if (raw == NULL)
    return NULL;

  dup = g_key_file_new ();
  if (!g_key_file_load_from_data (key_file,
                                  raw,
                                  len,
                                  G_KEY_FILE_NONE,
                                  error))
    return NULL;

  return g_steal_pointer (&dup);
}

static gboolean
data_generate_config (Data *data,
                      GError **error)
{
  g_autoptr(GKeyFile) config = key_file_copy (ostree_repo_get_config (data->repo),
                                              error);
  g_autofree gchar *raw = NULL;
  gsize len = 0;

  if (config == NULL)
    {
      message ("Failed to copy repository config");
      return FALSE;
    }
  g_key_file_set_string (config, "core", "mode", "archive-z2");
  raw = g_key_file_to_data (config, &len, error);
  if (raw == NULL)
    {
      message ("Failed to get raw contents of modified repository config");
      return FALSE;
    }

  data->cached_config = g_bytes_new_take (g_steal_pointer (&raw), len);
  return TRUE;
}

static gchar *
quit_file_name (void)
{
  return eos_updater_dup_envvar_or ("EOS_UPDATER_TEST_UPDATE_SERVER_QUIT_FILE",
                                     NULL);
}

static EosQuitFileCheckResult
check_and_quit (gpointer data_ptr)
{
  Data *data = data_ptr;

  if (data->async_requests_pending > 0)
    return EOS_QUIT_FILE_KEEP_CHECKING;

  g_main_loop_quit (data->loop);
  return EOS_QUIT_FILE_QUIT;
}

static gboolean
maybe_setup_quit_file (Data *data,
                       GError **error)
{
  g_autofree gchar *filename = quit_file_name ();
  g_autoptr(EosQuitFile) quit_file = NULL;

  if (filename == NULL)
    return TRUE;

  quit_file = eos_updater_setup_quit_file (filename,
                                           check_and_quit,
                                           data,
                                           NULL,
                                           5,
                                           error);
  if (quit_file == NULL)
    return FALSE;

  data->quit_file = g_steal_pointer (&quit_file);
  return TRUE;
}

static gboolean
data_init (Data *data,
           Options *options,
           SoupServer *server,
           GError **error)
{
  GFile *repo_path;
  g_autoptr(GFile) path = NULL;
  g_autoptr(GFile) sig_path = NULL;

  memset (data, 0, sizeof (*data));
  data->repo = eos_updater_local_repo ();
  data->loop = g_main_loop_new (NULL, FALSE);
  data->server = g_object_ref (server);
  data->timeout_seconds = options->timeout_seconds;
  data->remote_name = g_strdup (options->served_remote);

  repo_path = ostree_repo_get_path (data->repo);
  data->cached_repo_root = g_file_get_path (repo_path);

  if (!data_generate_config (data, error))
    return FALSE;

  data_setup_timeout (data);
  if (!maybe_setup_quit_file (data, error))
    return FALSE;

  return TRUE;
}

static void
data_clear (Data *data)
{
  g_clear_object (&data->quit_file);
  data->async_requests_pending = 0;
  g_clear_pointer (&data->cached_config, g_bytes_unref);
  g_clear_pointer (&data->cached_repo_root, g_free);
  g_clear_pointer (&data->remote_name, g_free);
  clear_source (&data->timeout_id);
  data->timeout_seconds = 0;
  g_clear_object (&data->server);
  g_clear_pointer (&data->loop, g_main_loop_unref);
  g_clear_object (&data->repo);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (Data, data_clear)

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

static gboolean
send_bytes (SoupMessage *msg,
            GBytes *bytes)
{
  g_autoptr(SoupBuffer) buffer = buffer_from_bytes (bytes);

  soup_message_body_append_buffer (msg->response_body, buffer);
  soup_message_set_status (msg, SOUP_STATUS_OK);

  return TRUE;
}

static gboolean
handle_config (SoupMessage *msg,
               Data *data)
{
  return send_bytes (msg, data->cached_config);
}

static gboolean
serve_file_if_exists (SoupMessage *msg,
                      const gchar *raw_path,
                      gboolean *served)
{
  g_autoptr(GFile) path = g_file_new_for_path (raw_path);
  g_autoptr(GMappedFile) mapping = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(SoupBuffer) buffer = NULL;

  if (!g_file_query_exists (path, NULL))
    {
      *served = FALSE;
      return TRUE;
    }

  mapping = g_mapped_file_new (raw_path, FALSE, &error);
  if (mapping == NULL)
    {
      message ("Failed to map %s: %s", raw_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
      return FALSE;
    }

  message ("Serving %s", raw_path);
  buffer = soup_buffer_new_with_owner (g_mapped_file_get_contents (mapping),
                                       g_mapped_file_get_length (mapping),
                                       g_mapped_file_ref (mapping),
                                       (GDestroyNotify)g_mapped_file_unref);
  soup_message_body_append_buffer (msg->response_body, buffer);
  soup_message_set_status (msg, SOUP_STATUS_OK);
  *served = TRUE;

  return TRUE;
}

static gboolean
serve_file (SoupMessage *msg,
            const gchar *raw_path)
{
  gboolean served = FALSE;

  if (!serve_file_if_exists (msg, raw_path, &served))
    return FALSE;

  if (!served)
    {
      message ("File %s not found", raw_path);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    }

  return served;
}

static gboolean
handle_refs_heads (SoupMessage *msg,
                   Data *data,
                   const gchar *requested_path)
{
  gsize prefix_len = strlen ("/refs/heads/");
  size_t len = strlen (requested_path);
  g_autofree gchar *raw_path = NULL;
  gboolean served = FALSE;
  const gchar *head;

  if (len <= prefix_len)
    {
      message ("Invalid request for /refs/heads/");
      soup_message_set_status (msg, SOUP_STATUS_BAD_REQUEST);
      return FALSE;
    }

  raw_path = g_build_filename (data->cached_repo_root, requested_path, NULL);
  if (!serve_file_if_exists (msg, raw_path, &served))
    return FALSE;

  if (served)
    return TRUE;

  head = requested_path + prefix_len; /* e.g eos2/i386 */
  g_clear_pointer (&raw_path, g_free);
  raw_path = g_build_filename (data->cached_repo_root,
                               "refs",
                               "remotes",
                               data->remote_name,
                               head,
                               NULL);

  return serve_file (msg, raw_path);
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
  g_autofree char *first_two = NULL;
  g_autofree char *rest = NULL;
  g_autofree char *checksum = NULL;

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

#define EOS_TYPE_FILEZ_READ_DATA eos_filez_read_data_get_type ()
EOS_DECLARE_REFCOUNTED (EosFilezReadData,
                        eos_filez_read_data,
                        EOS,
                        FILEZ_READ_DATA)

struct _EosFilezReadData
{
  GObject parent_instance;

  Data *data;
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
    {
      gulong id = read_data->finished_signal_id;

      read_data->finished_signal_id = 0;
      g_signal_handler_disconnect (read_data->msg, id);
    }
  g_clear_object (&read_data->msg);
}

static void
eos_filez_read_data_dispose_impl (EosFilezReadData *read_data)
{
  eos_filez_read_data_disconnect_and_clear_msg (read_data);
}

static void
eos_filez_read_data_finalize_impl (EosFilezReadData *read_data)
{
  --read_data->data->async_requests_pending;
  g_free (read_data->buffer);
  g_free (read_data->filez_path);
}

EOS_DEFINE_REFCOUNTED (EOS_FILEZ_READ_DATA,
                       EosFilezReadData,
                       eos_filez_read_data,
                       eos_filez_read_data_dispose_impl,
                       eos_filez_read_data_finalize_impl)

static void
filez_read_data_finished_cb (SoupMessage *msg, gpointer read_data_ptr)
{
  EosFilezReadData *read_data = EOS_FILEZ_READ_DATA (read_data_ptr);

  message ("Downloading %s cancelled by client", read_data->filez_path);
  eos_filez_read_data_disconnect_and_clear_msg (read_data);
}

static EosFilezReadData *
filez_read_data_new (Data *data,
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
  read_data->data = data;
  read_data->buffer = g_malloc (buflen);
  read_data->buflen = buflen;
  read_data->msg = g_object_ref (msg);
  read_data->filez_path = g_strdup (filez_path);
  read_data->finished_signal_id = g_signal_connect (msg, "finished", G_CALLBACK (filez_read_data_finished_cb), read_data);

  ++read_data->data->async_requests_pending;

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

  if (read_data->msg == NULL)
    /* got cancelled */
    return;

  if (bytes_read < 0)
    {
      message ("Failed to read the file %s: %s", read_data->filez_path, error->message);
      soup_message_set_status (read_data->msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
      soup_message_body_complete (read_data->msg->response_body);
      soup_server_unpause_message (read_data->data->server, read_data->msg);
      return;
    }
  if (bytes_read > 0)
    {
      gpointer buffer;
      gsize buflen;

      message ("Read %" G_GSSIZE_FORMAT " bytes of the file %s", bytes_read, read_data->filez_path);
      soup_message_body_append (read_data->msg->response_body,
                                SOUP_MEMORY_COPY,
                                read_data->buffer,
                                bytes_read);
      soup_server_unpause_message (read_data->data->server, read_data->msg);

      buffer = read_data->buffer;
      buflen = read_data->buflen;
      g_input_stream_read_async (stream,
                                 buffer,
                                 buflen,
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 filez_stream_read_chunk_cb,
                                 g_steal_pointer (&read_data));
      return;
    }
  message ("Finished reading file %s", read_data->filez_path);
  soup_message_body_complete (read_data->msg->response_body);
  soup_server_unpause_message (read_data->data->server, read_data->msg);
}

static gboolean
load_compressed_file_stream (OstreeRepo *repo,
                             const gchar *checksum,
                             GInputStream **out_input,
                             guint64 *uncompressed_size,
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
                              NULL,
                              error))
    return FALSE;

  if (!ostree_raw_file_to_archive_z2_stream (bare,
                                             info,
                                             xattrs,
                                             &content,
                                             NULL,
                                             error))
      return FALSE;

  *out_input = g_steal_pointer (&content);
  *uncompressed_size = g_file_info_get_size (info);
  return TRUE;
}


static gboolean
handle_objects_filez (SoupMessage *msg,
                      Data *data,
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
      message ("Failed to get checksum of the filez object %s: %s", requested_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return FALSE;
    }
  message("Got checksum: %s", checksum);

  if (!load_compressed_file_stream (data->repo,
                                    checksum,
                                    &stream,
                                    &uncompressed_size,
                                    &error))
    {
      message ("Failed to get stream to the filez object %s: %s", requested_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return FALSE;
    }

  message ("Sending %s", requested_path);
  soup_message_headers_set_encoding (msg->response_headers,
                                     SOUP_ENCODING_CHUNKED);
  soup_message_set_status (msg, SOUP_STATUS_OK);
  read_data = filez_read_data_new (data, MIN(2 * 1024 * 1024, uncompressed_size + 1), msg, requested_path);
  buffer = read_data->buffer;
  buflen = read_data->buflen;
  g_input_stream_read_async (stream,
                             buffer,
                             buflen,
                             G_PRIORITY_DEFAULT,
                             NULL,
                             filez_stream_read_chunk_cb,
                             g_steal_pointer (&read_data));
  soup_server_pause_message (data->server, msg);
  return TRUE;
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
handle_as_is (SoupMessage *msg,
              Data *data,
              const gchar *requested_path)
{
  g_autofree gchar *raw_path = g_build_filename (data->cached_repo_root, requested_path, NULL);

  return serve_file (msg, raw_path);
}

static gboolean
handle_path (SoupMessage *msg,
             Data *data,
             const gchar *requested_path)
{
  if (strstr (requested_path, "..") != NULL)
    {
      soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);

      return FALSE;
    }

  if (g_str_has_prefix (requested_path, "/objects/") && g_str_has_suffix (requested_path, ".filez"))
    return handle_objects_filez (msg, data, requested_path);

  if (path_is_handled_as_is (requested_path))
    return handle_as_is (msg, data, requested_path);

  if (g_strcmp0 (requested_path, "/config") == 0)
    return handle_config (msg, data);

  if (g_str_has_prefix (requested_path, "/refs/heads/"))
    return handle_refs_heads (msg, data, requested_path);

  soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);

  return FALSE;
}

static void
server_cb (SoupServer *server,
           SoupMessage *msg,
           const gchar *path,
           GHashTable *query,
           SoupClientContext *context,
           gpointer data_ptr)
{
  Data *data = data_ptr;
  gboolean reset_timeout = FALSE;

  message ("Requested %s", path);
  reset_timeout = handle_path (msg, data, path);

  if (reset_timeout)
    data_setup_timeout (data);
}

static gboolean
listen_local (SoupServer *server,
              Options *options,
              GError **error)
{
  if (!soup_server_listen_local (server,
                                 options->local_port,
                                 0,
                                 error))
    return FALSE;

  if (options->raw_port_path != NULL)
    {
      g_autoptr(SoupURI) uri = NULL;
      g_autoptr(GFile) file = NULL;
      g_autofree gchar *contents = NULL;

      if (!get_first_uri_from_server (server, &uri, error))
        return FALSE;

      file = g_file_new_for_path (options->raw_port_path);
      contents = g_strdup_printf ("%u", soup_uri_get_port (uri));
      if (!g_file_replace_contents (file,
                                    contents,
                                    strlen (contents),
                                    NULL, /* no etag */
                                    FALSE, /* no backup */
                                    G_FILE_CREATE_NONE,
                                    NULL, /* no new etag */
                                    NULL, /* no cancellable */
                                    error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
start_listening (SoupServer *server,
                 Options *options,
                 GError **error)
{
  int result;

  if (options->local_port > 0 || options->raw_port_path)
    return listen_local (server, options, error);

  result = sd_listen_fds (1);
  if (result < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to get listen sockets count from systemd: %s", g_strerror (errno));
      return FALSE;
    }
  if (result == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Received zero listen sockets from system");
      return FALSE;
    }
  if (result > 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected only one listen socket from systemd, got %d", result);
      return FALSE;
    }
  return soup_server_listen_fd (server, SD_LISTEN_FDS_START, 0, error);
}

int
main (int argc, char **argv)
{
  g_autoptr(GError) error = NULL;
  g_auto(Options) options = OPTIONS_CLEARED;
  g_autoptr(SoupServer) server = NULL;
  g_auto(Data) data = DATA_CLEARED;

  if (!options_init (&options, &argc, &argv, &error))
    {
      message ("Failed to initialize options: %s", error->message);
      return 1;
    }

  server = soup_server_new (SOUP_SERVER_SERVER_HEADER, "eos-httpd", NULL);
  if (!start_listening (server, &options, &error))
    {
      message ("Failed to listen: %s", error->message);
      return 1;
    }

  if (!data_init (&data, &options, server, &error))
    {
      message ("Failed to initialize data: %s", error->message);
      return 1;
    }
  soup_server_add_handler (server, NULL, server_cb, &data, NULL);

  g_main_loop_run (data.loop);

  return 0;
}
