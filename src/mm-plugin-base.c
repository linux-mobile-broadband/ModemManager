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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#define _GNU_SOURCE  /* for strcasestr */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin-base.h"
#include "mm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-errors.h"
#include "mm-marshal.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginBase, mm_plugin_base, G_TYPE_OBJECT,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

#define MM_PLUGIN_BASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_BASE, MMPluginBasePrivate))

/* A hash table shared between all instances of the plugin base that
 * caches the probed capabilities so that only one plugin has to actually
 * probe a port.
 */
static GHashTable *cached_caps = NULL;


typedef struct {
    char *name;
    GUdevClient *client;

    GHashTable *modems;
    GHashTable *tasks;
} MMPluginBasePrivate;

enum {
    PROP_0,
    PROP_NAME,
    LAST_PROP
};

enum {
    PROBE_RESULT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


typedef enum {
    PROBE_STATE_GCAP_TRY1 = 0,
    PROBE_STATE_GCAP_TRY2,
    PROBE_STATE_GCAP_TRY3,
    PROBE_STATE_ATI,
    PROBE_STATE_CPIN,
    PROBE_STATE_CGMM,
    PROBE_STATE_LAST
} ProbeState;

/*****************************************************************************/

G_DEFINE_TYPE (MMPluginBaseSupportsTask, mm_plugin_base_supports_task, G_TYPE_OBJECT)

#define MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK, MMPluginBaseSupportsTaskPrivate))

typedef struct {
    MMPluginBase *plugin;
    GUdevDevice *port;
    GUdevDevice *physdev;
    char *driver;

    guint open_id;
    guint32 open_tries;

    MMSerialPort *probe_port;
    guint32 probed_caps;
    ProbeState probe_state;
    guint probe_id;
    char *probe_resp;
    GError *probe_error;

    char *custom_init;
    guint32 custom_init_max_tries;
    guint32 custom_init_tries;
    guint32 custom_init_delay_seconds;
    gboolean custom_init_fail_if_timeout;

    MMSupportsPortResultFunc callback;
    gpointer callback_data;
}  MMPluginBaseSupportsTaskPrivate;

static MMPluginBaseSupportsTask *
supports_task_new (MMPluginBase *self,
                   GUdevDevice *port,
                   GUdevDevice *physdev,
                   const char *driver,
                   MMSupportsPortResultFunc callback,
                   gpointer callback_data)
{
    MMPluginBaseSupportsTask *task;
    MMPluginBaseSupportsTaskPrivate *priv;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), NULL);
    g_return_val_if_fail (port != NULL, NULL);
    g_return_val_if_fail (physdev != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    task = (MMPluginBaseSupportsTask *) g_object_new (MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK, NULL);

    priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    priv->plugin = self;
    priv->port = g_object_ref (port);
    priv->physdev = g_object_ref (physdev);
    priv->driver = g_strdup (driver);
    priv->callback = callback;
    priv->callback_data = callback_data;

    return task;
}

MMPlugin *
mm_plugin_base_supports_task_get_plugin (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), NULL);

    return MM_PLUGIN (MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->plugin);
}

GUdevDevice *
mm_plugin_base_supports_task_get_port (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), NULL);

    return MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->port;
}

GUdevDevice *
mm_plugin_base_supports_task_get_physdev (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), NULL);

    return MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->physdev;
}

const char *
mm_plugin_base_supports_task_get_driver (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), NULL);

    return MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->driver;
}

guint32
mm_plugin_base_supports_task_get_probed_capabilities (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, 0);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), 0);

    return MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->probed_caps;
}

void
mm_plugin_base_supports_task_complete (MMPluginBaseSupportsTask *task,
                                       guint32 level)
{
    MMPluginBaseSupportsTaskPrivate *priv;
    const char *subsys, *name;

    g_return_if_fail (task != NULL);
    g_return_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task));

    priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    g_return_if_fail (priv->callback != NULL);

    subsys = g_udev_device_get_subsystem (priv->port);
    name = g_udev_device_get_name (priv->port);

    priv->callback (MM_PLUGIN (priv->plugin), subsys, name, level, priv->callback_data);

    /* Clear out the callback, it shouldn't be called more than once */
    priv->callback = NULL;
    priv->callback_data = NULL;
}

