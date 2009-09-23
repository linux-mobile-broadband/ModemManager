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
#include "mm-modem-sierra.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static gpointer mm_modem_sierra_parent_class = NULL;

MMModem *
mm_modem_sierra_new (const char *device,
                     const char *driver,
                     const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_SIERRA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static void
pin_check_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    mm_callback_info_schedule (info);
}

static gboolean
sierra_enabled (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;

    /* Now check the PIN explicitly, sierra doesn't seem to report
       that it needs it otherwise */
    mm_generic_gsm_check_pin (MM_GENERIC_GSM (info->modem), pin_check_done, info);

    return FALSE;
}

static void
parent_enable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        /* Sierra returns OK on +CFUN=1 right away but needs some time
           to finish initialization */
        g_timeout_add_seconds (10, sierra_enabled, info);
    }
}

static void
enable (MMModem *modem,
        MMModemFn callback,
        gpointer user_data)
{
    MMModem *parent_modem_iface;
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);
    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
    parent_modem_iface->enable (modem, parent_enable_done, info);
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

    if (port && MM_IS_SERIAL_PORT (port))
        g_object_set (G_OBJECT (port), MM_PORT_CARRIER_DETECT, FALSE, NULL);

    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->enable = enable;
    modem_class->grab_port = grab_port;
}

static void
mm_modem_sierra_init (MMModemSierra *self)
{
}

static void
mm_modem_sierra_class_init (MMModemSierraClass *klass)
{
    mm_modem_sierra_parent_class = g_type_class_peek_parent (klass);
}

GType
mm_modem_sierra_get_type (void)
{
    static GType modem_sierra_type = 0;

    if (G_UNLIKELY (modem_sierra_type == 0)) {
        static const GTypeInfo modem_sierra_type_info = {
            sizeof (MMModemSierraClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_sierra_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemSierra),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_sierra_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        modem_sierra_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemSierra", &modem_sierra_type_info, 0);
        g_type_add_interface_static (modem_sierra_type, MM_TYPE_MODEM, &modem_iface_info);
    }

    return modem_sierra_type;
}
