/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
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
 * Authors: Vivek Dasmohapatra <vivek@etla.org>
 *          Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include "eos-util.h"

#include <libsoup/soup.h>

#include <string.h>

static const GDBusErrorEntry eos_updater_error_entries[] = {
  { EOS_UPDATER_ERROR_WRONG_STATE, "com.endlessm.Updater.Error.WrongState" },
  { EOS_UPDATER_ERROR_LIVE_BOOT, "com.endlessm.Updater.Error.LiveBoot" },
  { EOS_UPDATER_ERROR_LAN_DISCOVERY_ERROR, "com.endlessm.Updater.Error.LANDiscoveryError" },
  { EOS_UPDATER_ERROR_WRONG_CONFIGURATION, "com.endlessm.Updater.Error.WrongConfiguration" }
};

/* Ensure that every error code has an associated D-Bus error name */
G_STATIC_ASSERT (G_N_ELEMENTS (eos_updater_error_entries) == EOS_UPDATER_N_ERRORS);

GQuark
eos_updater_error_quark (void)
{
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("eos-updater-error-quark",
                                      &quark_volatile,
                                      eos_updater_error_entries,
                                      G_N_ELEMENTS (eos_updater_error_entries));
  return (GQuark) quark_volatile;
}

static const gchar * state_str[] = {
   "None",
   "Ready",
   "Error",
   "Polling",
   "UpdateAvailable",
   "Fetching",
   "UpdateReady",
   "ApplyUpdate",
   "UpdateApplied"
};

G_STATIC_ASSERT (G_N_ELEMENTS (state_str) == EOS_UPDATER_N_STATES);

const gchar *
eos_updater_state_to_string (EosUpdaterState state)
{
  g_assert (state < EOS_UPDATER_N_STATES);

  return state_str[state];
};

OstreeRepo *
eos_updater_local_repo (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(OstreeRepo) repo = ostree_repo_new_default ();

  if (!ostree_repo_open (repo, NULL, &error))
    {
      GFile *file = ostree_repo_get_path (repo);
      g_autofree gchar *path = g_file_get_path (file);

      g_error ("Repo at '%s' is not Ok (%s)",
               path ? path : "", error->message);
    }

  return g_steal_pointer (&repo);
}

static gboolean
is_ancestor (GFile *dir,
             GFile *file)
{
  g_autoptr(GFile) child = g_object_ref (file);

  for (;;)
    {
      g_autoptr(GFile) parent = g_file_get_parent (child);

      if (parent == NULL)
        return FALSE;

      if (g_file_equal (dir, parent))
        break;

      g_set_object (&child, parent);
    }

  return TRUE;
}

/* pass /a as the dir parameter and /a/b/c/d as the file parameter,
 * will delete the /a/b/c/d file then /a/b/c and /a/b directories if
 * empty.
 */
static gboolean
delete_files_and_empty_parents (GFile *dir,
                                GFile *file,
                                GCancellable *cancellable,
                                GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GFile) child = NULL;

  if (!is_ancestor (dir, file))
    {
      g_autofree gchar *raw_dir_path = g_file_get_path (dir);
      g_autofree gchar *raw_file_path = g_file_get_path (file);

      g_warning ("%s is not an ancestor of %s, not deleting anything",
                 raw_dir_path,
                 raw_file_path);
      return FALSE;
    }

  if (!g_file_delete (file, cancellable, &local_error))
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_clear_error (&local_error);
      else
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }

  child = g_object_ref (file);
  for (;;)
    {
      g_autoptr(GFile) parent = g_file_get_parent (child);

      if (g_file_equal (dir, parent))
        break;

      if (!g_file_delete (parent, cancellable, &local_error))
        {
          if (!(g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
                g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY)))
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }

          break;
        }

      g_set_object (&child, parent);
    }

  return TRUE;
}

static gboolean
create_directories (GFile *directory,
                    GCancellable *cancellable,
                    GError **error)
{
  g_autoptr(GError) local_error = NULL;

  if (g_file_make_directory_with_parents (directory, cancellable, &local_error))
    return TRUE;

  if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    return TRUE;

  g_propagate_error (error, g_steal_pointer (&local_error));
  return FALSE;
}

