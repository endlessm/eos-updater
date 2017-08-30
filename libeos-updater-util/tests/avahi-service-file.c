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
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libeos-updater-util/avahi-service-file.h>
#include <locale.h>
#include <string.h>

typedef struct
{
  gchar *tmp_dir;
  GDateTime *example_timestamp;  /* owned */
  OstreeCollectionRefv refs;
} Fixture;

/* Set up a temporary directory to create test service files in. */
static void
setup (Fixture       *fixture,
       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;

  fixture->tmp_dir = g_dir_make_tmp ("eos-updater-util-tests-avahi-service-file-XXXXXX",
                                     &error);
  g_assert_no_error (error);
  fixture->refs = g_new0 (OstreeCollectionRef*, 2);
  fixture->refs[0] = ostree_collection_ref_new ("com.example", "ref");
  fixture->refs[1] = NULL;

  fixture->example_timestamp = g_date_time_new_utc (2017, 2, 17, 0, 0, 0);
}

/* Inverse of setup(). */
static void
teardown (Fixture       *fixture,
          gconstpointer  user_data G_GNUC_UNUSED)
{
  g_date_time_unref (fixture->example_timestamp);

  g_assert_cmpint (g_rmdir (fixture->tmp_dir), ==, 0);
  g_free (fixture->tmp_dir);

  ostree_collection_ref_freev (fixture->refs);
}

/* Check the contents of a generated .service file. */
static void
assert_service_file_contents_valid (const gchar *service_file)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *contents = NULL;
  gsize length;

  g_file_get_contents (service_file, &contents, &length, &error);
  g_assert_no_error (error);

  /* The eos_head_commit_timestamp value is the UNIX timestamp version of
   * fixture->example_timestamp. */
  g_assert_cmpstr (contents, ==,
                   "<service-group>\n"
                   "  <name replace-wildcards=\"yes\">EOS update service on %h</name>\n"
                   "  <service>\n"
                   "    <type>_eos_updater._tcp</type>\n"
                   "    <port>" G_STRINGIFY (EOS_AVAHI_PORT) "</port>\n"
                   "    <txt-record>eos_txt_version=1</txt-record>\n"
                   "    <txt-record>eos_ostree_path=ostree-path</txt-record>\n"
                   "    <txt-record>eos_head_commit_timestamp=1487289600</txt-record>\n"
                   "  </service>\n"
                   "</service-group>\n");
  g_assert_cmpuint (length, ==, strlen (contents));
}

/* Test that generating a .service file in an empty directory works. */
static void
test_avahi_service_file_generate_normal (Fixture       *fixture,
                                         gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Generate the file. */
  retval = eos_avahi_service_file_generate (fixture->tmp_dir, "ostree-path",
                                            fixture->example_timestamp,
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  /* Check its contents. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  assert_service_file_contents_valid (service_file);

  /* Tidy up. */
  g_assert_cmpint (g_unlink (service_file), ==, 0);
}

/* Test that generating a .service file in a directory which already contains
 * one overwrites the existing one. */
static void
test_avahi_service_file_generate_overwrite (Fixture       *fixture,
                                            gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Create a stub file. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service", NULL);
  g_file_set_contents (service_file, "overwrite me!", -1, &error);
  g_assert_no_error (error);

  /* Generate the file over the top. */
  retval = eos_avahi_service_file_generate (fixture->tmp_dir, "ostree-path",
                                            fixture->example_timestamp,
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  /* Check its contents. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  assert_service_file_contents_valid (service_file);

  /* Tidy up. */
  g_assert_cmpint (g_unlink (service_file), ==, 0);
}

/* Test that generating a .service file in a non-existent directory fails. */
static void
test_avahi_service_file_generate_nonexistent_directory (Fixture       *fixture,
                                                        gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  gboolean retval;

  subdirectory = g_build_filename (fixture->tmp_dir,
                                   "nonexistent-subdirectory",
                                   NULL);

  /* Try to generate a service file. */
  retval = eos_avahi_service_file_generate (subdirectory, "ostree-path",
                                            fixture->example_timestamp,
                                            NULL, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (retval);

  /* Directory should not have been created. */
  g_assert_false (g_file_test (subdirectory, G_FILE_TEST_EXISTS));
}

/* Test that generating a .service file in a directory we don’t have write
 * permission for fails. */
static void
test_avahi_service_file_generate_denied (Fixture       *fixture,
                                         gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  g_autofree gchar *service_file = NULL;
  g_autofree gchar *test_file = NULL;
  gboolean retval;

  subdirectory = g_build_filename (fixture->tmp_dir,
                                   "unwriteable-subdirectory",
                                   NULL);
  g_assert_cmpint (g_mkdir (subdirectory, 0500), ==, 0);
  service_file = g_build_filename (subdirectory, "eos-updater.service", NULL);

  /* If the test is run as root (or another user with CAP_DAC_OVERRIDE), the
   * user can write any file anyway. */
  test_file = g_build_filename (subdirectory, "permissions-test", NULL);
  if (g_file_set_contents (test_file, "permissions test", -1, NULL))
    {
      g_unlink (test_file);
      g_test_skip ("Test cannot be run as a user with CAP_DAC_OVERRIDE or "
                   "CAP_DAC_READ_SEARCH.");
    }
  else
    {
      /* Try to generate a service file. */
      retval = eos_avahi_service_file_generate (subdirectory, "ostree-path",
                                                fixture->example_timestamp,
                                                NULL, &error);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
      g_assert_false (retval);

      /* File should not have been created. */
      g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));
    }

  /* Clean up. */
  g_assert_cmpint (g_chmod (subdirectory, 0700), ==, 0);
  g_assert_cmpint (g_rmdir (subdirectory), ==, 0);
}

/* Test that deleting an existing .service file works. */
static void
test_avahi_service_file_delete_normal (Fixture       *fixture,
                                       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Create a service file. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  g_file_set_contents (service_file, "irrelevant", -1, &error);
  g_assert_no_error (error);

  /* Try to delete it. */
  retval = eos_avahi_service_file_delete (fixture->tmp_dir, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  /* Actually gone? */
  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));
}

