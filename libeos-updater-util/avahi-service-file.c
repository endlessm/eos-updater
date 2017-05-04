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

#include <gio/gio.h>
#include <glib.h>
#include <libeos-updater-util/avahi-service-file.h>
#include <libeos-updater-util/util.h>
#include <string.h>

#include "ostree-bloom-private.h"

#ifndef EOS_AVAHI_PORT
#error "EOS_AVAHI_PORT is not defined"
#endif

const gchar * const EOS_UPDATER_AVAHI_SERVICE_TYPE = "_eos_updater._tcp";

const gchar * const eos_avahi_v1_ostree_path = "eos_ostree_path";
const gchar * const eos_avahi_v1_head_commit_timestamp = "eos_head_commit_timestamp";

typedef enum
  {
    TXT_VALUE_TYPE_TEXT,
    TXT_VALUE_TYPE_BINARY,
  } TxtValueType;

static TxtValueType
classify_txt_value (GVariant *txt_value)
{
  gboolean txt_value_is_text = g_variant_is_of_type (txt_value,
                                                     G_VARIANT_TYPE_STRING);
  gboolean txt_value_is_binary = g_variant_is_of_type (txt_value,
                                                       G_VARIANT_TYPE_BYTESTRING);

  g_assert (txt_value_is_binary || txt_value_is_text);

  if (txt_value_is_text)
    return TXT_VALUE_TYPE_TEXT;

  return TXT_VALUE_TYPE_BINARY;
}

static gboolean
check_record_size (const gchar  *key,
                   gsize         record_size,
                   GError      **error)
{
  if (record_size > 255)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "the TXT record with key %s is longer than 255 bytes", key);
      return FALSE;
    }

  return TRUE;
}

static gboolean
handle_text_record (GString      *records_str,
                    gsize        *total_size,
                    const gchar  *key,
                    GVariant     *text_value,
                    GError      **error)
{
  gsize value_string_length;
  const gchar *value_string = g_variant_get_string (text_value,
                                                    &value_string_length);
  gsize key_length = strlen (key);
  g_autofree gchar *escaped_key = NULL;
  g_autofree gchar *escaped_value = NULL;
  gsize record_size = value_string_length + key_length + 1;

  if (!check_record_size (key, record_size, error))
    return FALSE;

  /* txt records are pascal strings, so one byte for length and then
   * payload.
   */
  g_assert (total_size != NULL);
  *total_size += 1 + record_size;
  escaped_key = g_markup_escape_text (key, key_length);
  escaped_value = g_markup_escape_text (value_string, value_string_length);
  g_string_append_printf (records_str,
                          "    <txt-record>%s=%s</txt-record>\n",
                          escaped_key,
                          escaped_value);
  return TRUE;
}

static gboolean
handle_binary_record (GString      *records_str,
                      gsize        *total_size,
                      const gchar  *key,
                      GVariant     *binary_value,
                      GError      **error)
{
  gsize value_data_length;
  gconstpointer value_data = g_variant_get_fixed_array (binary_value,
                                                        &value_data_length,
                                                        1);
  gsize key_length = strlen (key);
  g_autofree gchar *escaped_key = NULL;
  g_autofree gchar *encoded_value = NULL;
  g_autofree gchar *escaped_value = NULL;
  gsize record_size = value_data_length + key_length + 1;

  if (!check_record_size (key, record_size, error))
    return FALSE;

  /* txt records are pascal strings, so one byte for length and then
   * payload.
   */
  g_assert (total_size != NULL);
  *total_size += 1 + record_size;
  escaped_key = g_markup_escape_text (key, key_length);
  encoded_value = g_base64_encode (value_data, value_data_length);
  escaped_value = g_markup_escape_text (encoded_value, -1);
  g_string_append_printf (records_str,
                          "    <txt-record value-format=\"binary-base64\">%s=%s</txt-record>\n",
                          escaped_key,
                          escaped_value);
  return TRUE;
}

static gboolean
get_and_check_txt_records_size_level (GVariantDict  *options_dict,
                                      guint8        *out_size_level,
                                      GError       **error)
{
  guint8 size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_DNS_MESSAGE;

  g_variant_dict_lookup (options_dict, EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y,
                         "y", &size_level);
  switch (size_level)
    {
    case EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM:
      if (!g_variant_dict_lookup (options_dict,
                                  EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_CUSTOM_SIZE_T,
                                  "t",
                                  NULL))
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "custom size level set, but no custom size "
                               "limit passed to the options or it is of wrong type");
          return FALSE;
        }
      /* fall through */

    case EOS_OSTREE_AVAHI_SIZE_LEVEL_SUPPORT_FAULTY_HARDWARE:
    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_DNS_MESSAGE:
    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_ETHERNET_PACKET:
    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_MULTICAST_DNS_PACKET:
    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_16_BIT_LIMIT:
    case EOS_OSTREE_AVAHI_SIZE_LEVEL_ABSOLUTELY_LAX:
      if (out_size_level)
        *out_size_level = size_level;
      return TRUE;

    default:
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "unknown value %" G_GUINT16_FORMAT " for the %s option",
                   (guint16) size_level,
                   EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y);
      return FALSE;
    }
}