static gboolean
create_directories_and_file (GFile *target,
                             GBytes *contents,
                             GCancellable *cancellable,
                             GError **error)
{
  g_autoptr(GFile) target_parent = g_file_get_parent (target);
  gconstpointer raw;
  gsize len;

  if (!create_directories (target_parent, cancellable, error))
    return FALSE;

  raw = g_bytes_get_data (contents, &len);
  return g_file_replace_contents (target,
                                  raw,
                                  len,
                                  NULL,
                                  FALSE,
                                  G_FILE_CREATE_NONE,
                                  NULL,
                                  cancellable,
                                  error);
}

gboolean
eos_updater_save_or_delete  (GBytes *contents,
                             GFile *dir,
                             const gchar *filename,
                             GCancellable *cancellable,
                             GError **error)
{
  g_autoptr(GFile) target = g_file_get_child (dir, filename);
  g_autoptr(GFile) target_parent = g_file_get_parent (target);

  if (contents == NULL)
    return delete_files_and_empty_parents (dir, target, cancellable, error);

  return create_directories_and_file (target, contents, cancellable, error);
}

gboolean
eos_updater_create_extensions_dir (OstreeRepo *repo,
                                   GFile **dir,
                                   GError **error)
{
  g_autoptr(GFile) ext_path = eos_updater_get_eos_extensions_dir (repo);

  if (!create_directories (ext_path, NULL, error))
    return FALSE;

  *dir = g_steal_pointer (&ext_path);
  return TRUE;
}

void
eos_updater_set_state_changed (EosUpdater *updater, EosUpdaterState state)
{
  eos_updater_set_state (updater, state);
  eos_updater_emit_state_changed (updater, state);
}

void
eos_updater_set_error (EosUpdater *updater,
                       const GError *error)
{
  gint code = error ? error->code : -1;
  const gchar *msg = (error && error->message) ? error->message : "Unspecified";
  g_autofree gchar *error_name = g_dbus_error_encode_gerror (error);

  g_warn_if_fail (error != NULL);

  message ("Changing to error state: %s, %d, %s", error_name, code, msg);

  eos_updater_set_error_name (updater, error_name);
  eos_updater_set_error_code (updater, code);
  eos_updater_set_error_message (updater, msg);
  eos_updater_set_state_changed (updater, EOS_UPDATER_STATE_ERROR);
}

void
eos_updater_clear_error (EosUpdater *updater,
                         EosUpdaterState state)
{
  message ("Clearing error state and changing to state %d", state);

  eos_updater_set_error_code (updater, 0);
  eos_updater_set_error_message (updater, "");
  eos_updater_set_state_changed (updater, state);
}

static gboolean
fallback_to_the_fake_deployment (void)
{
  const gchar *value = NULL;

  value = g_getenv ("EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK");

  return value != NULL;
}

static OstreeDeployment *
get_fake_deployment (OstreeSysroot *sysroot,
                     GError **error)
{
  static OstreeDeployment *fake_booted_deployment = NULL;

  if (fake_booted_deployment == NULL)
    {
      g_autoptr(GPtrArray) deployments = NULL;

      deployments = ostree_sysroot_get_deployments (sysroot);
      if (deployments->len == 0)
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                               "No deployments found at all");
          return NULL;
        }
      fake_booted_deployment = g_object_ref (g_ptr_array_index (deployments, 0));
    }

  return g_object_ref (fake_booted_deployment);
}

OstreeDeployment *
eos_updater_get_booted_deployment_from_loaded_sysroot (OstreeSysroot *sysroot,
                                                       GError **error)
{
  OstreeDeployment *booted_deployment = NULL;

  booted_deployment = ostree_sysroot_get_booted_deployment (sysroot);
  if (booted_deployment != NULL)
    return g_object_ref (booted_deployment);

  if (fallback_to_the_fake_deployment ())
    return get_fake_deployment (sysroot, error);

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                       "Not an ostree system");
  return NULL;
}

OstreeDeployment *
eos_updater_get_booted_deployment (GError **error)
{
  g_autoptr(OstreeSysroot) sysroot = ostree_sysroot_new_default ();

  if (!ostree_sysroot_load (sysroot, NULL, error))
    return NULL;

  return eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot, error);
}

