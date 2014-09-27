
#include "eos-updater-generated.h"
#include "ostree-daemon-generated.h"

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

G_DEFINE_QUARK (eos-updater-error-quark, eos_updater_error)

typedef enum {
  EOS_UPDATER_ERROR_INVALID_CONFIGURATION,
  EOS_UPDATER_ERROR_OSTREE_ERROR,
  EOS_UPDATER_ERROR_INVALID_STAMP,
  EOS_UPDATER_ERROR_NOT_ONLINE,
  EOS_UPDATER_ERROR_MOBILE_CONNECTED
} EosUpdaterError;

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
#define UPDATE_STAMP_DIR        "/var/lib/eos-updater"
#define UPDATE_STAMP_NAME       "eos-updater-stamp"

static const char *CONFIG_FILE_PATH = "/etc/eos-updater.conf";
static const char *AUTOMATIC_GROUP = "Automatic Updates";
static const char *LAST_STEP_KEY = "LastAutomaticStep";
static const char *INTERVAL_KEY = "IntervalDays";
static const char *ON_MOBILE_KEY = "UpdateOnMobile";

/* ------------------------------------------------------------------ */

enum {
  PROP_0,
  PROP_LAST_AUTOMATIC_STEP,
  NUM_PROPS
};

static GParamSpec *props[NUM_PROPS] = { NULL, };

typedef struct {
  GObject parent;

  GTask *task;
  OTD *proxy;

  /* Avoid erroneous additional state transitions */
  OTDState previous_state;

  /* Ensures that the updater never tries to poll twice in one run */
  gboolean polled_already;

  /* Current update step and last automatic step */
  UpdateStep current_step;
  UpdateStep last_automatic_step;
} EosUpdaterTransaction;

typedef GObjectClass EosUpdaterTransactionClass;

static void eos_updater_transaction_initable_iface_init (GInitableIface *iface);

#define EOS_TYPE_UPDATER_TRANSACTION (eos_updater_transaction_get_type ())
#define EOS_UPDATER_TRANSACTION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EOS_TYPE_UPDATER_TRANSACTION, EosUpdaterTransaction))
G_DEFINE_TYPE_WITH_CODE (EosUpdaterTransaction, eos_updater_transaction, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, eos_updater_transaction_initable_iface_init))

static void
eos_updater_transaction_finalize (GObject *object)
{
  EosUpdaterTransaction *self = EOS_UPDATER_TRANSACTION (object);

  g_clear_object (&self->proxy);
  g_clear_object (&self->task);

  G_OBJECT_CLASS (eos_updater_transaction_parent_class)->finalize (object);
}

