/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
 * Copyright © 2017 Endless Mobile, Inc.
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <libeos-update-server/repo.h>
#include <libeos-updater-util/util.h>

#include <string.h>

/**
 * SECTION:repo
 * @title: Bare repository server
 * @short_description: Server for the EOS bare OSTree repository
 * @include: libeos-update-server/repo.h
 *
 * A server that sits on top of the bare repository and lies to
 * clients about the repositories' mode, so it is possible to do pulls
 * from this repository.
 *
 * It currently only supports version 1 of the repository format
 * (`repo_version=1` in the configuration file).
 */

/**
 * EusRepo:
 *
 * A server which handles serving a single #OstreeRepo at a specified path.
 */
struct _EusRepo
{
  GObject parent_instance;

  SoupServer *server;  /* (nullable), owned*/
  OstreeRepo *repo;
  gchar *root_path;  /* (not nullable) if non-empty, must start with ‘/’ and have no trailing ‘/’ */
  gchar *remote_name;
  GCancellable *cancellable;
  gchar *cached_repo_root;
  GBytes *cached_config;
};

static void eus_repo_initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (EusRepo, eus_repo, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                eus_repo_initable_iface_init))

typedef enum
{
  PROP_SERVER = 1,
  PROP_REPO,
  PROP_ROOT_PATH,
  PROP_SERVED_REMOTE,
} EusRepoProperty;

static GParamSpec *props[PROP_SERVED_REMOTE + 1] = { NULL, };

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
  g_autoptr(GError) local_error = NULL;

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

  raw = g_key_file_to_data (config, &len, &local_error);
  if (raw == NULL)
    {
      g_warning ("Failed to get raw contents of modified repository config: %s",
                 local_error->message);
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  *out_faked_config_contents = g_bytes_new_take (g_steal_pointer (&raw), len);
  return TRUE;
}

static void
eus_repo_init (EusRepo *self)
{
  self->cancellable = g_cancellable_new ();
}

