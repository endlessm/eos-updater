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

#include "eos-refcounted.h"
#include "eos-util.h"

#include <ostree.h>

#include <libsoup/soup.h>

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

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
  GMainLoop *loop;
  EosUpdaterRepoServer *server;

  gint timeout_seconds;
  guint timeout_id;

  EosQuitFile *quit_file;
  gint quit_file_timeout_seconds;
} TimeoutData;

#define TIMEOUT_DATA_CLEARED { NULL, NULL, 0, 0u, NULL, 0 }

static void
timeout_data_setup_timeout (TimeoutData *data);

static gboolean
no_requests_timeout (EosUpdaterRepoServer *server,
                     gint seconds)
{
  guint pending_requests = eos_updater_repo_server_get_pending_requests (server);
  gint64 last_request_time;
  gint64 monotonic_now;
  gint64 diff;

  if (pending_requests > 0)
    return FALSE;

  last_request_time = eos_updater_repo_server_get_last_request_time (server);
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
      message ("Resetting timeout");
      timeout_data_setup_timeout (data);
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
timeout_data_setup_timeout (TimeoutData *data)
{
  clear_source (&data->timeout_id);
  if (data->timeout_seconds > 0)
    data->timeout_id = g_timeout_add_seconds (data->timeout_seconds, timeout_cb, data);
}

static gchar *
quit_file_name (void)
{
  return eos_updater_dup_envvar_or ("EOS_UPDATER_TEST_UPDATE_SERVER_QUIT_FILE",
                                     NULL);
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
  g_autofree gchar *filename = quit_file_name ();
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
timeout_data_init (TimeoutData *data,
                   Options *options,
                   EosUpdaterRepoServer *server,
                   GError **error)
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
  g_autoptr(EosUpdaterRepoServer) server = NULL;
  g_auto(TimeoutData) data = TIMEOUT_DATA_CLEARED;
  g_autoptr(OstreeRepo) repo;

  if (!options_init (&options, &argc, &argv, &error))
    {
      message ("Failed to initialize options: %s", error->message);
      return 1;
    }

  repo = eos_updater_local_repo ();
  server = eos_updater_repo_server_new (repo,
                                        options.served_remote,
                                        NULL,
                                        &error);
  if (server == NULL)
    {
      message ("Failed to create a server: %s", error->message);
      return 1;
    }


  if (!timeout_data_init (&data, &options, server, &error))
    {
      message ("Failed to initialize timeout data: %s", error->message);
      return 1;
    }

  if (!start_listening (SOUP_SERVER (server), &options, &error))
    {
      message ("Failed to listen: %s", error->message);
      return 1;
    }

  g_main_loop_run (data.loop);

  return 0;
}