static void
eos_updater_transaction_set_property (GObject      *object,
				      guint         prop_id,
				      const GValue *value,
				      GParamSpec   *pspec)
{
  EosUpdaterTransaction *self = EOS_UPDATER_TRANSACTION (object);

  switch (prop_id) {
  case PROP_LAST_AUTOMATIC_STEP:
    self->last_automatic_step = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
update_stamp_file (void)
{
  GFile *stamp_file;
  GError *error = NULL;

  if (g_mkdir_with_parents (UPDATE_STAMP_DIR, 0644) != 0) {
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
update_step_callback (GObject *source_object,
		      GAsyncResult *res,
                      gpointer user_data)
{
  OTD *proxy = (OTD *) source_object;
  EosUpdaterTransaction *self = user_data;
  GError *error = NULL;

  switch (self->current_step) {
    case UPDATE_STEP_POLL:
      otd__call_poll_finish (proxy, res, &error);

      /* Update the stamp file on successful poll */
      if (!error)
        update_stamp_file ();
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

  if (error != NULL) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_OSTREE_DAEMON_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Error calling OSTree daemon: %s", error->message,
                     NULL);
    g_task_return_error (self->task, error);
  }
}

static gboolean
do_update_step (EosUpdaterTransaction *self,
		UpdateStep step)
{
  GCancellable *cancellable;

  /* Don't do more of the process than configured */
  if (step > self->last_automatic_step)
    return FALSE;

  self->current_step = step;
  cancellable = g_task_get_cancellable (self->task);

  switch (step) {
    case UPDATE_STEP_POLL:
      /* Don't poll more than once, or we will get stuck in a loop */
      if (self->polled_already)
        return FALSE;

      self->polled_already = TRUE;
      otd__call_poll (self->proxy, cancellable, update_step_callback, self);
      break;

    case UPDATE_STEP_FETCH:
      otd__call_fetch (self->proxy, cancellable, update_step_callback, self);
      break;

    case UPDATE_STEP_APPLY:
      otd__call_apply (self->proxy, cancellable, update_step_callback, self);
      break;

    default:
      g_assert_not_reached ();
  }

  return TRUE;
}

/* The updater is driven by state transitions in the ostree daemon.
 * Whenever the state changes, we check if we need to do something
 * as a result of that state change. */
static void
on_state_changed (EosUpdaterTransaction *self,
		  OTDState state)
{
  gboolean continue_running = TRUE;
  GError *error = NULL;

  if (state == self->previous_state)
    return;

  self->previous_state = state;

  g_message ("OSTree daemon state is: %u", state);

  switch (state) {
    case OTD_STATE_NONE: /* State should change soon */
      break;

    case OTD_STATE_READY: /* Must poll */
      continue_running = do_update_step (self, UPDATE_STEP_POLL);
      break;

    case OTD_STATE_ERROR: /* Log error and quit */
      g_set_error (&error, eos_updater_error_quark (),
		   EOS_UPDATER_ERROR_OSTREE_ERROR,
		   "OSTree daemon error (code: %u): %s",
		   otd__get_error_code (self->proxy),
		   otd__get_error_message (self->proxy));

      continue_running = FALSE;
      break;

    case OTD_STATE_POLLING: /* Wait for completion */
      break;

    case OTD_STATE_UPDATE_AVAILABLE: /* Possibly fetch */
      continue_running = do_update_step (self, UPDATE_STEP_FETCH);
      break;

    case OTD_STATE_FETCHING: /* Wait for completion */
      break;

    case OTD_STATE_UPDATE_READY: /* Possibly apply */
      continue_running = do_update_step (self, UPDATE_STEP_APPLY);
      break;

    case OTD_STATE_APPLYING_UPDATE: /* Wait for completion */
      break;

    case OTD_STATE_UPDATE_APPLIED: /* Done; exit */
      continue_running = FALSE;
      break;

    default:
      g_set_error (&error, eos_updater_error_quark (),
		   EOS_UPDATER_ERROR_OSTREE_ERROR,
		   "OSTree daemon entered invalid state: %u",
		   state);

      continue_running = FALSE;
      break;
  }

  if (continue_running)
    return;

  if (error != NULL) {
      sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_OSTREE_DAEMON_ERROR_MSGID,
		       "PRIORITY=%d", LOG_ERR,
		       "MESSAGE=%s", error->message,
		       NULL);
      g_task_return_error (self->task, error);
  } else {
      g_task_return_boolean (self->task, TRUE);
  }
}

static void
on_state_changed_notify (OTD *proxy,
                         GParamSpec *pspec,
                         gpointer data)
{
  EosUpdaterTransaction *self = data;
  OTDState state = otd__get_state (proxy);
  on_state_changed (self, state);
}

static gboolean
eos_updater_transaction_initable_init (GInitable *initable,
				       GCancellable *cancellable,
				       GError **error)
{
  EosUpdaterTransaction *self = EOS_UPDATER_TRANSACTION (initable);
  GError *local_error = NULL;

  self->proxy = otd__proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					     G_DBUS_PROXY_FLAGS_NONE,
					     "org.gnome.OSTree",
					     "/org/gnome/OSTree",
					     cancellable,
					     &local_error);

  if (local_error) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_OSTREE_DAEMON_ERROR_MSGID,
                     "PRIORITY=%d", LOG_ERR,
                     "MESSAGE=Error getting OSTree proxy object: %s", local_error->message,
                     NULL);

    g_set_error (error, eos_updater_error_quark (),
		 EOS_UPDATER_ERROR_OSTREE_ERROR,
		 "Error getting OSTree proxy object: %s",
		 local_error->message);

    return FALSE;
  }

  return TRUE;
}

static void
eos_updater_transaction_initable_iface_init (GInitableIface *iface)
{
  iface->init = eos_updater_transaction_initable_init;
}

static void
eos_updater_transaction_init (EosUpdaterTransaction *self)
{
}

static void
eos_updater_transaction_class_init (EosUpdaterTransactionClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = eos_updater_transaction_finalize;
  oclass->set_property = eos_updater_transaction_set_property;

  props[PROP_LAST_AUTOMATIC_STEP] = g_param_spec_uint ("last-automatic-step",
						       "Last automatic step",
						       "Last automatic step",
						       UPDATE_STEP_NONE,
						       UPDATE_STEP_APPLY,
						       UPDATE_STEP_NONE,
						       G_PARAM_CONSTRUCT | G_PARAM_WRITABLE);
  g_object_class_install_properties (oclass, NUM_PROPS, props);
}