static gboolean
validate_total_size (gsize          total_size,
                     GVariantDict  *options_dict,
                     GError       **error)
{
  guint8 size_level;
  guint64 limit;

  if (!get_and_check_txt_records_size_level (options_dict, &size_level, error))
    return FALSE;

  switch (size_level)
    {
    case EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM:
      {
        gboolean result = g_variant_dict_lookup (options_dict,
                                                 EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_CUSTOM_SIZE_T,
                                                 "t",
                                                 &limit);
        g_assert (result);
      }
      break;

    case EOS_OSTREE_AVAHI_SIZE_LEVEL_SUPPORT_FAULTY_HARDWARE:
      limit = 256;
      break;

    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_DNS_MESSAGE:
      limit = 400;
      break;

    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_ETHERNET_PACKET:
      limit = 1300;
      break;

    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_MULTICAST_DNS_PACKET:
      limit = 8900;
      break;

    case EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_16_BIT_LIMIT:
      limit = G_MAXUINT16;
      break;

    case EOS_OSTREE_AVAHI_SIZE_LEVEL_ABSOLUTELY_LAX:
      return TRUE;

    default:
      g_assert_not_reached ();
    }

  if (total_size > limit)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "TXT records of size %" G_GSIZE_FORMAT " break "
                   "the limit of %" G_GUINT64_FORMAT " bytes",
                   total_size, limit);
      return FALSE;
    }

  return TRUE;
}

static gchar *
txt_records_to_string (GVariant      *txt_records,
                       GVariantDict  *options_dict,
                       GError       **error)
{
  GVariantIter iter;
  g_autoptr(GString) str = NULL;
  GVariant *txt_value;
  const gchar *txt_key;
  gsize total_size = 0;

  g_assert (g_variant_is_of_type (txt_records, G_VARIANT_TYPE ("a(sv)")));

  str = g_string_new ("");
  g_variant_iter_init (&iter, txt_records);
  while (g_variant_iter_loop (&iter, "(&sv)", &txt_key, &txt_value))
    {
      TxtValueType txt_value_type = classify_txt_value (txt_value);

      switch (txt_value_type)
        {
        case TXT_VALUE_TYPE_TEXT:
          if (!handle_text_record (str, &total_size, txt_key, txt_value, error))
            return FALSE;
          break;

        case TXT_VALUE_TYPE_BINARY:
          if (!handle_binary_record (str, &total_size, txt_key, txt_value, error))
            return FALSE;
          break;

        default:
          g_assert_not_reached ();
        }
    }

  if (!validate_total_size (total_size,
                            options_dict,
                            error))
    return NULL;

  return g_string_free (g_steal_pointer (&str), FALSE);
}

static GBytes *
generate_from_avahi_service_template (const gchar   *name,
                                      const gchar   *type,
                                      guint16        port,
                                      GVariant      *txt_records,
                                      GVariantDict  *options_dict,
                                      GError       **error)
{
  g_autofree gchar *service_group = NULL;
  gsize service_group_len;  /* bytes, not including trailing nul */
  g_autofree gchar *txt_records_str = txt_records_to_string (txt_records,
                                                             options_dict,
                                                             error);
  g_autofree gchar *type_escaped = NULL;
  g_autofree gchar *name_escaped = NULL;

  if (txt_records_str == NULL)
    return NULL;

  type_escaped = g_markup_escape_text (type, -1);
  name_escaped = g_markup_escape_text (name, -1);
  service_group = g_strdup_printf (
      "<service-group>\n"
      "  <name replace-wildcards=\"yes\">%s</name>\n"
      "  <service>\n"
      "    <type>%s</type>\n"
      "    <port>%" G_GUINT16_FORMAT "</port>\n"
      "%s"
      "  </service>\n"
      "</service-group>\n",
      name_escaped, type_escaped, port, txt_records_str);
  service_group_len = strlen (service_group);

  return g_bytes_new_take (g_steal_pointer (&service_group), service_group_len);
}

static gboolean
generate_avahi_service_template_to_file (GFile         *path,
                                         const gchar   *name,
                                         const gchar   *type,
                                         guint16        port,
                                         GVariant      *txt_records,
                                         GVariantDict  *options_dict,
                                         GCancellable  *cancellable,
                                         GError       **error)
{
  g_autoptr(GBytes) contents = NULL;
  gconstpointer raw;
  gsize raw_len;
  g_autoptr(GVariant) reffed_txt_records = g_variant_ref_sink (txt_records);

  contents = generate_from_avahi_service_template (name,
                                                   type,
                                                   port,
                                                   reffed_txt_records,
                                                   options_dict,
                                                   error);
  if (contents == NULL)
    return FALSE;

  raw = g_bytes_get_data (contents, &raw_len);
  return g_file_replace_contents (path,
                                  raw,
                                  raw_len,
                                  NULL,  /* old ETag */
                                  FALSE,  /* make backup */
                                  G_FILE_CREATE_NONE,
                                  NULL,  /* new ETag */
                                  cancellable,
                                  error);
}

