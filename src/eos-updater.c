#include <gio/gio.h>
#include <stdio.h>

#include "ostree-daemon-generated.h"

static void
on_state_changed (OTD *proxy, guint state)
{
  g_print ("State changed to: %u\n", state);
}

int
main (int argc, char **argv)
{
  OTD *proxy;
  GMainLoop *loop;
  GError *error = NULL;

  loop = g_main_loop_new (NULL, FALSE);

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

  g_signal_connect (proxy, "StateChanged", G_CALLBACK (on_state_changed), NULL);
  g_main_loop_run (loop);

  return 0;
}