/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013, 2014, 2015, 2016, 2017, 2018 Endless Mobile, Inc.
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
 */

#include "eos-updater-generated.h"
#include "resources.h"
#include <libeos-updater-util/config.h>
#include <libeos-updater-util/types.h>
#include <libeos-updater-util/util.h>

#include <gio/gio.h>
#include <glib.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib/gprintf.h>
#include <NetworkManager.h>

#define EOS_UPDATER_INVALID_ARGS_MSGID          "27b3a4600f7242acadf1855a2a1eaa6d"
#define EOS_UPDATER_CONFIGURATION_ERROR_MSGID   "5af9f4df37f949a1948971e00be0d620"
#define EOS_UPDATER_DAEMON_ERROR_MSGID          "f31fd043074a4a21b04784cf895c56ae"
#define EOS_UPDATER_STAMP_ERROR_MSGID           "da96f3494a5d432d8bcea1217433ecbf"
#define EOS_UPDATER_SUCCESS_MSGID               "ce0a80bb9f734dc09f8b56a7fb981ae4"
#define EOS_UPDATER_NOT_ONLINE_MSGID            "2797d0eaca084a9192e21838ab12cbd0"
#define EOS_UPDATER_NOT_TIME_MSGID              "7c853d8fbc0b4a9b9f331b5b9aee4435"

#define EOS_UPDATER_MSGID_LENGTH                32

/* The step of the update. These constants are used in the configuration
 * file to indicate which is the final automatic step before the user
 * needs to intervene.
 */
typedef enum {
  UPDATE_STEP_NONE = 0,
  UPDATE_STEP_POLL = 1,
  UPDATE_STEP_FETCH = 2,
  UPDATE_STEP_APPLY = 3,
} UpdateStep;

/* These must be kept in sync with #UpdateStep. */
#define UPDATE_STEP_FIRST UPDATE_STEP_NONE
#define UPDATE_STEP_LAST UPDATE_STEP_APPLY

#define SEC_PER_DAY (3600ul * 24)

/* This file is touched whenever the updater starts */
static const char *UPDATE_STAMP_DIR = LOCALSTATEDIR "/lib/eos-updater";
static const char *UPDATE_STAMP_NAME = "eos-updater-stamp";

static const char *CONFIG_FILE_PATH = SYSCONFDIR "/" PACKAGE "/eos-autoupdater.conf";
static const char *OLD_CONFIG_FILE_PATH = SYSCONFDIR "/eos-updater.conf";
static const char *STATIC_CONFIG_FILE_PATH = PKGDATADIR "/eos-autoupdater.conf";
static const char *LOCAL_CONFIG_FILE_PATH = PREFIX "/local/share/" PACKAGE "/eos-autoupdater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";
static const char *INTERVAL_KEY = "IntervalDays";
static const char *RANDOMIZED_DELAY_KEY = "RandomizedDelayDays";

/* Ensures that the updater never tries to poll twice in one run */
static gboolean polled_already = FALSE;

/* Read from config file */
static UpdateStep last_automatic_step = UPDATE_STEP_NONE;

/* Set when main should return failure */
static gboolean should_exit_failure = FALSE;

/* Avoid erroneous additional state transitions */
static guint previous_state = EOS_UPDATER_STATE_NONE;

static GMainLoop *main_loop = NULL;
static gchar *volume_path = NULL;

static const gchar *
get_envvar_or (const gchar *envvar,
               const gchar *default_value)
{
  const gchar *value = g_getenv (envvar);

  if (value != NULL)
    return value;

  return default_value;
}

static const gchar *
get_stamp_dir (void)
{
  return get_envvar_or ("EOS_UPDATER_TEST_AUTOUPDATER_UPDATE_STAMP_DIR",
                        UPDATE_STAMP_DIR);
}

static void
log_with_msgid (const gchar *msgid,
                GLogLevelFlags log_level,
                const gchar *numeric_log_level,
                const gchar *format,
                va_list args)
{
  G_STATIC_ASSERT (G_LOG_DOMAIN != NULL);
  G_STATIC_ASSERT (G_LOG_DOMAIN[0] != '\0');

  g_autofree gchar *message = NULL;
  /* Apparently the version of GCC in Endless ignores the
   * G_GNUC_PRINTF annotation that has a zero as the second parameter,
   * so it suggests to use this attribute. Similarly, so does Clang 4. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
  gint message_length = g_vasprintf (&message, format, args);
#pragma GCC diagnostic pop
  const GLogField fields[] = {
    { "MESSAGE", message, message_length },
    { "MESSAGE_ID", msgid, EOS_UPDATER_MSGID_LENGTH },
    { "PRIORITY", numeric_log_level, -1 },
    /* strlen (G_LOG_DOMAIN) should be folded to a constant at a
     * compilation time, because G_LOG_DOMAIN is a macro defining a
     * literal string */
    { "GLIB_DOMAIN", G_LOG_DOMAIN, strlen (G_LOG_DOMAIN) },
  };

  g_log_structured_array (log_level, fields, G_N_ELEMENTS (fields));
}