static gboolean
generate_v1_service_file (const gchar   *ostree_path,
                          GDateTime     *head_commit_timestamp,
                          GFile         *service_file,
                          GCancellable  *cancellable,
                          GError       **error)
{
  g_autofree gchar *timestamp_str = NULL;
  const GVariantType *records_type = G_VARIANT_TYPE ("a(sv)");
  g_auto(GVariantBuilder) records_builder = G_VARIANT_BUILDER_INIT (records_type);
  g_auto(GVariantDict) empty_options_dict = G_VARIANT_DICT_INIT (NULL);

  timestamp_str = g_date_time_format (head_commit_timestamp, "%s");
  g_variant_builder_add (&records_builder, "(sv)",
                         "eos_txt_version",
                         g_variant_new_string ("1"));
  g_variant_builder_add (&records_builder, "(sv)",
                         eos_avahi_v1_ostree_path,
                         g_variant_new_string (ostree_path));
  g_variant_builder_add (&records_builder, "(sv)",
                         eos_avahi_v1_head_commit_timestamp,
                         g_variant_new_string (timestamp_str));

  return generate_avahi_service_template_to_file (service_file,
                                                  "EOS update service on %h",
                                                  EOS_UPDATER_AVAHI_SERVICE_TYPE,
                                                  EOS_AVAHI_PORT,
                                                  g_variant_builder_end (&records_builder),
                                                  &empty_options_dict,
                                                  cancellable,
                                                  error);
}

/**
 * eos_avahi_service_file_get_directory:
 *
 * Get the path of the directory where Avahi will look for `.service` files
 * advertising DNS-SD services. The directory might not have a trailing slash.
 *
 * This may be overridden by specifying the
 * `EOS_UPDATER_TEST_UPDATER_AVAHI_SERVICES_DIR` environment variable. This is
 * intended for testing only.
 *
 * Returns: service file directory
 */
const gchar *
eos_avahi_service_file_get_directory (void)
{
  return eos_updater_get_envvar_or ("EOS_UPDATER_TEST_UPDATER_AVAHI_SERVICES_DIR",
                                    SYSCONFDIR "/avahi/services");
}

static GFile *
get_service_file (const gchar *avahi_service_directory)
{
  g_autofree gchar *service_file_path = NULL;

  service_file_path = g_build_filename (avahi_service_directory,
                                        "eos-updater.service", NULL);
  return g_file_new_for_path (service_file_path);
}

/**
 * eos_avahi_service_file_generate:
 * @avahi_service_directory: path to the directory containing `.service` files
 * @ostree_path: OSTree path of the commit to advertise
 * @head_commit_timestamp: (transfer none): timestamp of the commit to
 *    advertise
 * @cancellable: (nullable): a #GCancellable
 * @error: return location for a #GError
 *
 * Create a `.service` file in @avahi_service_directory for the updater. This
 * instructs Avahi to advertise a DNS-SD service for the updater, with TXT
 * records indicating this machine has the refs for @ostree_path available with
 * a commit at @head_commit_timestamp.
 *
 * The latest version of the DNS-SD record structure will be used, and a
 * version record will be added if appropriate.
 *
 * If the `.service` file already exists, it will be atomically replaced. If the
 * @avahi_service_directory does not exist, or is not writeable, an error will
 * be returned. If an error is returned, the old file will remain in place (if
 * it exists), unmodified.
 *
 * @ostree_path should have the same format as returned by
 * eos_updater_get_ostree_path().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
eos_avahi_service_file_generate (const gchar   *avahi_service_directory,
                                 const gchar   *ostree_path,
                                 GDateTime     *head_commit_timestamp,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  g_autoptr(GFile) service_file = NULL;

  g_return_val_if_fail (avahi_service_directory != NULL, FALSE);
  g_return_val_if_fail (ostree_path != NULL, FALSE);
  g_return_val_if_fail (head_commit_timestamp != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  service_file = get_service_file (avahi_service_directory);

  return generate_v1_service_file (ostree_path, head_commit_timestamp,
                                   service_file, cancellable, error);
}

static gboolean
delete_file_if_exists (GFile         *file,
                       GCancellable  *cancellable,
                       GError       **error)
{
  g_autoptr(GError) local_error = NULL;

  if (!g_file_delete (file, cancellable, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

/**
 * eos_avahi_service_file_delete:
 * @avahi_service_directory: path to the directory containing `.service` files
 * @cancellable: (nullable): a #GCancellable
 * @error: return location for a #GError
 *
 * Delete the updater’s `.service` file from the @avahi_service_directory. This
 * has the same semantics as g_file_delete(); except if no `.service` file
 * exists, or if @avahi_service_directory does not exist, %TRUE is returned.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
eos_avahi_service_file_delete (const gchar   *avahi_service_directory,
                               GCancellable  *cancellable,
                               GError       **error)
{
  g_autoptr(GFile) service_file = NULL;

  g_return_val_if_fail (avahi_service_directory != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  service_file = get_service_file (avahi_service_directory);

  if (!delete_file_if_exists (service_file, cancellable, error))
    return FALSE;

  return TRUE;
}

/* new DNS-SD records format for ostree */

