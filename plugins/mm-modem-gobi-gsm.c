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

#include "mm-modem-gobi-gsm.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-gsm-card.h"

static gpointer mm_modem_gobi_gsm_parent_class = NULL;

MMModem *
mm_modem_gobi_gsm_new (const char *device,
                       const char *driver,
                       const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_GOBI_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

/*****************************************************************************/

static void
get_string_done (MMSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error && response && !strcmp (response->str, "ERROR")) {
        info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                           MM_MOBILE_ERROR_SIM_NOT_INSERTED,
                                           "Unable to read IMSI");
    } else if (error)
        info->error = g_error_copy (error);
    else
        mm_callback_info_set_result (info, g_strdup (response->str), g_free);

    mm_callback_info_schedule (info);
}

static void
get_imsi (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMSerialPort *primary;
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_serial_port_queue_command_cached (primary, "+CIMI", 3, get_string_done, info);
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_imsi = get_imsi;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
}

static void
mm_modem_gobi_gsm_init (MMModemGobiGsm *self)
{
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{

    /* These devices just don't implement AT+CFUN */

    switch (prop_id) {
    case MM_GENERIC_GSM_PROP_INIT_CMD:
        g_value_set_string (value, "Z E0 V1 +CMEE=1");
        break;
    default:
        break;
    }
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
}

static void
mm_modem_gobi_gsm_class_init (MMModemGobiGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_modem_gobi_gsm_parent_class = g_type_class_peek_parent (klass);

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_INIT_CMD,
                                      MM_GENERIC_GSM_INIT_CMD);
}

GType
mm_modem_gobi_gsm_get_type (void)
{
    static GType modem_gobi_gsm_type = 0;

    if (G_UNLIKELY (modem_gobi_gsm_type == 0)) {
        static const GTypeInfo modem_gobi_gsm_type_info = {
            sizeof (MMModemGobiGsmClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_gobi_gsm_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemGobiGsm),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_gobi_gsm_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        static const GInterfaceInfo modem_gsm_card_info = {
            (GInterfaceInitFunc) modem_gsm_card_init
        };

        modem_gobi_gsm_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemGobiGsm",
                                                         &modem_gobi_gsm_type_info, 0);

        g_type_add_interface_static (modem_gobi_gsm_type, MM_TYPE_MODEM, &modem_iface_info);
        g_type_add_interface_static (modem_gobi_gsm_type, MM_TYPE_MODEM_GSM_CARD, &modem_gsm_card_info);
    }

    return modem_gobi_gsm_type;
}
