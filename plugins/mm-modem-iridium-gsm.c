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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "mm-errors.h"
#include "mm-modem-helpers.h"
#include "mm-modem-iridium-gsm.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMModemIridiumGsm, mm_modem_iridium_gsm, MM_TYPE_GENERIC_GSM);

MMModem *
mm_modem_iridium_gsm_new (const char *device,
                          const char *driver,
                          const char *plugin,
                          guint32 vendor,
                          guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_IRIDIUM_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   MM_MODEM_BASE_MAX_TIMEOUTS, 3,
                                   NULL));
}

static void
get_sim_iccid (MMGenericGsm *modem,
               MMModemStringFn callback,
               gpointer callback_data)
{
    /* There seems to be no way of getting an ICCID/IMSI subscriber ID within
     * the Iridium AT command set, so we just skip this. */
    callback (MM_MODEM (modem), "", NULL, callback_data);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    /* Do nothing... see set_property() in parent, which also does nothing */
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    switch (prop_id) {
    case MM_GENERIC_GSM_PROP_POWER_UP_CMD:
        /* No need for any special power up command */
        g_value_set_string (value, "");
        break;
    case MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD:
        /* Enable RTS/CTS flow control.
         * Other available values:
         *   AT&K0: Disable flow control
         *   AT&K3: RTS/CTS
         *   AT&K4: XOFF/XON
         *   AT&K6: Both RTS/CTS and XOFF/XON
         */
        g_value_set_string (value, "&K3");
        break;
    default:
        break;
    }
}

/*****************************************************************************/

static void
mm_modem_iridium_gsm_init (MMModemIridiumGsm *self)
{
}

static void
mm_modem_iridium_gsm_class_init (MMModemIridiumGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_POWER_UP_CMD,
                                      MM_GENERIC_GSM_POWER_UP_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD,
                                      MM_GENERIC_GSM_FLOW_CONTROL_CMD);

    gsm_class->get_sim_iccid = get_sim_iccid;
}

