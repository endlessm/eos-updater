/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Kinvolk GmbH
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
 */

#include <locale.h>
#include <glib.h>
#include <libeos-updater-util/util.h>

typedef enum
  {
    SIGNED,
    UNSIGNED
  } SignType;

typedef struct
{
  const gchar *str;
  SignType sign_type;
  guint base;
  gint min;
  gint max;
  gint expected;
  gboolean should_fail;
} TestData;

const TestData test_data[] = {
  /* typical cases for signed */
  { "0",  SIGNED, 10, -2,  2,  0, FALSE },
  { "+0", SIGNED, 10, -2,  2,  0, FALSE },
  { "-0", SIGNED, 10, -2,  2,  0, FALSE },
  { "-2", SIGNED, 10, -2,  2, -2, FALSE },
  { "2",  SIGNED, 10, -2,  2,  2, FALSE },
  { "+2", SIGNED, 10, -2,  2,  2, FALSE },
  { "3",  SIGNED, 10, -2,  2,  0, TRUE  },
  { "+3", SIGNED, 10, -2,  2,  0, TRUE  },
  { "-3", SIGNED, 10, -2,  2,  0, TRUE  },

  /* typical cases for unsigned */
  { "-1", UNSIGNED, 10, 0, 2, 0, TRUE  },
  { "1",  UNSIGNED, 10, 0, 2, 1, FALSE },
  { "+1", UNSIGNED, 10, 0, 2, 0, TRUE  },
  { "0",  UNSIGNED, 10, 0, 2, 0, FALSE },
  { "+0", UNSIGNED, 10, 0, 2, 0, TRUE  },
  { "-0", UNSIGNED, 10, 0, 2, 0, TRUE  },
  { "2",  UNSIGNED, 10, 0, 2, 2, FALSE },
  { "+2", UNSIGNED, 10, 0, 2, 0, TRUE  },
  { "3",  UNSIGNED, 10, 0, 2, 0, TRUE  },
  { "+3", UNSIGNED, 10, 0, 2, 0, TRUE  },

  /* min == max cases for signed */
  { "-2", SIGNED, 10, -2, -2, -2, FALSE },
  { "-1", SIGNED, 10, -2, -2,  0, TRUE  },
  { "-3", SIGNED, 10, -2, -2,  0, TRUE  },

  /* min == max cases for unsigned */
  { "2", UNSIGNED, 10, 2, 2, 2, FALSE },
  { "3", UNSIGNED, 10, 2, 2, 0, TRUE  },
  { "1", UNSIGNED, 10, 2, 2, 0, TRUE  },

  /* invalid inputs */
  { "",    SIGNED,   10, -2,  2,  0, TRUE },
  { "",    UNSIGNED, 10,  0,  2,  0, TRUE },
  { "a",   SIGNED,   10, -2,  2,  0, TRUE },
  { "a",   UNSIGNED, 10,  0,  2,  0, TRUE },
  { "1a",  SIGNED,   10, -2,  2,  0, TRUE },
  { "1a",  UNSIGNED, 10,  0,  2,  0, TRUE },
  { "- 1", SIGNED,   10, -2,  2,  0, TRUE },

  /* leading/trailing whitespace */
  { " 1", SIGNED,   10, -2,  2,  0, TRUE },
  { " 1", UNSIGNED, 10,  0,  2,  0, TRUE },
  { "1 ", SIGNED,   10, -2,  2,  0, TRUE },
  { "1 ", UNSIGNED, 10,  0,  2,  0, TRUE },

  /* hexadecimal numbers */
  { "a",     SIGNED,   16,   0, 15, 10, FALSE },
  { "a",     UNSIGNED, 16,   0, 15, 10, FALSE },
  { "0xa",   SIGNED,   16,   0, 15,  0, TRUE  },
  { "0xa",   UNSIGNED, 16,   0, 15,  0, TRUE  },
  { "-0xa",  SIGNED,   16, -15, 15,  0, TRUE  },
  { "-0xa",  UNSIGNED, 16,   0, 15,  0, TRUE  },
  { "+0xa",  SIGNED,   16,   0, 15,  0, TRUE  },
  { "+0xa",  UNSIGNED, 16,   0, 15,  0, TRUE  },
  { "- 0xa", SIGNED,   16, -15, 15,  0, TRUE  },
  { "- 0xa", UNSIGNED, 16,   0, 15,  0, TRUE  },
  { "+ 0xa", SIGNED,   16, -15, 15,  0, TRUE  },
  { "+ 0xa", UNSIGNED, 16,   0, 15,  0, TRUE  },
};

