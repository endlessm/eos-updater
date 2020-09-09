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

#include "config.h"

#include <flatpak.h>
#include <glib.h>
#include <libeos-updater-util/enums.h>
#include <libeos-updater-util/flatpak-util.h>
#include <libeos-updater-util/metrics-private.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>
#include <libeos-updater-flatpak-installer/installer.h>
#include <locale.h>
#include <ostree.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#ifdef HAS_EOSMETRICS_0
#include <eosmetrics/eosmetrics.h>
#endif /* HAS_EOSMETRICS_0 */

/* main() exit codes. */
enum
{
  EXIT_OK = EXIT_SUCCESS,
  EXIT_FAILED = 1,
  EXIT_INVALID_ARGUMENTS = 2,
  EXIT_CHECK_FAILED = 3,
  EXIT_APPLY_FAILED = 4,
};

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

  return EXIT_INVALID_ARGUMENTS;
}

static gboolean
parse_installer_mode (const gchar              *mode,
                      EosUpdaterInstallerMode  *out_mode,
                      GError                  **error)
{
  GEnumClass *enum_class = g_type_class_ref (EOS_TYPE_UPDATER_INSTALLER_MODE);
  GEnumValue *enum_value = g_enum_get_value_by_nick (enum_class, mode);

  g_type_class_unref (enum_class);

  if (enum_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid installer mode ‘%s’", mode);
      return FALSE;
    }

  *out_mode = (EosUpdaterInstallerMode) enum_value->value;

  return TRUE;
}

static int
fail (int          exit_status,
      const gchar *error_message,
      ...) G_GNUC_PRINTF (2, 3);

static int
fail (int          exit_status,
      const gchar *error_message,
      ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;

  g_return_val_if_fail (exit_status > 0, EXIT_FAILED);

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  g_printerr ("%s: %s\n", g_get_prgname (), formatted_message);

  /* Report a metric. */
#ifdef HAS_EOSMETRICS_0
  if (euu_get_metrics_enabled ())
    {
      emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                             EOS_UPDATER_METRIC_FAILURE,
                                             g_variant_new ("(ss)",
                                                            "eos-updater-flatpak-installer",
                                                            formatted_message));
    }