static void
critical (const gchar *msgid,
          const gchar *format,
          ...) G_GNUC_PRINTF(2, 3);

static void
critical (const gchar *msgid,
          const gchar *format,
          ...)
{
  va_list args;

  va_start (args, format);
  log_with_msgid (msgid, G_LOG_LEVEL_CRITICAL, "4", format, args);
  va_end (args);
}

static void
warning (const gchar *msgid,
         const gchar *format,
         ...) G_GNUC_PRINTF(2, 3);

static void
warning (const gchar *msgid,
         const gchar *format,
         ...)
{
  va_list args;

  va_start (args, format);
  log_with_msgid (msgid, G_LOG_LEVEL_WARNING, "4", format, args);
  va_end (args);
}

static void
info (const gchar *msgid,
      const gchar *format,
      ...) G_GNUC_PRINTF(2, 3);

static void
info (const gchar *msgid,
      const gchar *format,
      ...)
{
  va_list args;

  va_start (args, format);
  log_with_msgid (msgid, G_LOG_LEVEL_INFO, "6", format, args);
  va_end (args);
}

/* Note: This function does not report errors as a GError because there’s no
 * harm in the stamp file not being updated: it just means we’re going to check
 * again for updates sooner than otherwise. */
static void
update_stamp_file (guint64 last_successful_update_secs,
                   guint   update_interval_days,
                   guint   randomized_delay_days)
{
  const gchar *stamp_dir = get_stamp_dir ();
  g_autofree gchar *stamp_path = NULL;
  g_autoptr(GFile) stamp_file = NULL;
  g_autoptr(GError) error = NULL;
  GTimeVal mtime;
  g_autofree gchar *next_update = NULL;
  g_autoptr(GFileInfo) file_info = NULL;

  if (g_mkdir_with_parents (stamp_dir, 0755) != 0) {
    int saved_errno = errno;
    const char *err_str = g_strerror (saved_errno);

    critical (EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
              "Failed to create updater timestamp directory: %s",
              err_str);
    return;
  }

  /* This will be subject to year 2038 problems on 32-bit architectures.
   * FIXME: Fix that by dropping use of #GTimeVal. */
  mtime.tv_sec = (glong) last_successful_update_secs;
  mtime.tv_usec = 0;

  stamp_path = g_build_filename (stamp_dir, UPDATE_STAMP_NAME, NULL);
  stamp_file = g_file_new_for_path (stamp_path);
  g_file_replace_contents (stamp_file, "", 0, NULL, FALSE,
                           G_FILE_CREATE_NONE, NULL, NULL,
                           &error);
  if (error) {
    critical (EOS_UPDATER_STAMP_ERROR_MSGID,
              "Failed to write updater stamp file: %s",
              error->message);
    return;
  }

  /* Set the file’s mtime to include the randomised delay. This will result in
   * the mtime either being now, or some number of days in the future. Setting
   * the mtime to the future should not be a problem, as the stamp file is only
   * accessed by eos-autoupdater, so the semantics of the mtime are clear. */
  file_info = g_file_query_info (stamp_file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                 G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (error != NULL)
    {
      critical (EOS_UPDATER_STAMP_ERROR_MSGID,
                "Failed to get stamp file info: %s",
                error->message);
      return;
    }

  if (randomized_delay_days > 0)
    {
      gint32 actual_delay_days = g_random_int_range (0, (gint32) randomized_delay_days + 1);
      mtime.tv_sec += (glong) actual_delay_days * (glong) SEC_PER_DAY;
    }

  g_file_info_set_modification_time (file_info, &mtime);

  g_file_set_attributes_from_info (stamp_file, file_info,
                                   G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (error != NULL)
    {
      critical (EOS_UPDATER_STAMP_ERROR_MSGID,
                "Failed to set stamp file info: %s",
                error->message);
      return;
    }

  /* A little bit of help for debuggers. */
  mtime.tv_sec += (glong) update_interval_days * (glong) SEC_PER_DAY;
  next_update = g_time_val_to_iso8601 (&mtime);
  g_debug ("Wrote stamp file. Next update at %s", next_update);
}

/* Called on completion of the async dbus calls to check whether they
 * succeeded. Success doesn't mean that the operation succeeded, but it
 * does mean the call reached the daemon.
 */
static void
update_step_callback (GObject *source_object, GAsyncResult *res,
                      gpointer step_data)
{
  EosUpdater *proxy = (EosUpdater *) source_object;
  UpdateStep step = (UpdateStep) GPOINTER_TO_INT (step_data);
  GError *error = NULL;

  switch (step) {
    case UPDATE_STEP_POLL:
      if (volume_path != NULL)
        eos_updater_call_poll_volume_finish (proxy, res, &error);
      else
        eos_updater_call_poll_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_FETCH:
      eos_updater_call_fetch_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_APPLY:
      eos_updater_call_apply_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_NONE:
    default:
      g_assert_not_reached ();
  }

  if (error) {
    warning (EOS_UPDATER_DAEMON_ERROR_MSGID,
             "Error calling EOS updater: %s",
             error->message);
    should_exit_failure = TRUE;
    g_main_loop_quit (main_loop);
    g_error_free (error);
  }
}

static gboolean
do_update_step (UpdateStep step, EosUpdater *proxy)
{
  gpointer step_data = GINT_TO_POINTER (step);

  /* Don't do more of the process than configured */
  if (step > last_automatic_step)
    return FALSE;

  switch (step) {
    case UPDATE_STEP_POLL:
      /* Don't poll more than once, or we will get stuck in a loop */
      if (polled_already)
        return FALSE;

      /* TODO: What to do with the volume code path? */
      polled_already = TRUE;
      if (volume_path != NULL)
        eos_updater_call_poll_volume (proxy, volume_path, NULL, update_step_callback, step_data);
      else
        eos_updater_call_poll (proxy, NULL, update_step_callback, step_data);
      break;

    case UPDATE_STEP_FETCH:
      eos_updater_call_fetch (proxy, NULL, update_step_callback, step_data);
      break;

    case UPDATE_STEP_APPLY:
      eos_updater_call_apply (proxy, NULL, update_step_callback, step_data);
      break;

    case UPDATE_STEP_NONE:
    default:
      g_assert_not_reached ();
  }

  return TRUE;
}

static void
report_error_status (EosUpdater *proxy)
{
  const gchar *name, *error_message;

  name = eos_updater_get_error_name (proxy);
  error_message = eos_updater_get_error_message (proxy);

  warning (EOS_UPDATER_DAEMON_ERROR_MSGID,
           "EOS updater error (%s): %s",
           name, error_message);
}

/* The autoupdater is driven by state transitions in the updater daemon.
 * Whenever the state changes, we check if we need to do something as a
 * result of that state change. */
static void
on_state_changed (EosUpdater *proxy, EosUpdaterState state)
{
  gboolean continue_running = TRUE;

  if (state == previous_state)
    return;

  previous_state = state;

  g_message ("EOS updater state is: %s", eos_updater_state_to_string (state));

  switch (state) {
    case EOS_UPDATER_STATE_NONE: /* State should change soon */
      break;

    case EOS_UPDATER_STATE_READY: /* Must poll */
      continue_running = do_update_step (UPDATE_STEP_POLL, proxy);
      break;

    case EOS_UPDATER_STATE_ERROR: /* Log error and quit */
      report_error_status (proxy);
      should_exit_failure = TRUE;
      continue_running = FALSE;
      break;

    case EOS_UPDATER_STATE_POLLING: /* Wait for completion */
      break;

    case EOS_UPDATER_STATE_UPDATE_AVAILABLE: /* Possibly fetch */
      continue_running = do_update_step (UPDATE_STEP_FETCH, proxy);
      break;

    case EOS_UPDATER_STATE_FETCHING: /* Wait for completion */
      break;

    case EOS_UPDATER_STATE_UPDATE_READY: /* Possibly apply */
      continue_running = do_update_step (UPDATE_STEP_APPLY, proxy);
      break;

    case EOS_UPDATER_STATE_APPLYING_UPDATE: /* Wait for completion */
      break;

    case EOS_UPDATER_STATE_UPDATE_APPLIED: /* Done; exit */
      continue_running = FALSE;
      break;

    default:
      g_critical ("EOS updater entered invalid state: %u", state);
      continue_running = FALSE;
      should_exit_failure = TRUE;
      break;
  }

  if (!continue_running) {
    g_main_loop_quit (main_loop);
  }
}

static void
on_state_changed_notify (EosUpdater *proxy,
                         GParamSpec *pspec,
                         gpointer data)
{
  EosUpdaterState state = eos_updater_get_state (proxy);
  on_state_changed (proxy, state);
}

static const gchar *
get_config_file_path (void)
{
  return get_envvar_or ("EOS_UPDATER_TEST_AUTOUPDATER_CONFIG_FILE_PATH",
                        CONFIG_FILE_PATH);
}

static gboolean
read_config_file (const gchar *config_path,
                  guint *update_interval_days,
                  guint *randomized_delay_days)
{
  g_autoptr(EuuConfigFile) config = NULL;
  g_autoptr(GError) error = NULL;
  guint _update_interval_days;
  guint _randomized_delay_days;
  const gchar * const paths[] =
    {
      config_path,  /* typically CONFIG_FILE_PATH unless testing */
      OLD_CONFIG_FILE_PATH,
      LOCAL_CONFIG_FILE_PATH,
      STATIC_CONFIG_FILE_PATH,
      NULL
    };

  g_return_val_if_fail (update_interval_days != NULL, FALSE);

  /* Load the config files. */
  config = euu_config_file_new (paths, eos_updater_resources_get_resource (),
                                "/com/endlessm/Updater/config/eos-autoupdater.conf");

  last_automatic_step = (UpdateStep) euu_config_file_get_uint (config,
                                                               AUTOMATIC_GROUP,
                                                               LAST_STEP_KEY,
                                                               UPDATE_STEP_FIRST,
                                                               UPDATE_STEP_LAST,
                                                               &error);
  if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))
    {
      warning (EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
               "Specified last automatic step is not a valid step");
      return FALSE;
    }
  else if (error != NULL)
    {
      warning (EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
               "Unable to read key '%s' in config file",
               LAST_STEP_KEY);
      return FALSE;
    }

  _update_interval_days = euu_config_file_get_uint (config, AUTOMATIC_GROUP,
                                                    INTERVAL_KEY, 0, G_MAXUINT,
                                                    &error);

  if (error != NULL)
    {
      warning (EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
               "Unable to read key '%s' in config file",
               INTERVAL_KEY);
      return FALSE;
    }

  /* This should always be true, as the RHS is out of range for a guint (it’s
   * around 10^14 days, which should be a long enough update period for anyone).
   * We use G_MAXUINT64 rather than G_MAXUINT because the time calculation in
   * is_time_to_update() uses guint64 variables. */
_Pragma ("GCC diagnostic push")
_Pragma ("GCC diagnostic ignored \"-Wtype-limits\"")
  g_assert ((guint64) _update_interval_days <= G_MAXUINT64 / SEC_PER_DAY);
_Pragma ("GCC diagnostic pop")

  *update_interval_days = (guint) _update_interval_days;

  /* We use G_MAXINT32 as g_random_int_range() operates on gint32. */
  _randomized_delay_days = euu_config_file_get_uint (config, AUTOMATIC_GROUP,
                                                     RANDOMIZED_DELAY_KEY,
                                                     0, (G_MAXINT32 / SEC_PER_DAY) - 1,
                                                     &error);

  if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))
    {
      warning (EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
               "Specified randomized delay is less than zero or too large");
      return FALSE;
    }
  else if (error != NULL)
    {
      warning (EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
               "Unable to read key '%s' in config file",
               RANDOMIZED_DELAY_KEY);
      return FALSE;
    }

  *randomized_delay_days = _randomized_delay_days;

  return TRUE;
}