#define EOS_OSTREE_AVAHI_SERVICE_TYPE "_ostree_repo._tcp"

#define EOS_OSTREE_AVAHI_VERSION_FIELD "v"
#define EOS_OSTREE_AVAHI_VERSION_VARIANT_TYPE G_VARIANT_TYPE_BYTE

#define EOS_OSTREE_AVAHI_V1_REFS_BLOOM_FILTER_FIELD "rb"
#define EOS_OSTREE_AVAHI_V1_REFS_BLOOM_FILTER_VARIANT_TYPE (G_VARIANT_TYPE ("(yyay)"))
#define EOS_OSTREE_AVAHI_V1_SUMMARY_TIMESTAMP_FIELD "st"
#define EOS_OSTREE_AVAHI_V1_SUMMARY_TIMESTAMP_VARIANT_TYPE G_VARIANT_TYPE_UINT64
#define EOS_OSTREE_AVAHI_V1_REPOSITORY_INDEX_FIELD "ri"
#define EOS_OSTREE_AVAHI_V1_REPOSITORY_INDEX_VARIANT_TYPE G_VARIANT_TYPE_UINT16

static GFile *
get_ostree_service_file (const gchar *avahi_service_directory,
                         guint16      repository_index)
{
  g_autofree gchar *filename = NULL;
  g_autofree gchar *service_file_path = NULL;

  filename = g_strdup_printf ("eos-ostree-updater-%" G_GUINT16_FORMAT ".service",
                              repository_index);
  service_file_path = g_build_filename (avahi_service_directory,
                                        filename,
                                        NULL);
  return g_file_new_for_path (service_file_path);
}

static guint16
get_repository_index (GVariantDict *options_dict)
{
  guint16 index = 0;

  g_variant_dict_lookup (options_dict, EOS_OSTREE_AVAHI_OPTION_REPO_INDEX_Q,
                         "q", &index);

  return index;
}

static gboolean
get_and_check_avahi_service_port (GVariantDict  *options_dict,
                                  guint16       *out_port,
                                  GError       **error)
{
  // FIXME: Should we store the port number in the configuration
  // instead having it as the compile-time constant? I can't remember
  // why I opted for this solution (was it lack of the configuration
  // file?)
  //
  // In case when config file doesn't specify the port number and the
  // number wasn't provided with the options_dict variant, likely bail
  // out.
  guint16 port = EOS_AVAHI_PORT;

  g_variant_dict_lookup (options_dict, EOS_OSTREE_AVAHI_OPTION_PORT_Q,
                         "q", &port);

  if (port == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "invalid port number 0");
      return FALSE;
    }

  if (out_port)
    *out_port = port;
  return TRUE;
}

static gboolean
get_and_check_bloom_size (GVariantDict  *options_dict,
                          guint32       *out_bloom_size,
                          GError       **error)
{
  /* 255 bytes is a maximum size of the key=value TXT record pair.  We
   * subtract the length of the key name, then 1 byte for =, 1 byte
   * for bloom k and 1 byte for hash id. There is no space reserved
   * for the array of bytes being the bloom filter bits, because it is
   * the last member of the variant tuple and it is treated specially.
   */
  guint32 max_bloom_size = 255 - strlen (EOS_OSTREE_AVAHI_V1_REFS_BLOOM_FILTER_FIELD) - 3;
  guint32 bloom_size = max_bloom_size;

  g_variant_dict_lookup (options_dict, EOS_OSTREE_AVAHI_OPTION_BLOOM_SIZE_U,
                         "u", &bloom_size);
  if (bloom_size == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "bloom filter size must be greater than zero");
      return FALSE;
    }
  if (bloom_size > max_bloom_size)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "bloom filter with size %" G_GUINT32_FORMAT " is too "
                   "large to be sent via DNS-SD records, maximum allowed "
                   "size is %" G_GUINT32_FORMAT,
                   bloom_size, max_bloom_size);
      return FALSE;
    }

  if (out_bloom_size)
    *out_bloom_size = bloom_size;
  return TRUE;
}

