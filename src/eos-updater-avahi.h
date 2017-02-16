/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2016 Kinvolk GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#pragma once

#include <libeos-updater-util/refcounted.h>

#include <ostree.h>

#include <glib.h>

G_BEGIN_DECLS

#define EOS_TYPE_AVAHI_SERVICE eos_avahi_service_get_type ()
EOS_DECLARE_REFCOUNTED (EosAvahiService, eos_avahi_service, EOS, AVAHI_SERVICE)

struct _EosAvahiService
{
  GObject parent_instance;

  gchar *name;
  gchar *domain;
  gchar *address;
  guint16 port;
  gchar **txt;
};

#define EOS_TYPE_AVAHI_DISCOVERER eos_avahi_discoverer_get_type ()
EOS_DECLARE_REFCOUNTED (EosAvahiDiscoverer, eos_avahi_discoverer, EOS, AVAHI_DISCOVERER)

typedef void
(*EosAvahiDiscovererCallback) (EosAvahiDiscoverer *discoverer,
                               GPtrArray *found_services,
                               gpointer user_data,
                               GError *error);

EosAvahiDiscoverer *
eos_avahi_discoverer_new (GMainContext *context,
                          EosAvahiDiscovererCallback callback,
                          gpointer user_data,
                          GDestroyNotify notify,
                          GError **error);

G_END_DECLS