/* We want to poll once when the updater starts.  To make sure that we
 * can quit ourselves gracefully, we wait until the main loop starts.
 */
static gboolean
initial_poll_idle_func (gpointer pointer)
{
  EosUpdater *proxy = (EosUpdater *) pointer;
  EosUpdaterState initial_state = eos_updater_get_state (proxy);

  /* Attempt to clear the error by pretending to be ready, which will
   * trigger a poll
   */
  if (initial_state == EOS_UPDATER_STATE_ERROR)
    initial_state = EOS_UPDATER_STATE_READY;

  on_state_changed (proxy, initial_state);

  /* Disable this function after the first run */
  return G_SOURCE_REMOVE;
}

static gboolean
is_time_to_update (guint update_interval_days,
                   guint randomized_delay_days)
{
  const gchar *stamp_dir = get_stamp_dir ();
  g_autofree gchar *stamp_path = NULL;
  g_autoptr (GFile) stamp_file = NULL;
  g_autoptr (GFileInfo) stamp_file_info = NULL;
  guint64 last_update_time_secs;
  gint64 current_time_usec;
  g_autoptr (GError) error = NULL;
  gboolean is_time_to_update = FALSE;

  stamp_path = g_build_filename (stamp_dir, UPDATE_STAMP_NAME, NULL);
  stamp_file = g_file_new_for_path (stamp_path);
  stamp_file_info = g_file_query_info (stamp_file,
                                       G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                       G_FILE_QUERY_INFO_NONE, NULL,
                                       &error);

  if (error != NULL &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
    /* Failed for some reason other than the file not being present */
    critical (EOS_UPDATER_STAMP_ERROR_MSGID,
              "Failed to read attributes of updater timestamp file");
    is_time_to_update = TRUE;
    g_debug ("Time to update, due to stamp file (%s) not being queryable.",
             stamp_path);
  } else if (error != NULL) {
    /* Stamp file is not present, so this is likely the first time the
     * computer’s run eos-autoupdater. In order to avoid a thundering herd of
     * computers requesting updates when a lab is first turned on, create a
     * stamp file with a random delay applied, and check again for updates
     * later. To do this, we fake the date of the most recent successful update
     * to be @update_interval_days in the past, so only the
     * @randomized_delay_days is taken into account for triggering the next
     * update. */
    if (randomized_delay_days > 0)
      {
        guint64 last_successful_update_secs;

        g_debug ("Not time to update, due to stamp file not being present, but %s is set to %u days.",
                 RANDOMIZED_DELAY_KEY, randomized_delay_days);
        last_successful_update_secs = (guint64) g_get_real_time () / G_USEC_PER_SEC;
        if (last_successful_update_secs >= update_interval_days * SEC_PER_DAY)
          last_successful_update_secs -= update_interval_days * SEC_PER_DAY;

        update_stamp_file (last_successful_update_secs,
                           update_interval_days, randomized_delay_days);
        is_time_to_update = FALSE;
      }
    else
      {
        g_debug ("Time to update, due to stamp file not being present.");
        is_time_to_update = TRUE;
      }
  } else {
    guint64 next_update_time_secs, update_interval_secs;

    /* Determine whether sufficient time has elapsed */
    current_time_usec = g_get_real_time ();
    last_update_time_secs =
      g_file_info_get_attribute_uint64 (stamp_file_info,
                                        G_FILE_ATTRIBUTE_TIME_MODIFIED);

    /* Guaranteed not to overflow, as we check update_interval_days when
     * loading it. */
    update_interval_secs = (guint64) update_interval_days * SEC_PER_DAY;

    /* next_update_time_secs = last_update_time_secs + update_interval_secs */
    if (!g_uint64_checked_add (&next_update_time_secs, last_update_time_secs,
                               update_interval_secs))
      next_update_time_secs = G_MAXUINT64;

    is_time_to_update = (next_update_time_secs < (guint64) current_time_usec / G_USEC_PER_SEC);

    if (is_time_to_update)
      g_debug ("Time to update");
    else
      g_debug ("Not time to update");
  }

  return is_time_to_update;
}

