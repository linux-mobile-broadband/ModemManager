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
#include <glib/gtypes.h>
#include <glib-object.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin.h"
#include "mm-modem.h"
#include "mm-port.h"

#define MM_PLUGIN_BASE_PORT_CAP_GSM         0x0001 /* GSM */
#define MM_PLUGIN_BASE_PORT_CAP_IS707_A     0x0002 /* CDMA Circuit Switched Data */
#define MM_PLUGIN_BASE_PORT_CAP_IS707_P     0x0004 /* CDMA Packet Switched Data */
#define MM_PLUGIN_BASE_PORT_CAP_DS          0x0008 /* Data compression selection (v.42bis) */
#define MM_PLUGIN_BASE_PORT_CAP_ES          0x0010 /* Error control selection (v.42) */
#define MM_PLUGIN_BASE_PORT_CAP_FCLASS      0x0020 /* Group III Fax */
#define MM_PLUGIN_BASE_PORT_CAP_MS          0x0040 /* Modulation selection */
#define MM_PLUGIN_BASE_PORT_CAP_W           0x0080 /* Wireless commands */
#define MM_PLUGIN_BASE_PORT_CAP_IS856       0x0100 /* CDMA 3G EVDO rev 0 */
#define MM_PLUGIN_BASE_PORT_CAP_IS856_A     0x0200 /* CDMA 3G EVDO rev A */
#define MM_PLUGIN_BASE_PORT_CAP_QCDM        0x0400 /* QCDM-capable port */

#define MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK            (mm_plugin_base_supports_task_get_type ())
#define MM_PLUGIN_BASE_SUPPORTS_TASK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK, MMPluginBaseSupportsTask))
#define MM_PLUGIN_BASE_SUPPORTS_TASK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK, MMPluginBaseSupportsTaskClass))
#define MM_IS_PLUGIN_BASE_SUPPORTS_TASK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK))
#define MM_IS_PLUBIN_BASE_SUPPORTS_TASK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK))
#define MM_PLUGIN_BASE_SUPPORTS_TASK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK, MMPluginBaseSupportsTaskClass))

typedef struct {
    GObject parent;
} MMPluginBaseSupportsTask;

typedef struct {
    GObjectClass parent;
} MMPluginBaseSupportsTaskClass;

GType mm_plugin_base_supports_task_get_type (void);

/* 
 * response: the response string from the modem, if no error occurred
 * error: the error returned by the modem or serial stack, if any
 * tries: number of times the custom init command has been sent to the modem
 * out_stop: on return, TRUE means stop the probe and close the port
 * out_level: on return, if out_stop is TRUE this should indicate the plugin's
 *            support level for this modem
 *
 * Function should return TRUE if the custom init command should be retried,
 * FALSE if it should not.  If FALSE is returned, generic probing will continue
 * if out_stop == FALSE.
 */
typedef gboolean (*MMBaseSupportsTaskCustomInitResultFunc) (MMPluginBaseSupportsTask *task,
                                                            GString *response,
                                                            GError *error,
                                                            guint32 tries,
                                                            gboolean *out_stop,
                                                            guint32 *out_level,
                                                            gpointer user_data);

MMPlugin *mm_plugin_base_supports_task_get_plugin (MMPluginBaseSupportsTask *task);

GUdevDevice *mm_plugin_base_supports_task_get_port (MMPluginBaseSupportsTask *task);

const char *mm_plugin_base_supports_task_get_physdev_path (MMPluginBaseSupportsTask *task);

const char *mm_plugin_base_supports_task_get_driver (MMPluginBaseSupportsTask *task);

guint32 mm_plugin_base_supports_task_get_probed_capabilities (MMPluginBaseSupportsTask *task);

void mm_plugin_base_supports_task_complete (MMPluginBaseSupportsTask *task,
                                            guint32 level);

void mm_plugin_base_supports_task_add_custom_init_command (MMPluginBaseSupportsTask *task,
                                                           const char *cmd,
                                                           guint32 delay_seconds,
                                                           MMBaseSupportsTaskCustomInitResultFunc callback,
                                                           gpointer callback_data);

#define MM_TYPE_PLUGIN_BASE            (mm_plugin_base_get_type ())
#define MM_PLUGIN_BASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_BASE, MMPluginBase))
#define MM_PLUGIN_BASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_BASE, MMPluginBaseClass))
#define MM_IS_PLUGIN_BASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_BASE))
#define MM_IS_PLUBIN_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_BASE))
#define MM_PLUGIN_BASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_BASE, MMPluginBaseClass))

#define MM_PLUGIN_BASE_NAME "name"

typedef struct _MMPluginBase MMPluginBase;
typedef struct _MMPluginBaseClass MMPluginBaseClass;

struct _MMPluginBase {
    GObject parent;
};

struct _MMPluginBaseClass {
    GObjectClass parent;

    /* Mandatory subclass functions */
    MMPluginSupportsResult (*supports_port) (MMPluginBase *plugin,
                                             MMModem *existing,
                                             MMPluginBaseSupportsTask *task);

    MMModem *(*grab_port)     (MMPluginBase *plugin,
                               MMModem *existing,
                               MMPluginBaseSupportsTask *task,
                               GError **error);

    /* Optional subclass functions */
    void (*cancel_task)       (MMPluginBase *plugin,
                               MMPluginBaseSupportsTask *task);

    /* Lets plugins read the probe response before the generic plugin processes it */
    void (*handle_probe_response) (MMPluginBase *plugin,
                                   MMPluginBaseSupportsTask *task,
                                   const char *command,
                                   const char *response,
                                   const GError *error);

    /* Signals */
    void (*probe_result) (MMPluginBase *self,
                          MMPluginBaseSupportsTask *task,
                          guint32 capabilities);
};

GType mm_plugin_base_get_type (void);

gboolean mm_plugin_base_get_device_ids (MMPluginBase *self,
                                        const char *subsys,
                                        const char *name,
                                        guint16 *vendor,
                                        guint16 *product);

gboolean mm_plugin_base_probe_port (MMPluginBase *self,
                                    MMPluginBaseSupportsTask *task,
                                    guint64 send_delay_us,
                                    GError **error);

/* Returns TRUE if the port was previously probed, FALSE if not */
gboolean mm_plugin_base_get_cached_port_capabilities (MMPluginBase *self,
                                                      GUdevDevice *port,
                                                      guint32 *capabilities);

#endif /* MM_PLUGIN_BASE_H */

