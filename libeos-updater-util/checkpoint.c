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

  /* Checkpoint can be followed. */
  return TRUE;
}

