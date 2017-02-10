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

#include <glib.h>
#include <locale.h>
#include <ostree.h>
#include <sys/types.h>
#include <unistd.h>

#include "eos-prepare-usb-update.h"

static int
fail (gboolean     quiet,
      const gchar *error_message,
      ...) G_GNUC_PRINTF (2, 3);

static int
fail (gboolean     quiet,
      const gchar *error_message,
      ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;

  if (quiet)
    return 1;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  g_printerr ("%s: %s\n", g_get_prgname (), formatted_message);

  return 1;
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
    return 1;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  help = g_option_context_get_help (context, TRUE, NULL);
  g_printerr ("%s: %s\n\n%s\n", g_get_prgname (), formatted_message, help);

  return 1;
}

int
main (int argc,
      char **argv)
{
  gboolean quiet = FALSE;
  g_auto(GStrv) remaining = NULL;
  g_autoptr(GOptionContext) context = g_option_context_new ("— Endless USB Drive Preparation Tool");
  GOptionEntry entries[] =
    {
      { "quiet", 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &quiet, "Do not print anything, check exit status for success", NULL },
      { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY,
        &remaining, "Path to the USB drive to prepare", "VOLUME-PATH" },
      { NULL }
    };
  g_autoptr(GError) error = NULL;
  const gchar *raw_usb_path = NULL;
  g_autoptr(GFile) usb_path = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autoptr(OstreeAsyncProgress) progress = NULL;

  setlocale (LC_ALL, "");

  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Prepare a USB drive with a copy of the local "
                                "ostree repository, so it can be used to "
                                "update other machines offline. The repository "
                                "copy will be put in the eos-update directory "
                                "on the USB drive; other files will not be "
                                "affected.");

  if (!g_option_context_parse (context,
                               &argc,
                               &argv,
                               &error))
    {
      return usage (context, quiet, "Failed to parse options: %s",
                    error->message);
    }

  /* We need to be root in order to read all the files in the OSTree repo
   * (unless we’re running the unit tests). */
  if (geteuid () != 0 &&
      g_getenv ("EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK") == NULL)
    {
      return fail (quiet, "Must be run as root");
    }

  if (argc != 1 || remaining == NULL || g_strv_length (remaining) != 1)
    {
      return usage (context, quiet,
                    "Expected exactly one path to the USB drive");
    }

  raw_usb_path = remaining[0];
  usb_path = g_file_new_for_commandline_arg (raw_usb_path);
  if (!g_file_query_exists (usb_path, NULL))
    {
      return fail (quiet, "Path ‘%s’ does not exist", raw_usb_path);
    }

  sysroot = ostree_sysroot_new_default ();

  /* Lock the sysroot so it can’t be updated while we’re pulling from it. The
   * lock is automatically released when we finalise the sysroot. */
  if (!ostree_sysroot_lock (sysroot, &error) ||
      !ostree_sysroot_load (sysroot, NULL, &error))
    {
      return fail (quiet, "Failed to load sysroot: %s", error->message);
    }

  if (!quiet)
    progress = ostree_async_progress_new_and_connect (ostree_repo_pull_default_console_progress_changed,
                                                      /* just whatever that is not NULL, the function
                                                       * above early-quits if user-data is NULL
                                                       */
                                                      main);

  if (!eos_updater_prepare_volume_from_sysroot (sysroot,
                                                usb_path,
                                                progress,
                                                NULL,
                                                &error))
    {
      return fail (quiet, "Failed to prepare the update: %s", error->message);
    }

  return 0;
}