void
mm_plugin_base_supports_task_set_custom_init_command (MMPluginBaseSupportsTask *task,
                                                      const char *cmd,
                                                      guint32 delay_seconds,
                                                      guint32 max_tries,
                                                      gboolean fail_if_timeout)
{
    MMPluginBaseSupportsTaskPrivate *priv;

    g_return_if_fail (task != NULL);
    g_return_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task));

    priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    g_free (priv->custom_init);
    priv->custom_init = g_strdup (cmd);
    priv->custom_init_max_tries = max_tries;
    priv->custom_init_delay_seconds = delay_seconds;
    priv->custom_init_fail_if_timeout = fail_if_timeout;
}

static void
mm_plugin_base_supports_task_init (MMPluginBaseSupportsTask *self)
{
}

static void
supports_task_dispose (GObject *object)
{
    MMPluginBaseSupportsTaskPrivate *priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (object);

    if (MM_IS_SERIAL_PORT (priv->port))
        mm_serial_port_flash_cancel (MM_SERIAL_PORT (priv->port));

    g_object_unref (priv->port);
    g_object_unref (priv->physdev);
    g_free (priv->driver);
    g_free (priv->probe_resp);
    g_clear_error (&(priv->probe_error));
    g_free (priv->custom_init);

    if (priv->open_id)
        g_source_remove (priv->open_id);

    if (priv->probe_id)
        g_source_remove (priv->probe_id);
    if (priv->probe_port)
        g_object_unref (priv->probe_port);

    G_OBJECT_CLASS (mm_plugin_base_supports_task_parent_class)->dispose (object);
}

static void
mm_plugin_base_supports_task_class_init (MMPluginBaseSupportsTaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginBaseSupportsTaskPrivate));

    /* Virtual methods */
    object_class->dispose = supports_task_dispose;
}

/*****************************************************************************/

#define MM_PLUGIN_BASE_PORT_CAP_CDMA (MM_PLUGIN_BASE_PORT_CAP_IS707_A | \
                                      MM_PLUGIN_BASE_PORT_CAP_IS707_P | \
                                      MM_PLUGIN_BASE_PORT_CAP_IS856 | \
                                      MM_PLUGIN_BASE_PORT_CAP_IS856_A)

#define CAP_GSM_OR_CDMA (MM_PLUGIN_BASE_PORT_CAP_CDMA | MM_PLUGIN_BASE_PORT_CAP_GSM)

struct modem_caps {
	char *name;
	guint32 bits;
};

static struct modem_caps modem_caps[] = {
	{"+CGSM",     MM_PLUGIN_BASE_PORT_CAP_GSM},
	{"+CIS707-A", MM_PLUGIN_BASE_PORT_CAP_IS707_A},
	{"+CIS707A",  MM_PLUGIN_BASE_PORT_CAP_IS707_A}, /* Cmotech */
	{"+CIS707",   MM_PLUGIN_BASE_PORT_CAP_IS707_A},
	{"CIS707",    MM_PLUGIN_BASE_PORT_CAP_IS707_A}, /* Qualcomm Gobi */
	{"+CIS707P",  MM_PLUGIN_BASE_PORT_CAP_IS707_P},
	{"CIS-856",   MM_PLUGIN_BASE_PORT_CAP_IS856},
	{"+IS-856",   MM_PLUGIN_BASE_PORT_CAP_IS856},   /* Cmotech */
	{"CIS-856-A", MM_PLUGIN_BASE_PORT_CAP_IS856_A},
	{"CIS-856A",  MM_PLUGIN_BASE_PORT_CAP_IS856_A}, /* Kyocera KPC680 */
	{"+DS",       MM_PLUGIN_BASE_PORT_CAP_DS},
	{"+ES",       MM_PLUGIN_BASE_PORT_CAP_ES},
	{"+MS",       MM_PLUGIN_BASE_PORT_CAP_MS},
	{"+FCLASS",   MM_PLUGIN_BASE_PORT_CAP_FCLASS},
	{NULL}
};

static guint32
parse_gcap (const char *buf)
{
    struct modem_caps *cap = modem_caps;
    guint32 ret = 0;

    while (cap->name) {
        if (strstr (buf, cap->name))
            ret |= cap->bits;
        cap++;
    }
    return ret;
}

