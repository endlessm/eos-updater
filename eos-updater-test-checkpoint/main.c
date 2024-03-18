/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2024 Endless OS Foundation LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <glib/gi18n.h>
#include <libeos-updater-util/checkpoint-private.h>

/* main() exit codes. */
enum
{
  EXIT_OK = EXIT_SUCCESS,
  EXIT_FAILED = 1,
  EXIT_INVALID_ARGUMENTS = 2,
};


gint
main (gint   argc,
      gchar *argv[])
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) context = NULL;
  GOptionEntry entries[] = {
    { 0 }
  };
  const char *source_ref, *target_ref;
  g_autoptr(OstreeSysroot) sysroot = NULL;

  setlocale (LC_ALL, "");

  context = g_option_context_new ("SOURCE_REF TARGET_REF");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Checks whether crossing a checkpoint between "
                                "SOURCE_REF and TARGET_REF would be permitted "
                                "or blocked.");

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("Error parsing command line arguments: %s\n", error->message);
      return EXIT_INVALID_ARGUMENTS;
    }

  switch (argc)
    {
    case 1:
      g_printerr ("SOURCE_REF and TARGET_REF are required\n");
      return EXIT_INVALID_ARGUMENTS;
    case 2:
      g_printerr ("TARGET_REF is required\n");
      return EXIT_INVALID_ARGUMENTS;
    case 3:
      source_ref = argv[1];
      target_ref = argv[2];
      break;
    default:
      g_printerr ("Too many arguments\n");
      return EXIT_INVALID_ARGUMENTS;
    }

  sysroot = ostree_sysroot_new_default ();
  if (!ostree_sysroot_load (sysroot, NULL, &error))
    {
      g_printerr ("Couldn't open sysroot (not an ostree system?): %s\n",
                  error->message);
      return EXIT_FAILED;
    }

  if (euu_should_follow_checkpoint (sysroot, source_ref, target_ref, &error))
    {
      g_message ("This system would upgrade from %s to %s",
                 source_ref,
                 target_ref);
      return EXIT_OK;
    }
  else if (error->domain == EUU_CHECKPOINT_BLOCK)
    {
      g_message ("This system would not upgrade from %s to %s due to %s: %s",
                 source_ref,
                 target_ref,
                 euu_checkpoint_block_to_string ((EuuCheckpointBlock) error->code),
                 error->message);
      return EXIT_OK;
    }
  else
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILED;
    }
}
