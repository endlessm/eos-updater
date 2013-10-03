#include <gio/gio.h>
#include <glib.h>

#include "ostree-daemon-generated.h"

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

typedef enum _UpdateStep {
  UPDATE_STEP_NONE,
  UPDATE_STEP_POLL,
  UPDATE_STEP_FETCH,
  UPDATE_STEP_APPLY
} UpdateStep;

#define SEC_PER_DAY (3600ll * 24)

static const char *UPDATE_STAMP_FILE = "/var/lib/eos-updater-stamp";

static const char *CONFIG_FILE_PATH = "/etc/eos-updater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";
static const char *INTERVAL_KEY = "IntervalDays";

static gboolean polled_already = FALSE;
static GMainLoop *main_loop;
static UpdateStep last_automatic_step;

static void handle_call_error (OTD *proxy, GError *error) {
  g_critical ("Error calling OSTree daemon: %s", error->message);

  g_main_loop_quit (main_loop);
}

static void
update_step_callback (GObject *source_object, GAsyncResult *res,
                      gpointer currentStep)
{
  OTD *proxy = (OTD *) source_object;
  UpdateStep step = *(UpdateStep *) currentStep;
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
    handle_call_error (proxy, error);
    g_error_free (error);
  }

  g_free (currentStep);
}

static gboolean
do_update_step (UpdateStep step, OTD *proxy)
{
  UpdateStep *currentStep;

  if (step > last_automatic_step) {
    return FALSE;
  }

  if (step == UPDATE_STEP_POLL && polled_already) {
    return FALSE;
  }

  polled_already = TRUE;

  currentStep = g_malloc (sizeof (UpdateStep));
  *currentStep = step;

  switch (step) {
    case UPDATE_STEP_POLL:
      otd__call_poll (proxy, NULL, update_step_callback, currentStep);
      break;

    case UPDATE_STEP_FETCH:
      otd__call_fetch (proxy, NULL, update_step_callback, currentStep);
      break;

    case UPDATE_STEP_APPLY:
      otd__call_apply (proxy, NULL, update_step_callback, currentStep);
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

static void
on_state_changed (OTD *proxy, guint state)
{
  gboolean continue_running = TRUE;

  g_print ("State changed to: %u\n", state);

  switch (state) {
    case OTD_STATE_NONE: // State should change soon
      break;

    case OTD_STATE_READY: // Must poll
      continue_running = do_update_step (UPDATE_STEP_POLL, proxy);
      break;

    case OTD_STATE_ERROR: // Log error and quit
      report_error_status (proxy);
      continue_running = FALSE;
      break;

    case OTD_STATE_POLLING: // Wait for completion
      break;

    case OTD_STATE_UPDATE_AVAILABLE: // Possibly fetch
      continue_running = do_update_step (UPDATE_STEP_FETCH, proxy);
      break;

    case OTD_STATE_FETCHING: // Wait for completion
      break;

    case OTD_STATE_UPDATE_READY: // Possibly apply
      continue_running = do_update_step (UPDATE_STEP_APPLY, proxy);
      break;

    case OTD_STATE_APPLYING_UPDATE: // Wait for completion
      break;

    case OTD_STATE_UPDATE_APPLIED: // Done; exit
      continue_running = FALSE;
      break;

    default:
      g_critical ("OSTree daemon entered invalid state: %u", state);
      continue_running = FALSE;
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

  if (!g_key_file_load_from_file (config, CONFIG_FILE_PATH,
                                  G_KEY_FILE_NONE, &error)) {
    g_critical ("Can't open config file: %s", CONFIG_FILE_PATH);
    return FALSE;
  }

  last_automatic_step = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                                LAST_STEP_KEY, &error);
  if (error) {
    g_critical ("Can't find key in config file");
    g_error_free (error);
    return FALSE;
  }

  *update_interval = g_key_file_get_integer (config, AUTOMATIC_GROUP,
                                             INTERVAL_KEY, &error);

  if (error) {
    g_critical ("Can't find key in config file");
    g_error_free (error);
    return FALSE;    
  }

  return TRUE;
}

static gboolean
initial_poll_idle_func (gpointer pointer)
{
  OTD *proxy = (OTD *) pointer;
  gboolean continue_running;

  continue_running = do_update_step (UPDATE_STEP_POLL, proxy);
  if (!continue_running) {
    g_main_loop_quit (main_loop);
  }

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

  if (update_interval < 0)
    return FALSE;

  stamp_file = g_file_new_for_path (UPDATE_STAMP_FILE);

  stamp_file_info = g_file_query_info (stamp_file,
                                       G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                       G_FILE_QUERY_INFO_NONE, NULL, &error);

  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      // Failed for some reason other than the file not being present
      g_warning ("Failed to read attributes of updater timestamp file %s",
                 UPDATE_STAMP_FILE);
    }

    time_to_update = TRUE;
    g_error_free (error);
  } else {
    // Determine whether sufficient time has elapsed
    current_time_usec = g_get_real_time ();
    last_update_time =
      g_file_info_get_attribute_uint64 (stamp_file_info,
                                       G_FILE_ATTRIBUTE_TIME_MODIFIED);

    time_to_update = (last_update_time + update_interval * SEC_PER_DAY) *
                      G_USEC_PER_SEC < current_time_usec;

    g_object_unref (stamp_file_info);
  }

  if (!g_file_replace_contents (stamp_file, "", 0, NULL, FALSE, 
                                G_FILE_CREATE_NONE, NULL, NULL, NULL)) {
    g_warning ("Failed to write updater stamp file %s", UPDATE_STAMP_FILE);
  }

  return time_to_update;
}

int
main (int argc, char **argv)
{
  OTD *proxy;
  GError *error = NULL;
  gint update_interval;

  if (!read_config_file (&update_interval))
    return 1;

  if (!is_time_to_update (update_interval))
    return 0;

  main_loop = g_main_loop_new (NULL, FALSE);

  proxy = otd__proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                     G_DBUS_PROXY_FLAGS_NONE,
                     "org.gnome.OSTree",
                     "/org/gnome/OSTree",
                     NULL,
                     &error);

  if (error) {
    g_printerr ("Error getting proxy: %s", error->message);
    g_error_free (error);
    return 1;
  }

  g_signal_connect (proxy, "state-changed",
                    G_CALLBACK (on_state_changed), NULL);

  g_idle_add (initial_poll_idle_func, proxy);
  g_main_loop_run (main_loop);

  return 0;
}
