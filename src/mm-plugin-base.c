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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#define _GNU_SOURCE  /* for strcasestr */

#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <unistd.h>

#include <string.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin-base.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-errors.h"
#include "mm-marshal.h"
#include "mm-utils.h"
#include "libqcdm/src/commands.h"
#include "libqcdm/src/utils.h"
#include "mm-log.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginBase, mm_plugin_base, G_TYPE_OBJECT,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

#define MM_PLUGIN_BASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_BASE, MMPluginBasePrivate))

/* Struct to be stored in the cached hash table */
typedef struct {
    guint32 capabilities;
    gchar *vendor;
    gchar *product;
} MMPluginBaseProbedInfo;

/* A hash table shared between all instances of the plugin base that
 * caches the probed capabilities and reported vendor/model (when available)
 * so that only one plugin has to actually probe a port.
 */
static GHashTable *probed_info = NULL;

/* Virtual port corresponding to the embeded modem */
static gchar *virtual_port[] = {"smd0", NULL};

typedef struct {
    char *name;
    GUdevClient *client;
    GHashTable *tasks;
    gboolean sort_last;
} MMPluginBasePrivate;

enum {
    PROP_0,
    PROP_NAME,
    PROP_SORT_LAST,
    LAST_PROP
};

enum {
    PROBE_RESULT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef enum {
    /* Probing capabilities... */
    PROBE_STATE_CAPS_GCAP_TRY1 = 0,
    PROBE_STATE_CAPS_GCAP_TRY2,
    PROBE_STATE_CAPS_GCAP_TRY3,
    PROBE_STATE_CAPS_ATI,
    PROBE_STATE_CAPS_CPIN,
    PROBE_STATE_CAPS_CGMM,
    /* Probing vendor... */
    PROBE_STATE_VENDOR_CGMI,
    PROBE_STATE_VENDOR_GMI,
    PROBE_STATE_VENDOR_ATI,
    /* Probing product... */
    PROBE_STATE_PRODUCT_CGMM,
    PROBE_STATE_PRODUCT_GMM,
    PROBE_STATE_PRODUCT_ATI,
    PROBE_STATE_LAST
} ProbeState;

/* Additional helper IDs for the states, to be updated whenever new intermediate
 * states are added in each probing group */
#define PROBE_STATE_CAPS_FIRST    PROBE_STATE_CAPS_GCAP_TRY1
#define PROBE_STATE_CAPS_LAST     PROBE_STATE_CAPS_CGMM
#define PROBE_STATE_VENDOR_FIRST  PROBE_STATE_VENDOR_CGMI
#define PROBE_STATE_VENDOR_LAST   PROBE_STATE_VENDOR_ATI
#define PROBE_STATE_PRODUCT_FIRST PROBE_STATE_PRODUCT_CGMM
#define PROBE_STATE_PRODUCT_LAST  PROBE_STATE_PRODUCT_ATI

typedef struct {
    /* The command to send in this probing state */
    gchar *cmd;
    /* Whether the command reply should be cached */
    gboolean cached;
} ProbeStateCmd;

/* List of commands sent in each state */
static const ProbeStateCmd state_commands[PROBE_STATE_LAST] = {
    /* Probing capabilities... */
    { "+GCAP",  FALSE }, /* PROBE_STATE_CAPS_GCAP_TRY1 */
    { "+GCAP",  FALSE }, /* PROBE_STATE_CAPS_GCAP_TRY2 */
    { "+GCAP",  FALSE }, /* PROBE_STATE_CAPS_GCAP_TRY3 */
    { "I",      TRUE  }, /* PROBE_STATE_CAPS_ATI */
    { "+CPIN?", FALSE }, /* PROBE_STATE_CAPS_CPIN */
    { "+CGMM",  TRUE  }, /* PROBE_STATE_CAPS_CGMM */
    /* Probing vendor... */
    { "+CGMI",  TRUE  }, /* PROBE_STATE_VENDOR_CGMI */
    { "+GMI",   TRUE  }, /* PROBE_STATE_VENDOR_GMI */
    { "I",      TRUE  }, /* PROBE_STATE_VENDOR_ATI */
    /* Probing product... */
    { "+CGMM",  TRUE  }, /* PROBE_STATE_PRODUCT_CGMM */
    { "+GMM",   TRUE  }, /* PROBE_STATE_PRODUCT_GMM */
    { "I",      TRUE  }  /* PROBE_STATE_PRODUCT_ATI */
};

static void probe_complete (MMPluginBaseSupportsTask *task);
static void parse_response (MMAtSerialPort *port,
                            GString *response,
                            GError *error,
                            gpointer user_data);

/*****************************************************************************/

G_DEFINE_TYPE (MMPluginBaseSupportsTask, mm_plugin_base_supports_task, G_TYPE_OBJECT)

#define MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK, MMPluginBaseSupportsTaskPrivate))

typedef struct {
    char *command;
    guint32 tries;
    guint32 delay_seconds;
    MMBaseSupportsTaskCustomInitResultFunc callback;
    gpointer callback_data;
} CustomInit;

typedef struct {
    MMPluginBase *plugin;
    GUdevDevice *port;
    char *physdev_path;
    char *driver;

    guint open_id;
    guint32 open_tries;
    guint full_id;

    MMAtSerialPort *probe_port;
    MMQcdmSerialPort *qcdm_port;
    guint32 probed_caps;
    gchar *probed_vendor;
    gchar *probed_product;
    ProbeState probe_state;
    guint probe_id;
    char *probe_resp;
    GError *probe_error;

    /* Custom init commands plugins might want */
    GSList *custom;
    GSList *cur_custom; /* Pointer to current custom init command */

    MMSupportsPortResultFunc callback;
    gpointer callback_data;
}  MMPluginBaseSupportsTaskPrivate;

static MMPluginBaseSupportsTask *
supports_task_new (MMPluginBase *self,
                   GUdevDevice *port,
                   const char *physdev_path,
                   const char *driver,
                   MMSupportsPortResultFunc callback,
                   gpointer callback_data)
{
    MMPluginBaseSupportsTask *task;
    MMPluginBaseSupportsTaskPrivate *priv;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), NULL);
    g_return_val_if_fail (port != NULL, NULL);
    g_return_val_if_fail (physdev_path != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    task = (MMPluginBaseSupportsTask *) g_object_new (MM_TYPE_PLUGIN_BASE_SUPPORTS_TASK, NULL);

    priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    priv->plugin = self;
    priv->port = g_object_ref (port);
    priv->physdev_path = g_strdup (physdev_path);
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

const char *
mm_plugin_base_supports_task_get_physdev_path (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), NULL);

    return MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->physdev_path;
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