gchar *
eos_updater_get_booted_checksum (GError **error)
{
  g_autoptr(OstreeDeployment) booted_deployment = NULL;

  booted_deployment = eos_updater_get_booted_deployment (error);
  if (booted_deployment == NULL)
    return NULL;

  return g_strdup (ostree_deployment_get_csum (booted_deployment));
}

gchar *
eos_updater_get_baseurl (OstreeDeployment *booted_deployment,
                         OstreeRepo *repo,
                         GError **error)
{
  const gchar *osname;
  g_autofree gchar *url = NULL;

  osname = ostree_deployment_get_osname (booted_deployment);
  if (!ostree_repo_remote_get_url (repo, osname, &url, error))
    return NULL;

  return g_steal_pointer (&url);
}

gboolean
eos_updater_get_ostree_path (OstreeRepo *repo,
                             gchar **ostree_path,
                             GError **error)
{
  g_autoptr(OstreeDeployment) deployment = NULL;
  g_autofree gchar *ostree_url = NULL;
  g_autoptr(SoupURI) uri = NULL;
  g_autofree gchar *path = NULL;
  gsize to_move = 0;

  deployment = eos_updater_get_booted_deployment (error);
  if (deployment == NULL)
    return FALSE;

  ostree_url = eos_updater_get_baseurl (deployment,
                                        repo,
                                        error);
  if (ostree_url == NULL)
    return FALSE;

  uri = soup_uri_new (ostree_url);
  if (uri == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "ostree %s remote's URL is invalid (%s)",
                   ostree_deployment_get_osname (deployment),
                   ostree_url);
      return FALSE;
    }

  path = g_strdup (soup_uri_get_path (uri));
  while (path[to_move] == '/')
    ++to_move;
  if (to_move > 0)
    memmove (path, path + to_move, strlen (path) - to_move + 1);
  *ostree_path = g_steal_pointer (&path);
  return TRUE;
}

guint
eos_updater_queue_callback (GMainContext *context,
                            GSourceFunc function,
                            gpointer user_data,
                            const gchar *name)
{
  g_autoptr(GSource) source = g_idle_source_new ();

  if (name != NULL)
    g_source_set_name (source, name);
  g_source_set_callback (source, function, user_data, NULL);

  return g_source_attach (source, context);
}