static guint32
parse_cpin (const char *buf)
{
    if (   strcasestr (buf, "SIM PIN")
        || strcasestr (buf, "SIM PUK")
        || strcasestr (buf, "PH-SIM PIN")
        || strcasestr (buf, "PH-FSIM PIN")
        || strcasestr (buf, "PH-FSIM PUK")
        || strcasestr (buf, "SIM PIN2")
        || strcasestr (buf, "SIM PUK2")
        || strcasestr (buf, "PH-NET PIN")
        || strcasestr (buf, "PH-NET PUK")
        || strcasestr (buf, "PH-NETSUB PIN")
        || strcasestr (buf, "PH-NETSUB PUK")
        || strcasestr (buf, "PH-SP PIN")
        || strcasestr (buf, "PH-SP PUK")
        || strcasestr (buf, "PH-CORP PIN")
        || strcasestr (buf, "PH-CORP PUK"))
        return MM_PLUGIN_BASE_PORT_CAP_GSM;

    return 0;
}

static guint32
parse_cgmm (const char *buf)
{
    if (strstr (buf, "GSM900") || strstr (buf, "GSM1800") ||
        strstr (buf, "GSM1900") || strstr (buf, "GSM850"))
        return MM_PLUGIN_BASE_PORT_CAP_GSM;
    return 0;
}

static gboolean
emit_probe_result (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMPlugin *self = mm_plugin_base_supports_task_get_plugin (task);

    /* Close the serial port */
    g_object_unref (task_priv->probe_port);
    task_priv->probe_port = NULL;

    task_priv->probe_id = 0;
    g_signal_emit (self, signals[PROBE_RESULT], 0, task, task_priv->probed_caps);
    return FALSE;
}

static void
probe_complete (MMPluginBaseSupportsTask *task)
{
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    g_hash_table_insert (cached_caps,
                         g_strdup (mm_port_get_device (MM_PORT (task_priv->probe_port))),
                         GUINT_TO_POINTER (task_priv->probed_caps));

    task_priv->probe_id = g_idle_add (emit_probe_result, task);
}

static void
parse_response (MMSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data);

static void
real_handle_probe_response (MMPluginBase *self,
                            MMPluginBaseSupportsTask *task,
                            const char *cmd,
                            const char *response,
                            const GError *error)
{
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMSerialPort *port = task_priv->probe_port;
    gboolean ignore_error = FALSE;

    /* Some modems (Huawei E160g) won't respond to +GCAP with no SIM, but
     * will respond to ATI.
     */
    if (response && strstr (response, "+CME ERROR:"))
        ignore_error = TRUE;

    if (error && !ignore_error) {
        if (error->code == MM_SERIAL_RESPONSE_TIMEOUT) {
            /* Try GCAP again */
            if (task_priv->probe_state < PROBE_STATE_GCAP_TRY3) {
                task_priv->probe_state++;
                mm_serial_port_queue_command (port, "+GCAP", 3, parse_response, task);
            } else {
               /* Otherwise, if all the GCAP tries timed out, ignore the port
                * as it's probably not an AT-capable port.
                */
               probe_complete (task);
            }
            return;
        }

        /* Otherwise proceed to the next command */
    } else if (response) {
        /* Parse the response */

        switch (task_priv->probe_state) {
        case PROBE_STATE_GCAP_TRY1:
        case PROBE_STATE_GCAP_TRY2:
        case PROBE_STATE_GCAP_TRY3:
        case PROBE_STATE_ATI:
            /* Some modems don't respond to AT+GCAP, but often they put a
             * GCAP-style response as a line in the ATI response.
             */
            task_priv->probed_caps = parse_gcap (response);
            break;
        case PROBE_STATE_CPIN:
            /* Some devices (ZTE MF628/ONDA MT503HS for example) reply to
             * anything but AT+CPIN? with ERROR if the device has a PIN set.
             * Since no known CDMA modems support AT+CPIN? we can consider the
             * device a GSM device if it returns a non-error response to AT+CPIN?.
             */
            task_priv->probed_caps = parse_cpin (response);
            break;
        case PROBE_STATE_CGMM:
            /* Some models (BUSlink SCWi275u) stick stupid stuff in the CGMM
             * response but at least it allows us to identify them.
             */
            task_priv->probed_caps = parse_cgmm (response);
            break;
        default:
            break;
        }

        if (task_priv->probed_caps & CAP_GSM_OR_CDMA) {
            probe_complete (task);
            return;
        }
    }

    task_priv->probe_state++;

    /* Try a different command */
    switch (task_priv->probe_state) {
    case PROBE_STATE_GCAP_TRY2:
    case PROBE_STATE_GCAP_TRY3:
        mm_serial_port_queue_command (port, "+GCAP", 3, parse_response, task);
        break;
    case PROBE_STATE_ATI:
        /* After the last GCAP attempt, try ATI */
        mm_serial_port_queue_command (port, "I", 3, parse_response, task);
        break;
    case PROBE_STATE_CPIN:
        /* After the ATI attempt, try CPIN */
        mm_serial_port_queue_command (port, "+CPIN?", 3, parse_response, task);
        break;
    case PROBE_STATE_CGMM:
        /* After the CPIN attempt, try CGMM */
        mm_serial_port_queue_command (port, "+CGMM", 3, parse_response, task);
        break;
    default:
        /* Probably not GSM or CDMA */
        probe_complete (task);
        break;
    }
}