const gchar *
mm_plugin_base_supports_task_get_probed_vendor (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), NULL);

    return MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->probed_vendor;
}

const gchar *
mm_plugin_base_supports_task_get_probed_product (MMPluginBaseSupportsTask *task)
{
    g_return_val_if_fail (task != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), NULL);

    return MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task)->probed_product;
}

gboolean
mm_plugin_base_supports_task_propagate_cached (MMPluginBaseSupportsTask *task)
{
    MMPluginBaseSupportsTaskPrivate *priv;

    g_return_val_if_fail (task != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task), FALSE);

    priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    g_free (priv->probed_vendor);
    g_free (priv->probed_product);

    /* Returns TRUE if a previous supports task already cached probing results.
     * It will also store a copy of the cached result. */
    return mm_plugin_base_get_cached_probe_result (priv->plugin,
                                                   priv->port,
                                                   &priv->probed_caps,
                                                   &priv->probed_vendor,
                                                   &priv->probed_product);
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

    if (priv->full_id) {
        g_source_remove (priv->full_id);
        priv->full_id = 0;
    }

    subsys = g_udev_device_get_subsystem (priv->port);
    name = g_udev_device_get_name (priv->port);

    priv->callback (MM_PLUGIN (priv->plugin), subsys, name, level, priv->callback_data);

    /* Clear out the callback, it shouldn't be called more than once */
    priv->callback = NULL;
    priv->callback_data = NULL;
}

void
mm_plugin_base_supports_task_add_custom_init_command (MMPluginBaseSupportsTask *task,
                                                      const char *cmd,
                                                      guint32 delay_seconds,
                                                      MMBaseSupportsTaskCustomInitResultFunc callback,
                                                      gpointer callback_data)
{
    MMPluginBaseSupportsTaskPrivate *priv;
    CustomInit *custom;

    g_return_if_fail (task != NULL);
    g_return_if_fail (MM_IS_PLUGIN_BASE_SUPPORTS_TASK (task));
    g_return_if_fail (callback != NULL);

    priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    custom = g_malloc0 (sizeof (*custom));
    custom->command = g_strdup (cmd);
    custom->delay_seconds = delay_seconds ? delay_seconds : 3;
    custom->callback = callback;
    custom->callback_data = callback_data;

    priv->custom = g_slist_append (priv->custom, custom);
}

static void
mm_plugin_base_supports_task_init (MMPluginBaseSupportsTask *self)
{
}

