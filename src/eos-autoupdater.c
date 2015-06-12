#include "ostree-daemon-generated.h"
#include "ostree-daemon-types.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>
#include <errno.h>

#include <nm-client.h>
#include <nm-device.h>

#include <systemd/sd-journal.h>

#define EOS_UPDATER_CONFIGURATION_ERROR_MSGID   "5af9f4df37f949a1948971e00be0d620"
#define EOS_UPDATER_OSTREE_DAEMON_ERROR_MSGID   "f31fd043074a4a21b04784cf895c56ae"
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
#define UPDATE_STAMP_DIR        "/var/lib/eos-updater"
#define UPDATE_STAMP_NAME       "eos-updater-stamp"

static const char *CONFIG_FILE_PATH = "/etc/eos-updater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";
static const char *INTERVAL_KEY = "IntervalDays";
static const char *ON_MOBILE_KEY = "UpdateOnMobile";

/* Ensures that the updater never tries to poll twice in one run */
static gboolean polled_already = FALSE;

/* Read from config file */
static UpdateStep last_automatic_step;

/* Set when main should return failure */
static gboolean should_exit_failure = FALSE;

/* Avoid erroneous additional state transitions */
static guint previous_state = OTD_STATE_NONE;

static GMainLoop *main_loop;

static void
update_stamp_file (void)
{
  GFile *stamp_file;
  GError *error = NULL;
  gboolean ret = TRUE;

  if (g_mkdir_with_parents (UPDATE_STAMP_DIR, 0755) != 0) {
    int saved_errno = errno;
    const char *err_str = g_strerror (saved_errno);

    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_CRIT,
                     "MESSAGE=Failed to create updater timestamp directory: %s", err_str,
                     NULL);
    return;
  }

  stamp_file = g_file_new_for_path (UPDATE_STAMP_DIR G_DIR_SEPARATOR_S UPDATE_STAMP_NAME);
  g_file_replace_contents (stamp_file, "", 0, NULL, FALSE,
                           G_FILE_CREATE_NONE, NULL, NULL,
                           &error);
  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_STAMP_ERROR_MSGID,
                     "PRIORITY=%d", LOG_CRIT,
                     "MESSAGE=Failed to write updater stamp file: %s", error->message,
                     NULL);
    g_error_free (error);
  }

  g_object_unref (stamp_file);
}

/* Called on completion of the async dbus calls to check whether they
 * succeeded. Success doesn't mean that the operation succeeded, but it
 * does mean the call reached the daemon.
 */
static void
update_step_callback (GObject *source_object, GAsyncResult *res,
                      gpointer step_data)
{
  OTDOSTree *proxy = (OTDOSTree *) source_object;
  UpdateStep step = GPOINTER_TO_INT (step_data);
  GError *error = NULL;

  switch (step) {
    case UPDATE_STEP_POLL:
      otd_ostree_call_poll_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_FETCH:
      otd_ostree_call_fetch_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_APPLY:
      otd_ostree_call_apply_finish (proxy, res, &error);
      break;

    default:
      g_assert_not_reached ();
  }

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_OSTREE_DAEMON_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Error calling OSTree daemon: %s", error->message,
                     NULL);
    should_exit_failure = TRUE;
    g_main_loop_quit (main_loop);
    g_error_free (error);
  }
}

static gboolean
do_update_step (UpdateStep step, OTDOSTree *proxy)
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
      otd_ostree_call_poll (proxy, NULL, update_step_callback, step_data);
      break;

    case UPDATE_STEP_FETCH:
      otd_ostree_call_fetch (proxy, NULL, update_step_callback, step_data);
      break;

    case UPDATE_STEP_APPLY:
      otd_ostree_call_apply (proxy, NULL, update_step_callback, step_data);
      break;

    default:
      g_assert_not_reached ();
  }

  return TRUE;
}

static void
report_error_status (OTDOSTree *proxy)
{
  const gchar *error_message;
  guint error_code;

  error_code = otd_ostree_get_error_code (proxy);
  error_message = otd_ostree_get_error_message (proxy);

  sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_OSTREE_DAEMON_ERROR_MSGID,
                   "PRIORITY=%d", LOG_ERR,
                   "MESSAGE=OSTree daemon error (code:%u): %s", error_code, error_message,
                   NULL);
}

/* The updater is driven by state transitions in the ostree daemon.
 * Whenever the state changes, we check if we need to do something
 * as a result of that state change. */
static void
on_state_changed (OTDOSTree *proxy, OTDState state)
{
  gboolean continue_running = TRUE;

  if (state == previous_state)
    return;

  previous_state = state;

  g_message ("OSTree daemon state is: %u", state);

  switch (state) {
    case OTD_STATE_NONE: /* State should change soon */
      break;

    case OTD_STATE_READY: /* Must poll */
      continue_running = do_update_step (UPDATE_STEP_POLL, proxy);
      break;

    case OTD_STATE_ERROR: /* Log error and quit */
      report_error_status (proxy);
      should_exit_failure = TRUE;
      continue_running = FALSE;
      break;

    case OTD_STATE_POLLING: /* Wait for completion */
      break;

    case OTD_STATE_UPDATE_AVAILABLE: /* Possibly fetch */
      continue_running = do_update_step (UPDATE_STEP_FETCH, proxy);
      break;

    case OTD_STATE_FETCHING: /* Wait for completion */
      break;

    case OTD_STATE_UPDATE_READY: /* Possibly apply */
      continue_running = do_update_step (UPDATE_STEP_APPLY, proxy);
      break;

    case OTD_STATE_APPLYING_UPDATE: /* Wait for completion */
      break;

    case OTD_STATE_UPDATE_APPLIED: /* Done; exit */
      continue_running = FALSE;
      break;

    default:
      g_critical ("OSTree daemon entered invalid state: %u", state);
      continue_running = FALSE;
      should_exit_failure = TRUE;
      break;
  }

  if (!continue_running) {
    g_main_loop_quit (main_loop);
  }
}

