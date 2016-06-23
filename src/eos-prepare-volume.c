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

#include "eos-prepare-usb-update.h"

int
main (int argc,
      char **argv)
{
  gboolean quiet = FALSE;
  g_autoptr(GOptionContext) context = g_option_context_new ("Endless Pendrive Prepare Tool");
  GOptionEntry entries[] =
    {
      { "quiet", 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &quiet, "Do not print anything, check exit status for success", NULL },
      { NULL }
    };
  g_autoptr(GError) error = NULL;
  const gchar *raw_usb_path = NULL;
  g_autoptr(GFile) usb_path = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autoptr(OstreeAsyncProgress) progress = NULL;

  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context,
                               &argc,
                               &argv,
                               &error))
    {
      g_message ("Failed to parse options: %s",
                 error->message);
      return 1;
    }

  if (argc != 2)
    {
      if (!quiet)
        g_message ("Expected exactly one path to the pendrive");
      return 1;
    }

  raw_usb_path = argv[1];
  usb_path = g_file_new_for_path (raw_usb_path);
  if (!g_file_query_exists (usb_path, NULL))
    {
      if (!quiet)
        g_message ("Path %s does not exist", raw_usb_path);
      return 1;
    }

  sysroot = ostree_sysroot_new_default ();
  if (!ostree_sysroot_load (sysroot, NULL, &error))
    {
      if (!quiet)
        g_message ("Failed to load sysroot: %s",
                   error->message);
      return 1;
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
      if (!quiet)
        g_message ("Failed to prepare the update: %s",
                   error->message);
      return 1;
    }

  return 0;
}
