/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

extern const gchar * const EOS_UPDATER_AVAHI_SERVICE_TYPE;
extern const gchar * const eos_avahi_v1_ostree_path;
extern const gchar * const eos_avahi_v1_head_commit_timestamp;

const gchar *eos_avahi_service_file_get_directory (void);

gboolean eos_avahi_service_file_generate (const gchar   *avahi_service_directory,
                                          const gchar   *ostree_path,
                                          GDateTime     *head_commit_timestamp,
                                          GCancellable  *cancellable,
                                          GError       **error);
gboolean eos_avahi_service_file_delete (const gchar   *avahi_service_directory,
                                        GCancellable  *cancellable,
                                        GError       **error);

/**
 * EOS_OSTREE_AVAHI_OPTION_FORCE_VERSION_Y:
 *
 * Tells which version of DNS-SD records should be generated. Also,
 * tells which set of options will be used when during the check or
 * generation.
 *
 * Currently there is only one version available: 1.
 *
 * The options specific for the version 1 are:
 *
 * - %EOS_OSTREE_AVAHI_OPTION_BLOOM_HASH_ID_Y
 * - %EOS_OSTREE_AVAHI_OPTION_BLOOM_K_Y
 * - %EOS_OSTREE_AVAHI_OPTION_BLOOM_SIZE_U
 * - %EOS_OSTREE_AVAHI_OPTION_REPO_INDEX_Q
 * - %EOS_OSTREE_AVAHI_OPTION_PORT_Q
 * - %EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y
 * - %EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_CUSTOM_SIZE_T
 *
 * Default value of this option (if not overridden) is 1.
 */
#define EOS_OSTREE_AVAHI_OPTION_FORCE_VERSION_Y "force-version"
/**
 * EOS_OSTREE_AVAHI_OPTION_BLOOM_HASH_ID_Y:
 *
 * Specifies the ID of the hashing function for the bloom filter. See
 * #EosOstreeAvahiBloomHashId for possible values.
 *
 * Default value of this option (if not overridden) is
 * %EOS_OSTREE_AVAHI_BLOOM_HASH_ID_STR.
 */
#define EOS_OSTREE_AVAHI_OPTION_BLOOM_HASH_ID_Y "bloom-hash-id"
/**
 * EOS_OSTREE_AVAHI_OPTION_BLOOM_K_Y:
 *
 * Specifies the k parameter for the bloom filter. It translates to
 * how many times an element will be hashed before using it to set a
 * bit in the bloom filter.
 *
 * Default value of this option (if not overridden) is 1.
 */
#define EOS_OSTREE_AVAHI_OPTION_BLOOM_K_Y "bloom-k"
/**
 * EOS_OSTREE_AVAHI_OPTION_BLOOM_SIZE_U:
 *
 * Specifies the size of the bloom filter in bytes. Note that it
 * cannot exceed the 250 bytes to fit it in the TXT record. The maths
 * behind it is as follows:
 *
 * The TXT record can have maximum 256 bytes. 1 byte is reserved
 * implicitly for the size of the record (you can think about the
 * record as a pascal string). 2 bytes go for the name of the TXT
 * record (it is "rb" from "refs bloom"). 1 byte go for the equal
 * sign. 1 byte goes for the bloom k paramater and 1 byte goes for the
 * bloom hashing function ID. That gives us 250 bytes max.
 *
 * Default value of this option (if not overridden) is 250.
 */
#define EOS_OSTREE_AVAHI_OPTION_BLOOM_SIZE_U "bloom-size"
/**
 * EOS_OSTREE_AVAHI_OPTION_REPO_INDEX_Q:
 *
 * Specifies the repo index for which the service file will be
 * generated.
 *
 * Default value of this option (if not overridden) is 0.
 */
#define EOS_OSTREE_AVAHI_OPTION_REPO_INDEX_Q "repository-index"
/**
 * EOS_OSTREE_AVAHI_OPTION_PORT_Q:
 *
 * Specifies the port where the server serving the repository contents
 * is listening.
 *
 * Default value of this option (if not overridden) is set at the
 * compilation time.
 */
#define EOS_OSTREE_AVAHI_OPTION_PORT_Q "port"
/**
 * EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y:
 *
 * Specifies the size limit generated TXT records can have. See
 * #EosOstreeAvahiSizeLevel for possible values.
 *
 * Default value of this option (if not overridden) is
 * %EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_DNS_MESSAGE.
 */
#define EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y "txt-records-size-level"
/**
 * EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_CUSTOM_SIZE_T:
 *
 * Specifies the custom size limit. Only applicable if
 * %EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y was set to
 * %EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM.
 *
 * It has no default value - must be specified explicitly.
 */
#define EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_CUSTOM_SIZE_T "txt-records-custom-size"

/**
 * EosOstreeAvahiBloomHashId:
 * @EOS_OSTREE_AVAHI_BLOOM_HASH_ID_STR: Use ostree_str_bloom_hash() for hashing; it takes nul-terminated strings as an input.
 *
 * Possible values for the
 * %EOS_OSTREE_AVAHI_OPTION_BLOOM_HASH_ID_Y option.
 */
typedef enum
  {
    EOS_OSTREE_AVAHI_BLOOM_HASH_ID_STR = 1
  } EosOstreeAvahiBloomHashId;

/**
 * EosOstreeAvahiSizeLevel:
 * @EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM: The size limit is specified in the %EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_CUSTOM_SIZE_T option
 * @EOS_OSTREE_AVAHI_SIZE_LEVEL_SUPPORT_FAULTY_HARDWARE: TXT records size cannot exceed 256 bytes
 * @EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_DNS_MESSAGE: TXT records size cannot exceed approximately 400
 * bytes
 * @EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_ETHERNET_PACKET: TXT records size cannot exceed approximately 1300
 * bytes
 * @EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_MULTICAST_DNS_PACKET: TXT records size cannot exceed approximately 8900
 * bytes
 * @EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_16_BIT_LIMIT: TXT records size cannot exceed %G_MAXUINT16 bytes
 * @EOS_OSTREE_AVAHI_SIZE_LEVEL_ABSOLUTELY_LAX: TXT records size can be of any size
 *
 * Possible values for the
 * %EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y option.
 */
typedef enum
  {
    EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM,
    EOS_OSTREE_AVAHI_SIZE_LEVEL_SUPPORT_FAULTY_HARDWARE,
    EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_DNS_MESSAGE,
    EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_ETHERNET_PACKET,
    EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_MULTICAST_DNS_PACKET,
    EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_16_BIT_LIMIT,
    EOS_OSTREE_AVAHI_SIZE_LEVEL_ABSOLUTELY_LAX
  } EosOstreeAvahiSizeLevel;

G_END_DECLS