static void
on_state_changed_notify (OTDOSTree *proxy,
                         GParamSpec *pspec,
                         gpointer data)
{
  OTDState state = otd_ostree_get_state (proxy);
  on_state_changed (proxy, state);
}

static gboolean
read_config_file (gint *update_interval,
                  gboolean *update_on_mobile)
{
  GKeyFile *config = g_key_file_new ();
  GError *error = NULL;
  gboolean success = TRUE;

  g_key_file_load_from_file (config, CONFIG_FILE_PATH, G_KEY_FILE_NONE, &error);
  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to open the configuration file: %s", error->message,
                     NULL);
    success = FALSE;
    goto out;
  }

  last_automatic_step = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                                LAST_STEP_KEY, &error);
  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to read key '%s' in config file", LAST_STEP_KEY,
                     NULL);
    success = FALSE;
    goto out;
  }

  *update_interval = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                             INTERVAL_KEY, &error);

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to read key '%s' in config file", INTERVAL_KEY,
                     NULL);
    success = FALSE;
    goto out;
  }

  if (*update_interval < 0) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Specified update interval is less than zero",
                     NULL);
    success = FALSE;
    goto out;
  }

  *update_on_mobile = g_key_file_get_boolean (config, AUTOMATIC_GROUP,
                                              ON_MOBILE_KEY, &error);

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_CONFIGURATION_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Unable to read key '%s' in config file", ON_MOBILE_KEY,
                     NULL);
    success = FALSE;
    goto out;
  }

out:
  g_clear_error (&error);
  g_key_file_free (config);

  return success;
}

/* We want to poll once when the updater starts.  To make sure that we
 * can quit ourselves gracefully, we wait until the main loop starts.
 */
static gboolean
initial_poll_idle_func (gpointer pointer)
{
  OTDOSTree *proxy = (OTDOSTree *) pointer;
  OTDState initial_state = otd_ostree_get_state (proxy);

  /* Attempt to clear the error by pretending to be ready, which will
   * trigger a poll
   */
  if (initial_state == OTD_STATE_ERROR)
    initial_state = OTD_STATE_READY;

  on_state_changed (proxy, initial_state);

  /* Disable this function after the first run */
  return FALSE;
}

static gboolean
is_time_to_update (gint update_interval)
{
  GFile *stamp_file;
  GFileInfo *stamp_file_info;
  guint64 last_update_time;
  gint64 current_time_usec;
  GError *error = NULL;
  gboolean time_to_update = FALSE;

  stamp_file = g_file_new_for_path (UPDATE_STAMP_DIR G_DIR_SEPARATOR_S UPDATE_STAMP_NAME);
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

    time_to_update = (last_update_time + update_interval * SEC_PER_DAY) *
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
    }
  }

  g_object_unref (connection);
  g_object_unref (client);

  return is_mobile;
}

int
main (int argc, char **argv)
{
  OTDOSTree *proxy;
  GError *error = NULL;
  gint update_interval;
  gboolean update_on_mobile;
  gboolean force_update = FALSE;
  GOptionContext *context;

  GOptionEntry entries[] = {
    { "force-update", 0, 0, G_OPTION_ARG_NONE, &force_update, "Force an update", NULL },
    { NULL }
  };

  context = g_option_context_new ("Endless Automatic Updater");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, NULL);
  g_option_context_free (context);

  if (!read_config_file (&update_interval, &update_on_mobile))
    return EXIT_FAILURE;

  if (!is_online ())
    return EXIT_SUCCESS;

  if (!force_update) {
    if (!update_on_mobile && is_connected_through_mobile ()) {
      sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_MOBILE_CONNECTED_MSGID,
                       "PRIORITY=%d", LOG_INFO,
                       "MESSAGE=Connected to mobile network. Not updating",
                       NULL);
      return EXIT_SUCCESS;
    }

    if (!is_time_to_update (update_interval)) {
      sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_NOT_TIME_MSGID,
                       "PRIORITY=%d", LOG_INFO,
                       "MESSAGE=Less than IntervalDays since last update. Exiting",
                       NULL);
      return EXIT_SUCCESS;
    }
  }

  main_loop = g_main_loop_new (NULL, FALSE);

  proxy = otd_ostree_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                     G_DBUS_PROXY_FLAGS_NONE,
                     "org.gnome.OSTree",
                     "/org/gnome/OSTree",
                     NULL,
                     &error);

  if (error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_OSTREE_DAEMON_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Error getting OSTree proxy object: %s", error->message,
                     NULL);
    g_error_free (error);
    should_exit_failure = TRUE;
    goto out;
  }

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
