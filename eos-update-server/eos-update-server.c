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

#include <libeos-update-server/config.h>
#include <libeos-update-server/repo.h>
#include <libeos-update-server/server.h>
#include <libeos-updater-util/config.h>
#include <libeos-updater-util/refcounted.h>
#include <libeos-updater-util/util.h>

#include <ostree.h>

#include <libsoup/soup.h>
#include <locale.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <stdlib.h>
#include <systemd/sd-daemon.h>

#include <errno.h>
#include <string.h>

typedef struct
{
  guint16 local_port;
  gchar *raw_port_path;
  gint timeout_seconds;
  gchar *served_remote;
  gchar *config_file;
} Options;

#define OPTIONS_CLEARED { 0u, NULL, 0, NULL, NULL }

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
    { "timeout", 't', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &options->timeout_seconds, "Number of seconds of inactivity allowed before exiting (default: 200 seconds; ≤0 means no timeout)", "SECONDS" },
    { "serve-remote", 'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, serve_remote_goption, "Name of the remote to serve (default: eos)", "REMOTE-NAME" },
    { "port-file", 'f', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options->raw_port_path, "File to write the port number in (default: do not write the port number)", "PATH" },
    { "config-file", 'c',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &options->config_file,
      "Configuration file to use (default: "
      SYSCONFDIR "/" PACKAGE "/eos-update-server.conf" ")", "PATH" },
    { NULL }
  };

  context = g_option_context_new ("— Endless OS OSTree Server");
  group = g_option_group_new (NULL, NULL, NULL, options, NULL);
  g_option_group_add_entries (group, entries);
  g_option_context_set_main_group (context, g_steal_pointer (&group));
  g_option_context_set_summary (context,
                                "Serve the local OSTree repository to other "
                                "computers on the network, to allow Endless OS "
                                "updates to be shared between computers.");

  memset (options, 0, sizeof (*options));
  options->timeout_seconds = 200;
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
  g_clear_pointer (&options->config_file, g_free);
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
  GMainLoop *loop;
  EusServer *server;

  gint timeout_seconds;
  guint timeout_id;

  EosQuitFile *quit_file;
  gint quit_file_timeout_seconds;
} TimeoutData;

#define TIMEOUT_DATA_CLEARED { NULL, NULL, 0, 0u, NULL, 0 }

static void
timeout_data_setup_timeout (TimeoutData *data);

static gboolean
no_requests_timeout (EusServer *server,
                     gint       seconds)
{
  guint pending_requests = eus_server_get_pending_requests (server);
  gint64 last_request_time;
  gint64 monotonic_now;
  gint64 diff;

  if (pending_requests > 0)
    {
      g_debug ("%s: %u requests pending.", G_STRFUNC, pending_requests);
      return FALSE;
    }

  last_request_time = eus_server_get_last_request_time (server);
  monotonic_now = g_get_monotonic_time ();
  diff = monotonic_now - last_request_time;

  return diff > 1000 * 1000 * seconds;
}

static gboolean
timeout_cb (gpointer timeout_data_ptr)
{
  TimeoutData *data = timeout_data_ptr;

  if (!no_requests_timeout (data->server, data->timeout_seconds))
    {
      g_message ("Resetting timeout");
      timeout_data_setup_timeout (data);
    }
  else
    {
      g_message ("Timeout passed, quitting");
      g_main_loop_quit (data->loop);
      data->timeout_id = 0;
    }

  return G_SOURCE_REMOVE;
}

static void
timeout_data_setup_timeout (TimeoutData *data)
{
  clear_source (&data->timeout_id);
  if (data->timeout_seconds > 0)
    data->timeout_id = g_timeout_add_seconds (data->timeout_seconds, timeout_cb, data);
}

static const gchar *
quit_file_name (void)
{
  return g_getenv ("EOS_UPDATER_TEST_UPDATE_SERVER_QUIT_FILE");
}

static EosQuitFileCheckResult
check_and_quit (gpointer timeout_data_ptr)
{
  TimeoutData *data = timeout_data_ptr;

  if (!no_requests_timeout (data->server, data->quit_file_timeout_seconds))
    return EOS_QUIT_FILE_KEEP_CHECKING;

  g_main_loop_quit (data->loop);
  return EOS_QUIT_FILE_QUIT;
}

static gboolean
timeout_data_maybe_setup_quit_file (TimeoutData *data,
                                    GError **error)
{
  const gchar *filename = quit_file_name ();
  g_autoptr(EosQuitFile) quit_file = NULL;
  gint timeout_seconds = 5;

  if (filename == NULL)
    return TRUE;

  quit_file = eos_updater_setup_quit_file (filename,
                                           check_and_quit,
                                           data,
                                           NULL,
                                           timeout_seconds,
                                           error);
  if (quit_file == NULL)
    return FALSE;

  data->quit_file = g_steal_pointer (&quit_file);
  data->quit_file_timeout_seconds = timeout_seconds;
  return TRUE;
}

static gboolean
timeout_data_init (TimeoutData  *data,
                   Options      *options,
                   EusServer    *server,
                   GError      **error)
{
  memset (data, 0, sizeof (*data));
  data->loop = g_main_loop_new (NULL, FALSE);
  data->server = g_object_ref (server);
  data->timeout_seconds = options->timeout_seconds;

  timeout_data_setup_timeout (data);
  if (!timeout_data_maybe_setup_quit_file (data, error))
    return FALSE;

  return TRUE;
}