static gboolean
get_and_check_bloom_k (GVariantDict  *options_dict,
                       guint8        *out_bloom_k,
                       GError       **error)
{
  guint8 bloom_k = 1;

  g_variant_dict_lookup (options_dict, EOS_OSTREE_AVAHI_OPTION_BLOOM_K_Y,
                         "y", &bloom_k);
  if (bloom_k == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "bloom k parameter must be greater than zero");
      return FALSE;
    }

  if (out_bloom_k)
    *out_bloom_k = bloom_k;
  return TRUE;
}

static gboolean
get_and_check_bloom_hash_func_id (GVariantDict         *options_dict,
                                  guint8               *out_bloom_hash_func_id,
                                  GError              **error)
{
  guint8 bloom_hash_id = EOS_OSTREE_AVAHI_BLOOM_HASH_ID_STR;

  g_variant_dict_lookup (options_dict, EOS_OSTREE_AVAHI_OPTION_BLOOM_HASH_ID_Y,
                         "y", &bloom_hash_id);

  switch (bloom_hash_id)
    {
    case EOS_OSTREE_AVAHI_BLOOM_HASH_ID_STR:
      if (out_bloom_hash_func_id)
        *out_bloom_hash_func_id = bloom_hash_id;
      return TRUE;

    default:
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "unknown bloom hash function id %u", bloom_hash_id);
      return FALSE;
    }
}

static guint8
hash_func_to_id (OstreeBloomHashFunc hash_func)
{
  if (hash_func == ostree_str_bloom_hash)
    return EOS_OSTREE_AVAHI_BLOOM_HASH_ID_STR;

  g_assert_not_reached ();
}

static OstreeBloomHashFunc
id_to_hash_func (guint8 id)
{
  switch (id)
    {
    case EOS_OSTREE_AVAHI_BLOOM_HASH_ID_STR:
      return ostree_str_bloom_hash;

    default:
      g_assert_not_reached ();
    }
}

static gboolean
get_clean_bloom_filter (GVariantDict  *options_dict,
                        OstreeBloom  **out_bloom_filter,
                        GError       **error)
{
  guint32 bloom_size;
  guint8 bloom_k;
  guint8 bloom_hash_func_id;

  if (!get_and_check_bloom_size (options_dict, &bloom_size, error))
    return FALSE;

  if (!get_and_check_bloom_k (options_dict, &bloom_k, error))
    return FALSE;

  if (!get_and_check_bloom_hash_func_id (options_dict,
                                         &bloom_hash_func_id,
                                         error))
    return FALSE;

  g_assert (out_bloom_filter != NULL);
  *out_bloom_filter = ostree_bloom_new (bloom_size,
                                        bloom_k,
                                        id_to_hash_func (bloom_hash_func_id));
  return TRUE;
}

static gboolean
get_bloom_filter_data (const gchar *const  *refs_to_advertise,
                       GVariantDict        *options_dict,
                       guint8              *out_bloom_k,
                       guint8              *out_bloom_hash_func_id,
                       GBytes             **out_bloom_filter_bits,
                       GError             **error)
{
  g_autoptr(OstreeBloom) filter = NULL;
  const gchar *const *iter;

  if (!get_clean_bloom_filter (options_dict, &filter, error))
    return FALSE;

  for (iter = refs_to_advertise; iter != NULL && *iter != NULL; ++iter)
    ostree_bloom_add_element (filter, *iter);

  g_assert (out_bloom_k != NULL);
  g_assert (out_bloom_hash_func_id != NULL);
  g_assert (out_bloom_filter_bits != NULL);
  *out_bloom_k = ostree_bloom_get_k (filter);
  *out_bloom_hash_func_id = hash_func_to_id (ostree_bloom_get_hash_func (filter));
  *out_bloom_filter_bits = ostree_bloom_seal (filter);
  return TRUE;
}

static GVariant *
get_version_variant (guint8 version)
{
  GVariant *variant = g_variant_new ("y", 1);

  g_assert (g_variant_is_of_type (variant,
                                  EOS_OSTREE_AVAHI_VERSION_VARIANT_TYPE));

  return variant;
}

static GVariant *
get_summary_timestamp_variant (guint64 summary_timestamp)
{
  GVariant *variant = g_variant_new ("t", GUINT64_TO_BE (summary_timestamp));

  g_assert (g_variant_is_of_type (variant,
                                  EOS_OSTREE_AVAHI_V1_SUMMARY_TIMESTAMP_VARIANT_TYPE));

  return variant;
}

static GVariant *
get_repository_index_variant (guint16 repository_index)
{
  GVariant *variant = g_variant_new ("q", GUINT16_TO_BE (repository_index));

  g_assert (g_variant_is_of_type (variant,
                                  EOS_OSTREE_AVAHI_V1_REPOSITORY_INDEX_VARIANT_TYPE));

  return variant;
}