static gboolean
handle_probe_response (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMPluginBase *self = MM_PLUGIN_BASE (mm_plugin_base_supports_task_get_plugin (task));
    const char *cmd = NULL;

    switch (task_priv->probe_state) {
    case PROBE_STATE_GCAP_TRY1:
    case PROBE_STATE_GCAP_TRY2:
    case PROBE_STATE_GCAP_TRY3:
        cmd = "+GCAP";
        break;
    case PROBE_STATE_ATI:
        cmd = "I";
        break;
    case PROBE_STATE_CPIN:
        cmd = "+CPIN?";
        break;
    case PROBE_STATE_CGMM:
    default:
        cmd = "+CGMM";
        break;
    }

    MM_PLUGIN_BASE_GET_CLASS (self)->handle_probe_response (self,
                                                            task,
                                                            cmd,
                                                            task_priv->probe_resp,
                                                            task_priv->probe_error);
    return FALSE;
}

static void
parse_response (MMSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    if (task_priv->probe_id)
        g_source_remove (task_priv->probe_id);
    g_free (task_priv->probe_resp);
    task_priv->probe_resp = NULL;
    g_clear_error (&(task_priv->probe_error));

    if (response && response->len)
        task_priv->probe_resp = g_strdup (response->str);
    if (error)
        task_priv->probe_error = g_error_copy (error);

    /* Schedule the response handler in an idle, since we can't emit the
     * PROBE_RESULT signal from the serial port response handler without
     * potentially destroying the serial port in the middle of its response
     * handler, which it understandably doesn't like.
     */
    task_priv->probe_id = g_idle_add (handle_probe_response, task);
}

static void flash_done (MMSerialPort *port, GError *error, gpointer user_data);

static void
custom_init_response (MMSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    if (error) {
        task_priv->custom_init_tries++;
        if (task_priv->custom_init_tries < task_priv->custom_init_max_tries) {
            /* Try the custom command again */
            flash_done (port, NULL, user_data);
            return;
        } else if (task_priv->custom_init_fail_if_timeout) {
            /* Fail the probe if the plugin wanted it and the command timed out */
            if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_RESPONSE_TIMEOUT)) {
                probe_complete (task);
                return;
            }
        }
    }

    /* Otherwise proceed to probing */
    mm_serial_port_queue_command (port, "+GCAP", 3, parse_response, user_data);
}

static void
flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    guint32 delay_secs = task_priv->custom_init_delay_seconds;

    /* Send the custom init command if any */
    if (task_priv->custom_init) {
        if (!delay_secs)
            delay_secs = 3;
        mm_serial_port_queue_command (port,
                                      task_priv->custom_init,
                                      delay_secs,
                                      custom_init_response,
                                      user_data);
    } else {
        /* Otherwise start normal probing */
        custom_init_response (port, NULL, NULL, user_data);
    }
}

