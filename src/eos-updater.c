#include "ostree-daemon-generated.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>

/* This represents the ostree daemon state, and matches the definition
 * inside ostree.  Ideally ostree would expose it in a header.
 */
typedef enum {
  OTD_STATE_NONE = 0,
  OTD_STATE_READY,
  OTD_STATE_ERROR,
  OTD_STATE_POLLING,
  OTD_STATE_UPDATE_AVAILABLE,
  OTD_STATE_FETCHING,
  OTD_STATE_UPDATE_READY,
  OTD_STATE_APPLYING_UPDATE,
  OTD_STATE_UPDATE_APPLIED,
} OTDState;

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
static const char *UPDATE_STAMP_DIR = "/var/lib/eos-updater";
static const char *UPDATE_STAMP_NAME = "eos-updater-stamp";

static const char *CONFIG_FILE_PATH = "/etc/eos-updater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";
static const char *INTERVAL_KEY = "IntervalDays";

/* Ensures that the updater never tries to poll twice in one run */
static gboolean polled_already = FALSE;

/* Read from config file */
static UpdateStep last_automatic_step;

/* Set when main should return failure */
static gboolean should_exit_failure = FALSE;

/* Avoid erroneous additional state transitions */
static guint previous_state = OTD_STATE_NONE;

static GMainLoop *main_loop;

/* Called on completion of the async dbus calls to check whether they
 * succeeded. Success doesn't mean that the operation succeeded, but it
 * does mean the call reached the daemon.
 */
static void
update_step_callback (GObject *source_object, GAsyncResult *res,
                      gpointer step_data)
{
  OTD *proxy = (OTD *) source_object;
  UpdateStep step = GPOINTER_TO_INT (step_data);
  GError *error = NULL;

  switch (step) {
    case UPDATE_STEP_POLL:
      otd__call_poll_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_FETCH:
      otd__call_fetch_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_APPLY:
      otd__call_apply_finish (proxy, res, &error);
      break;

    default:
      g_assert_not_reached ();
  }

  if (error) {
    g_critical ("Error calling OSTree daemon: %s", error->message);
    should_exit_failure = TRUE;
    g_main_loop_quit (main_loop);
    g_error_free (error);
  }
}

static gboolean
do_update_step (UpdateStep step, OTD *proxy)
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
      otd__call_poll (proxy, NULL, update_step_callback, step_data);
      break;

    case UPDATE_STEP_FETCH:
      otd__call_fetch (proxy, NULL, update_step_callback, step_data);
      break;

    case UPDATE_STEP_APPLY:
      otd__call_apply (proxy, NULL, update_step_callback, step_data);
      break;

    default:
      g_assert_not_reached ();
  }

  return TRUE;
}

static void
report_error_status (OTD *proxy)
{
  const gchar *error_message;
  guint error_code;

  error_code = otd__get_error_code (proxy);
  error_message = otd__get_error_message (proxy);
  g_critical ("OSTree daemon returned error code %u: %s",
              error_code, error_message);
}

/* The updater is driven by state transitions in the ostree daemon.
 * Whenever the state changes, we check if we need to do something
 * as a result of that state change. */
static void
on_state_changed (OTD *proxy, guint state)
{
  gboolean continue_running = TRUE;

  if (state == previous_state) {
    g_warning ("State changed to state %u it was already in", state);
    return;
  }

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

static gboolean
read_config_file (gint *update_interval)
{
  GKeyFile *config = g_key_file_new ();
  GError *error = NULL;
  gboolean success = TRUE;

  if (!g_key_file_load_from_file (config, CONFIG_FILE_PATH,
                                  G_KEY_FILE_NONE, &error)) {
    g_critical ("Can't open config file: %s", CONFIG_FILE_PATH);
    success = FALSE;
    goto out;
  }

  last_automatic_step = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                                LAST_STEP_KEY, &error);
  if (error) {
    g_critical ("Can't read key \"%s\" in config file", LAST_STEP_KEY);
    success = FALSE;
    goto out;
  }

  *update_interval = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                             INTERVAL_KEY, &error);

  if (error) {
    g_critical ("Can't read key \"%s\" in config file", INTERVAL_KEY);
    success = FALSE;
    goto out;
  }

  if (*update_interval < 0) {
    g_critical ("Specified update interval is less than zero");
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
  OTD *proxy = (OTD *) pointer;
  OTDState initial_state = otd__get_state (proxy);

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
  gchar *stamp_file_path;
  GFile *stamp_file;
  GFileInfo *stamp_file_info;
  guint64 last_update_time;
  gint64 current_time_usec;
  GError *error = NULL;
  gboolean time_to_update = FALSE;

  if (g_mkdir_with_parents (UPDATE_STAMP_DIR, 0644) != 0)
    g_critical ("Failed to create updater timestamp directory %s",
                UPDATE_STAMP_DIR);

  stamp_file_path = g_strconcat (UPDATE_STAMP_DIR, "/",
                                 UPDATE_STAMP_NAME, NULL);

  stamp_file = g_file_new_for_path (stamp_file_path);

  stamp_file_info = g_file_query_info (stamp_file,
                                       G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                       G_FILE_QUERY_INFO_NONE, NULL, &error);

  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      /* Failed for some reason other than the file not being present */
      g_critical ("Failed to read attributes of updater timestamp file %s",
                  stamp_file_path);
    }

    time_to_update = TRUE;
    g_error_free (error);
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

  if (!g_file_replace_contents (stamp_file, "", 0, NULL, FALSE, 
                                G_FILE_CREATE_NONE, NULL, NULL, NULL))
    g_critical ("Failed to write updater stamp file %s", stamp_file_path);

  g_object_unref (stamp_file);
  g_free (stamp_file_path);

  return time_to_update;
}

int
main (int argc, char **argv)
{
  OTD *proxy;
  GError *error = NULL;
  gint update_interval;

  if (!read_config_file (&update_interval))
    return EXIT_FAILURE;

  if (!is_time_to_update (update_interval))
    return EXIT_SUCCESS;

  main_loop = g_main_loop_new (NULL, FALSE);

  proxy = otd__proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                     G_DBUS_PROXY_FLAGS_NONE,
                     "org.gnome.OSTree",
                     "/org/gnome/OSTree",
                     NULL,
                     &error);

  if (error) {
    g_printerr ("Error getting OSTree proxy object: %s", error->message);
    g_error_free (error);
    should_exit_failure = TRUE;
    goto out;
  }

  g_signal_connect (proxy, "state-changed",
                    G_CALLBACK (on_state_changed), NULL);

  g_idle_add (initial_poll_idle_func, proxy);
  g_main_loop_run (main_loop);

out:
  g_main_loop_unref (main_loop);
  g_object_unref (proxy);

  if (should_exit_failure) /* All paths setting this print an error message */
    return EXIT_FAILURE;

  g_message ("Updater finished successfully; exiting");
  return EXIT_SUCCESS;
}
