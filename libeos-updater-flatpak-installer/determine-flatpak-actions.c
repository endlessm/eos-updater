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

#include <glib.h>
#include <libeos-updater-flatpak-installer/installer.h>
#include <libeos-updater-util/enums.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>


static gboolean
flatpak_ref_actions_and_progresses (GStrv        directories_to_search,
                                    GHashTable **out_actions  /* (element-type filename GPtrArray<EuuFlatpakRemoteRefAction>) */,
                                    GHashTable **out_progresses  /* (element-type filename gint32) */,
                                    GError     **error)
{
  g_autoptr(GHashTable) flatpak_ref_actions_for_this_boot = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_progress = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_for_this_boot = NULL;
  g_autofree gchar *formatted_flatpak_ref_actions_progress_for_this_boot = NULL;

  g_assert (out_actions != NULL);
  g_assert (out_progresses != NULL);

  flatpak_ref_actions_for_this_boot = euu_flatpak_ref_actions_from_paths (directories_to_search,
                                                                          error);

  if (flatpak_ref_actions_for_this_boot == NULL)
    {
      g_prefix_error (error,
                      "Could get flatpak ref actions for this OSTree deployment: ");
      return FALSE;
    }

  flatpak_ref_actions_progress = euu_flatpak_ref_action_application_progress_in_state_path (NULL, error);

  if (flatpak_ref_actions_progress == NULL)
    {
      g_prefix_error (error,
                      "Could not get information on which flatpak ref actions have been applied: ");
      return FALSE;
    }

  /* Sysadmin debug output. */
  formatted_flatpak_ref_actions_for_this_boot =
    euu_format_all_flatpak_ref_actions ("Flatpak ref actions that should be applied once this boot is complete",
                                        flatpak_ref_actions_for_this_boot);
  g_message ("%s", formatted_flatpak_ref_actions_for_this_boot);

  formatted_flatpak_ref_actions_progress_for_this_boot = euu_format_all_flatpak_ref_actions_progresses (flatpak_ref_actions_progress);
  g_message ("%s", formatted_flatpak_ref_actions_progress_for_this_boot);

  *out_actions = g_steal_pointer (&flatpak_ref_actions_for_this_boot);
  *out_progresses = g_steal_pointer (&flatpak_ref_actions_progress);

  return TRUE;
}

/* See documentation for euu_filter_for_existing_flatpak_ref_actions(). */
GHashTable *
eufi_determine_flatpak_ref_actions_to_check (GStrv    directories_to_search,
                                             GError **error)
{
  g_autoptr(GHashTable) flatpak_ref_actions_for_this_boot = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_progress = NULL;

  if (!flatpak_ref_actions_and_progresses (directories_to_search,
                                           &flatpak_ref_actions_for_this_boot,
                                           &flatpak_ref_actions_progress,
                                           error))
    return NULL;

  return euu_filter_for_existing_flatpak_ref_actions (flatpak_ref_actions_for_this_boot,
                                                      flatpak_ref_actions_progress);
}

/* See documentation for euu_filter_for_new_flatpak_ref_actions(). */
GHashTable *
eufi_determine_flatpak_ref_actions_to_apply (GStrv    directories_to_search,
                                             GError **error)
{
  g_autoptr(GHashTable) flatpak_ref_actions_for_this_boot = NULL;
  g_autoptr(GHashTable) flatpak_ref_actions_progress = NULL;

  if (!flatpak_ref_actions_and_progresses (directories_to_search,
                                           &flatpak_ref_actions_for_this_boot,
                                           &flatpak_ref_actions_progress,
                                           error))
    return NULL;

  return euu_filter_for_new_flatpak_ref_actions (flatpak_ref_actions_for_this_boot,
                                                 flatpak_ref_actions_progress);
}