static gboolean
try_open (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    GError *error = NULL;

    task_priv->open_id = 0;

    if (!mm_serial_port_open (task_priv->probe_port, &error)) {
        if (++task_priv->open_tries > 4) {
            /* took too long to open the port; give up */
            g_warning ("(%s): failed to open after 4 tries.",
                       mm_port_get_device (MM_PORT (task_priv->probe_port)));
            probe_complete (task);
        } else if (g_error_matches (error,
                                    MM_SERIAL_ERROR,
                                    MM_SERIAL_OPEN_FAILED_NO_DEVICE)) {
            /* this is nozomi being dumb; try again */
            task_priv->open_id = g_timeout_add_seconds (1, try_open, task);
        } else {
            /* some other hard error */
            probe_complete (task);
        }
        g_clear_error (&error);
    } else {
        /* success, start probing */
        GUdevDevice *port;

        port = mm_plugin_base_supports_task_get_port (task);
        g_assert (port);

        g_debug ("(%s): probe requested by plugin '%s'",
                 g_udev_device_get_name (port),
                 mm_plugin_get_name (MM_PLUGIN (task_priv->plugin)));
        mm_serial_port_flash (task_priv->probe_port, 100, flash_done, task);
    }

    return FALSE;
}

gboolean
mm_plugin_base_probe_port (MMPluginBase *self,
                           MMPluginBaseSupportsTask *task,
                           GError **error)
{
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMSerialPort *serial;
    const char *name;
    GUdevDevice *port;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), FALSE);
    g_return_val_if_fail (task != NULL, FALSE);

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);
    name = g_udev_device_get_name (port);
    g_assert (name);

    serial = mm_serial_port_new (name, MM_PORT_TYPE_PRIMARY);
    if (serial == NULL) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Failed to create new serial port.");
        return FALSE;
    }

    g_object_set (serial,
                  MM_SERIAL_PORT_SEND_DELAY, (guint64) 100000,
                  MM_PORT_CARRIER_DETECT, FALSE,
                  NULL);

    mm_serial_port_set_response_parser (serial,
                                        mm_serial_parser_v1_parse,
                                        mm_serial_parser_v1_new (),
                                        mm_serial_parser_v1_destroy);

    /* Open the port */
    task_priv->probe_port = serial;
    task_priv->open_id = g_idle_add (try_open, task);
    return TRUE;
}

gboolean
mm_plugin_base_get_cached_port_capabilities (MMPluginBase *self,
                                             GUdevDevice *port,
                                             guint32 *capabilities)
{
    gpointer tmp = NULL;
    gboolean found;

    found = g_hash_table_lookup_extended (cached_caps, g_udev_device_get_name (port), NULL, tmp);
    *capabilities = GPOINTER_TO_UINT (tmp);
    return found;
}

/*****************************************************************************/

static void
modem_destroyed (gpointer data, GObject *modem)
{
    MMPluginBase *self = MM_PLUGIN_BASE (data);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    GHashTableIter iter;
    gpointer key, value;

    /* Remove it from the modems info */
    g_hash_table_iter_init (&iter, priv->modems);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        if (value == modem) {
            g_hash_table_iter_remove (&iter);
            break;
        }
    }

    /* Since we don't track port cached capabilities on a per-modem basis,
     * we just have to live with blowing away the cached capabilities whenever
     * a modem gets removed.  Could do better here by storing a structure
     * in the cached capabilities table that includes { caps, modem device }
     * or something and then only removing cached capabilities for ports
     * that the modem that was just removed owned, but whatever.
     */
    g_hash_table_remove_all (cached_caps);
}

MMModem *
mm_plugin_base_find_modem (MMPluginBase *self,
                           const char *master_device)
{
    MMPluginBasePrivate *priv;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), NULL);
    g_return_val_if_fail (master_device != NULL, NULL);
    g_return_val_if_fail (strlen (master_device) > 0, NULL);

    priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    return g_hash_table_lookup (priv->modems, master_device);
}

/* From hostap, Copyright (c) 2002-2005, Jouni Malinen <jkmaline@cc.hut.fi> */