static void
eus_repo_get_property (GObject    *object,
                       guint       property_id,
                       GValue     *value,
                       GParamSpec *spec)
{
  EusRepo *self = EUS_REPO (object);

  switch ((EusRepoProperty) property_id)
    {
    case PROP_SERVER:
      g_value_set_object (value, self->server);
      break;

    case PROP_REPO:
      g_value_set_object (value, self->repo);
      break;

    case PROP_ROOT_PATH:
      g_value_set_string (value, self->root_path);
      break;

    case PROP_SERVED_REMOTE:
      g_value_set_string (value, self->remote_name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
eus_repo_set_property (GObject      *object,
                       guint         property_id,
                       const GValue *value,
                       GParamSpec   *spec)
{
  EusRepo *self = EUS_REPO (object);

  switch ((EusRepoProperty) property_id)
    {
    case PROP_REPO:
      g_set_object (&self->repo, g_value_get_object (value));
      break;

    case PROP_ROOT_PATH:
      g_clear_pointer (&self->root_path, g_free);
      self->root_path = g_value_dup_string (value);

      g_assert (self->root_path != NULL);

      /* Add a missing leading slash if the root path is non-empty. */
      if (self->root_path[0] != '\0' && self->root_path[0] != '/')
        {
          g_autofree gchar *tmp = g_strconcat ("/", self->root_path, NULL);
          g_free (self->root_path);
          self->root_path = g_steal_pointer (&tmp);
        }

      /* Drop any trailing slash. */
      if (strlen (self->root_path) > 1 &&
          self->root_path[strlen (self->root_path) - 1] == '/')
        self->root_path[strlen (self->root_path) - 1] = '\0';

      break;

    case PROP_SERVED_REMOTE:
      self->remote_name = g_value_dup_string (value);
      break;

    case PROP_SERVER:
      /* Read only. */

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
      break;
    }
}

static void
eus_repo_dispose (GObject *object)
{
  EusRepo *self = EUS_REPO (object);

  eus_repo_disconnect (self);

  g_clear_object (&self->cancellable);
  g_clear_pointer (&self->cached_config, g_bytes_unref);
  g_clear_object (&self->repo);
  g_clear_object (&self->server);

  G_OBJECT_CLASS (eus_repo_parent_class)->dispose (object);
}

static void
eus_repo_finalize (GObject *object)
{
  EusRepo *self = EUS_REPO (object);

  g_free (self->cached_repo_root);
  g_free (self->remote_name);
  g_free (self->root_path);

  G_OBJECT_CLASS (eus_repo_parent_class)->finalize (object);
}

static void
eus_repo_class_init (EusRepoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = eus_repo_dispose;
  object_class->finalize = eus_repo_finalize;
  object_class->get_property = eus_repo_get_property;
  object_class->set_property = eus_repo_set_property;

  /**
   * EusRepo:server:
   *
   * The #SoupServer to handle requests from.
   *
   * Since: UNRELEASED
   */
  props[PROP_SERVER] = g_param_spec_object ("server",
                                            "Server",
                                            "The #SoupServer to handle requests from.",
                                            SOUP_TYPE_SERVER,
                                            G_PARAM_READABLE |
                                            G_PARAM_STATIC_STRINGS);

  /**
   * EusRepo:repo:
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
   * EusRepo:root-path:
   *
   * Root path to handle requests underneath. Any requests for paths not
   * underneath this root will result in a HTTP 404 status code.
   *
   * It should be the empty string, or a string starting with a `/` and not
   * ending in a `/`.
   *
   * Since: UNRELEASED
   */
  props[PROP_ROOT_PATH] = g_param_spec_string ("root-path",
                                               "Root Path",
                                               "Root path to handle requests underneath.",
                                               "",
                                               G_PARAM_READWRITE |
                                               G_PARAM_CONSTRUCT_ONLY |
                                               G_PARAM_STATIC_STRINGS);

  /**
   * EusRepo:served-remote:
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

  g_object_class_install_properties (object_class,
                                     G_N_ELEMENTS (props),
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
                             goffset *out_uncompressed_size,
                             GError **error)
{
  g_autoptr(GInputStream) bare = NULL;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GVariant) xattrs = NULL;
  g_autoptr(GInputStream) content = NULL;
  g_auto(GVariantBuilder) builder = { { { 0, } } };
  g_autoptr(GVariant) options = NULL;

  if (!ostree_repo_load_file (repo,
                              checksum,
                              &bare,
                              &info,
                              &xattrs,
                              cancellable,
                              error))
    return FALSE;

  /* Use compression level 2 (the maximum is 9) as a balance between CPU usage
   * and compression attained. This gives fairly low CPU usage (a third of
   * what’s needed for level 9) while halving the size of the uncompressed
   * files. */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{s@v}", "compression-level",
                         g_variant_new_variant (g_variant_new_int32 (2)));
  options = g_variant_ref_sink (g_variant_builder_end (&builder));

  if (!ostree_raw_file_to_archive_z2_stream_with_options (bare,
                                                          info,
                                                          xattrs,
                                                          options,
                                                          &content,
                                                          cancellable,
                                                          error))
    return FALSE;

  *out_input = g_steal_pointer (&content);
  *out_uncompressed_size = g_file_info_get_size (info);
  return TRUE;
}

#define EOS_TYPE_FILEZ_READ_DATA eos_filez_read_data_get_type ()
G_DECLARE_FINAL_TYPE (EosFilezReadData,
                      eos_filez_read_data,
                      EOS,
                      FILEZ_READ_DATA,
                      GObject)

struct _EosFilezReadData
{
  GObject parent_instance;

  EusRepo *server_repo;
  gpointer buffer;
  gsize buflen;
  SoupMessage *msg;
  gchar *filez_path;

  gulong finished_signal_id;
};

static void
eos_filez_read_data_disconnect_and_clear_msg (EosFilezReadData *read_data)
{
  g_return_if_fail (EOS_IS_FILEZ_READ_DATA (read_data));

  if (read_data->finished_signal_id > 0)
    g_signal_handler_disconnect (read_data->msg, read_data->finished_signal_id);
  read_data->finished_signal_id = 0;
  g_clear_object (&read_data->msg);
  g_clear_object (&read_data->server_repo);
}

static void
eos_filez_read_data_dispose_impl (EosFilezReadData *read_data)
{
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
filez_read_data_new (EusRepo     *self,
                     gsize        buflen,
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
  read_data->server_repo = g_object_ref (self);
  read_data->buffer = g_malloc (buflen);
  read_data->buflen = buflen;
  read_data->msg = g_object_ref (msg);
  read_data->filez_path = g_strdup (filez_path);
  read_data->finished_signal_id = g_signal_connect (msg, "finished", G_CALLBACK (filez_read_data_finished_cb), read_data);

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
  EusRepo *self;

  if (read_data->msg == NULL)
    /* got cancelled */
    return;

  self = read_data->server_repo;
  if (bytes_read < 0)
    {
      g_warning ("Failed to read the file %s: %s", read_data->filez_path, error->message);
      soup_message_set_status (read_data->msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
      soup_message_body_complete (read_data->msg->response_body);
      soup_server_unpause_message (self->server, read_data->msg);
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
                                (gsize) bytes_read);
      soup_server_unpause_message (self->server, read_data->msg);

      buffer = read_data->buffer;
      buflen = read_data->buflen;
      cancellable = self->cancellable;
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
  soup_server_unpause_message (self->server, read_data->msg);
}

static void
handle_objects_filez (EusRepo     *self,
                      SoupMessage *msg,
                      const gchar *requested_path)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GInputStream) stream = NULL;
  gpointer buffer;
  goffset uncompressed_size;
  gsize buflen;
  g_autoptr(EosFilezReadData) read_data = NULL;

  checksum = get_checksum_from_filez (requested_path,
                                      &error);
  if (checksum == NULL)
    {
      g_warning ("Failed to get checksum of the filez object %s: %s", requested_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return;
    }
  g_debug ("Got checksum: %s", checksum);

  if (!load_compressed_file_stream (self->repo,
                                    checksum,
                                    self->cancellable,
                                    &stream,
                                    &uncompressed_size,
                                    &error))
    {
      g_warning ("Failed to get stream to the filez object %s: %s", requested_path, error->message);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return;
    }

  g_debug ("Sending %s", requested_path);
  soup_message_headers_set_encoding (msg->response_headers,
                                     SOUP_ENCODING_CHUNKED);
  soup_message_set_status (msg, SOUP_STATUS_OK);
  read_data = filez_read_data_new (self,
                                   MIN (2 * 1024 * 1024, (gsize) (uncompressed_size + 1)),
                                   msg,
                                   requested_path);
  buffer = read_data->buffer;
  buflen = read_data->buflen;
  g_input_stream_read_async (stream,
                             buffer,
                             buflen,
                             G_PRIORITY_DEFAULT,
                             self->cancellable,
                             filez_stream_read_chunk_cb,
                             g_steal_pointer (&read_data));
  soup_server_pause_message (self->server, msg);
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
path_is_summary (const gchar *path)
{
  if (g_str_equal (path, "/summary") ||
      g_str_equal (path, "/summary.sig"))
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
  g_autoptr(GBytes) file_bytes = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(SoupBuffer) buffer = NULL;
  GFileType file_type;

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

  /* Check it’s actually a file. If not, return a 404 in the absence of support
   * for directory listings or anything else useful. Follow symlinks when
   * querying. */
  file_type = g_file_query_file_type (path, G_FILE_QUERY_INFO_NONE, cancellable);
  if (file_type != G_FILE_TYPE_REGULAR)
    {
      g_debug ("File ‘%s’ has type %u, not a regular file", raw_path, file_type);
      *served = FALSE;
      return TRUE;
    }

  mapping = g_mapped_file_new (raw_path, FALSE, &error);
  if (mapping != NULL)
    file_bytes = g_mapped_file_get_bytes (mapping);
  else
    {
      g_autofree guint8 *contents = NULL;
      gsize contents_len = 0;

      /* mmap() can legitimately fail if the underlying file system doesn’t
       * support it, which can happen if we’re using an overlayfs. Fall back to
       * reading in the file. */
      g_clear_error (&error);
      if (!g_file_get_contents (raw_path, (gchar **) &contents, &contents_len, &error))
        {
          g_warning ("Failed to load ‘%s’: %s", raw_path, error->message);
          soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
          return FALSE;
        }

      file_bytes = g_bytes_new_take (g_steal_pointer (&contents), contents_len);
      contents_len = 0;
    }

  g_debug ("Serving %s", raw_path);
  buffer = soup_buffer_new_with_owner (g_bytes_get_data (file_bytes, NULL),
                                       g_bytes_get_size (file_bytes),
                                       g_bytes_ref (file_bytes),
                                       (GDestroyNotify) g_bytes_unref);
  if (buffer->length > 0)
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
handle_as_is (EusRepo     *self,
              SoupMessage *msg,
              const gchar *requested_path)
{
  g_autofree gchar *raw_path = g_build_filename (self->cached_repo_root, requested_path, NULL);

  serve_file (msg, self->cached_repo_root, raw_path, self->cancellable);
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

  if (buffer->length > 0)
    soup_message_body_append_buffer (msg->response_body, buffer);
  soup_message_set_status (msg, SOUP_STATUS_OK);
}

static void
handle_config (EusRepo     *self,
               SoupMessage *msg)
{
  send_bytes (msg, self->cached_config);
}

static void
handle_summary (EusRepo     *self,
                SoupMessage *msg,
                const gchar *requested_path)
{
  g_autofree gchar *raw_path = g_build_filename (self->cached_repo_root, requested_path, NULL);
  gboolean served = FALSE;
  g_autoptr(GError) local_error = NULL;

  if (!serve_file_if_exists (msg,
                             self->cached_repo_root,
                             raw_path,
                             self->cancellable,
                             &served))
    return;
  if (served)
    return;

  /* Regenerate the summary since it doesn’t exist. */
  if (!ostree_repo_regenerate_summary (self->repo, NULL, self->cancellable, &local_error))
    {
      g_debug ("Error regenerating summary: %s", local_error->message);
      g_clear_error (&local_error);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return;
    }

  serve_file (msg, self->cached_repo_root, raw_path, self->cancellable);
}

static void
handle_refs_heads (EusRepo     *self,
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
  raw_path = g_build_filename (self->cached_repo_root, requested_path, NULL);
  if (!serve_file_if_exists (msg, self->cached_repo_root, raw_path, self->cancellable, &served))
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
  raw_path = g_build_filename (self->cached_repo_root,
                               "refs",
                               "remotes",
                               self->remote_name,
                               head,
                               NULL);

  serve_file (msg, self->cached_repo_root, raw_path, self->cancellable);
}

static void
handle_path (EusRepo     *self,
             SoupMessage *msg,
             const gchar *path)
{
  if (g_cancellable_is_cancelled (self->cancellable))
    {
      soup_message_set_status (msg, SOUP_STATUS_SERVICE_UNAVAILABLE);
      return;
    }

  g_debug ("Requested %s", path);

  /* Strip the server root path. */
  if (g_str_has_prefix (path, self->root_path))
    {
      path += strlen (self->root_path);
    }
  else
    {
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      goto out;
    }

  if (strstr (path, "..") != NULL)
    soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);
  else if (g_str_has_prefix (path, "/objects/") && g_str_has_suffix (path, ".filez"))
    handle_objects_filez (self, msg, path);
  else if (path_is_handled_as_is (path))
    handle_as_is (self, msg, path);
  else if (g_str_equal (path, "/config"))
    handle_config (self, msg);
  else if (path_is_summary (path))
    handle_summary (self, msg, path);
  else if (g_str_has_prefix (path, "/refs/heads/"))
    handle_refs_heads (self, msg, path);
  else
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);

out:
  g_debug ("Returning status %u (%s)", msg->status_code, msg->reason_phrase);
}

static void
server_cb (SoupServer *soup_server,
           SoupMessage *msg,
           const gchar *path,
           GHashTable *query,
           SoupClientContext *context,
           gpointer user_data)
{
  EusRepo *self = EUS_REPO (user_data);

  handle_path (self, msg, path);
}

static gboolean
eus_repo_initable_init (GInitable     *initable,
                        GCancellable  *cancellable,
                        GError       **error)
{
  EusRepo *self = EUS_REPO (initable);

  if (!generate_faked_config (self->repo,
                              &self->cached_config,
                              error))
    return FALSE;

  self->cached_repo_root = g_file_get_path (ostree_repo_get_path (self->repo));

  return TRUE;
}

static void
eus_repo_initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = eus_repo_initable_init;
}

/**
 * eus_repo_new:
 * @server: #SoupServer to handle requests from
 * @repo: A repo
 * @root_path: Root path to serve underneath
 * @served_remote: The name of the remote
 * @cancellable: (nullable): A #GCancellable
 * @error: A location for an error
 *
 * Creates an #EusRepo, which will serve the contents of
 * the @repo from the remote @served_remote.
 *
 * Returns: (transfer full): The server.
 */
EusRepo *
eus_repo_new (OstreeRepo    *repo,
              const gchar   *root_path,
              const gchar   *served_remote,
              GCancellable  *cancellable,
              GError       **error)
{
  g_return_val_if_fail (OSTREE_IS_REPO (repo), NULL);
  g_return_val_if_fail (served_remote != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_initable_new (EUS_TYPE_REPO, cancellable, error,
                         "repo", repo,
                         "root-path", root_path,
                         "served-remote", served_remote,
                         NULL);
}

/**
 * eus_repo_connect:
 * @self: an #EusRepo
 * @server: #SoupServer to handle requests from
 *
 * Connect this #EusRepo to the @server and start handling incoming requests
 * underneath its #EusRepo:root-path.
 *
 * To stop handling requests, call eus_repo_disconnect(). It is an error to
 * call eus_repo_connect() twice in a row without calling eus_repo_disconnect()
 * inbetween.
 *
 * Since: UNRELEASED
 */
void
eus_repo_connect (EusRepo    *self,
                  SoupServer *server)
{
  g_return_if_fail (EUS_IS_REPO (self));
  g_return_if_fail (SOUP_IS_SERVER (server));
  g_return_if_fail (self->server == NULL);

  self->server = g_object_ref (server);

  soup_server_add_handler (self->server,
                           self->root_path,
                           server_cb,
                           self,
                           NULL);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SERVER]);
}

/**
 * eus_repo_disconnect:
 * @self: an #EusRepo
 *
 * Disconnect this #EusRepo from the #SoupServer it was connected to by calling
 * eus_repo_connect().
 *
 * This is called automatically if the #EusRepo is disposed.
 *
 * Since: UNRELEASED
 */
void
eus_repo_disconnect (EusRepo *self)
{
  g_return_if_fail (EUS_IS_REPO (self));

  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);

  if (self->server != NULL)
    soup_server_remove_handler (self->server, self->root_path);

  g_clear_object (&self->server);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SERVER]);
}