static gboolean
should_listen_on_session_bus (void)
{
  const gchar *value = NULL;

  value = g_getenv ("EOS_UPDATER_TEST_AUTOUPDATER_USE_SESSION_BUS");

  return value != NULL;
}

static gboolean
is_online (void)
{
  NMClient *client;
  gboolean online;
  g_autoptr(GError) error = NULL;

  /* Don’t connect to NetworkManager when we are supposed to use the session
   * bus, as NM is on the system bus, and we don’t want to mock it up. */
  if (should_listen_on_session_bus ())
    {
      g_message ("Not using NetworkManager: assuming network is online.");
      return TRUE;
    }

  client = nm_client_new (NULL, &error);
  if (!client)
    {
      g_message ("Failed to get the NetworkManager client: %s", error->message);
      return FALSE;
    }

  /* Assume that the ostree server is remote and only consider to be
   * online for ostree updates if we have global connectivity.
   * For Avahi updates, local or site connectivity is adequate.
   */
  switch (nm_client_get_state (client)) {
  case NM_STATE_CONNECTED_LOCAL:
  case NM_STATE_CONNECTED_SITE:
  case NM_STATE_CONNECTED_GLOBAL:
    online = TRUE;
    break;
  case NM_STATE_UNKNOWN:
  case NM_STATE_ASLEEP:
  case NM_STATE_DISCONNECTED:
  case NM_STATE_DISCONNECTING:
  case NM_STATE_CONNECTING:
  default:
    online = FALSE;
    break;
  }
  g_object_unref (client);

  if (!online)
    info (EOS_UPDATER_NOT_ONLINE_MSGID,
          "Not currently online. Not updating");
  return online;
}

