/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 * TODO: Copyright?
 */

#include "eos-updater-types.h"
#include "eos-updater-generated.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>
#include <errno.h>

#include <nm-client.h>
#include <nm-device.h>

#include <systemd/sd-journal.h>

#define EOS_UPDATER_CONFIGURATION_ERROR_MSGID   "5af9f4df37f949a1948971e00be0d620"
#define EOS_UPDATER_DAEMON_ERROR_MSGID          "f31fd043074a4a21b04784cf895c56ae"
#define EOS_UPDATER_STAMP_ERROR_MSGID           "da96f3494a5d432d8bcea1217433ecbf"
#define EOS_UPDATER_SUCCESS_MSGID               "ce0a80bb9f734dc09f8b56a7fb981ae4"
#define EOS_UPDATER_NOT_ONLINE_MSGID            "2797d0eaca084a9192e21838ab12cbd0"
#define EOS_UPDATER_MOBILE_CONNECTED_MSGID      "7c80d571cbc248d2a5cfd985c7cbd44c"
#define EOS_UPDATER_NOT_TIME_MSGID              "7c853d8fbc0b4a9b9f331b5b9aee4435"

/* The step of the update. These constants are used in the configuration
 * file to indicate which is the final automatic step before the user
 * needs to intervene.
 */
typedef enum _UpdateStep {
  UPDATE_STEP_NONE,
  UPDATE_STEP_POLL,
  UPDATE_STEP_FETCH,
  UPDATE_STEP_APPLY
} UpdateStep;

#define SEC_PER_DAY (3600ll * 24)

/* This file is touched whenever the updater starts */
static const char *UPDATE_STAMP_DIR = LOCALSTATEDIR "/eos-updater";
static const char *UPDATE_STAMP_NAME = "eos-updater-stamp";

static const char *CONFIG_FILE_PATH = SYSCONFDIR "/eos-updater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";
static const char *INTERVAL_KEY = "IntervalDays";
static const char *ON_MOBILE_KEY = "UpdateOnMobile";

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

/* Note: This function does not report errors as a GError because there’s no
 * harm in the stamp file not being updated: it just means we’re going to check
 * again for updates sooner than otherwise. */
static void
update_stamp_file (void)
{
  const gchar *stamp_dir = get_stamp_dir ();
  g_autofree gchar *stamp_path = NULL;
  g_autoptr(GFile) stamp_file = NULL;
  g_autoptr(GError) error = NULL;

  if (g_mkdir_with_parents (stamp_dir, 0755) != 0) {
    int saved_errno = errno;
    const char *err_str = g_strerror (saved_errno);

    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_CRIT,
                     "MESSAGE=Failed to create updater timestamp directory: %s", err_str,
                     NULL);
    return;
  }

  stamp_path = g_build_filename (stamp_dir, UPDATE_STAMP_NAME, NULL);
  stamp_file = g_file_new_for_path (stamp_path);
  g_file_replace_contents (stamp_file, "", 0, NULL, FALSE,
                           G_FILE_CREATE_NONE, NULL, NULL,
                           &error);
  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_STAMP_ERROR_MSGID,
                     "PRIORITY=%d", LOG_CRIT,
                     "MESSAGE=Failed to write updater stamp file: %s", error->message,
                     NULL);
    return;
  }
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
  UpdateStep step = GPOINTER_TO_INT (step_data);
  GError *error = NULL;

  switch (step) {
    case UPDATE_STEP_POLL:
      eos_updater_call_poll_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_FETCH:
      eos_updater_call_fetch_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_APPLY:
      eos_updater_call_apply_finish (proxy, res, &error);
      break;

    default:
      g_assert_not_reached ();
  }

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_DAEMON_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Error calling EOS updater: %s", error->message,
                     NULL);
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

  sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_DAEMON_ERROR_MSGID,
                   "PRIORITY=%d", LOG_ERR,
                   "MESSAGE=EOS updater error (%s): %s", name, error_message,
                   NULL);
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

  g_message ("EOS updater state is: %u", state);

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
read_config_file (guint *update_interval_days,
                  gboolean *update_on_mobile)
{
  const gchar *config_path = get_config_file_path ();
  g_autoptr(GKeyFile) config = g_key_file_new ();
  g_autoptr(GError) error = NULL;
  gint _update_interval_days;

  g_return_val_if_fail (update_interval_days != NULL, FALSE);
  g_return_val_if_fail (update_on_mobile != NULL, FALSE);

  g_key_file_load_from_file (config, config_path, G_KEY_FILE_NONE, &error);
  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to open the configuration file: %s", error->message,
                     NULL);
    return FALSE;
  }

  last_automatic_step = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                                LAST_STEP_KEY, &error);
  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to read key '%s' in config file", LAST_STEP_KEY,
                     NULL);
    return FALSE;
  }

  _update_interval_days = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                                  INTERVAL_KEY, &error);

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to read key '%s' in config file", INTERVAL_KEY,
                     NULL);
    return FALSE;
  }

  if (_update_interval_days < 0) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Specified update interval is less than zero",
                     NULL);
    return FALSE;
  }

  *update_interval_days = (guint) _update_interval_days;

  *update_on_mobile = g_key_file_get_boolean (config, AUTOMATIC_GROUP,
                                              ON_MOBILE_KEY, &error);

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to read key '%s' in config file", ON_MOBILE_KEY,
                     NULL);
    return FALSE;
  }

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
  return FALSE;
}