#endif

  return exit_status;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(FlatpakInstallation) installation = NULL;
  const gchar *resolved_mode = NULL;
  EosUpdaterInstallerMode parsed_mode;

  g_autofree gchar *mode = NULL;
  gboolean also_pull = FALSE;
  gboolean dry_run = FALSE;
  GOptionEntry entries[] =
    {
      { "dry-run", 0, 0, G_OPTION_ARG_NONE, &dry_run, "Print actions without applying them", NULL },
      { "mode", 'm', 0, G_OPTION_ARG_STRING, &mode, "Mode to use (perform, stamp, check) (default: perform)", NULL },
      { "pull", 'p', 0, G_OPTION_ARG_NONE, &also_pull, "Also pull flatpaks", NULL },
      { NULL }
    };

  setlocale (LC_ALL, "");

  context = g_option_context_new ("— Endless OS Updater Flatpak Installer");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Install flatpak packages on system boot");

  if (!g_option_context_parse (context, &argc, &argv, &error))
    return usage (context, "Failed to parse options: %s", error->message);

  resolved_mode = mode != NULL ? mode : "perform";

  if (!parse_installer_mode (resolved_mode,
                             &parsed_mode,
                             &error))
    return fail (EXIT_INVALID_ARGUMENTS, "%s", error->message);

  installation = eos_updater_get_flatpak_installation (NULL, &error);

  if (installation == NULL)
    return fail (EXIT_FAILED, "Could not get flatpak installation: %s", error->message);

  g_message ("Running in mode ‘%s’", resolved_mode);

  if (also_pull)
    g_message ("Will pull flatpaks as well as deploying them");

  /* Check mode is completely different — we need to read in the action
   * application state and check if there’s a delta between what we expect
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
          g_autoptr(GHashTable) flatpak_ref_actions_to_check = NULL;
          g_autoptr(GPtrArray) squashed_ref_actions_to_check = NULL;
          g_autofree gchar *formatted_flatpak_ref_actions_to_check = NULL;
          g_autofree gchar *formatted_ordered_flatpak_ref_actions_to_check = NULL;

          flatpak_ref_actions_to_check = eufi_determine_flatpak_ref_actions_to_check (NULL, &error);

          if (flatpak_ref_actions_to_check == NULL)
            return fail (EXIT_FAILED, 
                         "Could not get information on which flatpak ref actions to check: %s",
                         error->message);

          squashed_ref_actions_to_check = euu_flatten_flatpak_ref_actions_table (flatpak_ref_actions_to_check);

          formatted_flatpak_ref_actions_to_check =
            euu_format_all_flatpak_ref_actions ("All flatpak ref actions that should have been applied",
                                                flatpak_ref_actions_to_check);
          g_message ("%s", formatted_flatpak_ref_actions_to_check);

          formatted_ordered_flatpak_ref_actions_to_check =
            euu_format_flatpak_ref_actions_array ("Order in which actions will be checked",
                                                  squashed_ref_actions_to_check);
          g_message ("%s", formatted_ordered_flatpak_ref_actions_to_check);

          if (dry_run)
            return EXIT_OK;

          if (!eufi_check_ref_actions_applied (installation,
                                               squashed_ref_actions_to_check,
                                               &error))
            return fail (EXIT_CHECK_FAILED,
                         "Flatpak ref actions are not up to date: %s",
                         error->message);

          break;
        }
      case EU_INSTALLER_MODE_PERFORM:
      case EU_INSTALLER_MODE_STAMP:
        {
          g_autoptr(GHashTable) new_flatpak_ref_actions_to_apply = NULL;
          g_autoptr(GPtrArray) squashed_ref_actions_to_apply = NULL;
          g_autofree gchar *formatted_flatpak_ref_actions_to_apply = NULL;
          g_autofree gchar *formatted_ordered_flatpak_ref_actions_to_apply = NULL;

          new_flatpak_ref_actions_to_apply = eufi_determine_flatpak_ref_actions_to_apply (NULL, &error);

          if (new_flatpak_ref_actions_to_apply == NULL)
            return fail (EXIT_FAILED,
                         "Could not get information on which flatpak ref actions to apply: %s",
                         error->message);

          squashed_ref_actions_to_apply = euu_flatten_flatpak_ref_actions_table (new_flatpak_ref_actions_to_apply);

          g_autoptr(GPtrArray) squashed_ref_actions_to_apply_with_dependencies = NULL;

          if (also_pull)
            {
              /* We can only add the dependencies when also pulling (which only
               * happens when e-u-f-i is run manually). When not pulling, the
               * dependencies should have been pulled and deployed before
               * reboot already. */
              squashed_ref_actions_to_apply_with_dependencies =
                  euu_add_dependency_ref_actions_for_installation (installation,
                                                                   squashed_ref_actions_to_apply,
                                                                   NULL,
                                                                   &error);

              if (squashed_ref_actions_to_apply_with_dependencies == NULL)
                return fail (EXIT_FAILED,
                             "Could not get dependencies for flatpak ref actions: %s",
                             error->message);
            }
          else
            {
              squashed_ref_actions_to_apply_with_dependencies = g_ptr_array_ref (squashed_ref_actions_to_apply);
            }

          formatted_flatpak_ref_actions_to_apply =
            euu_format_all_flatpak_ref_actions ("All flatpak ref actions that are not yet applied",
                                                new_flatpak_ref_actions_to_apply);
          g_message ("%s", formatted_flatpak_ref_actions_to_apply);

          const gchar *msg = also_pull ?
              "Order in which actions will be applied (with dependencies)" :
              "Order in which actions will be applied";
          formatted_ordered_flatpak_ref_actions_to_apply =
            euu_format_flatpak_ref_actions_array (msg,
                                                  squashed_ref_actions_to_apply_with_dependencies);
          g_message ("%s", formatted_ordered_flatpak_ref_actions_to_apply);

          if (dry_run)
            return EXIT_OK;

          if (!eufi_apply_flatpak_ref_actions (installation,
                                               euu_pending_flatpak_deployments_state_path (),
                                               squashed_ref_actions_to_apply_with_dependencies,
                                               parsed_mode,
                                               also_pull ? EU_INSTALLER_FLAGS_ALSO_PULL : EU_INSTALLER_FLAGS_NONE,
                                               &error))
            return fail (EXIT_APPLY_FAILED,
                         "Couldn’t apply some flatpak update actions for this boot: %s",
                         error->message);

          break;
        }
      default:
        g_assert_not_reached ();
    }

  return EXIT_OK;
}