static int hex2num (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex2byte (const char *hex)
{
	int a, b;
	a = hex2num(*hex++);
	if (a < 0)
		return -1;
	b = hex2num(*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}

/* End from hostap */

gboolean
mm_plugin_base_get_device_ids (MMPluginBase *self,
                               const char *subsys,
                               const char *name,
                               guint16 *vendor,
                               guint16 *product)
{
    MMPluginBasePrivate *priv;
    GUdevDevice *device = NULL;
    const char *vid, *pid;
    gboolean success = FALSE;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), FALSE);
    g_return_val_if_fail (subsys != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    if (vendor)
        g_return_val_if_fail (*vendor == 0, FALSE);
    if (product)
        g_return_val_if_fail (*product == 0, FALSE);

    priv = MM_PLUGIN_BASE_GET_PRIVATE (self);

    device = g_udev_client_query_by_subsystem_and_name (priv->client, subsys, name);
    if (!device)
        goto out;

    vid = g_udev_device_get_property (device, "ID_VENDOR_ID");
    if (!vid || (strlen (vid) != 4))
        goto out;

    if (vendor) {
        *vendor = (guint16) (hex2byte (vid + 2) & 0xFF);
        *vendor |= (guint16) ((hex2byte (vid) & 0xFF) << 8);
    }

    pid = g_udev_device_get_property (device, "ID_MODEL_ID");
    if (!pid || (strlen (pid) != 4)) {
        *vendor = 0;
        goto out;
    }

    if (product) {
        *product = (guint16) (hex2byte (pid + 2) & 0xFF);
        *product |= (guint16) ((hex2byte (pid) & 0xFF) << 8);
    }

    success = TRUE;

out:
    if (device)
        g_object_unref (device);
    return success;
}

static char *
get_key (const char *subsys, const char *name)
{
    return g_strdup_printf ("%s%s", subsys, name);
}

static const char *
get_name (MMPlugin *plugin)
{
    return MM_PLUGIN_BASE_GET_PRIVATE (plugin)->name;
}

static char *
get_driver_name (GUdevDevice *device)
{
    GUdevDevice *parent = NULL;
    const char *driver, *subsys;
    char *ret = NULL;

    driver = g_udev_device_get_driver (device);
    if (!driver) {
        parent = g_udev_device_get_parent (device);
        if (parent)
            driver = g_udev_device_get_driver (parent);

        /* Check for bluetooth; it's driver is a bunch of levels up so we
         * just check for the subsystem of the parent being bluetooth.
         */
        if (!driver && parent) {
            subsys = g_udev_device_get_subsystem (parent);
            if (subsys && !strcmp (subsys, "bluetooth"))
                driver = "bluetooth";
        }
    }

    if (driver)
        ret = g_strdup (driver);
    if (parent)
        g_object_unref (parent);

    return ret;
}

static GUdevDevice *
real_find_physical_device (MMPluginBase *plugin, GUdevDevice *child)
{
    GUdevDevice *iter, *old = NULL;
    GUdevDevice *physdev = NULL;
    const char *subsys, *type;
    guint32 i = 0;
    gboolean is_usb = FALSE, is_pci = FALSE;

    g_return_val_if_fail (child != NULL, NULL);

    iter = g_object_ref (child);
    while (iter && i++ < 8) {
        subsys = g_udev_device_get_subsystem (iter);
        if (subsys) {
            if (is_usb || !strcmp (subsys, "usb")) {
                is_usb = TRUE;
                type = g_udev_device_get_devtype (iter);
                if (type && !strcmp (type, "usb_device")) {
                    physdev = iter;
                    break;
                }
            } else if (is_pci || !strcmp (subsys, "pci")) {
                is_pci = TRUE;
                physdev = iter;
                break;
            }
        }

        old = iter;
        iter = g_udev_device_get_parent (old);
        g_object_unref (old);
    }

    return physdev;
}

static MMPluginSupportsResult
supports_port (MMPlugin *plugin,
               const char *subsys,
               const char *name,
               MMSupportsPortResultFunc callback,
               gpointer callback_data)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    GUdevDevice *port = NULL, *physdev = NULL;
    char *driver = NULL, *key = NULL;
    MMPluginBaseSupportsTask *task;
    MMPluginSupportsResult result = MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    MMModem *existing;
    const char *master_path;

    key = get_key (subsys, name);
    task = g_hash_table_lookup (priv->tasks, key);
    if (task) {
        g_free (key);
        g_return_val_if_fail (task == NULL, MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);
    }

    port = g_udev_client_query_by_subsystem_and_name (priv->client, subsys, name);
    if (!port)
        goto out;

    physdev = MM_PLUGIN_BASE_GET_CLASS (self)->find_physical_device (self, port);
    if (!physdev)
        goto out;

    driver = get_driver_name (port);
    if (!driver)
        goto out;

    task = supports_task_new (self, port, physdev, driver, callback, callback_data);
    g_assert (task);
    g_hash_table_insert (priv->tasks, g_strdup (key), g_object_ref (task));

    /* Help the plugin out a bit by finding an existing modem for this port */
    master_path = g_udev_device_get_sysfs_path (physdev);
    existing = g_hash_table_lookup (priv->modems, master_path);

    result = MM_PLUGIN_BASE_GET_CLASS (self)->supports_port (self, existing, task);
    if (result != MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS) {
        /* If the plugin doesn't support the port at all, the supports task is
         * not needed.
         */
        g_hash_table_remove (priv->tasks, key);
    }
    g_object_unref (task);

out:
    if (physdev)
        g_object_unref (physdev);
    if (port)
        g_object_unref (port);
    g_free (key);
    g_free (driver);
    return result;
}