static gboolean
is_time_to_update (guint update_interval_days)
{
  const gchar *stamp_dir = get_stamp_dir ();
  g_autofree gchar *stamp_path = NULL;
  GFile *stamp_file;
  GFileInfo *stamp_file_info;
  guint64 last_update_time;
  gint64 current_time_usec;
  GError *error = NULL;
  gboolean time_to_update = FALSE;

  stamp_path = g_build_filename (stamp_dir, UPDATE_STAMP_NAME, NULL);
  stamp_file = g_file_new_for_path (stamp_path);
  stamp_file_info = g_file_query_info (stamp_file,
                                       G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                       G_FILE_QUERY_INFO_NONE, NULL,
                                       &error);

  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      /* Failed for some reason other than the file not being present */
      sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_STAMP_ERROR_MSGID,
                       "PRIORITY=%d", LOG_CRIT,
                       "MESSAGE=Failed to read attributes of updater timestamp file",
                       NULL);
    }

    time_to_update = TRUE;
    g_clear_error (&error);
  } else {
    /* Determine whether sufficient time has elapsed */
    current_time_usec = g_get_real_time ();
    last_update_time =
      g_file_info_get_attribute_uint64 (stamp_file_info,
                                       G_FILE_ATTRIBUTE_TIME_MODIFIED);

    time_to_update = (last_update_time + update_interval_days * SEC_PER_DAY) *
                      G_USEC_PER_SEC < current_time_usec;

    g_object_unref (stamp_file_info);
  }

  g_object_unref (stamp_file);

  return time_to_update;
}

static gboolean
is_online (void)
{
  NMClient *client;
  gboolean online;

  client = nm_client_new ();
  if (!client)
    return FALSE;

  /* Assume that the ostree server is remote and only consider to be
   * online if we have global connectivity.
   */
  switch (nm_client_get_state (client)) {
  case NM_STATE_CONNECTED_GLOBAL:
    online = TRUE;
    break;
  default:
    online = FALSE;
    break;
  }
  g_object_unref (client);

  if (!online)
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_NOT_ONLINE_MSGID,
                     "PRIORITY=%d", LOG_INFO,
                     "MESSAGE=Not currently online. Not updating",
                     NULL);
  return online;
}

