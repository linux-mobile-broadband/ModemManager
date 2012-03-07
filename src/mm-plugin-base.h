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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_PLUGIN_BASE_H
#define MM_PLUGIN_BASE_H

#include <glib.h>
#include <glib-object.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin.h"
#include "mm-base-modem.h"
#include "mm-port.h"
#include "mm-port-probe.h"

#define MM_TYPE_PLUGIN_BASE            (mm_plugin_base_get_type ())
#define MM_PLUGIN_BASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_BASE, MMPluginBase))
#define MM_PLUGIN_BASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_BASE, MMPluginBaseClass))
#define MM_IS_PLUGIN_BASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_BASE))
#define MM_IS_PLUBIN_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_BASE))
#define MM_PLUGIN_BASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_BASE, MMPluginBaseClass))

#define MM_PLUGIN_BASE_NAME                    "name"
#define MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS      "allowed-subsystems"
#define MM_PLUGIN_BASE_ALLOWED_DRIVERS         "allowed-drivers"
#define MM_PLUGIN_BASE_ALLOWED_VENDOR_IDS      "allowed-vendor-ids"
#define MM_PLUGIN_BASE_ALLOWED_PRODUCT_IDS     "allowed-product-ids"
#define MM_PLUGIN_BASE_ALLOWED_VENDOR_STRINGS  "allowed-vendor-strings"
#define MM_PLUGIN_BASE_ALLOWED_PRODUCT_STRINGS "allowed-product-strings"
#define MM_PLUGIN_BASE_ALLOWED_UDEV_TAGS       "allowed-udev-tags"
#define MM_PLUGIN_BASE_ALLOWED_AT              "allowed-at"
#define MM_PLUGIN_BASE_ALLOWED_SINGLE_AT       "allowed-single-at"
#define MM_PLUGIN_BASE_ALLOWED_QCDM            "allowed-qcdm"
#define MM_PLUGIN_BASE_CUSTOM_INIT             "custom-init"
#define MM_PLUGIN_BASE_SEND_DELAY              "send-delay"
#define MM_PLUGIN_BASE_SORT_LAST               "sort-last"

typedef struct _MMPluginBase MMPluginBase;
typedef struct _MMPluginBaseClass MMPluginBaseClass;

struct _MMPluginBase {
    GObject parent;
};

struct _MMPluginBaseClass {
    GObjectClass parent;

    /* Mandatory subclass functions */
    MMBaseModem *(*grab_port) (MMPluginBase *plugin,
                               MMBaseModem *existing,
                               MMPortProbe *probe,
                               GError **error);
};

GType mm_plugin_base_get_type (void);

gboolean mm_plugin_base_get_device_ids (MMPluginBase *self,
                                        const char *subsys,
                                        const char *name,
                                        guint16 *vendor,
                                        guint16 *product);

#endif /* MM_PLUGIN_BASE_H */