/* Test that deleting a non-existent .service file returns success. */
static void
test_avahi_service_file_delete_nonexistent_file (Fixture       *fixture,
                                                 gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *service_file = NULL;
  gboolean retval;

  /* Double-check no .service file exists. */
  service_file = g_build_filename (fixture->tmp_dir, "eos-updater.service",
                                   NULL);
  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));

  /* Try to delete it. */
  retval = eos_avahi_service_file_delete (fixture->tmp_dir, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
}

/* Test that deleting a .service file from a non-existent directory returns
 * success. */
static void
test_avahi_service_file_delete_nonexistent_directory (Fixture       *fixture,
                                                      gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  gboolean retval;

  /* Double-check the subdirectory doesn’t exist. */
  subdirectory = g_build_filename (fixture->tmp_dir, "some-subdirectory", NULL);
  g_assert_false (g_file_test (subdirectory, G_FILE_TEST_EXISTS));

  /* Try to delete from it. */
  retval = eos_avahi_service_file_delete (subdirectory, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (retval);
}

/* Test that deleting a .service file from a directory we don’t have write
 * permissions on fails. */
static void
test_avahi_service_file_delete_denied (Fixture       *fixture,
                                       gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *subdirectory = NULL;
  g_autofree gchar *service_file = NULL;
  g_autofree gchar *test_file = NULL;
  gboolean retval;

  /* Create a service file. */
  subdirectory = g_build_filename (fixture->tmp_dir, "unwriteable-subdirectory",
                                   NULL);
  g_assert_cmpint (g_mkdir (subdirectory, 0700), ==, 0);

  service_file = g_build_filename (subdirectory, "eos-updater.service", NULL);
  g_file_set_contents (service_file, "irrelevant", -1, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_chmod (subdirectory, 0500), ==, 0);

  /* If the test is run as root (or another user with CAP_DAC_OVERRIDE), the
   * user can write or delete any file anyway. */
  test_file = g_build_filename (subdirectory, "permissions-test", NULL);
  if (g_file_set_contents (test_file, "permissions test", -1, NULL))
    {
      g_unlink (test_file);
      g_test_skip ("Test cannot be run as a user with CAP_DAC_OVERRIDE or "
                   "CAP_DAC_READ_SEARCH.");
    }
  else
    {
      /* Try to delete it. */
      retval = eos_avahi_service_file_delete (subdirectory, NULL, &error);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
      g_assert_false (retval);
    }

  /* Clean up. */
  g_assert_cmpint (g_chmod (subdirectory, 0700), ==, 0);
  g_assert_cmpint (g_unlink (service_file), ==, 0);
  g_assert_cmpint (g_rmdir (subdirectory), ==, 0);
}

static const gchar *
get_encoded_bloom_bits (gboolean short_bloom_size)
{
  /* - AQEAA... - guint8 1, guint8 1, guint8[] of bloom filter bits encoding "ref" */
  if (short_bloom_size)
    return "AQFAAAAAAAAAAAAAAAA=";
  return "AQEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
}

static const gchar *
get_encoded_repository_index (gboolean default_repository_index)
{
  /* - AAA= - big-endian guint16 0
   * - AAY= - big-endian guint16 6
   */
  if (default_repository_index)
    return "AAA=";
  return "AAY=";
}

/* Check the contents of a generated .service file. */
static void
assert_ostree_service_file_contents_valid (const gchar *service_file,
                                           gboolean     short_bloom_size,
                                           gboolean     default_repository_index)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *contents = NULL;
  gsize length;
  const gchar *encoded_bloom_bits;
  const gchar *encoded_repository_index;
  g_autofree gchar *expected_contents = NULL;

  g_file_get_contents (service_file, &contents, &length, &error);
  g_assert_no_error (error);
  encoded_bloom_bits = get_encoded_bloom_bits (short_bloom_size);
  encoded_repository_index = get_encoded_repository_index (default_repository_index);
  /* base64 values below are (note that these are raw numbers, not characters):
   * - AQ== - guint8 1
   * - AAAAAFhoRoA= - big-endian guint64 1483228800 (2017-01-01 00:00:00 UTC)
   */
  expected_contents = g_strdup_printf ("<service-group>\n"
                                       "  <name replace-wildcards=\"yes\">EOS OSTree update service on %%h</name>\n"
                                       "  <service>\n"
                                       "    <type>_ostree_repo._tcp</type>\n"
                                       "    <port>43381</port>\n"
                                       "    <txt-record value-format=\"binary-base64\">v=AQ==</txt-record>\n"
                                       "    <txt-record value-format=\"binary-base64\">rb=%s</txt-record>\n"
                                       "    <txt-record value-format=\"binary-base64\">st=AAAAAFhoRoA=</txt-record>\n"
                                       "    <txt-record value-format=\"binary-base64\">ri=%s</txt-record>\n"
                                       "  </service>\n"
                                       "</service-group>\n",
                                       encoded_bloom_bits,
                                       encoded_repository_index);

  g_assert_cmpstr (contents, ==, expected_contents);
  g_assert_cmpuint (length, ==, strlen (contents));
}

