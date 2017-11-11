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
 *  - Sam Spilsbury <sam@endlessm.com>
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <locale.h>
#include <flatpak.h>
#include <ostree.h>
#include <sysexits.h>
#include <libeos-updater-util/enums.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>

#include "installer.h"

#define EXIT_CHECK_FAILED 3

static int
usage (GOptionContext *context,
       const gchar    *error_message,
       ...) G_GNUC_PRINTF (2, 3);

static int
usage (GOptionContext *context,
       const gchar    *error_message,
       ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;
  g_autofree gchar *help = NULL;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  help = g_option_context_get_help (context, TRUE, NULL);
  g_printerr ("%s: %s\n\n%s\n", g_get_prgname (), formatted_message, help);

  return 128;
}

static gboolean
parse_installer_mode (const gchar              *mode,
                      EosUpdaterInstallerMode  *out_mode,
                      GError                  **error)
{
  GEnumClass *enum_class = g_type_class_ref (EOS_TYPE_UPDATER_INSTALLER_MODE);
  GEnumValue *enum_value = g_enum_get_value_by_nick (enum_class, mode);

  g_type_class_unref (enum_class);

  if (!enum_value)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Invalid installer mode %s",
                   mode);
      return FALSE;
    }

  *out_mode = (EosUpdaterInstallerMode) enum_value->value;

  return TRUE;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GFile) flatpaks_to_export_file = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_for_this_boot = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_progress = NULL;
  g_autoptr(FlatpakInstallation) installation = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_for_this_boot = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_progress_for_this_boot = NULL;
  const gchar *resolved_mode = NULL;
  const gchar *pending_flatpak_deployments_state_path = eos_updater_util_pending_flatpak_deployments_state_path ();
  EosUpdaterInstallerMode parsed_mode;

  gchar *mode = NULL;
  gboolean also_pull = FALSE;
  GOptionEntry entries[] =
  {
    { "mode", 'm', 0, G_OPTION_ARG_STRING, &mode, "Mode to use (perform, stamp, check)", NULL },
    { "pull", 'p', 0, G_OPTION_ARG_NONE, &also_pull, "Also pull flatpaks", NULL },
    { NULL }
  };

  setlocale (LC_ALL, "");

  context = g_option_context_new ("— Endless OS Updater - Flatpak Installer");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Install flatpak packages on system boot");

  if (!g_option_context_parse (context, &argc, &argv, &error))
    return usage (context, "Failed to parse options: %s", error->message);

  resolved_mode = mode != NULL ? mode : "perform";

  if (!parse_installer_mode (resolved_mode,
                             &parsed_mode,
                             &error))
    {
      g_message ("%s", error->message);
      return EXIT_FAILURE;
    }

  installation = eos_updater_get_flatpak_installation (NULL, &error);

  if (installation == NULL)
    {
      g_message ("Could not get flatpak installation: %s", error->message);
      return EXIT_FAILURE;
    }

  g_message ("Running in mode '%s'", resolved_mode);

  if (also_pull)
    g_message ("Will pull flatpaks as well as deploying them");

  /* Check mode is completely different - we need to read in the action
   * application state and check if there's a delta between what we expect
   * and what we have.
   *
   * Note that on a user system it might be perfectly legitimate for there
   * to be a delta, because the user might have uninstalled or installed
   * an app we marked as auto-install or auto-uninstall. Generally speaking
   * you would use this mode on the image builder to catch situations where
   * the apps list is out of sync.
   */
  switch (parsed_mode)
    {
      case EU_INSTALLER_MODE_CHECK:
        {
          g_autoptr(GHashTable) flatpak_ref_actions_to_check =
            eos_updater_flatpak_installer_determine_flatpak_ref_actions_to_check (NULL, &error);
          g_autofree gchar *formatted_flatpak_ref_actions_to_check = NULL;

          if (flatpak_ref_actions_to_check == NULL)
            {
              g_message ("Could not get information on which flatpak ref actions to check: %s",
                         error->message);
              return EXIT_FAILURE;
            }

          formatted_flatpak_ref_actions_to_check =
            eos_updater_util_format_all_flatpak_ref_actions ("All flatpak ref actions that should have been applied",
                                                             flatpak_ref_actions_to_check);

          g_message ("%s", formatted_flatpak_ref_actions_to_check);

          if (!eos_updater_flatpak_installer_check_ref_actions_applied (installation,
                                                                        pending_flatpak_deployments_state_path,
                                                                        flatpak_ref_actions_to_check,
                                                                        &error))
            {
              g_message ("Flatpak ref actions are not up to date: %s",
                         error->message);
              return EXIT_CHECK_FAILED;
            }

          break;
        }
      case EU_INSTALLER_MODE_PERFORM:
      case EU_INSTALLER_MODE_STAMP:
        {
          g_autoptr(GHashTable) new_flatpak_ref_actions_to_apply =
            eos_updater_flatpak_installer_determine_flatpak_ref_actions_to_apply (NULL, &error);
          g_autofree gchar *formatted_flatpak_ref_actions_to_apply = NULL;

          if (new_flatpak_ref_actions_to_apply == NULL)
            {
              g_message ("Could not get information on which flatpak ref actions to check: %s",
                         error->message);
              return EXIT_FAILURE;
            }

          formatted_flatpak_ref_actions_to_apply =
            eos_updater_util_format_all_flatpak_ref_actions ("All flatpak ref actions that are not yet applied",
                                                             new_flatpak_ref_actions_to_apply);

          g_message ("%s", formatted_flatpak_ref_actions_to_apply);

          if (!eos_updater_flatpak_installer_apply_flatpak_ref_actions (installation,
                                                                        new_flatpak_ref_actions_to_apply,
                                                                        parsed_mode,
                                                                        also_pull ? EU_INSTALLER_FLAGS_ALSO_PULL : EU_INSTALLER_FLAGS_NONE,
                                                                        &error))
            {
               g_message ("Couldn't apply some flatpak update actions for this boot: %s",
                          error->message);
               return EXIT_FAILURE;
            }

          break;
        }
      default:
        g_assert_not_reached ();
    }

  return EXIT_SUCCESS;
}