static GVariant *
get_bloom_filter_variant (guint8  bloom_k,
                          guint8  bloom_hash_func_id,
                          GBytes *bloom_filter_bits)
{
  gsize data_length;
  gconstpointer data = g_bytes_get_data (bloom_filter_bits, &data_length);
  GVariant *variant = g_variant_new ("(yy@ay)",
                                     bloom_k,
                                     bloom_hash_func_id,
                                     g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                                                data,
                                                                data_length,
                                                                1));

  g_assert (g_variant_is_of_type (variant,
                                  EOS_OSTREE_AVAHI_V1_REFS_BLOOM_FILTER_VARIANT_TYPE));

  return variant;
}

static GVariant *
variant_to_binary_variant (GVariant *variant)
{
  g_autoptr(GVariant) reffed_variant = g_variant_ref_sink (variant);
  g_autoptr(GBytes) bytes = g_variant_get_data_as_bytes (reffed_variant);

  return g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                    g_bytes_get_data (bytes, NULL),
                                    g_bytes_get_size (bytes),
                                    1);
}

static gboolean
generate_ostree_avahi_v1_service_file_from_variants (GFile         *service_file,
                                                     guint16        port,
                                                     GVariant      *version_variant,
                                                     GVariant      *refs_bloom_filter_variant,
                                                     GVariant      *summary_timestamp_variant,
                                                     GVariant      *repository_index_variant,
                                                     GVariantDict  *options_dict,
                                                     GCancellable  *cancellable,
                                                     GError       **error)
{
  const GVariantType *records_type = G_VARIANT_TYPE ("a(sv)");
  g_auto(GVariantBuilder) records_builder = G_VARIANT_BUILDER_INIT (records_type);

  g_variant_builder_add (&records_builder, "(sv)",
                         EOS_OSTREE_AVAHI_VERSION_FIELD,
                         variant_to_binary_variant (g_steal_pointer (&version_variant)));
  // FIXME: Maybe split the rb field for overlong bloom filters into
  // rb1 which would be of gvariant type (yyay), and the followup rbX
  // fields, for X > 1, which would be simply of gvariant type ay).
  g_variant_builder_add (&records_builder, "(sv)",
                         EOS_OSTREE_AVAHI_V1_REFS_BLOOM_FILTER_FIELD,
                         variant_to_binary_variant (g_steal_pointer (&refs_bloom_filter_variant)));
  g_variant_builder_add (&records_builder, "(sv)",
                         EOS_OSTREE_AVAHI_V1_SUMMARY_TIMESTAMP_FIELD,
                         variant_to_binary_variant (g_steal_pointer (&summary_timestamp_variant)));
  g_variant_builder_add (&records_builder, "(sv)",
                         EOS_OSTREE_AVAHI_V1_REPOSITORY_INDEX_FIELD,
                         variant_to_binary_variant (g_steal_pointer (&repository_index_variant)));

  return generate_avahi_service_template_to_file (service_file,
                                                  "EOS OSTree update service on %h",
                                                  EOS_OSTREE_AVAHI_SERVICE_TYPE,
                                                  port,
                                                  g_variant_builder_end (&records_builder),
                                                  options_dict,
                                                  cancellable,
                                                  error);
}

static gboolean
get_unix_summary_timestamp (GDateTime  *summary_timestamp,
                            guint64    *out_summary_timestamp_unix,
                            GError    **error)
{
  gint64 timestamp_unix = g_date_time_to_unix (summary_timestamp);

  if (timestamp_unix < 0)
    {
      g_autofree gchar *formatted = g_date_time_format (summary_timestamp, "%FT%T%:z");
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "invalid summary timestamp %s", formatted);
      return FALSE;
    }

  g_assert (out_summary_timestamp_unix != NULL);
  *out_summary_timestamp_unix = (guint64)timestamp_unix;
  return TRUE;
}

static gboolean
generate_ostree_avahi_v1_service_file (const gchar         *avahi_service_directory,
                                       const gchar *const  *refs_to_advertise,
                                       GDateTime           *summary_timestamp,
                                       GVariantDict        *options_dict,
                                       GCancellable        *cancellable,
                                       GError             **error)
{
  guint8 bloom_k;
  guint8 bloom_hash_func_id;
  g_autoptr(GBytes) bloom_filter_bits = NULL;
  guint16 port;
  guint16 repository_index;
  guint64 summary_timestamp_unix;
  g_autoptr(GFile) service_file = NULL;

  if (!get_bloom_filter_data (refs_to_advertise,
                              options_dict,
                              &bloom_k,
                              &bloom_hash_func_id,
                              &bloom_filter_bits,
                              error))
    return FALSE;

  if (!get_and_check_avahi_service_port (options_dict, &port, error))
    return FALSE;

  if (!get_unix_summary_timestamp (summary_timestamp, &summary_timestamp_unix, error))
    return FALSE;

  repository_index = get_repository_index (options_dict);
  service_file = get_ostree_service_file (avahi_service_directory,
                                          repository_index);
  return generate_ostree_avahi_v1_service_file_from_variants (service_file,
                                                              port,
                                                              get_version_variant (1),
                                                              get_bloom_filter_variant (bloom_k,
                                                                                        bloom_hash_func_id,
                                                                                        bloom_filter_bits),
                                                              get_summary_timestamp_variant (summary_timestamp_unix),
                                                              get_repository_index_variant (repository_index),
                                                              options_dict,
                                                              cancellable,
                                                              error);
}

