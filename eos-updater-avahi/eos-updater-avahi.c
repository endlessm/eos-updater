/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <libeos-updater-util/avahi-service-file.h>
#include <libeos-updater-util/config.h>
#include <libeos-updater-util/ostree.h>
#include <locale.h>
#include <ostree.h>
#include <stdlib.h>

/* Paths for the configuration file. */
static const char *CONFIG_FILE_PATH = SYSCONFDIR "/" PACKAGE "/eos-update-server.conf";
static const char *STATIC_CONFIG_FILE_PATH = PKGDATADIR "/eos-update-server.conf";
static const char *LOCAL_CONFIG_FILE_PATH = PREFIX "/local/share/" PACKAGE "/eos-update-server.conf";

/* Configuration file keys. */
static const char *LOCAL_NETWORK_UPDATES_GROUP = "Local Network Updates";
static const char *ADVERTISE_UPDATES_KEY = "AdvertiseUpdates";

static gboolean
read_config_file (const gchar  *config_file_path,
                  gboolean     *advertise_updates,
                  GError      **error)
{
  g_autoptr(GKeyFile) config = NULL;
  g_autoptr(GError) local_error = NULL;
  const gchar * const default_paths[] =
    {
      CONFIG_FILE_PATH,
      LOCAL_CONFIG_FILE_PATH,
      STATIC_CONFIG_FILE_PATH,
      NULL
    };
  const gchar * const override_paths[] =
    {
      config_file_path,
      NULL
    };

  g_return_val_if_fail (advertise_updates != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Try loading the files in order. If the user specified a configuration file
   * on the command line, use only that. Otherwise use the normal hierarchy. */
  config = eos_updater_load_config_file ((config_file_path != NULL) ? override_paths : default_paths,
                                         error);
  if (config == NULL)
    return FALSE;

  /* Successfully loaded a file. Parse it. */
  *advertise_updates = g_key_file_get_boolean (config,
                                               LOCAL_NETWORK_UPDATES_GROUP,
                                               ADVERTISE_UPDATES_KEY,
                                               &local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

static gboolean
update_service_file (gboolean       advertise_updates,
                     const gchar   *avahi_service_directory,
                     GCancellable  *cancellable,
                     GError       **error)
{
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autofree gchar *commit_checksum = NULL;
  g_autofree gchar *commit_ostree_path = NULL;
  guint64 commit_timestamp;
  g_autoptr(GDateTime) commit_date_time = NULL;
  g_autofree gchar *formatted_commit_date_time = NULL;
  gboolean delete;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (avahi_service_directory != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Work out what commit we would advertise. Errors here are not fatal, as we
   * want to delete the file on failure. */
  sysroot = ostree_sysroot_new_default ();

  if (!ostree_sysroot_load (sysroot, cancellable, &local_error))
    {
      g_warning ("Error loading sysroot: %s", local_error->message);
      g_clear_error (&local_error);
    }
  else if (!eos_sysroot_get_advertisable_commit (sysroot,
                                                 &commit_checksum,
                                                 &commit_ostree_path,
                                                 &commit_timestamp,
                                                 &local_error))
    {
      g_warning ("Error getting advertisable commit: %s", local_error->message);
      g_clear_error (&local_error);
    }

  if (commit_checksum != NULL)
    {
      commit_date_time = g_date_time_new_from_unix_utc (commit_timestamp);
      formatted_commit_date_time = g_date_time_format (commit_date_time,
                                                       "%FT%T%:z");
    }

  /* Work out the update policy. */
  if (!advertise_updates && commit_checksum != NULL)
    {
      g_message ("Advertising updates is disabled. Deployed commit ‘%s’ "
                 "(%s, %s) will not be advertised.",
                 commit_checksum, formatted_commit_date_time,
                 commit_ostree_path);
      delete = TRUE;
    }
  else if (advertise_updates && commit_checksum == NULL)
    {
      g_message ("Advertising updates is enabled, but no appropriate deployed "
                 "commits were found. Not advertising updates.");
      delete = TRUE;
    }
  else if (!advertise_updates && commit_checksum == NULL)
    {
      g_message ("Advertising updates is disabled, and no appropriate deployed "
                 "commits were found. Not advertising updates.");
      delete = TRUE;
    }
  else
    {
      g_message ("Advertising updates is enabled, and deployed commit ‘%s’ "
                 "(%s, %s) will be advertised.", commit_checksum,
                 formatted_commit_date_time, commit_ostree_path);
      delete = FALSE;
    }

  /* Apply the policy. */
  if (!delete)
    {
      if (!eos_avahi_service_file_generate (avahi_service_directory,
                                            commit_ostree_path,
                                            commit_date_time,
                                            cancellable,
                                            error))
        {
          eos_avahi_service_file_delete (avahi_service_directory, cancellable,
                                         NULL);
          return FALSE;
        }

      return TRUE;
    }
  else
    {
      return eos_avahi_service_file_delete (avahi_service_directory,
                                            cancellable, error);
    }
}

/* main() exit codes. */
enum
{
  EXIT_OK = EXIT_SUCCESS,
  EXIT_FAILED = 1,
  EXIT_INVALID_ARGUMENTS = 2,
  EXIT_BAD_CONFIGURATION = 3,
};

static int
fail (gboolean     quiet,
      int          exit_status,
      const gchar *error_message,
      ...) G_GNUC_PRINTF (3, 4);

static int
fail (gboolean     quiet,
      int          exit_status,
      const gchar *error_message,
      ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;

  g_return_val_if_fail (exit_status > 0, EXIT_FAILED);

  if (quiet)
    return exit_status;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  g_printerr ("%s: %s\n", g_get_prgname (), formatted_message);

  return exit_status;
}

static int
usage (GOptionContext *context,
       gboolean        quiet,
       const gchar    *error_message,
       ...) G_GNUC_PRINTF (3, 4);

static int
usage (GOptionContext *context,
       gboolean        quiet,
       const gchar    *error_message,
       ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;
  g_autofree gchar *help = NULL;

  if (quiet)
    return EXIT_INVALID_ARGUMENTS;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  help = g_option_context_get_help (context, TRUE, NULL);
  g_printerr ("%s: %s\n\n%s\n", g_get_prgname (), formatted_message, help);

  return EXIT_INVALID_ARGUMENTS;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  gboolean advertise_updates = FALSE;
  g_autoptr(GOptionContext) context = NULL;
  g_autofree gchar *avahi_service_directory = NULL;
  g_autofree gchar *config_file = NULL;
  gboolean quiet = FALSE;

  const GOptionEntry entries[] =
    {
      { "service-directory", 'd',
        G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &avahi_service_directory,
        "Directory containing Avahi .service files "
        "(default: " SYSCONFDIR "/avahi/services" ")",
        "PATH" },
      { "config-file", 'c',
        G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &config_file,
        "Configuration file to use (default: "
        SYSCONFDIR "/" PACKAGE "/eos-update-server.conf" ")", "PATH" },
      { "quiet", 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &quiet,
        "Do not print anything; check exit status for success", NULL },
      { NULL }
    };

  setlocale (LC_ALL, "");

  /* Command line parsing and defaults. */
  context = g_option_context_new ("— Endless OS Avahi Advertisement Updater");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Update the Avahi DNS-SD service advertisement "
                                "for advertising updates from this machine to "
                                "the local network, enabling or disabling it "
                                "as appropriate to match the current "
                                "configuration and OSTree state.");

  if (!g_option_context_parse (context,
                               &argc,
                               &argv,
                               &error))
    {
      return usage (context, quiet, "Failed to parse options: %s",
                    error->message);
    }

  if (avahi_service_directory == NULL)
    avahi_service_directory = g_strdup (eos_avahi_service_file_get_directory ());

  /* Load our configuration. */
  if (!read_config_file (config_file, &advertise_updates, &error))
    {
      return fail (quiet, EXIT_BAD_CONFIGURATION,
                   "Failed to load configuration file: %s", error->message);
    }

  /* Update the Avahi configuration file to match. */
  if (!update_service_file (advertise_updates, avahi_service_directory, NULL,
                            &error))
    {
      return fail (quiet, EXIT_FAILED,
                   "Failed to update service file: %s", error->message);
    }

  return EXIT_OK;
}
