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
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include <glib.h>
#include <libeos-update-server/config.h>
#include <libeos-updater-util/avahi-service-file.h>
#include <libeos-updater-util/extensions.h>
#include <libeos-updater-util/ostree.h>
#include <locale.h>
#include <ostree.h>
#include <stdlib.h>

static gboolean
get_refs (OstreeRepo            *repo,
          OstreeCollectionRef ***out_refs,
          GCancellable          *cancellable,
          GError               **error)
{
  g_autoptr(GHashTable) refs = NULL;
  GHashTableIter iter;
  OstreeCollectionRef *key;
  gchar *value;
  g_autoptr(GPtrArray) refs_array = NULL;

  if (!ostree_repo_list_collection_refs (repo,
                                         /* match_collection_id: */ NULL,
                                         &refs,
                                         OSTREE_REPO_LIST_REFS_EXT_NONE,
                                         cancellable,
                                         error))
    return FALSE;

  if (g_hash_table_size (refs) == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "no refs to advertise");
      return FALSE;
    }
  refs_array = g_ptr_array_new_full (g_hash_table_size (refs) + 1, (GDestroyNotify)ostree_collection_ref_free);
  g_hash_table_iter_init (&iter, refs);
  while (g_hash_table_iter_next (&iter, (gpointer *)&key, (gpointer *)&value))
    {
      g_hash_table_iter_steal (&iter);
      g_free (g_steal_pointer (&value));
      g_ptr_array_add (refs_array, g_steal_pointer (&key));
    }
  g_ptr_array_add (refs_array, NULL);

  g_assert (out_refs);
  *out_refs = (OstreeCollectionRef **)g_ptr_array_free (g_steal_pointer (&refs_array), FALSE);
  return TRUE;
}

static gboolean
get_raw_summary_timestamp_from_metadata (GBytes    *summary,
                                         gboolean  *out_found,
                                         guint64   *out_timestamp,
                                         GError   **error)
{
  g_autoptr(GVariant) summary_variant = g_variant_ref_sink (g_variant_new_from_bytes (OSTREE_SUMMARY_GVARIANT_FORMAT, summary, FALSE));
  g_autoptr(GVariant) value = NULL;

  value = g_variant_lookup_value (summary_variant, "ostree.summary.last-modified", G_VARIANT_TYPE_UINT64);

  g_assert (out_found != NULL);
  g_assert (out_timestamp != NULL);
  if (value == NULL)
    {
      *out_found = FALSE;
      *out_timestamp = 0;
      return TRUE;
    }

  *out_found = TRUE;
  *out_timestamp = GUINT64_FROM_BE (g_variant_get_uint64 (value));
  return TRUE;
}

static gboolean
bad_timestamp (guint64   modification_time_secs,
               GError  **error)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Invalid timestamp %" G_GUINT64_FORMAT,
               modification_time_secs);
  return FALSE;
}

static gboolean
get_summary_timestamp_from_guint64 (guint64     modification_time_secs,
                                    GDateTime **out_summary_timestamp,
                                    GError    **error)
{
  g_autoptr(GDateTime) summary_timestamp = NULL;

  if (modification_time_secs > G_MAXINT64)
    return bad_timestamp (modification_time_secs, error);

  summary_timestamp = g_date_time_new_from_unix_utc ((gint64)modification_time_secs);
  if (summary_timestamp == NULL)
    return bad_timestamp (modification_time_secs, error);

  g_assert (out_summary_timestamp);
  *out_summary_timestamp = g_steal_pointer (&summary_timestamp);
  return TRUE;
}

static gboolean
get_summary_timestamp (OstreeRepo    *repo,
                       GDateTime    **out_summary_timestamp,
                       GCancellable  *cancellable,
                       GError       **error)
{
  // FIXME: Find the summary without using eos specific bits to ease
  // the moving of this code to ostree.
  g_autoptr(EosExtensions) extensions = eos_extensions_new_from_repo (repo,
                                                                      cancellable,
                                                                      error);
  gboolean found;
  guint64 raw_timestamp;

  if (extensions == NULL)
    return FALSE;

  if (!get_raw_summary_timestamp_from_metadata (extensions->summary, &found, &raw_timestamp, error))
    return FALSE;

  if (!found)
    raw_timestamp = extensions->summary_modification_time_secs;

  return get_summary_timestamp_from_guint64 (raw_timestamp, out_summary_timestamp, error);
}