static void
cancel_supports_port (MMPlugin *plugin,
                      const char *subsys,
                      const char *name)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    MMPluginBaseSupportsTask *task;
    char *key;

    key = get_key (subsys, name);
    task = g_hash_table_lookup (priv->tasks, key);
    if (task) {
        /* Let the plugin cancel any ongoing tasks */
        if (MM_PLUGIN_BASE_GET_CLASS (self)->cancel_task)
            MM_PLUGIN_BASE_GET_CLASS (self)->cancel_task (self, task);
        g_hash_table_remove (priv->tasks, key);
    }

    g_free (key);
}

static MMModem *
grab_port (MMPlugin *plugin,
           const char *subsys,
           const char *name,
           GError **error)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    MMPluginBaseSupportsTask *task;
    char *key;
    MMModem *existing = NULL, *modem = NULL;
    const char *master_path;

    key = get_key (subsys, name);
    task = g_hash_table_lookup (priv->tasks, key);
    if (!task) {
        g_free (key);
        g_return_val_if_fail (task != NULL, FALSE);
    }

    /* Help the plugin out a bit by finding an existing modem for this port */
    master_path = g_udev_device_get_sysfs_path (mm_plugin_base_supports_task_get_physdev (task));
    existing = g_hash_table_lookup (priv->modems, master_path);

    /* Let the modem grab the port */
    modem = MM_PLUGIN_BASE_GET_CLASS (self)->grab_port (self, existing, task, error);
    if (modem && !existing) {
        g_hash_table_insert (priv->modems, g_strdup (master_path), modem);
        g_object_weak_ref (G_OBJECT (modem), modem_destroyed, self);
    }

    g_hash_table_remove (priv->tasks, key);
    g_free (key);
    return modem;
}

/*****************************************************************************/

static void
plugin_init (MMPlugin *plugin_class)
{
    /* interface implementation */
    plugin_class->get_name = get_name;
    plugin_class->supports_port = supports_port;
    plugin_class->cancel_supports_port = cancel_supports_port;
    plugin_class->grab_port = grab_port;
}

static void
mm_plugin_base_init (MMPluginBase *self)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    const char *subsys[] = { "tty", "net", NULL };

    if (!cached_caps)
        cached_caps = g_hash_table_new (g_str_hash, g_str_equal);

    priv->client = g_udev_client_new (subsys);

    priv->modems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    priv->tasks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                         (GDestroyNotify) g_object_unref);
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NAME:
        /* Construct only */
        priv->name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (object);

    g_free (priv->name);

    g_object_unref (priv->client);

    g_hash_table_destroy (priv->modems);
    g_hash_table_destroy (priv->tasks);

    G_OBJECT_CLASS (mm_plugin_base_parent_class)->finalize (object);
}

static void
mm_plugin_base_class_init (MMPluginBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginBasePrivate));

    klass->find_physical_device = real_find_physical_device;
    klass->handle_probe_response = real_handle_probe_response;

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    g_object_class_install_property
        (object_class, PROP_NAME,
         g_param_spec_string (MM_PLUGIN_BASE_NAME,
                              "Name",
                              "Name",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    signals[PROBE_RESULT] =
        g_signal_new ("probe-result",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMPluginBaseClass, probe_result),
                      NULL, NULL,
                      mm_marshal_VOID__OBJECT_UINT,
                      G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_UINT);
}
