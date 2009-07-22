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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_PLUGIN_H
#define MM_PLUGIN_H

#include <glib-object.h>
#include <mm-modem.h>

#define MM_PLUGIN_GENERIC_NAME "Generic"

#define MM_PLUGIN_MAJOR_VERSION 3
#define MM_PLUGIN_MINOR_VERSION 0

#define MM_TYPE_PLUGIN      (mm_plugin_get_type ())
#define MM_PLUGIN(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN, MMPlugin))
#define MM_IS_PLUGIN(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN))
#define MM_PLUGIN_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_PLUGIN, MMPlugin))

typedef struct _MMPlugin MMPlugin;

typedef MMPlugin *(*MMPluginCreateFunc) (void);

/*
 * 'level' is a value between 0 and 100 inclusive, where 0 means the plugin has
 * no support for the port, and 100 means the plugin has full support for the
 * port.
 */
typedef void (*MMSupportsPortResultFunc) (MMPlugin *plugin,
                                          const char *subsys,
                                          const char *name,
                                          guint32 level,
                                          gpointer user_data);

typedef enum {
    MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED = 0x0,
    MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS,
    MM_PLUGIN_SUPPORTS_PORT_DEFER
} MMPluginSupportsResult;

struct _MMPlugin {
    GTypeInterface g_iface;

    /* Methods */
    const char *(*get_name)   (MMPlugin *self);

    /* Check whether a plugin supports a particular modem port, and what level
     * of support the plugin has for the device.  If the plugin can immediately
     * determine whether a port is unsupported, it should return
     * FALSE.  Otherwise, if the plugin needs to perform additional operations
     * (ie, probing) to determine the level of support or additional details
     * about a port, it should queue that operation (but *not* block on the
     * result) and return TRUE to indicate the operation is ongoing.  When the
     * operation is finished or the level of support is known, the plugin should
     * call the provided callback and pass that callback the provided user_data.
     */
    MMPluginSupportsResult (*supports_port)  (MMPlugin *self,
                                              const char *subsys,
                                              const char *name,
                                              MMSupportsPortResultFunc callback,
                                              gpointer user_data);

    /* Called to cancel an ongoing supports_port() operation or to notify the
     * plugin to clean up that operation.  For example, if two plugins support
     * a particular port, but the first plugin grabs the port, this method will
     * be called on the second plugin to allow that plugin to clean up resources
     * used while determining it's level of support for the port.
     */
    void (*cancel_supports_port) (MMPlugin *self,
                                  const char *subsys,
                                  const char *name);

    /* Will only be called if the plugin returns a value greater than 0 for
     * the supports_port() method for this port.  The plugin should create and
     * return a  new modem for the port's device if there is no existing modem
     * to handle the port's hardware device, or should add the port to an
     * existing modem and return that modem object.  If an error is encountered
     * while claiming the port, the error information should be returned in the
     * error argument, and the plugin should return NULL.
     */
    MMModem * (*grab_port)    (MMPlugin *self,
                               const char *subsys,
                               const char *name,
                               GError **error);
};

GType mm_plugin_get_type (void);

const char *mm_plugin_get_name   (MMPlugin *plugin);

MMPluginSupportsResult mm_plugin_supports_port  (MMPlugin *plugin,
                                                 const char *subsys,
                                                 const char *name,
                                                 MMSupportsPortResultFunc callback,
                                                 gpointer user_data);

void mm_plugin_cancel_supports_port (MMPlugin *plugin,
                                     const char *subsys,
                                     const char *name);

MMModem *mm_plugin_grab_port     (MMPlugin *plugin,
                                  const char *subsys,
                                  const char *name,
                                  GError **error);

#endif /* MM_PLUGIN_H */

