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

gboolean polled_yet = FALSE;
GMainLoop *main_loop;
UpdateStep last_automatic_step;

static gboolean
step_is_automatic (UpdateStep step) {
  return step <= last_automatic_step;
}

static void handle_state_error (OTD *proxy) {
  const gchar *message = otd__get_error_message (proxy);

  g_warning ("OSTree daemon entered error state: %s", message);
}

static void handle_call_error (OTD *proxy, GError *error) {
  g_critical ("Error calling OSTree daemon: %s", error->message);
}

static void
poll_result_callback (GObject *source_object, GAsyncResult *res, gpointer ign)
{
  OTD *proxy = (OTD *) source_object;
  gboolean success;
  GError *error = NULL;

  success = otd__call_poll_finish (proxy, res, &error);

  if (!success) {
    handle_call_error (proxy, error);
    return;
  }
}

static void
check_update_poll (OTD *proxy)
{
  if (!step_is_automatic (UPDATE_STEP_POLL)) {
    g_main_loop_quit (main_loop);
    return;
  }

  // TODO: check timer?
  if (polled_yet) {
    g_main_loop_quit (main_loop);
    return;
  }

  polled_yet = TRUE;
  otd__call_poll (proxy, NULL, poll_result_callback, NULL);
}

static void
fetch_result_callback (GObject *source_object, GAsyncResult *res, gpointer ign)
{
  OTD *proxy = (OTD *) source_object;
  gboolean success;
  GError *error = NULL;

  success = otd__call_fetch_finish (proxy, res, &error);

  if (!success) {
    handle_call_error (proxy, error);
    return;
  }
}

static void
check_update_fetch (OTD *proxy)
{
  if (!step_is_automatic (UPDATE_STEP_POLL)) {
    g_main_loop_quit (main_loop);
    return;
  }

  otd__call_fetch (proxy, NULL, fetch_result_callback, NULL);
}

static void
apply_result_callback (GObject *source_object, GAsyncResult *res, gpointer ign)
{
  OTD *proxy = (OTD *) source_object;
  gboolean success;
  GError *error = NULL;

  success = otd__call_apply_finish (proxy, res, &error);

  if (!success) {
    handle_call_error (proxy, error);
    return;
  }
}

static void
check_update_apply (OTD *proxy)
{
  if (!step_is_automatic (UPDATE_STEP_POLL)) {
    g_main_loop_quit (main_loop);
    return;
  }

  // Don't apply at the moment!
  g_print ("WOULD APPLY HERE!\n");
  // otd__call_apply (proxy, NULL, apply_result_callback, NULL);
}

static void
on_state_changed (OTD *proxy, guint state)
{
  g_print ("State changed to: %u\n", state);

  switch (state) {
    case OTD_STATE_NONE:
      break;
    case OTD_STATE_READY:
      check_update_poll (proxy);
      break;
    case OTD_STATE_ERROR:
      handle_state_error (proxy);
      break;
    case OTD_STATE_POLLING:
      break;
    case OTD_STATE_UPDATE_AVAILABLE:
      check_update_fetch (proxy);
      break;
    case OTD_STATE_FETCHING:
      break;
    case OTD_STATE_UPDATE_READY:
      check_update_apply (proxy);
      break;
    case OTD_STATE_APPLYING_UPDATE:
      break;
    case OTD_STATE_UPDATE_APPLIED:
      g_main_loop_quit (main_loop);
      break;
  }

  // if (previous_state != state)
  //   previous_state = state;
}

static const char *CONFIG_FILE_PATH = "/etc/eos-updater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";

static gboolean
read_config_file (void)
{
  GKeyFile *config = g_key_file_new ();
  GError *error = NULL;

  if (!g_key_file_load_from_file (config, CONFIG_FILE_PATH, G_KEY_FILE_NONE, &error)) {
    g_critical ("Can't open config file: %s", CONFIG_FILE_PATH);
    return FALSE;
  }

  last_automatic_step = g_key_file_get_integer (config, AUTOMATIC_GROUP, LAST_STEP_KEY, &error);
  if (error) {
    g_critical ("Can't find key in config file");
    return FALSE;
  }
  return TRUE;
}

static gboolean
initial_poll_idle_func (gpointer pointer) {
  OTD *proxy = (OTD *) pointer;

  check_update_poll (proxy);
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
                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                     "org.gnome.OSTree",
                     "/org/gnome/OSTree",
                     NULL,
                     &error);

  if (!proxy) {
    g_printerr ("Error getting proxy: %s", error->message);
    return 1;
  }

  g_signal_connect (proxy, "state-changed", G_CALLBACK (on_state_changed), NULL);

  g_idle_add (initial_poll_idle_func, proxy);
  g_main_loop_run (main_loop);

  return 0;
}