static void
supports_task_dispose (GObject *object)
{
    MMPluginBaseSupportsTaskPrivate *priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (object);
    GSList *iter;

    if (MM_IS_SERIAL_PORT (priv->probe_port))
        mm_serial_port_flash_cancel (MM_SERIAL_PORT (priv->probe_port));

    g_object_unref (priv->port);
    g_free (priv->physdev_path);
    g_free (priv->driver);
    g_free (priv->probe_resp);
    g_clear_error (&(priv->probe_error));

    for (iter = priv->custom; iter; iter = g_slist_next (iter)) {
        CustomInit *custom = iter->data;

        g_free (custom->command);
        memset (custom, 0, sizeof (*custom));
        g_free (custom);
    }

    if (priv->open_id)
        g_source_remove (priv->open_id);
    if (priv->full_id)
        g_source_remove (priv->full_id);

    if (priv->probe_id)
        g_source_remove (priv->probe_id);
    if (priv->probe_port) {
        mm_serial_port_close (MM_SERIAL_PORT (priv->probe_port));
        g_object_unref (priv->probe_port);
    }
    if (priv->qcdm_port) {
        mm_serial_port_close (MM_SERIAL_PORT (priv->qcdm_port));
        g_object_unref (priv->qcdm_port);
    }

    g_free (priv->probed_vendor);
    g_free (priv->probed_product);

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
parse_caps_gcap (const char *buf)
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
parse_caps_cpin (const char *buf)
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
        || strcasestr (buf, "PH-CORP PUK")
        || strcasestr (buf, "READY"))
        return MM_PLUGIN_BASE_PORT_CAP_GSM;

    return 0;
}

static guint32
parse_caps_cgmm (const char *buf)
{
    if (strstr (buf, "GSM900") || strstr (buf, "GSM1800") ||
        strstr (buf, "GSM1900") || strstr (buf, "GSM850"))
        return MM_PLUGIN_BASE_PORT_CAP_GSM;
    return 0;
}

static gchar *
parse_vendor_cgmi (const gchar *buf)
{
    return g_strstrip (g_strdelimit (g_strdup (buf), "\r\n", ' '));
}

static gchar *
parse_product_cgmm (const gchar *buf)
{
    return g_strstrip (g_strdelimit (g_strdup (buf), "\r\n", ' '));
}

static const char *dq_strings[] = {
    /* Option Icera-based devices */
    "option/faema_",
    "os_logids.h",
    /* Sierra CnS port */
    "NETWORK SERVICE CHANGE",
    NULL
};

static void
probed_info_free (MMPluginBaseProbedInfo *info)
{
    if (!info)
        return;

    g_free (info->vendor);
    g_free (info->product);
    g_free (info);
}

static void
port_buffer_full (MMSerialPort *port, GByteArray *buffer, gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (user_data);
    const char **iter;
    size_t iter_len;
    int i;

    /* Check for an immediate disqualification response.  There are some
     * ports (Option Icera-based chipsets have them, as do Qualcomm Gobi
     * devices before their firmware is loaded) that just shouldn't be
     * probed if we get a certain response because we know they can't be
     * used.  Kernel bugs (at least with 2.6.31 and 2.6.32) also trigger port
     * flow control kernel oopses if we read too much data for these ports.
     */

    for (iter = &dq_strings[0]; iter && *iter; iter++) {
        /* Search in the response for the item; the response could have embedded
         * nulls so we can't use memcmp() or strstr() on the whole response.
         */
        iter_len = strlen (*iter);
        for (i = 0; i < buffer->len - iter_len; i++) {
            if (!memcmp (&buffer->data[i], *iter, iter_len)) {
                /* Immediately close the port and complete probing */
                priv->probed_caps = 0;
                priv->probed_vendor = NULL;
                priv->probed_product = NULL;
                mm_serial_port_close (MM_SERIAL_PORT (priv->probe_port));
                probe_complete (task);
                return;
            }
        }
    }
}

static gboolean
emit_probe_result (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMPlugin *self = mm_plugin_base_supports_task_get_plugin (task);

    /* Close the serial ports */
    if (task_priv->probe_port) {
        g_object_unref (task_priv->probe_port);
        task_priv->probe_port = NULL;
    }
    if (task_priv->qcdm_port) {
        g_object_unref (task_priv->qcdm_port);
        task_priv->qcdm_port = NULL;
    }

    task_priv->probe_id = 0;
    g_signal_emit (self, signals[PROBE_RESULT], 0, task, task_priv->probed_caps);
    return FALSE;
}

