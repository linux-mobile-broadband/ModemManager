/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_PLUGIN_H
#define MM_PLUGIN_H

#include <glib.h>
#include <glib-object.h>
#include <gudev/gudev.h>

#include "mm-base-modem.h"
#include "mm-port.h"
#include "mm-port-probe.h"
#include "mm-device.h"

#define MM_PLUGIN_GENERIC_NAME "Generic"
#define MM_PLUGIN_MAJOR_VERSION 4
#define MM_PLUGIN_MINOR_VERSION 0

#define MM_TYPE_PLUGIN            (mm_plugin_get_type ())
#define MM_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN, MMPlugin))
#define MM_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN, MMPluginClass))
#define MM_IS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN))
#define MM_IS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN))
#define MM_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN, MMPluginClass))

#define MM_PLUGIN_NAME                      "name"
#define MM_PLUGIN_ALLOWED_SUBSYSTEMS        "allowed-subsystems"
#define MM_PLUGIN_ALLOWED_DRIVERS           "allowed-drivers"
#define MM_PLUGIN_FORBIDDEN_DRIVERS         "forbidden-drivers"
#define MM_PLUGIN_ALLOWED_VENDOR_IDS        "allowed-vendor-ids"
#define MM_PLUGIN_ALLOWED_PRODUCT_IDS       "allowed-product-ids"
#define MM_PLUGIN_FORBIDDEN_PRODUCT_IDS     "forbidden-product-ids"
#define MM_PLUGIN_ALLOWED_VENDOR_STRINGS    "allowed-vendor-strings"
#define MM_PLUGIN_ALLOWED_PRODUCT_STRINGS   "allowed-product-strings"
#define MM_PLUGIN_FORBIDDEN_PRODUCT_STRINGS "forbidden-product-strings"
#define MM_PLUGIN_ALLOWED_UDEV_TAGS         "allowed-udev-tags"
#define MM_PLUGIN_ALLOWED_AT                "allowed-at"
#define MM_PLUGIN_ALLOWED_SINGLE_AT         "allowed-single-at"
#define MM_PLUGIN_ALLOWED_QCDM              "allowed-qcdm"
#define MM_PLUGIN_ICERA_PROBE               "icera-probe"
#define MM_PLUGIN_ALLOWED_ICERA             "allowed-icera"
#define MM_PLUGIN_FORBIDDEN_ICERA           "forbidden-icera"
#define MM_PLUGIN_CUSTOM_INIT               "custom-init"
#define MM_PLUGIN_CUSTOM_AT_PROBE           "custom-at-probe"
#define MM_PLUGIN_SEND_DELAY                "send-delay"
#define MM_PLUGIN_REMOVE_ECHO               "remove-echo"

typedef enum {
    MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED = 0x0,
    MM_PLUGIN_SUPPORTS_PORT_DEFER,
    MM_PLUGIN_SUPPORTS_PORT_DEFER_UNTIL_SUGGESTED,
    MM_PLUGIN_SUPPORTS_PORT_SUPPORTED
} MMPluginSupportsResult;

typedef enum {
    MM_PLUGIN_SUPPORTS_HINT_UNSUPPORTED,
    MM_PLUGIN_SUPPORTS_HINT_MAYBE,
    MM_PLUGIN_SUPPORTS_HINT_LIKELY,
    MM_PLUGIN_SUPPORTS_HINT_SUPPORTED,
} MMPluginSupportsHint;

typedef struct _MMPlugin MMPlugin;
typedef struct _MMPluginClass MMPluginClass;
typedef struct _MMPluginPrivate MMPluginPrivate;

typedef MMPlugin *(*MMPluginCreateFunc) (void);

struct _MMPlugin {
    GObject parent;
    MMPluginPrivate *priv;
};

struct _MMPluginClass {
    GObjectClass parent;

    /* Plugins need to provide a method to create a modem object given
     * a list of port probes (Mandatory) */
    MMBaseModem *(*create_modem) (MMPlugin *plugin,
                                  const gchar *sysfs_path,
                                  const gchar **drivers,
                                  guint16 vendor,
                                  guint16 product,
                                  GList *probes,
                                  GError **error);

    /* Plugins need to provide a method to grab independent ports
     * identified by port probes (Optional) */
    gboolean (*grab_port) (MMPlugin *plugin,
                           MMBaseModem *modem,
                           MMPortProbe *probe,
                           GError **error);
};

GType mm_plugin_get_type (void);

const gchar *mm_plugin_get_name (MMPlugin *plugin);

/* This method will run all pre-probing filters, to see if we can discard this
 * plugin from the probing logic as soon as possible. */
MMPluginSupportsHint mm_plugin_discard_port_early (MMPlugin *plugin,
                                                   MMDevice *device,
                                                   GUdevDevice *port);

void                   mm_plugin_supports_port        (MMPlugin *plugin,
                                                       MMDevice *device,
                                                       GUdevDevice *port,
                                                       GAsyncReadyCallback callback,
                                                       gpointer user_data);
MMPluginSupportsResult mm_plugin_supports_port_finish (MMPlugin *plugin,
                                                       GAsyncResult *result,
                                                       GError **error);

MMBaseModem *mm_plugin_create_modem (MMPlugin *plugin,
                                     MMDevice *device,
                                     GError **error);

#endif /* MM_PLUGIN_H */