static gboolean
eos_updater_transaction_run_finish (EosUpdaterTransaction *self,
				    GAsyncResult *res,
				    GError **error)
{
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
eos_updater_transaction_run_async (EosUpdaterTransaction *self,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer user_data)
{
  OTDState initial_state = otd__get_state (self->proxy);

  g_assert (self->task == NULL);

  self->task = g_task_new (self, cancellable, callback, user_data);

  /* Attempt to clear the error by pretending to be ready, which will
   * trigger a poll
   */
  if (initial_state == OTD_STATE_ERROR)
    initial_state = OTD_STATE_READY;

  g_signal_connect (self->proxy, "notify::state",
                    G_CALLBACK (on_state_changed_notify), self);
  on_state_changed (self, initial_state);
}

static void
eos_updater_transaction_set_last_automatic_step (EosUpdaterTransaction *self,
						 UpdateStep step)
{
  self->last_automatic_step = step;
}

static EosUpdaterTransaction *
eos_updater_transaction_new (UpdateStep last_automatic_step,
			     GError **error)
{
  return g_initable_new (eos_updater_transaction_get_type (),
			 NULL, error, /* GCancellable */
			 "last-automatic-step", last_automatic_step,
			 NULL);
}

/* ------------------------------------------------------------------ */

typedef struct {
  GObject parent;

  guint bus_owner_id;
  SystemUpdater *skeleton;
  GMainLoop *main_loop;

  GCancellable *transaction_cancellable;
  EosUpdaterTransaction *transaction;
  GList *pending_invocations;
} EosUpdater;

typedef GObjectClass EosUpdaterClass;

#define EOS_TYPE_UPDATER (eos_updater_get_type ())
#define EOS_UPDATER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EOS_TYPE_UPDATER, EosUpdater))
G_DEFINE_TYPE (EosUpdater, eos_updater, G_TYPE_OBJECT)

static gboolean
read_config_file (gint *update_interval,
                  gboolean *update_on_mobile,
		  UpdateStep *last_automatic_step)
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

  *last_automatic_step = g_key_file_get_integer (config, AUTOMATIC_GROUP,
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
    default:
      break;
    }
  }

  g_object_unref (connection);
  g_object_unref (client);

  return is_mobile;
}

static void
updater_transaction_completed (GObject *object,
			       GAsyncResult *res,
			       gpointer user_data)
{
  EosUpdater *updater = user_data;
  GError *error = NULL;
  GDBusMethodInvocation *invocation;
  GList *l;

  eos_updater_transaction_run_finish (EOS_UPDATER_TRANSACTION (object), res, &error);

  for (l = updater->pending_invocations; l != NULL; l = l->next) {
    invocation = l->data;
    if (error)
      g_dbus_method_invocation_take_error (invocation, error);
    else
      g_dbus_method_invocation_return_value (invocation, NULL);
  }

  g_clear_pointer (&updater->pending_invocations, g_list_free);
  g_clear_object (&updater->transaction);
  g_clear_object (&updater->transaction_cancellable);

  /* ref in ensure_update_transaction_for_invocation() */
  g_object_unref (updater);

  /* Time to quit */
  g_main_loop_quit (updater->main_loop);
}

static void
ensure_update_transaction_for_invocation (EosUpdater *updater,
					  GDBusMethodInvocation *invocation,
					  UpdateStep last_automatic_step)
{
  GError *error = NULL;
  EosUpdaterTransaction *transaction;

  if (updater->transaction == NULL)
    transaction = eos_updater_transaction_new (last_automatic_step, &error);

  if (error != NULL) {
      g_dbus_method_invocation_take_error (invocation, error);
      return;
  }

  if (updater->transaction == NULL) {
    updater->transaction = transaction;
    /* unref in the updater_transaction_completed() */
    eos_updater_transaction_run_async (updater->transaction, NULL,
				       updater_transaction_completed, g_object_ref (updater));
  } else {
    eos_updater_transaction_set_last_automatic_step (updater->transaction, last_automatic_step);
  }

  updater->pending_invocations = g_list_prepend (updater->pending_invocations, invocation);
}