gboolean
eos_updater_get_timestamp_from_branch_file_keyfile (GKeyFile *branch_file,
                                                    GDateTime **out_timestamp,
                                                    GError **error)
{
  gint64 unix_utc;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GDateTime) timestamp = NULL;

  g_return_val_if_fail (branch_file != NULL, FALSE);
  g_return_val_if_fail (out_timestamp != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  unix_utc = g_key_file_get_int64 (branch_file,
                                   "main",
                                   "UnixUTCTimestamp",
                                   &local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  timestamp = g_date_time_new_from_unix_utc (unix_utc);
  if (timestamp == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Invalid branch file timestamp");
      return FALSE;
    }

  if (out_timestamp != NULL)
    *out_timestamp = g_steal_pointer (&timestamp);
  return TRUE;
}

gboolean
eos_updater_get_ostree_paths_from_branch_file_keyfile (GKeyFile *branch_file,
                                                       gchar ***out_ostree_paths,
                                                       GError **error)
{
  g_auto(GStrv) ostree_paths = NULL;

  ostree_paths = g_key_file_get_string_list (branch_file,
                                             "main",
                                             "OstreePaths",
                                             NULL,
                                             error);
  if (ostree_paths == NULL)
    return FALSE;

  if (out_ostree_paths != NULL)
    *out_ostree_paths = g_steal_pointer (&ostree_paths);
  return TRUE;
}

gchar *
eos_updater_dup_envvar_or (const gchar *envvar,
                           const gchar *default_value)
{
  const gchar *value = g_getenv (envvar);

  if (value != NULL)
    return g_strdup (value);

  return g_strdup (default_value);
}

GFile *
eos_updater_get_eos_extensions_dir (OstreeRepo *repo)
{
  g_autofree gchar *rel_path = g_build_filename ("extensions", "eos", NULL);

  return g_file_get_child (ostree_repo_get_path (repo), rel_path);
}

typedef GSList URIList;

static void
uri_list_free (URIList *uris)
{
  g_slist_free_full (uris, (GDestroyNotify)soup_uri_free);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (URIList, uri_list_free)

gboolean
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

gboolean
eos_updater_read_file_to_bytes (GFile *file,
                                GCancellable *cancellable,
                                GBytes **out_bytes,
                                GError **error)
{
  g_autofree gchar *contents = NULL;
  gsize len = 0;

  if (!g_file_load_contents (file,
                             cancellable,
                             &contents,
                             &len,
                             NULL,
                             error))
    return FALSE;

  *out_bytes = g_bytes_new_take (g_steal_pointer (&contents), len);
  return TRUE;
}

struct _EosQuitFile
{
  GObject parent_instance;

  GFileMonitor *monitor;
  guint signal_id;
  guint timeout_seconds;
  guint timeout_id;
  EosQuitFileCheckCallback callback;
  gpointer user_data;
  GDestroyNotify notify;
};

static void
quit_clear_user_data (EosQuitFile *quit_file)
{
  gpointer user_data = g_steal_pointer (&quit_file->user_data);
  GDestroyNotify notify = g_steal_pointer (&quit_file->notify);

  if (notify != NULL)
    notify (user_data);
}

static void
quit_disconnect_monitor (EosQuitFile *quit_file)
{
  guint id = quit_file->signal_id;

  quit_file->signal_id = 0;
  if (id > 0)
    g_signal_handler_disconnect (quit_file->monitor, id);
}

static void
quit_clear_source (EosQuitFile *quit_file)
{
  guint id = quit_file->timeout_id;

  quit_file->timeout_id = 0;
  if (id > 0)
    g_source_remove (id);
}

static void
eos_quit_file_dispose_impl (EosQuitFile *quit_file)
{
  quit_clear_user_data (quit_file);
  quit_clear_source (quit_file);
  quit_disconnect_monitor (quit_file);
  g_clear_object (&quit_file->monitor);
}

EOS_DEFINE_REFCOUNTED (EOS_QUIT_FILE,
                       EosQuitFile,
                       eos_quit_file,
                       eos_quit_file_dispose_impl,
                       NULL)

static gboolean
quit_file_source_func (gpointer quit_file_ptr)
{
  EosQuitFile *quit_file = EOS_QUIT_FILE (quit_file_ptr);

  if (quit_file->callback (quit_file->user_data) == EOS_QUIT_FILE_KEEP_CHECKING)
    return G_SOURCE_CONTINUE;

  quit_file->timeout_id = 0;
  quit_clear_user_data (quit_file);
  return G_SOURCE_REMOVE;
}

static void
on_quit_file_changed (GFileMonitor *monitor,
                      GFile *file,
                      GFile *other,
                      GFileMonitorEvent event,
                      gpointer quit_file_ptr)
{
  EosQuitFile *quit_file = EOS_QUIT_FILE (quit_file_ptr);

  if (event != G_FILE_MONITOR_EVENT_DELETED)
    return;

  if (quit_file->callback (quit_file->user_data) == EOS_QUIT_FILE_KEEP_CHECKING)
    quit_file->timeout_id = g_timeout_add_seconds (quit_file->timeout_seconds,
                                                   quit_file_source_func,
                                                   quit_file);
  g_signal_handler_disconnect (quit_file->monitor, quit_file->signal_id);
  quit_file->signal_id = 0;
}

EosQuitFile *
eos_updater_setup_quit_file (const gchar *path,
                             EosQuitFileCheckCallback check_callback,
                             gpointer user_data,
                             GDestroyNotify notify,
                             guint timeout_seconds,
                             GError **error)
{
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileMonitor) monitor = NULL;
  g_autoptr(EosQuitFile) quit_file = NULL;

  file = g_file_new_for_path (path);
  monitor = g_file_monitor_file (file,
                                 G_FILE_MONITOR_NONE,
                                 NULL,
                                 error);
  if (monitor == NULL)
    return NULL;

  quit_file = g_object_new (EOS_TYPE_QUIT_FILE, NULL);
  quit_file->monitor = g_steal_pointer (&monitor);
  quit_file->signal_id = g_signal_connect (quit_file->monitor,
                                           "changed",
                                           G_CALLBACK (on_quit_file_changed),
                                           quit_file);
  quit_file->timeout_seconds = timeout_seconds;
  quit_file->callback = check_callback;
  quit_file->user_data = user_data;
  quit_file->notify = notify;

  return g_steal_pointer (&quit_file);
}
