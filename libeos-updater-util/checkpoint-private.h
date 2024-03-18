/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2024 Endless OS Foundation LLC <maintainers@endlessos.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <ostree.h>

G_BEGIN_DECLS

#define EUU_CHECKPOINT_BLOCK (euu_checkpoint_block_quark ())
GQuark euu_checkpoint_block_quark (void);

/**
 * EuuCheckpointBlock:
 * @EUU_CHECKPOINT_BLOCK_NVME_REMAP: The system uses the nvme-remap driver
 *
 * Reasons why eos-updater may block the system from crossing a checkpoint.
 */
typedef enum {
  EUU_CHECKPOINT_BLOCK_FORCED,
  EUU_CHECKPOINT_BLOCK_NVME_REMAP,
} EuuCheckpointBlock;

const char *euu_checkpoint_block_to_string (EuuCheckpointBlock reason);

gboolean euu_should_follow_checkpoint (OstreeSysroot  *sysroot,
                                       const gchar    *booted_ref,
                                       const gchar    *target_ref,
                                       GError        **error);
G_END_DECLS