static gboolean
get_and_check_version (GVariantDict  *options_dict,
                       guint8        *out_version,
                       GError       **error)
{
  /* This can't be changed, otherwise it may break the code that does
   * not force the version in options, so assumes that version 1 will
   * be used.
   */
  guint8 version = 1;

  g_variant_dict_lookup (options_dict, EOS_OSTREE_AVAHI_OPTION_FORCE_VERSION_Y,
                         "y", &version);
  if (version == 0 || version > 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "unknown TXT record version: %u", version);
      return FALSE;
    }

  if (out_version)
    *out_version = version;
  return TRUE;
}

static gboolean
check_v1_options (GVariantDict  *options_dict,
                  GError       **error)
{
  if (!get_and_check_bloom_size (options_dict, NULL, error))
    return FALSE;

  if (!get_and_check_bloom_k (options_dict, NULL, error))
    return FALSE;

  if (!get_and_check_bloom_hash_func_id (options_dict, NULL, error))
    return FALSE;

  if (!get_and_check_avahi_service_port (options_dict, NULL, error))
    return FALSE;

  if (!get_and_check_txt_records_size_level (options_dict, NULL, error))
    return FALSE;

  return TRUE;
}

/**
 * eos_ostree_avahi_service_file_check_options:
 * @options: (nullable): vardict #GVariant
 * @error: return location for a #GError
 *
 * Validates the contents of @options. Unknown keys in @options are
 * ignored. If some key-value pair in @options is not valid in some
 * way, the function will fill the @error variable and return %FALSE.
 *
 * If @options has a floating reference then this function sinks it.
 *
 * Note that this function can not check the real validity of the
 * #EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y key - it only
 * checks if the key has a valid value, but it is not able to check if
 * generated TXT records do not break the imposed limit. This error
 * can be reported only by the
 * eos_ostree_avahi_service_file_generate() function.
 *
 * Returns: whether contents of @options are valid
 */
gboolean
eos_ostree_avahi_service_file_check_options (GVariant  *options,
                                             GError   **error)
{
  g_autoptr(GVariant) reffed_options = (options != NULL) ? g_variant_ref_sink (options) : NULL;
  g_auto(GVariantDict) options_dict = G_VARIANT_DICT_INIT (reffed_options);
  guint8 version;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!get_and_check_version (&options_dict, &version, error))
    return FALSE;

  switch (version)
    {
    case 1:
      return check_v1_options (&options_dict, error);

    default:
      g_assert_not_reached ();
    }
}