static gboolean
handle_force_update (GDBusInterfaceSkeleton *skeleton,
                     GDBusMethodInvocation  *invocation,
                     gpointer                user_data)
{
  EosUpdater *updater = user_data;

  ensure_update_transaction_for_invocation (updater, invocation, UPDATE_STEP_APPLY);
  return TRUE;
}

static gboolean
handle_auto_updates_check (GDBusInterfaceSkeleton *skeleton,
			   GDBusMethodInvocation  *invocation,
			   gpointer                user_data)
{
  EosUpdater *updater = user_data;
  gint update_interval;
  gboolean update_on_mobile;
  gboolean up_to_date = FALSE;
  GError *error = NULL;
  UpdateStep last_automatic_step;

  if (!read_config_file (&update_interval, &update_on_mobile, &last_automatic_step))
    {
      g_set_error (&error, eos_updater_error_quark (),
		   EOS_UPDATER_ERROR_INVALID_CONFIGURATION,
		   "Invalid configuration detected");
      goto out;
    }

  if (!is_online ())
    {
      g_set_error (&error, eos_updater_error_quark (),
		   EOS_UPDATER_ERROR_NOT_ONLINE,
		   "Network is offline");
      goto out;
    }

  if (!update_on_mobile && is_connected_through_mobile ()) {
    sd_journal_send ("MESSAGE_ID=%s", EOS_UPDATER_MOBILE_CONNECTED_MSGID,
		     "PRIORITY=%d", LOG_INFO,
		     "MESSAGE=Connected to mobile network. Not updating",
		     NULL);
    g_set_error (&error, eos_updater_error_quark (),
		 EOS_UPDATER_ERROR_MOBILE_CONNECTED,
		 "Mobile connection detected, and policy prevents update");
    goto out;
  }

  up_to_date = is_time_to_update (update_interval);

 out:
  if (error != NULL)
    g_dbus_method_invocation_take_error (invocation, error);
  else if (up_to_date)
    /* If it's not time to update, just return */
    g_dbus_method_invocation_return_value (invocation, NULL);
  else
    ensure_update_transaction_for_invocation (updater, invocation, last_automatic_step);

  return TRUE;
}

static void
name_lost (GDBusConnection *connection,
	   const gchar *name,
	   gpointer user_data)
{
  EosUpdater *updater = user_data;
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (updater->skeleton));
}

static void
bus_acquired (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
  EosUpdater *updater = user_data;
  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (updater->skeleton),
                                    connection,
                                    "/com/endlessm/SystemUpdater",
                                    NULL);
}

static void
eos_updater_finalize (GObject *object)
{
  EosUpdater *updater = EOS_UPDATER (object);

  if (updater->skeleton != NULL)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (updater->skeleton));

  if (updater->bus_owner_id != 0) {
      g_bus_unown_name (updater->bus_owner_id);
      updater->bus_owner_id = 0;
  }

  g_clear_pointer (&updater->main_loop, g_main_loop_unref);
  g_clear_object (&updater->skeleton);

  if (updater->transaction_cancellable != NULL) {
    g_cancellable_cancel (updater->transaction_cancellable);
    g_clear_object (&updater->transaction_cancellable);
  }

  g_clear_object (&updater->transaction);
  g_clear_pointer (&updater->pending_invocations, g_list_free);

  G_OBJECT_CLASS (eos_updater_parent_class)->finalize (object);
}

static void
eos_updater_init (EosUpdater *updater)
{
  updater->skeleton = system_updater__skeleton_new ();

  g_signal_connect (updater->skeleton, "handle-auto-updates-check",
                    G_CALLBACK (handle_auto_updates_check), updater);
  g_signal_connect (updater->skeleton, "handle-force-update",
                    G_CALLBACK (handle_force_update), updater);

  updater->bus_owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
					  "com.endlessm.SystemUpdater",
					  G_BUS_NAME_OWNER_FLAGS_NONE,
					  bus_acquired,
					  NULL, /* name acquired */
					  name_lost, /* name lost */
					  updater, NULL);

  updater->main_loop = g_main_loop_new (NULL, FALSE);
}

static void
eos_updater_class_init (EosUpdaterClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = eos_updater_finalize;
}

static EosUpdater *
eos_updater_new (void)
{
  return g_object_new (eos_updater_get_type (), NULL);
}

/* ------------------------------------------------------------------ */

int
main (int argc, char **argv)
{
  EosUpdater *updater;

  updater = eos_updater_new ();
  g_main_loop_run (updater->main_loop);
  g_object_unref (updater);

  return EXIT_SUCCESS;
}
