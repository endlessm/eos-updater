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

static const char *CONFIG_FILE_PATH = "/etc/eos-updater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";

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
  gboolean success;
  GError *error = NULL;

  switch (step) {
    case UPDATE_STEP_POLL:
      success = otd__call_poll_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_FETCH:
      success = otd__call_fetch_finish (proxy, res, &error);
      break;

    case UPDATE_STEP_APPLY:
      success = otd__call_apply_finish (proxy, res, &error);
      break;

    default:
      g_assert_not_reached ();
  }

  if (!success) {
    handle_call_error (proxy, error);
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
read_config_file (void)
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
    return FALSE;
  }
  return TRUE;
}

static gboolean
initial_poll_idle_func (gpointer pointer) {
  OTD *proxy = (OTD *) pointer;
  gboolean continue_running;

  continue_running = do_update_step (UPDATE_STEP_POLL, proxy);
  if (!continue_running) {
    g_main_loop_quit (main_loop);
  }

  return FALSE;
}

int
main (int argc, char **argv)
{
  OTD *proxy;
  GError *error = NULL;

  if (!read_config_file())
    return 1;

  main_loop = g_main_loop_new (NULL, FALSE);

  proxy = otd__proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                     G_DBUS_PROXY_FLAGS_NONE,
                     "org.gnome.OSTree",
                     "/org/gnome/OSTree",
                     NULL,
                     &error);

  if (!proxy) {
    g_printerr ("Error getting proxy: %s", error->message);
    return 1;
  }

  g_signal_connect (proxy, "state-changed",
                    G_CALLBACK (on_state_changed), NULL);

  g_idle_add (initial_poll_idle_func, proxy);
  g_main_loop_run (main_loop);

  return 0;
}