/**
 * eos_ostree_avahi_service_file_generate:
 * @avahi_service_directory: path to the directory containing `.service` files
 * @refs_to_advertise: an array of refs to advertise
 * @summary_timestamp: timestamp of the repo summary
 * @options: (nullable): vardict #GVariant
 * @cancellable: (nullable): a #GCancellable
 * @error: return location for a #GError
 *
 * Create a `.service` file in @avahi_service_directory for the updater. This
 * instructs Avahi to advertise a DNS-SD service for the updater, with TXT
 * records indicating this machine has the refs for @ostree_path available with
 * a commit at @head_commit_timestamp.
 *
 * @refs_to_advertise is an array of refs that will be advertised over
 * the network. Note that at least one ref is expected. How the ref is
 * advertised is dependent on the used version of the DNS-SD records.
 *
 * @summary_timestamp describes how old the summary is. Ideally, it
 * should be something that is provided by the source of the summary
 * (like metadata in the summary). As a fallback, a modification time
 * of the locally stored summary file could be used, but it is rather
 * fragile.
 *
 * @options can contain various options, which are dependent on the
 * version of DNS-SD records. For the details, start reading about the
 * %EOS_OSTREE_AVAHI_OPTION_FORCE_VERSION_Y option. If @options is
 * %NULL, default values will be used instead. Default values are
 * described in each options' documentation.
 *
 * If @options has a floating reference then this function sinks it.
 *
 * If the `.service` file already exists, it will be atomically
 * replaced. If the @avahi_service_directory does not exist, or is not
 * writeable, an error will be returned. If an error is returned, the
 * old file will remain in place (if it exists), unmodified.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
eos_ostree_avahi_service_file_generate (const gchar         *avahi_service_directory,
                                        const gchar *const  *refs_to_advertise,
                                        GDateTime           *summary_timestamp,
                                        GVariant            *options,
                                        GCancellable        *cancellable,
                                        GError             **error)
{
  g_autoptr(GVariant) reffed_options = (options != NULL) ? g_variant_ref_sink (options) : NULL;
  g_auto(GVariantDict) options_dict = G_VARIANT_DICT_INIT (reffed_options);
  guint8 version;

  g_return_val_if_fail (avahi_service_directory != NULL, FALSE);
  g_return_val_if_fail (refs_to_advertise != NULL && *refs_to_advertise != NULL, FALSE);
  g_return_val_if_fail (summary_timestamp != NULL, FALSE);
  g_return_val_if_fail (options == NULL ||
                        g_variant_is_of_type (options, G_VARIANT_TYPE_VARDICT),
                        FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!get_and_check_version (&options_dict,
                              &version,
                              error))
    return FALSE;

  switch (version)
    {
    case 1:
      return generate_ostree_avahi_v1_service_file (avahi_service_directory,
                                                    refs_to_advertise,
                                                    summary_timestamp,
                                                    &options_dict,
                                                    cancellable,
                                                    error);

    default:
      g_assert_not_reached ();
    }
}

/**
 * eos_ostree_avahi_service_file_delete:
 * @avahi_service_directory: path to the directory containing `.service` files
 * @repository_index: index of a repository
 * @cancellable: (nullable): a #GCancellable
 * @error: return location for a #GError
 *
 * Delete the updater’s `.service` file for the given repository index
 * from the @avahi_service_directory. This has the same semantics as
 * g_file_delete(); except if no `.service` file exists, or if
 * @avahi_service_directory does not exist, %TRUE is returned.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
eos_ostree_avahi_service_file_delete (const gchar   *avahi_service_directory,
                                      guint16        repository_index,
                                      GCancellable  *cancellable,
                                      GError       **error)
{
  g_autoptr(GFile) service_file = NULL;

  g_return_val_if_fail (avahi_service_directory != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  service_file = get_ostree_service_file (avahi_service_directory,
                                          repository_index);

  if (!delete_file_if_exists (service_file, cancellable, error))
    return FALSE;

  return TRUE;
}

static gboolean
iterate_and_remove_ostree_service_files (GFileEnumerator  *enumerator,
                                         GCancellable     *cancellable,
                                         GError          **error)
{
  g_autoptr(GRegex) re = g_regex_new ("^eos-ostree-updater-([0-9]+)\\.service$",
                                      G_REGEX_OPTIMIZE,
                                      0,
                                      NULL);

  g_assert (re != NULL);

  while (TRUE)
    {
      GFileInfo *file_info;
      GFile* file;
      g_autoptr(GError) local_error = NULL;
      g_autoptr(GMatchInfo) match_info = NULL;
      g_autofree gchar *matched_repository_index = NULL;
      const gchar *filename;

      if (!g_file_enumerator_iterate (enumerator,
                                      &file_info,
                                      &file,
                                      cancellable,
                                      error))
        return FALSE;

      if (file_info == NULL || file == NULL)
        break;

      filename = g_file_info_get_name (file_info);
      if (!g_regex_match (re, filename, 0, &match_info))
        continue;

      matched_repository_index = g_match_info_fetch (match_info, 1);
      if (!eos_string_to_unsigned (matched_repository_index, 10, 0, G_MAXUINT16, NULL, NULL))
        continue;
      if (!delete_file_if_exists (file, cancellable, error))
        return FALSE;
    }

  return TRUE;
}

/**
 * eos_ostree_avahi_service_file_cleanup_directory:
 * @avahi_service_directory: path to the directory containing `.service` files
 * @cancellable: (nullable): a #GCancellable
 * @error: return location for a #GError
 *
 * Delete the updater’s `.service` files for the repository indices in
 * range from 0 to 65535 (inclusive) from the
 * @avahi_service_directory. If other files exist in the directory,
 * they are left untouched. Note that it will not remove the file
 * generated by the eos_avahi_service_file_generate() function.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
eos_ostree_avahi_service_file_cleanup_directory (const gchar   *avahi_service_directory,
                                                 GCancellable  *cancellable,
                                                 GError       **error)
{
  g_autoptr(GFile) dir = NULL;
  g_autoptr(GFileEnumerator) enumerator = NULL;

  g_return_val_if_fail (avahi_service_directory != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  dir = g_file_new_for_path (avahi_service_directory);
  enumerator = g_file_enumerate_children (dir,
                                          "",
                                          G_FILE_QUERY_INFO_NONE,
                                          cancellable,
                                          error);
  if (enumerator == NULL)
    return FALSE;

  return iterate_and_remove_ostree_service_files (enumerator,
                                                  cancellable,
                                                  error);
}
