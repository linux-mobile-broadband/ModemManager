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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-sierra-gsm.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemSierraGsm, mm_modem_sierra_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

#define MM_MODEM_SIERRA_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_SIERRA_GSM, MMModemSierraGsmPrivate))

typedef struct {
    guint enable_wait_id;
} MMModemSierraGsmPrivate;

MMModem *
mm_modem_sierra_gsm_new (const char *device,
                         const char *driver,
                         const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_SIERRA_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static gboolean
sierra_enabled (gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsm *modem;
    MMModemSierraGsmPrivate *priv;

    /* Make sure we don't use an invalid modem that may have been removed */
    if (info->modem) {
        modem = MM_GENERIC_GSM (info->modem);
        priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (modem);
        priv->enable_wait_id = 0;
        MM_GENERIC_GSM_CLASS (mm_modem_sierra_gsm_parent_class)->do_enable_power_up_done (modem, NULL, NULL, info);
    }
    return FALSE;
}

static void
real_do_enable_power_up_done (MMGenericGsm *gsm,
                              GString *response,
                              GError *error,
                              MMCallbackInfo *info)
{
    MMModemSierraGsmPrivate *priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (gsm);

    if (error) {
        /* Chain up to parent */
        MM_GENERIC_GSM_CLASS (mm_modem_sierra_gsm_parent_class)->do_enable_power_up_done (gsm, NULL, error, info);
        return;
    }

    /* Some Sierra devices return OK on +CFUN=1 right away but need some time
     * to finish initialization.
     */
    g_warn_if_fail (priv->enable_wait_id == 0);
    priv->enable_wait_id = g_timeout_add_seconds (10, sierra_enabled, info);
}

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    MMGenericGsm *gsm = MM_GENERIC_GSM (modem);
    MMPortType ptype = MM_PORT_TYPE_IGNORED;
    MMPort *port;

    if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
        if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_PRIMARY))
                ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);

    if (port && MM_IS_SERIAL_PORT (port)) {
        GRegex *regex;

        g_object_set (G_OBJECT (port), MM_PORT_CARRIER_DETECT, FALSE, NULL);

        regex = g_regex_new ("\\r\\n\\+PACSP0\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);
    }

    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
mm_modem_sierra_gsm_init (MMModemSierraGsm *self)
{
}

static void
dispose (GObject *object)
{
    MMModemSierraGsmPrivate *priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (object);

    if (priv->enable_wait_id)
        g_source_remove (priv->enable_wait_id);
}

static void
mm_modem_sierra_gsm_class_init (MMModemSierraGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_sierra_gsm_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemSierraGsmPrivate));

    object_class->dispose = dispose;
    gsm_class->do_enable_power_up_done = real_do_enable_power_up_done;
}