static void
probe_complete (MMPluginBaseSupportsTask *task)
{
    MMPluginBaseSupportsTaskPrivate *priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMPluginBaseProbedInfo *info;
    MMPort *port;

    port = priv->probe_port ? MM_PORT (priv->probe_port) : MM_PORT (priv->qcdm_port);
    g_assert (port);

    /* Allocate new cached info struct and store it in the hash table */
    info = g_malloc0 (sizeof (*info));
    info->capabilities = priv->probed_caps;
    info->vendor = g_strdup (priv->probed_vendor);
    info->product = g_strdup (priv->probed_product);
    g_hash_table_insert (probed_info,
                         g_strdup (mm_port_get_device (port)),
                         info);

    priv->probe_id = g_idle_add (emit_probe_result, task);
}

static void
qcdm_verinfo_cb (MMQcdmSerialPort *port,
                 GByteArray *response,
                 GError *error,
                 gpointer user_data)
{
    MMPluginBaseSupportsTask *task;
    MMPluginBaseSupportsTaskPrivate *priv;
    QCDMResult *result;
    GError *dm_error = NULL;

    /* Just the initial poke; ignore it */
    if (!user_data)
        return;

    task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    if (error) {
        /* Probably not a QCDM port */
        goto done;
    }

    /* Parse the response */
    result = qcdm_cmd_version_info_result ((const char *) response->data, response->len, &dm_error);
    if (!result) {
        g_warning ("(%s) failed to parse QCDM version info command result: (%d) %s.",
                   g_udev_device_get_name (priv->port),
                   dm_error ? dm_error->code : -1,
                   dm_error && dm_error->message ? dm_error->message : "(unknown)");
        g_clear_error (&dm_error);
        goto done;
    }

    /* yay, probably a QCDM port */
    qcdm_result_unref (result);
    priv->probed_caps |= MM_PLUGIN_BASE_PORT_CAP_QCDM;
done:
    probe_complete (task);
}

