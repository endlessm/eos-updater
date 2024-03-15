/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2024 Endless OS Foundation LLC
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

#include "config.h"

#include <libeos-updater-util/checkpoint-private.h>
#include <glib/gi18n.h>

static gboolean
is_nvme_remap_in_use (OstreeSysroot *sysroot)
{
  g_autoptr(GFile) driver_dir = NULL;
  g_autoptr(GFileEnumerator) enumerator = NULL;
  g_autoptr(GError) error = NULL;
  GFileInfo *info = NULL;

  driver_dir = g_file_resolve_relative_path (ostree_sysroot_get_path (sysroot), "sys/bus/pci/drivers/intel-nvme-remap");
  enumerator = g_file_enumerate_children (driver_dir, G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK, G_FILE_QUERY_INFO_NONE, NULL, &error);

  if (enumerator == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to enumerate %s: %s", g_file_peek_path (driver_dir), error->message);
      return FALSE;
    }

  while (TRUE)
    {
      if (!g_file_enumerator_iterate (enumerator, &info, NULL, NULL, &error))
        {
          g_warning ("Error while enumerating %s: %s", g_file_peek_path (driver_dir), error->message);
          /* The driver is present, and something went wrong: assume it's in use */
          return TRUE;
        }

      if (info == NULL)
        break;

      const char *name = g_file_info_get_name (info);
      g_debug ("Considering '%s'", name);
      if (g_file_info_get_is_symlink (info) &&
          g_str_has_prefix (name, "0000:"))
        {
          g_debug ("Symbolic link %s indicates that nvme-remap is in use", name);
          return TRUE;
        }

      g_debug ("'%s' not a symlink or doesn't begin with '0000:', the search continues", name);
    }

  return FALSE;
}

/**
 * euu_should_follow_checkpoint:
 * @sysroot: the filesystem root of the system being upgraded
 * @booted_ref: the currently-booted ref
 * @target_ref: the candidate new ref to move to
 * @out_reason: (out): a human-readable reason not to follow the checkpoint
 *
 * Whether the upgrade should follow the given checkpoint and move to the given
 * @target_ref for the upgrade deployment. The default for this is %TRUE, but
 * there are various systems for which support has been withdrawn, which need
 * to stay on old branches. In those cases, this function will return %FALSE
 * and will set a human-readable reason for this in @out_reason.
 */
gboolean
euu_should_follow_checkpoint (OstreeSysroot     *sysroot,
                              const gchar       *booted_ref,
                              const gchar       *target_ref,
                              gchar            **out_reason)
{
  g_return_val_if_fail (OSTREE_IS_SYSROOT (sysroot), FALSE);
  g_return_val_if_fail (booted_ref != NULL, FALSE);
  g_return_val_if_fail (target_ref != NULL, FALSE);
  g_return_val_if_fail (out_reason != NULL, FALSE);
  g_return_val_if_fail (*out_reason == NULL, FALSE);

  /* Allow an override in case the logic below is incorrect or doesn’t age well. */
  if (g_strcmp0 (g_getenv ("EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT"), "1") == 0)
    {
      g_message ("Forcing checkpoint target ‘%s’ to be used as EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT=1 is set",
                  target_ref);
      return TRUE;
    }

  /* And an override in the opposite direction, for testing */
  if (g_strcmp0 (g_getenv ("EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT"), "0") == 0)
    {
      g_message ("Forcing checkpoint target ‘%s’ not to be used as EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT=0 is set",
                  target_ref);
      *out_reason = g_strdup ("EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT=0 is set");
      return FALSE;
    }

  if (is_nvme_remap_in_use (sysroot))
    {
      *out_reason = g_strdup (_("This device uses remapped NVME storage, which is not supported in Endless OS 6"));
      return FALSE;
    }

  /* Checkpoint can be followed. */
  return TRUE;
}