static gboolean
get_refs_and_summary_timestamp (OstreeSysroot         *sysroot,
                                OstreeCollectionRef ***out_refs,
                                GDateTime            **out_summary_timestamp,
                                GCancellable          *cancellable,
                                GError               **error)
{
  g_autoptr(OstreeRepo) repo = NULL;
  g_auto(OstreeCollectionRefv) refs = NULL;
  g_autoptr(GDateTime) summary_timestamp = NULL;

  if (!ostree_sysroot_get_repo (sysroot,
                                &repo,
                                cancellable,
                                error))
    return FALSE;

  if (!get_refs (repo, &refs, cancellable, error))
    return FALSE;

  if (!get_summary_timestamp (repo, &summary_timestamp, cancellable, error))
    return FALSE;

  g_assert (out_refs);
  g_assert (out_summary_timestamp);
  *out_refs = g_steal_pointer (&refs);
  *out_summary_timestamp = g_steal_pointer (&summary_timestamp);
  return TRUE;
}

static gboolean
update_service_file (gboolean       advertise_updates,
                     const gchar   *avahi_service_directory,
                     GCancellable  *cancellable,
                     GError       **error)
{
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autofree gchar *commit_checksum = NULL;
  g_autofree gchar *commit_ostree_path = NULL;
  guint64 commit_timestamp;
  g_autoptr(GDateTime) commit_date_time = NULL;
  g_autofree gchar *formatted_commit_date_time = NULL;
  gboolean delete;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (avahi_service_directory != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Work out what commit we would advertise. Errors here are not fatal, as we
   * want to delete the file on failure. */
  sysroot = ostree_sysroot_new_default ();

  if (!ostree_sysroot_load (sysroot, cancellable, &local_error))
    {
      g_warning ("Error loading sysroot: %s", local_error->message);
      g_clear_error (&local_error);
    }
  else if (!eos_sysroot_get_advertisable_commit (sysroot,
                                                 &commit_checksum,
                                                 &commit_ostree_path,
                                                 &commit_timestamp,
                                                 &local_error))
    {
      g_warning ("Error getting advertisable commit: %s", local_error->message);
      g_clear_error (&local_error);
    }

  if (commit_checksum != NULL)
    {
      commit_date_time = g_date_time_new_from_unix_utc (commit_timestamp);
      formatted_commit_date_time = g_date_time_format (commit_date_time,
                                                       "%FT%T%:z");
    }

  /* Work out the update policy. */
  if (!advertise_updates && commit_checksum != NULL)
    {
      g_message ("Advertising updates is disabled. Deployed commit ‘%s’ "
                 "(%s, %s) will not be advertised.",
                 commit_checksum, formatted_commit_date_time,
                 commit_ostree_path);
      delete = TRUE;
    }
  else if (advertise_updates && commit_checksum == NULL)
    {
      g_message ("Advertising updates is enabled, but no appropriate deployed "
                 "commits were found. Not advertising updates.");
      delete = TRUE;
    }
  else if (!advertise_updates && commit_checksum == NULL)
    {
      g_message ("Advertising updates is disabled, and no appropriate deployed "
                 "commits were found. Not advertising updates.");
      delete = TRUE;
    }
  else
    {
      g_message ("Advertising updates is enabled, and deployed commit ‘%s’ "
                 "(%s, %s) will be advertised.", commit_checksum,
                 formatted_commit_date_time, commit_ostree_path);
      delete = FALSE;
    }

  /* Apply the policy. */
  if (!delete)
    {
      g_autoptr(GDateTime) summary_timestamp = NULL;
      g_auto(OstreeCollectionRefv) refs = NULL;

      if (!get_refs_and_summary_timestamp (sysroot,
                                           &refs,
                                           &summary_timestamp,
                                           cancellable,
                                           error))
        return FALSE;

      if (!eos_avahi_service_file_generate (avahi_service_directory,
                                            commit_ostree_path,
                                            commit_date_time,
                                            cancellable,
                                            error))
        {
          eos_avahi_service_file_delete (avahi_service_directory, cancellable,
                                         NULL);
          return FALSE;
        }
      if (!eos_ostree_avahi_service_file_generate (avahi_service_directory,
                                                   refs,
                                                   summary_timestamp,
                                                   NULL, /* no options, use defaults */
                                                   cancellable,
                                                   error))
        {
          eos_ostree_avahi_service_file_delete (avahi_service_directory,
                                                0,
                                                cancellable,
                                                NULL);
          eos_avahi_service_file_delete (avahi_service_directory, cancellable,
                                         NULL);
          return FALSE;
        }

      return TRUE;
    }
  else
    {
      if (!eos_avahi_service_file_delete (avahi_service_directory,
                                          cancellable, error))
        return FALSE;
      return eos_ostree_avahi_service_file_delete (avahi_service_directory,
                                                   0,
                                                   cancellable,
                                                   error);
    }
}

/* main() exit codes. */
enum
{
  EXIT_OK = EXIT_SUCCESS,
  EXIT_FAILED = 1,
  EXIT_INVALID_ARGUMENTS = 2,
  EXIT_BAD_CONFIGURATION = 3,
};

static int
fail (gboolean     quiet,
      int          exit_status,
      const gchar *error_message,
      ...) G_GNUC_PRINTF (3, 4);

static int
fail (gboolean     quiet,
      int          exit_status,
      const gchar *error_message,
      ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;

  g_return_val_if_fail (exit_status > 0, EXIT_FAILED);

  if (quiet)
    return exit_status;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  g_printerr ("%s: %s\n", g_get_prgname (), formatted_message);

  return exit_status;
}

static int
usage (GOptionContext *context,
       gboolean        quiet,
       const gchar    *error_message,
       ...) G_GNUC_PRINTF (3, 4);

static int
usage (GOptionContext *context,
       gboolean        quiet,
       const gchar    *error_message,
       ...)
{
  va_list ap;
  g_autofree gchar *formatted_message = NULL;
  g_autofree gchar *help = NULL;

  if (quiet)
    return EXIT_INVALID_ARGUMENTS;

  /* Format the arguments. */
  va_start (ap, error_message);
  formatted_message = g_strdup_vprintf (error_message, ap);
  va_end (ap);

  /* Include the usage. */
  help = g_option_context_get_help (context, TRUE, NULL);
  g_printerr ("%s: %s\n\n%s\n", g_get_prgname (), formatted_message, help);

  return EXIT_INVALID_ARGUMENTS;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  gboolean advertise_updates = FALSE;
  g_autoptr(GOptionContext) context = NULL;
  g_autofree gchar *avahi_service_directory = NULL;
  g_autofree gchar *config_file = NULL;
  gboolean quiet = FALSE;

  const GOptionEntry entries[] =
    {
      { "service-directory", 'd',
        G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &avahi_service_directory,
        "Directory containing Avahi .service files "
        "(default: " SYSCONFDIR "/avahi/services" ")",
        "PATH" },
      { "config-file", 'c',
        G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &config_file,
        "Configuration file to use (default: "
        SYSCONFDIR "/" PACKAGE "/eos-update-server.conf" ")", "PATH" },
      { "quiet", 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &quiet,
        "Do not print anything; check exit status for success", NULL },
      { NULL }
    };

  setlocale (LC_ALL, "");

  /* Command line parsing and defaults. */
  context = g_option_context_new ("— Endless OS Avahi Advertisement Updater");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Update the Avahi DNS-SD service advertisement "
                                "for advertising updates from this machine to "
                                "the local network, enabling or disabling it "
                                "as appropriate to match the current "
                                "configuration and OSTree state.");

  if (!g_option_context_parse (context,
                               &argc,
                               &argv,
                               &error))
    {
      return usage (context, quiet, "Failed to parse options: %s",
                    error->message);
    }

  if (avahi_service_directory == NULL)
    avahi_service_directory = g_strdup (eos_avahi_service_file_get_directory ());

  /* Load our configuration. */
  if (!eus_read_config_file (config_file, &advertise_updates, NULL, &error))
    {
      return fail (quiet, EXIT_BAD_CONFIGURATION,
                   "Failed to load configuration file: %s", error->message);
    }

  /* Update the Avahi configuration file to match. */
  if (!update_service_file (advertise_updates, avahi_service_directory, NULL,
                            &error))
    {
      return fail (quiet, EXIT_FAILED,
                   "Failed to update service file: %s", error->message);
    }

  return EXIT_OK;
}