static void
try_qcdm_probe (MMPluginBaseSupportsTask *task)
{
    MMPluginBaseSupportsTaskPrivate *priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    const char *name;
    GError *error = NULL;
    GByteArray *verinfo = NULL, *verinfo2;
    gint len;

    /* Close the AT port */
    if (priv->probe_port) {
        mm_serial_port_close (MM_SERIAL_PORT (priv->probe_port));
        g_object_unref (priv->probe_port);
        priv->probe_port = NULL;
    }

    /* Open the QCDM port */
    name = g_udev_device_get_name (priv->port);
    g_assert (name);
    priv->qcdm_port = mm_qcdm_serial_port_new (name, MM_PORT_TYPE_PRIMARY);
    if (priv->qcdm_port == NULL) {
        g_warning ("(%s) failed to create new QCDM serial port.", name);
        probe_complete (task);
        return;
    }

    if (!mm_serial_port_open (MM_SERIAL_PORT (priv->qcdm_port), &error)) {
        g_warning ("(%s) failed to open new QCDM serial port: (%d) %s.",
                   name,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
        g_clear_error (&error);
        probe_complete (task);
        return;
    }

    /* Build up the probe command */
    verinfo = g_byte_array_sized_new (50);
    len = qcdm_cmd_version_info_new ((char *) verinfo->data, 50, &error);
    if (len <= 0) {
        g_byte_array_free (verinfo, TRUE);
        g_warning ("(%s) failed to create QCDM version info command: (%d) %s.",
                   name,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
        g_clear_error (&error);
        probe_complete (task);
        return;
    }
    verinfo->len = len;

    /* Queuing the command takes ownership over it; copy it for the second try */
    verinfo2 = g_byte_array_sized_new (verinfo->len);
    g_byte_array_append (verinfo2, verinfo->data, verinfo->len);

    /* Send the command twice; the ports often need to be woken up */
    mm_qcdm_serial_port_queue_command (priv->qcdm_port, verinfo, 3, qcdm_verinfo_cb, NULL);
    mm_qcdm_serial_port_queue_command (priv->qcdm_port, verinfo2, 3, qcdm_verinfo_cb, task);
}

static void
real_handle_probe_response (MMPluginBase *self,
                            MMPluginBaseSupportsTask *task,
                            const char *cmd,
                            const char *response,
                            const GError *error)
{
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMAtSerialPort *port = task_priv->probe_port;
    gboolean ignore_error = FALSE;

    /* Some modems (Huawei E160g) won't respond to +GCAP with no SIM, but
     * will respond to ATI.
     */
    if (response && strstr (response, "+CME ERROR:"))
        ignore_error = TRUE;

    if (error && !ignore_error) {
        /* Only allow timeout errors in the initial AT+GCAP queries. If all AT+GCAP
         * get timed out, assume it's not an AT port. */
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            /* Try GCAP again */
            if (task_priv->probe_state < PROBE_STATE_CAPS_GCAP_TRY3) {
                task_priv->probe_state++;
                mm_at_serial_port_queue_command (port,
                                                 state_commands[task_priv->probe_state].cmd,
                                                 3,
                                                 parse_response,
                                                 task);
                return;
            }

            /* If all the GCAP tries timed out, ignore the port as it's
             * probably not an AT-capable port.  Try QCDM.
             */
            if (task_priv->probe_state == PROBE_STATE_CAPS_GCAP_TRY3) {
                try_qcdm_probe (task);
                return;
            }

            /* Otherwise, it's a timeout in some other command being probed, so try the
             * next one if any */
        }

        /* Non-timeout error, so proceed to the next command if any */
    } else if (response) {
        /* Parse the response */

        switch (task_priv->probe_state) {
        case PROBE_STATE_CAPS_GCAP_TRY1:
        case PROBE_STATE_CAPS_GCAP_TRY2:
        case PROBE_STATE_CAPS_GCAP_TRY3:
        case PROBE_STATE_CAPS_ATI:
            /* Some modems don't respond to AT+GCAP, but often they put a
             * GCAP-style response as a line in the ATI response.
             */
            task_priv->probed_caps = parse_caps_gcap (response);
            break;
        case PROBE_STATE_CAPS_CPIN:
            /* Some devices (ZTE MF628/ONDA MT503HS for example) reply to
             * anything but AT+CPIN? with ERROR if the device has a PIN set.
             * Since no known CDMA modems support AT+CPIN? we can consider the
             * device a GSM device if it returns a non-error response to AT+CPIN?.
             */
            task_priv->probed_caps = parse_caps_cpin (response);
            break;
        case PROBE_STATE_CAPS_CGMM:
            /* Some models (BUSlink SCWi275u) stick stupid stuff in the CGMM
             * response but at least it allows us to identify them.
             */
            task_priv->probed_caps = parse_caps_cgmm (response);
            break;
        case PROBE_STATE_VENDOR_CGMI:
        case PROBE_STATE_VENDOR_GMI:
        case PROBE_STATE_VENDOR_ATI:
            /* These replies parsed the same way, just removing EOLs */
            task_priv->probed_vendor = parse_vendor_cgmi (response);
            break;
        case PROBE_STATE_PRODUCT_CGMM:
        case PROBE_STATE_PRODUCT_GMM:
        case PROBE_STATE_PRODUCT_ATI:
            /* These replies parsed the same way, just removing EOLs */
            task_priv->probed_product = parse_product_cgmm (response);
            break;
        default:
            break;
        }
    }

    /* Now, choose the proper next state */

    if (task_priv->probe_state <= PROBE_STATE_CAPS_LAST) {
        /* Probing capabilities */
        if (task_priv->probed_caps & CAP_GSM_OR_CDMA) {
            /* Got capabilities probed, go on with vendor probing */
            task_priv->probe_state = PROBE_STATE_VENDOR_FIRST;
        } else if (task_priv->probe_state < PROBE_STATE_CAPS_LAST) {
            /* Didn't get capabilities probed yet, go on to next caps
             * probing command */
            task_priv->probe_state++;
        } else {
            /* Tried probing all commands for capabilities and didn't get
             * any: just end probing here, it probably isn't a GSM or CDMA
             * modem */
            mm_warn ("Couldn't probe for capabilities, probably not a GSM or CDMA modem");
            probe_complete (task);
            return;
        }
    } else if (task_priv->probe_state <= PROBE_STATE_VENDOR_LAST) {
        /* Probing vendors */
        if (task_priv->probed_vendor) {
            /* Got vendor probed, go on with product probing. */
            task_priv->probe_state = PROBE_STATE_PRODUCT_FIRST;
        } else if (task_priv->probe_state < PROBE_STATE_VENDOR_LAST) {
            /* Didn't get vendor probed yet, go on to next vendor
             * probing command */
            task_priv->probe_state++;
        } else {
            /* Tried probing all commands for vendor and didn't get
             * any: just end probing here, product ID  is useless without
             * vendor ID */
            probe_complete (task);
            return;
        }
    } else if (task_priv->probe_state <= PROBE_STATE_PRODUCT_LAST) {
        /* Probing products */
        if (!task_priv->probed_product &&
            task_priv->probe_state < PROBE_STATE_PRODUCT_LAST) {
            /* Didn't get probed product yet but more commands available,
             * go on with the next one */
            task_priv->probe_state++;
        } else {
            /* Got product probed or no more commands to probe, so end. */
            probe_complete (task);
            return;
        }
    } else {
        g_warn_if_reached ();
    }

    /* Out of last state? */
    if (task_priv->probe_state >= PROBE_STATE_LAST) {
        /* Probing ended */
        probe_complete (task);
        return;
    }

    /* Go on with the command in next state */

    if (state_commands[task_priv->probe_state].cached)
        mm_at_serial_port_queue_command_cached (port,
                                                state_commands[task_priv->probe_state].cmd,
                                                3,
                                                parse_response,
                                                task);
    else
        mm_at_serial_port_queue_command (port,
                                         state_commands[task_priv->probe_state].cmd,
                                         3,
                                         parse_response,
                                         task);
}

static gboolean
handle_probe_response (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMPluginBase *self = MM_PLUGIN_BASE (mm_plugin_base_supports_task_get_plugin (task));

    MM_PLUGIN_BASE_GET_CLASS (self)->handle_probe_response (self,
                                                            task,
                                                            state_commands[task_priv->probe_state].cmd,
                                                            task_priv->probe_resp,
                                                            task_priv->probe_error);
    return FALSE;
}

static void
parse_response (MMAtSerialPort *port,
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

static void
start_generic_probing (MMPluginBaseSupportsTask *task, MMAtSerialPort *port)
{
    mm_at_serial_port_queue_command (port, "+GCAP", 3, parse_response, task);
}

static void flash_done (MMSerialPort *port, GError *error, gpointer user_data);

static void
custom_init_response (MMAtSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    CustomInit *custom = task_priv->cur_custom->data;
    gboolean retry = FALSE;
    gboolean stop = FALSE;
    guint32 level = 0;

    custom->tries++;
    retry = custom->callback (task, response, error, custom->tries, &stop, &level, custom->callback_data);

    if (stop) {
        task_priv->probed_caps = level;
        probe_complete (task);
        return;
    }

    if (retry) {
        /* Try the custom command again */
        flash_done (MM_SERIAL_PORT (port), NULL, task);
        return;
    }

    /* Any more custom init commands? */
    task_priv->cur_custom = g_slist_next (task_priv->cur_custom);
    if (task_priv->cur_custom) {
        /* There are more custom init commands */
        flash_done (MM_SERIAL_PORT (port), NULL, task);
        return;
    }

    /* Otherwise continue with generic probing */
    start_generic_probing (task, port);
}

static void
flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);

    /* Send the custom init command if any */
    if (task_priv->cur_custom) {
        CustomInit *custom = task_priv->cur_custom->data;

        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port),
                                         custom->command,
                                         custom->delay_seconds,
                                         custom_init_response,
                                         task);
    } else {
        /* Otherwise start normal probing */
        start_generic_probing (task, MM_AT_SERIAL_PORT (port));
    }
}