static gboolean
is_connected_through_mobile (void)
{
  NMActiveConnection *connection;
  NMClient *client;
  NMDevice *device;
  const GPtrArray *devices;
  gboolean is_mobile = FALSE;
  gint i;

  client = nm_client_new ();
  if (!client) {
    return FALSE;
  }

  g_object_get (client, "primary-connection", &connection, NULL);
  if (!connection) {
    g_object_unref (client);
    return FALSE;
  }

  devices = nm_active_connection_get_devices (connection);
  for (i = 0; i < devices->len; i++) {
    device = (NMDevice *) g_ptr_array_index (devices, i);
    switch (nm_device_get_device_type (device)) {
    case NM_DEVICE_TYPE_MODEM:
    case NM_DEVICE_TYPE_BT:
    case NM_DEVICE_TYPE_WIMAX:
      is_mobile |= TRUE;
      break;
    default:
      break;
    }
  }

  g_object_unref (connection);
  g_object_unref (client);

  return is_mobile;
}

static gboolean
listen_on_session_bus (void)
{
  const gchar *value = NULL;

  value = g_getenv ("EOS_UPDATER_TEST_AUTOUPDATER_USE_SESSION_BUS");

  return value != NULL;
}

static gint
get_dbus_timeout (void)
{
  const gchar *value = NULL;
  gint64 timeout;
  gchar *str_end = NULL;

  value = get_envvar_or ("EOS_UPDATER_TEST_AUTOUPDATER_DBUS_TIMEOUT",
                         NULL);

  if (value == NULL || value[0] == '\0')
    return -1;

  errno = 0;
  timeout = g_ascii_strtoll (value, &str_end, 10);
  if (errno != 0 ||
      str_end == NULL ||
      *str_end != '\0' ||
      timeout > G_MAXINT ||
      timeout < 0)
    return -1;

  return timeout;
}

int
main (int argc, char **argv)
{
  EosUpdater *proxy;
  GError *error = NULL;
  guint update_interval_days;
  gboolean update_on_mobile;
  gboolean force_update = FALSE;
  GOptionContext *context;

  GOptionEntry entries[] = {
    { "force-update", 0, 0, G_OPTION_ARG_NONE, &force_update, "Force an update", NULL },
    { "from-volume", 0, 0, G_OPTION_ARG_STRING, &volume_path, "Poll for updates from the volume", "PATH" },
    { NULL }
  };
  GBusType bus_type = G_BUS_TYPE_SYSTEM;
  gint dbus_timeout;

  context = g_option_context_new ("Endless Automatic Updater");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, NULL);
  g_option_context_free (context);

  if (!read_config_file (&update_interval_days, &update_on_mobile))
    return EXIT_FAILURE;

  if (volume_path == NULL && !is_online ())
    return EXIT_SUCCESS;

  if (!force_update) {
    if (volume_path == NULL &&
        !update_on_mobile &&
        is_connected_through_mobile ()) {
      sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_MOBILE_CONNECTED_MSGID,
                       "PRIORITY=%d", LOG_INFO,
                       "MESSAGE=Connected to mobile network. Not updating",
                       NULL);
      return EXIT_SUCCESS;
    }

    if (!is_time_to_update (update_interval_days)) {
      sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_NOT_TIME_MSGID,
                       "PRIORITY=%d", LOG_INFO,
                       "MESSAGE=Less than IntervalDays since last update. Exiting",
                       NULL);
      return EXIT_SUCCESS;
    }
  }

  main_loop = g_main_loop_new (NULL, FALSE);

  if (listen_on_session_bus ())
    bus_type = G_BUS_TYPE_SESSION;
  proxy = eos_updater_proxy_new_for_bus_sync (bus_type,
                     G_DBUS_PROXY_FLAGS_NONE,
                     "com.endlessm.Updater",
                     "/com/endlessm/Updater",
                     NULL,
                     &error);

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_DAEMON_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Error getting EOS updater object: %s", error->message,
                     NULL);
    g_error_free (error);
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
  g_object_unref (proxy);

  if (should_exit_failure) /* All paths setting this print an error message */
    return EXIT_FAILURE;

  /* Update the stamp file since all configured steps have succeeded. */
  update_stamp_file ();
  sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_SUCCESS_MSGID,
                   "PRIORITY=%d", LOG_INFO,
                   "MESSAGE=Updater finished successfully",
                   NULL);

  return EXIT_SUCCESS;
}