static void
timeout_data_clear (TimeoutData *data)
{
  data->quit_file_timeout_seconds = 0;
  g_clear_object (&data->quit_file);
  clear_source (&data->timeout_id);
  data->timeout_seconds = 0;
  g_clear_object (&data->server);
  g_clear_pointer (&data->loop, g_main_loop_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (TimeoutData, timeout_data_clear)

typedef GSList URIList;

static void
uri_list_free (URIList *uris)
{
  g_slist_free_full (uris, (GDestroyNotify)soup_uri_free);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (URIList, uri_list_free)

static gboolean
get_first_uri_from_server (SoupServer *server,
                           SoupURI **out_uri,
                           GError **error)
{
  g_autoptr(URIList) uris = soup_server_get_uris (server);
  URIList *iter;

  for (iter = uris; iter != NULL; iter = iter->next)
    {
      SoupURI *uri = iter->data;

      if (uri == NULL)
        continue;

      uris = g_slist_delete_link (uris, iter);
      *out_uri = uri;
      return TRUE;
    }

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Server has no accessible URIs");
  return FALSE;
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

/* Create an #EusRepo to wrap the given #OstreeRepo and add it to the
 * #EusServer. Print an error and return %FALSE on failure. */
static gboolean
add_repo (EusServer   *server,
          OstreeRepo  *repo,
          const gchar *root_path,
          const gchar *remote_name)
{
  g_autoptr(EusRepo) eus_repo = NULL;
  g_autoptr(GError) error = NULL;

  if (!ostree_repo_open (repo, NULL, &error))
    {
      GFile *path = ostree_repo_get_path (repo);
      g_autofree gchar *path_str = g_file_get_path (path);

      g_message ("OSTree repository at ‘%s’ could not be opened: %s",
                 path_str, error->message);
      return FALSE;
    }

  eus_repo = eus_repo_new (repo, root_path, remote_name, NULL, &error);

  if (eus_repo == NULL)
    {
      GFile *path = ostree_repo_get_path (repo);
      g_autofree gchar *path_str = g_file_get_path (path);

      g_message ("Failed to create server for repo ‘%s’: %s",
                 path_str, error->message);
      return FALSE;
    }

  eus_server_add_repo (server, eus_repo);

  return TRUE;
}

/* main() exit codes. */
enum
{
  EXIT_OK = EXIT_SUCCESS,
  EXIT_FAILED = 1,
  EXIT_INVALID_ARGUMENTS = 2,
  EXIT_BAD_CONFIGURATION = 3,
  EXIT_DISABLED = 4,
  EXIT_NO_SOCKETS = 5,
};

int
main (int argc, char **argv)
{
  g_autoptr(GError) error = NULL;
  g_auto(Options) options = OPTIONS_CLEARED;
  g_autoptr(SoupServer) soup_server = NULL;
  g_autoptr(EusServer) eus_server = NULL;
  g_auto(TimeoutData) data = TIMEOUT_DATA_CLEARED;
  gboolean advertise_updates = FALSE;
  g_autoptr(GPtrArray) repository_configs = NULL;
  gsize i;

  setlocale (LC_ALL, "");

  if (!options_init (&options, &argc, &argv, &error))
    {
      g_message ("Failed to initialize options: %s", error->message);
      return EXIT_INVALID_ARGUMENTS;
    }

  /* Load our configuration. */
  if (!eus_read_config_file (options.config_file, &advertise_updates,
                             &repository_configs, &error))
    {
      g_message ("Failed to load configuration file: %s", error->message);
      return EXIT_BAD_CONFIGURATION;
    }

  /* Should we actually run? */
  if (!advertise_updates)
    {
      g_message ("Advertising updates is disabled in the configuration file. "
                 "Exiting.");
      return EXIT_DISABLED;
    }

  /* Set up the server and repositories. */
  soup_server = soup_server_new (NULL, NULL);
  eus_server = eus_server_new (soup_server);

  for (i = 0; i < repository_configs->len; i++)
    {
      const EusRepoConfig *config = g_ptr_array_index (repository_configs, i);
      g_autoptr(GFile) ostree_repo_path = NULL;
      g_autoptr(OstreeRepo) ostree_repo = NULL;
      g_autofree gchar *root_path = NULL;

      /* Serve the (config->index == 0) repository at (root_path == "") for
       * backwards compatibility with the old version of eos-update-server which
       * could only serve a single repository. It’s intended that
       * (config->index == 0) is always the system OSTree repository (though
       * this is not enforced). */
      ostree_repo_path = g_file_new_for_path (config->path);
      ostree_repo = ostree_repo_new (ostree_repo_path);
      root_path = (config->index != 0) ? g_strdup_printf ("/%u", config->index) : g_strdup ("");

      if (!add_repo (eus_server, ostree_repo, root_path, config->remote_name))
        return EXIT_FAILED;
    }

  if (repository_configs->len == 0)
    {
      g_autoptr(OstreeRepo) ostree_repo = NULL;

      ostree_repo = ostree_repo_new_default ();
      if (!add_repo (eus_server, ostree_repo, "", options.served_remote))
        return EXIT_FAILED;
    }

  /* Set up exit timeout. */
  if (!timeout_data_init (&data, &options, eus_server, &error))
    {
      g_message ("Failed to initialize timeout data: %s", error->message);
      return EXIT_FAILED;
    }

  /* Listen! */
  if (!start_listening (soup_server, &options, &error))
    {
      g_message ("Failed to listen: %s", error->message);
      return EXIT_NO_SOCKETS;
    }

  g_main_loop_run (data.loop);

  return EXIT_OK;
}