static gint
get_dbus_timeout (void)
{
  const gchar *value = NULL;
  gint64 timeout;

  value = get_envvar_or ("EOS_UPDATER_TEST_AUTOUPDATER_DBUS_TIMEOUT",
                         NULL);

  if (value == NULL || value[0] == '\0')
    return -1;

  if (!g_ascii_string_to_signed (value, 10, 0, G_MAXINT, &timeout, NULL))
    return -1;

  return (gint) timeout;
}

/* main() exit codes. */
enum
{
  EXIT_OK = EXIT_SUCCESS,
  EXIT_FAILED = 1,
  EXIT_INVALID_ARGUMENTS = 2,
  EXIT_BAD_CONFIGURATION = 3,
};

int
main (int argc, char **argv)
{
  g_autoptr(EosUpdater) proxy = NULL;
  g_autoptr(GError) error = NULL;
  guint update_interval_days, randomized_delay_days;
  gboolean force_update = FALSE;
  g_autoptr(GOptionContext) context = NULL;

  GOptionEntry entries[] = {
    { "force-update", 0, 0, G_OPTION_ARG_NONE, &force_update, "Force an update", NULL },
    { "from-volume", 0, 0, G_OPTION_ARG_STRING, &volume_path, "Poll for updates from the volume", "PATH" },
    { NULL }
  };
  GBusType bus_type = G_BUS_TYPE_SYSTEM;
  gint dbus_timeout;

  setlocale (LC_ALL, "");

  context = g_option_context_new ("— Endless OS Automatic Updater");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context,
                                "Automatically poll for, fetch and apply "
                                "updates in the background. This drives the "
                                "state changes in the eos-updater service.");

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      warning (EOS_UPDATER_INVALID_ARGS_MSGID,
               "Error parsing command line arguments: %s",
               error->message);
      return EXIT_INVALID_ARGUMENTS;
    }

  if (!read_config_file (get_config_file_path (),
                         &update_interval_days, &randomized_delay_days))
    return EXIT_BAD_CONFIGURATION;

  if (volume_path == NULL && !is_online ())
    return EXIT_OK;

  /* Always force an update if running with --from-volume; it doesn’t make
   * sense not to. */
  if (volume_path != NULL)
    force_update = TRUE;

  if (!force_update) {
    if (!is_time_to_update (update_interval_days, randomized_delay_days)) {
      info (EOS_UPDATER_NOT_TIME_MSGID,
            "Less than %s since last update. Exiting",
            INTERVAL_KEY);
      return EXIT_OK;
    }
  }

  main_loop = g_main_loop_new (NULL, FALSE);

  if (should_listen_on_session_bus ())
    bus_type = G_BUS_TYPE_SESSION;
  proxy = eos_updater_proxy_new_for_bus_sync (bus_type,
                     G_DBUS_PROXY_FLAGS_NONE,
                     "com.endlessm.Updater",
                     "/com/endlessm/Updater",
                     NULL,
                     &error);

  if (error) {
    warning (EOS_UPDATER_DAEMON_ERROR_MSGID,
             "Error getting EOS updater object: %s",
             error->message);
    should_exit_failure = TRUE;
    goto out;
  }

  dbus_timeout = get_dbus_timeout ();
  g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (proxy), dbus_timeout);

  g_signal_connect (proxy, "notify::state",
                    G_CALLBACK (on_state_changed_notify), NULL);

  g_idle_add (initial_poll_idle_func, proxy);
  g_main_loop_run (main_loop);

out:
  g_main_loop_unref (main_loop);
  g_free (volume_path);

  if (should_exit_failure) /* All paths setting this print an error message */
    return EXIT_FAILED;

  /* Update the stamp file since all configured steps have succeeded. */
  update_stamp_file ((guint64) g_get_real_time () / G_USEC_PER_SEC,
                     update_interval_days, randomized_delay_days);
  info (EOS_UPDATER_SUCCESS_MSGID,
        "Updater finished successfully");

  return EXIT_OK;
}