typedef enum
  {
    SET_NOTHING                 = 0,
    SET_FORCE_VERSION           = 1 << 0,
    SET_BLOOM_HASH_ID           = 1 << 1,
    SET_BLOOM_K                 = 1 << 2,
    SET_BLOOM_SIZE              = 1 << 3,
    SET_REPOSITORY_INDEX        = 1 << 4,
    SET_PORT                    = 1 << 5,
    SET_TXT_RECORDS_SIZE_LEVEL  = 1 << 6,
    SET_TXT_RECORDS_CUSTOM_SIZE = 1 << 7,
  } TestSetFlags;

typedef struct
{
  guint32 set_flags;
  guint8 force_version;
  guint8 bloom_hash_id;
  guint8 bloom_k;
  guint32 bloom_size;
  guint16 repository_index;
  guint16 port;
  guint8 txt_records_size_level;
  guint64 txt_records_custom_size;
} AvahiOstreeTestOptions;

#define CHECK_FLAG(flags, flag) (((flags) & (flag)) == (flag))

static GVariant *
avahi_ostree_test_options_to_variant (const AvahiOstreeTestOptions *test_options)
{
  g_auto(GVariantDict) options_dict = G_VARIANT_DICT_INIT (NULL);

  if (CHECK_FLAG (test_options->set_flags, SET_FORCE_VERSION))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_FORCE_VERSION_Y,
                           "y",
                           test_options->force_version);

  if (CHECK_FLAG (test_options->set_flags, SET_BLOOM_HASH_ID))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_BLOOM_HASH_ID_Y,
                           "y",
                           test_options->bloom_hash_id);

  if (CHECK_FLAG (test_options->set_flags, SET_BLOOM_K))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_BLOOM_K_Y,
                           "y",
                           test_options->bloom_k);

  if (CHECK_FLAG (test_options->set_flags, SET_BLOOM_SIZE))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_BLOOM_SIZE_U,
                           "u",
                           test_options->bloom_size);

  if (CHECK_FLAG (test_options->set_flags, SET_REPOSITORY_INDEX))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_REPO_INDEX_Q,
                           "q",
                           test_options->repository_index);

  if (CHECK_FLAG (test_options->set_flags, SET_PORT))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_PORT_Q,
                           "q",
                           test_options->port);

  if (CHECK_FLAG (test_options->set_flags, SET_TXT_RECORDS_SIZE_LEVEL))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_SIZE_LEVEL_Y,
                           "y",
                           test_options->txt_records_size_level);

  if (CHECK_FLAG (test_options->set_flags, SET_TXT_RECORDS_CUSTOM_SIZE))
    g_variant_dict_insert (&options_dict,
                           EOS_OSTREE_AVAHI_OPTION_TXT_RECORDS_CUSTOM_SIZE_T,
                           "t",
                           test_options->txt_records_custom_size);

  return g_variant_dict_end (&options_dict);
}