static gboolean
try_open (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = MM_PLUGIN_BASE_SUPPORTS_TASK (user_data);
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    GError *error = NULL;

    task_priv->open_id = 0;

    if (!mm_serial_port_open (MM_SERIAL_PORT (task_priv->probe_port), &error)) {
        if (++task_priv->open_tries > 4) {
            /* took too long to open the port; give up */
            g_warning ("(%s): failed to open after 4 tries.",
                       mm_port_get_device (MM_PORT (task_priv->probe_port)));
            probe_complete (task);
        } else if (g_error_matches (error,
                                    MM_SERIAL_ERROR,
                                    MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE)) {
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

        task_priv->full_id = g_signal_connect (task_priv->probe_port, "buffer-full",
                                               G_CALLBACK (port_buffer_full), task);

        mm_dbg ("(%s): probe requested by plugin '%s'",
                g_udev_device_get_name (port),
                mm_plugin_get_name (MM_PLUGIN (task_priv->plugin)));
        mm_serial_port_flash (MM_SERIAL_PORT (task_priv->probe_port), 100, TRUE, flash_done, task);
    }

    return FALSE;
}

gboolean
mm_plugin_base_probe_port (MMPluginBase *self,
                           MMPluginBaseSupportsTask *task,
                           guint64 send_delay_us,
                           GError **error)
{
    MMPluginBaseSupportsTaskPrivate *task_priv = MM_PLUGIN_BASE_SUPPORTS_TASK_GET_PRIVATE (task);
    MMAtSerialPort *serial;
    const char *name;
    GUdevDevice *port;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), FALSE);
    g_return_val_if_fail (task != NULL, FALSE);

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);
    name = g_udev_device_get_name (port);
    g_assert (name);

    serial = mm_at_serial_port_new (name, MM_PORT_TYPE_PRIMARY);
    if (serial == NULL) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Failed to create new serial port.");
        return FALSE;
    }

    g_object_set (serial,
                  MM_SERIAL_PORT_SEND_DELAY, send_delay_us,
                  MM_PORT_CARRIER_DETECT, FALSE,
                  MM_SERIAL_PORT_SPEW_CONTROL, TRUE,
                  NULL);

    mm_at_serial_port_set_response_parser (serial,
                                           mm_serial_parser_v1_parse,
                                           mm_serial_parser_v1_new (),
                                           mm_serial_parser_v1_destroy);

    /* Open the port */
    task_priv->probe_port = serial;
    task_priv->cur_custom = task_priv->custom;
    task_priv->open_id = g_idle_add (try_open, task);
    return TRUE;
}