static void
test_util_strtonum_usual (void)
{
  gsize idx;

  for (idx = 0; idx < G_N_ELEMENTS (test_data); ++idx)
    {
      g_autoptr(GError) error = NULL;
      const TestData *data = &test_data[idx];
      gboolean result;
      gint value;

      switch (data->sign_type)
        {
        case SIGNED:
          {
            gint64 value64 = 0;
            result = eos_string_to_signed (data->str,
                                           data->base,
                                           data->min,
                                           data->max,
                                           &value64,
                                           &error);
            value = (gint) value64;
            g_assert_cmpint (value, ==, value64);
            break;
          }

        case UNSIGNED:
          {
            guint64 value64 = 0;
            g_assert_cmpint (data->min, >=, 0);
            g_assert_cmpint (data->max, >=, 0);
            result = eos_string_to_unsigned (data->str,
                                             data->base,
                                             (guint64) data->min,
                                             (guint64) data->max,
                                             &value64,
                                             &error);
            value = (gint) value64;
            g_assert_cmpuint ((guint64) value, ==, value64);
            break;
          }

        default:
          g_assert_not_reached ();
        }

      if (data->should_fail)
        {
          g_assert_false (result);
          g_assert_nonnull (error);
        }
      else
        {
          g_assert_true (result);
          g_assert_null (error);
          g_assert_cmpint (value, ==, data->expected);
        }
    }
}

static void
test_util_strtonum_pathological (void)
{
  g_autoptr(GError) error = NULL;
  const gchar *crazy_high = "999999999999999999999999999999999999";
  const gchar *crazy_low = "-999999999999999999999999999999999999";
  const gchar *max_uint64 = "18446744073709551615";
  const gchar *max_int64 = "9223372036854775807";
  const gchar *min_int64 = "-9223372036854775808";
  guint64 uvalue = 0;
  gint64 svalue = 0;

  g_assert_false (eos_string_to_unsigned (crazy_high,
                                          10,
                                          0,
                                          G_MAXUINT64,
                                          NULL,
                                          &error));
  g_assert_nonnull (error);
  g_clear_error (&error);
  g_assert_false (eos_string_to_unsigned (crazy_low,
                                          10,
                                          0,
                                          G_MAXUINT64,
                                          NULL,
                                          &error));
  // crazy_low is a signed number so it is not a valid unsigned number
  g_assert_nonnull (error);
  g_clear_error (&error);

  g_assert_false (eos_string_to_signed (crazy_high,
                                        10,
                                        G_MININT64,
                                        G_MAXINT64,
                                        NULL,
                                        &error));
  g_assert_nonnull (error);
  g_clear_error (&error);
  g_assert_false (eos_string_to_signed (crazy_low,
                                        10,
                                        G_MININT64,
                                        G_MAXINT64,
                                        NULL,
                                        &error));
  g_assert_nonnull (error);
  g_clear_error (&error);

  g_assert_true (eos_string_to_unsigned (max_uint64,
                                         10,
                                         0,
                                         G_MAXUINT64,
                                         &uvalue,
                                         &error));
  g_assert_no_error (error);
  g_assert_cmpuint (uvalue, ==, G_MAXUINT64);

  g_assert_true (eos_string_to_signed (max_int64,
                                       10,
                                       G_MININT64,
                                       G_MAXINT64,
                                       &svalue,
                                       &error));
  g_assert_no_error (error);
  g_assert_cmpint (svalue, ==, G_MAXINT64);

  g_assert_true (eos_string_to_signed (min_int64,
                                       10,
                                       G_MININT64,
                                       G_MAXINT64,
                                       &svalue,
                                       &error));
  g_assert_no_error (error);
  g_assert_cmpint (svalue, ==, G_MININT64);
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/util/strtonum/usual", test_util_strtonum_usual);
  g_test_add_func ("/util/strtonum/pathological", test_util_strtonum_pathological);

  return g_test_run ();
}