typedef struct
{
  AvahiOstreeTestOptions options;
  gboolean success;
} TestOptions;

const TestOptions test_options[] =
  {
    {
      {SET_NOTHING},
      TRUE,
    },
    {
      {SET_FORCE_VERSION, .force_version = 1},
      TRUE,
    },
    {
      {SET_FORCE_VERSION, .force_version = 0},
      FALSE,
    },
    {
      {SET_FORCE_VERSION, .force_version = 2},
      FALSE,
    },
    {
      {SET_BLOOM_HASH_ID, .bloom_hash_id = 0},
      FALSE,
    },
    {
      {SET_BLOOM_HASH_ID, .bloom_hash_id = EOS_OSTREE_AVAHI_BLOOM_HASH_ID_OSTREE_COLLECTION_REF},
      TRUE,
    },
    {
      {SET_BLOOM_HASH_ID, .bloom_hash_id = 2},
      FALSE,
    },
    {
      {SET_BLOOM_K, .bloom_k = 0},
      FALSE,
    },
    {
      {SET_BLOOM_K, .bloom_k = 1},
      TRUE,
    },
    {
      {SET_BLOOM_SIZE, .bloom_size = 0},
      FALSE,
    },
    {
      {SET_BLOOM_SIZE, .bloom_size = 1},
      TRUE,
    },
    {
      {SET_BLOOM_SIZE, .bloom_size = 255 - 3 /* rb= */ - 2 /* bloom hash id + bloom k */},
      TRUE,
    },
    {
      {SET_BLOOM_SIZE, .bloom_size = 255 - 3 /* rb= */ - 2 /* bloom hash id + bloom k */ + 1},
      FALSE,
    },
    {
      {SET_PORT, .port = 0},
      FALSE,
    },
    {
      {SET_PORT, .port = 12345},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM},
      FALSE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL | SET_TXT_RECORDS_CUSTOM_SIZE, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM, .txt_records_custom_size = 12},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_SUPPORT_FAULTY_HARDWARE},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_DNS_MESSAGE},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_ETHERNET_PACKET},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_SINGLE_MULTICAST_DNS_PACKET},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_FIT_16_BIT_LIMIT},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_ABSOLUTELY_LAX},
      TRUE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = 7},
      FALSE,
    },
  };

static void
test_avahi_ostree_options_check (void)
{
  gsize idx;

  for (idx = 0; idx < G_N_ELEMENTS (test_options); ++idx)
    {
      g_autoptr(GError) error = NULL;
      const TestOptions *test_data = &test_options[idx];
      gboolean result = eos_ostree_avahi_service_file_check_options (avahi_ostree_test_options_to_variant (&test_data->options), &error);

      if (test_data->success)
        {
          g_assert_no_error (error);
          g_assert_true (result);
        }
      else
        {
          g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
          g_assert_false (result);
        }
    }
}

// 01. wrong version
// 02. wrong bloom filter size
// 03. wrong bloom filter k
// 04. wrong hash func id
// 05. wrong port
// 06. wrong timestamp
// 07. too long text record (likely impossible?)
// 08. too long binary record (likely impossible?)
// 09. wrong size level
// 11. too big for a custom size
// 12. too big for a crappy hardware
// 13. too big for a single dns message (likely impossible?)
// 14. too big for a single ethernet packet (likely impossible?)
// 15. too big for a single multicast dns packet (likely impossible?)
// 16. too big for a 16 bit limit (likely impossible?)
// 17. check lax size level (likely impossible?)
// 18. short bloom filter size
// 19. long bloom filter size (within limits still)
//
// some of the tests are likely impossible to check
typedef struct
{
  AvahiOstreeTestOptions options;
  gint summary_timestamp_year;
  gboolean success;
} AvahiOstreeGenerateTest;