gboolean
mm_plugin_base_get_cached_probe_result (MMPluginBase *self,
                                        GUdevDevice *port,
                                        guint32 *capabilities,
                                        gchar **vendor,
                                        gchar **product)
{
    MMPluginBaseProbedInfo *info;

    if (g_hash_table_lookup_extended (probed_info,
                                      g_udev_device_get_name (port),
                                      NULL,
                                      (gpointer *)&info)) {
        if (capabilities)
            *capabilities = info->capabilities;
        if (vendor)
            *vendor = (info->vendor ? g_strdup (info->vendor) : NULL);
        if (product)
            *product = (info->product ? g_strdup (info->product) : NULL);
        return TRUE;
    }

    if (capabilities)
        *capabilities = 0;
    if (vendor)
        *vendor = NULL;
    if (product)
        *product = NULL;
    return FALSE;
}

/*****************************************************************************/

static void
modem_destroyed (gpointer data, GObject *modem)
{
    /* Since we don't track port cached capabilities on a per-modem basis,
     * we just have to live with blowing away the cached capabilities whenever
     * a modem gets removed.  Could do better here by storing a structure
     * in the cached capabilities table that includes { caps, modem device }
     * or something and then only removing cached capabilities for ports
     * that the modem that was just removed owned, but whatever.
     */
    g_hash_table_remove_all (probed_info);
}