#define GOOD_YEAR 2017
#define BAD_YEAR 1234
#define SMALL_BLOOM_SIZE 12
#define CUSTOM_REPOSITORY_INDEX 6

const AvahiOstreeGenerateTest avahi_ostree_generate_tests[] =
  {
    {
      {SET_FORCE_VERSION, .force_version = 0},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_FORCE_VERSION, .bloom_size = 0},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_FORCE_VERSION, .bloom_k = 0},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_FORCE_VERSION, .bloom_hash_id = 0},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_FORCE_VERSION, .port = 0},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_NOTHING},
      BAD_YEAR,
      FALSE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL | SET_TXT_RECORDS_CUSTOM_SIZE, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_CUSTOM, .txt_records_custom_size = 10},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_TXT_RECORDS_SIZE_LEVEL, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_SUPPORT_FAULTY_HARDWARE},
      GOOD_YEAR,
      FALSE,
    },
    {
      {SET_BLOOM_SIZE | SET_TXT_RECORDS_SIZE_LEVEL, .bloom_size = SMALL_BLOOM_SIZE, .txt_records_size_level = EOS_OSTREE_AVAHI_SIZE_LEVEL_SUPPORT_FAULTY_HARDWARE},
      GOOD_YEAR,
      TRUE,
    },
    {
      {SET_REPOSITORY_INDEX, .repository_index = CUSTOM_REPOSITORY_INDEX},
      GOOD_YEAR,
      TRUE,
    },
  };

static void
test_avahi_ostree_service_file_generate (Fixture       *fixture,
                                         gconstpointer  user_data G_GNUC_UNUSED)
{
  gsize idx;
  g_autoptr(GTimeZone) utc_tz = g_time_zone_new_utc ();

  for (idx = 0; idx < G_N_ELEMENTS (avahi_ostree_generate_tests); ++idx)
    {
      g_autofree gchar *filename = NULL;
      g_autofree gchar *service_file = NULL;
      g_autoptr(GError) error = NULL;
      const AvahiOstreeGenerateTest *test_data = &avahi_ostree_generate_tests[idx];
      gboolean result;
      g_autoptr(GDateTime) timestamp = g_date_time_new (utc_tz,
                                                        test_data->summary_timestamp_year,
                                                        1, 1, 0, 0, 0);

      g_assert_nonnull (timestamp);
      result = eos_ostree_avahi_service_file_generate (fixture->tmp_dir,
                                                       fixture->refs,
                                                       timestamp,
                                                       avahi_ostree_test_options_to_variant (&test_data->options),
                                                       NULL,
                                                       &error);

      if (CHECK_FLAG (test_data->options.set_flags, SET_REPOSITORY_INDEX))
        filename = g_strdup_printf ("eos-ostree-updater-%" G_GUINT16_FORMAT ".service",
                                    test_data->options.repository_index);
      else
        filename = g_strdup ("eos-ostree-updater-0.service");

      service_file = g_build_filename (fixture->tmp_dir,
                                       filename,
                                       NULL);

      if (test_data->success)
        {
          gboolean small_bloom_size;
          gboolean default_repository_index;

          g_assert_no_error (error);
          g_assert_true (result);

          small_bloom_size = CHECK_FLAG (test_data->options.set_flags, SET_BLOOM_SIZE) && test_data->options.bloom_size == SMALL_BLOOM_SIZE;
          default_repository_index = !CHECK_FLAG (test_data->options.set_flags, SET_REPOSITORY_INDEX) || test_data->options.repository_index == 0;
          assert_ostree_service_file_contents_valid (service_file,
                                                     small_bloom_size,
                                                     default_repository_index);

          g_assert_cmpint (g_unlink (service_file), ==, 0);
        }
      else
        {
          g_assert_nonnull (error); /* no point in checking the domain and code */
          g_assert_false (result);
          g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));
        }
    }
}

static void
create_file (const gchar *path)
{
  g_autoptr(GError) error = NULL;
  gboolean result = g_file_set_contents (path, "foo", -1, &error);

  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
}

static gchar *
ostree_service_file (const gchar *repo_index)
{
  return g_strdup_printf ("eos-ostree-updater-%s.service",
                          repo_index);
}