gboolean
mm_plugin_base_get_device_ids (MMPluginBase *self,
                               const char *subsys,
                               const char *name,
                               guint16 *vendor,
                               guint16 *product)
{
    MMPluginBasePrivate *priv;
    GUdevDevice *device = NULL, *parent = NULL;
    const char *vid = NULL, *pid = NULL, *parent_subsys;
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

    parent = g_udev_device_get_parent (device);
    if (parent) {
        parent_subsys = g_udev_device_get_subsystem (parent);
        if (parent_subsys) {
            if (!strcmp (parent_subsys, "bluetooth")) {
                /* Bluetooth devices report the VID/PID of the BT adapter here,
                 * which isn't really what we want.  Just return null IDs instead.
                 */
                success = TRUE;
                goto out;
            } else if (!strcmp (parent_subsys, "pcmcia")) {
                /* For PCMCIA devices we need to grab the PCMCIA subsystem's
                 * manfid and cardid, since any IDs on the tty device itself
                 * may be from PCMCIA controller or something else.
                 */
                vid = g_udev_device_get_sysfs_attr (parent, "manf_id");
                pid = g_udev_device_get_sysfs_attr (parent, "card_id");
                if (!vid || !pid)
                    goto out;
            } else if (!strcmp (parent_subsys, "platform")) {
                /* Platform devices don't usually have a VID/PID */
                success = TRUE;
                goto out;
            }
        }
    }

    if (!vid)
        vid = g_udev_device_get_property (device, "ID_VENDOR_ID");
    if (!vid)
        goto out;

    if (strncmp (vid, "0x", 2) == 0)
        vid += 2;
    if (strlen (vid) != 4)
        goto out;

    if (vendor) {
        *vendor = (guint16) (utils_hex2byte (vid + 2) & 0xFF);
        *vendor |= (guint16) ((utils_hex2byte (vid) & 0xFF) << 8);
    }

    if (!pid)
        pid = g_udev_device_get_property (device, "ID_MODEL_ID");
    if (!pid) {
        *vendor = 0;
        goto out;
    }

    if (strncmp (pid, "0x", 2) == 0)
        pid += 2;
    if (strlen (pid) != 4) {
        *vendor = 0;
        goto out;
    }

    if (product) {
        *product = (guint16) (utils_hex2byte (pid + 2) & 0xFF);
        *product |= (guint16) ((utils_hex2byte (pid) & 0xFF) << 8);
    }

    success = TRUE;

out:
    if (device)
        g_object_unref (device);
    if (parent)
        g_object_unref (parent);
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

static gboolean
get_sort_last (const MMPlugin *plugin)
{
    return MM_PLUGIN_BASE_GET_PRIVATE (plugin)->sort_last;
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

static gboolean
device_file_exists(const char *name)
{
    char *devfile;
    struct stat s;
    int result;

    devfile = g_strdup_printf ("/dev/%s", name);
    result = stat (devfile, &s);
    g_free (devfile);

    return (0 == result) ? TRUE : FALSE;
}

static MMPluginSupportsResult
supports_port (MMPlugin *plugin,
               const char *subsys,
               const char *name,
               const char *physdev_path,
               MMModem *existing,
               MMSupportsPortResultFunc callback,
               gpointer callback_data)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    GUdevDevice *port = NULL;
    char *driver = NULL, *key = NULL;
    MMPluginBaseSupportsTask *task;
    MMPluginSupportsResult result = MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    int idx;

    key = get_key (subsys, name);
    task = g_hash_table_lookup (priv->tasks, key);
    if (task) {
        g_free (key);
        g_return_val_if_fail (task == NULL, MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);
    }

    port = g_udev_client_query_by_subsystem_and_name (priv->client, subsys, name);
    if (!port)
        goto out;

    // Detect any modems accessible through the list of virtual ports
    for (idx = 0; virtual_port[idx]; idx++)  {
        if (strcmp(name, virtual_port[idx]))
            continue;
        if (!device_file_exists(virtual_port[idx]))
            continue;

        task = supports_task_new (self, port, physdev_path, "virtual", callback, callback_data);
        g_assert (task);
        g_hash_table_insert (priv->tasks, g_strdup (key), g_object_ref (task));
        goto find_plugin;
    }

    driver = get_driver_name (port);
    if (!driver)
        goto out;

    task = supports_task_new (self, port, physdev_path, driver, callback, callback_data);
    g_assert (task);
    g_hash_table_insert (priv->tasks, g_strdup (key), g_object_ref (task));

find_plugin:
    result = MM_PLUGIN_BASE_GET_CLASS (self)->supports_port (self, existing, task);
    if (result != MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS) {
        /* If the plugin doesn't support the port at all, the supports task is
         * not needed.
         */
        g_hash_table_remove (priv->tasks, key);
    }
    g_object_unref (task);

out:
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
           MMModem *existing,
           GError **error)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    MMPluginBaseSupportsTask *task;
    MMModem *modem = NULL;
    char *key;

    key = get_key (subsys, name);
    task = g_hash_table_lookup (priv->tasks, key);
    if (!task) {
        g_free (key);
        g_return_val_if_fail (task != NULL, FALSE);
    }

    /* Let the modem grab the port */
    modem = MM_PLUGIN_BASE_GET_CLASS (self)->grab_port (self, existing, task, error);
    if (modem && !existing)
        g_object_weak_ref (G_OBJECT (modem), modem_destroyed, self);

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
    plugin_class->get_sort_last = get_sort_last;
    plugin_class->supports_port = supports_port;
    plugin_class->cancel_supports_port = cancel_supports_port;
    plugin_class->grab_port = grab_port;
}

static void
mm_plugin_base_init (MMPluginBase *self)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    const char *subsys[] = { "tty", "net", NULL };

    if (!probed_info) {
        probed_info = g_hash_table_new_full (g_str_hash,
                                             g_str_equal,
                                             g_free,
                                             (GDestroyNotify) probed_info_free);
    }

    priv->client = g_udev_client_new (subsys);

    priv->tasks = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
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
    case PROP_SORT_LAST:
        /* Construct only */
        priv->sort_last = g_value_get_boolean (value);
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
    case PROP_SORT_LAST:
        g_value_set_boolean (value, priv->sort_last);
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

    g_hash_table_destroy (priv->tasks);

    G_OBJECT_CLASS (mm_plugin_base_parent_class)->finalize (object);
}

static void
mm_plugin_base_class_init (MMPluginBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginBasePrivate));

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

    g_object_class_install_property
        (object_class, PROP_SORT_LAST,
         g_param_spec_boolean (MM_PLUGIN_BASE_SORT_LAST,
                               "Sort Last",
                               "Whether the plugin should be sorted last in the"
                               "list of plugins loaded",
                               FALSE,
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