static void
test_avahi_ostree_cleanup_directory (Fixture       *fixture,
                                     gconstpointer  user_data G_GNUC_UNUSED)
{
  guint idx;
  guint max = 6;
  gboolean result;
  g_autoptr(GPtrArray) valid_files = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) invalid_files = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GError) error = NULL;

  for (idx = 0; idx < max; ++idx)
    {
      const gchar repo_index[] = { '0' + idx, '\0' };
      g_autofree gchar *filename = ostree_service_file (repo_index);

      g_ptr_array_add (valid_files, g_build_filename (fixture->tmp_dir,
                                                      filename,
                                                      NULL));
    }

  {
    gchar *filenames[] =
      {
        ostree_service_file ("foo"),
        ostree_service_file ("100000"),
        g_strdup ("whatever")
      };

    for (idx = 0; idx < G_N_ELEMENTS (filenames); ++idx)
      {
        g_ptr_array_add (invalid_files, g_build_filename (fixture->tmp_dir,
                                                          filenames[idx],
                                                          NULL));
        g_free (filenames[idx]);
      }
  }

  for (idx = 0; idx < valid_files->len; ++idx)
    create_file (g_ptr_array_index (valid_files, idx));
  for (idx = 0; idx < invalid_files->len; ++idx)
    create_file (g_ptr_array_index (invalid_files, idx));

  result = eos_ostree_avahi_service_file_cleanup_directory (fixture->tmp_dir,
                                                            NULL,
                                                            &error);
  g_assert_no_error (error);
  g_assert_true (result);

  for (idx = 0; idx < valid_files->len; ++idx)
    g_assert_false (g_file_test (g_ptr_array_index (valid_files, idx),
                                 G_FILE_TEST_EXISTS));

  for (idx = 0; idx < invalid_files->len; ++idx)
    {
      const gchar *service_file = g_ptr_array_index (invalid_files, idx);
      g_assert_true (g_file_test (service_file, G_FILE_TEST_EXISTS));
      g_assert_cmpint (g_unlink (service_file), ==, 0);
    }
}

static void
test_avahi_ostree_delete (Fixture       *fixture,
                          gconstpointer  user_data G_GNUC_UNUSED)
{
  g_autoptr(GError) error = NULL;
  gboolean result;
  guint16 repo_index = 6;
  g_autofree gchar *repo_index_str = g_strdup_printf ("%" G_GUINT16_FORMAT,
                                                      repo_index);
  g_autofree gchar *filename = ostree_service_file (repo_index_str);
  g_autofree gchar *service_file = g_build_filename (fixture->tmp_dir,
                                                     filename,
                                                     NULL);

  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));
  result = eos_ostree_avahi_service_file_delete (fixture->tmp_dir,
                                                 repo_index,
                                                 NULL,
                                                 &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));

  create_file (service_file);
  result = eos_ostree_avahi_service_file_delete (fixture->tmp_dir,
                                                 repo_index,
                                                 NULL,
                                                 &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_false (g_file_test (service_file, G_FILE_TEST_EXISTS));
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/avahi-service-file/generate/normal", Fixture, NULL, setup,
              test_avahi_service_file_generate_normal, teardown);
  g_test_add ("/avahi-service-file/generate/overwrite", Fixture, NULL, setup,
              test_avahi_service_file_generate_overwrite, teardown);
  g_test_add ("/avahi-service-file/generate/nonexistent-directory", Fixture,
              NULL, setup,
              test_avahi_service_file_generate_nonexistent_directory, teardown);
  g_test_add ("/avahi-service-file/generate/denied", Fixture, NULL, setup,
              test_avahi_service_file_generate_denied, teardown);

  g_test_add ("/avahi-service-file/delete/normal", Fixture, NULL, setup,
              test_avahi_service_file_delete_normal, teardown);
  g_test_add ("/avahi-service-file/delete/nonexistent-file", Fixture, NULL,
              setup, test_avahi_service_file_delete_nonexistent_file, teardown);
  g_test_add ("/avahi-service-file/delete/nonexistent-directory", Fixture, NULL,
              setup, test_avahi_service_file_delete_nonexistent_directory,
              teardown);
  g_test_add ("/avahi-service-file/delete/denied", Fixture, NULL, setup,
              test_avahi_service_file_delete_denied, teardown);

  g_test_add_func ("/avahi-service-file/ostree/check",
                   test_avahi_ostree_options_check);
  g_test_add ("/avahi-service-file/ostree/generate", Fixture, NULL, setup,
              test_avahi_ostree_service_file_generate, teardown);
  g_test_add ("/avahi-service-file/ostree/cleanup-directory", Fixture, NULL, setup,
              test_avahi_ostree_cleanup_directory, teardown);
  g_test_add ("/avahi-service-file/ostree/delete", Fixture, NULL, setup,
              test_avahi_ostree_delete, teardown);

  return g_test_run ();
}
