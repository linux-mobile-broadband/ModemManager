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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google Inc.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-huawei.h"

G_DEFINE_TYPE (MMBroadbandModemHuawei, mm_broadband_modem_huawei, MM_TYPE_BROADBAND_MODEM);

struct _MMBroadbandModemHuaweiPrivate {
    /* Regex for signal quality related notifications */
    GRegex *rssi_regex;

    /* Regex for access-technology related notifications */
    GRegex *mode_regex;

    /* Regex for connection status related notifications */
    GRegex *dsflowrpt_regex;

    /* Regex to ignore */
    GRegex *boot_regex;
};

/*****************************************************************************/

static void
huawei_signal_changed (MMAtSerialPort *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemHuawei *self)
{
}

static void
huawei_mode_changed (MMAtSerialPort *port,
                     GMatchInfo *match_info,
                     MMBroadbandModemHuawei *self)
{
}

static void
huawei_status_changed (MMAtSerialPort *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemHuawei *self)
{
}

static void
set_3gpp_unsolicited_events_handlers (MMBroadbandModemHuawei *self,
                                      gboolean enable)
{
    MMAtSerialPort *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable/disable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Signal quality related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->rssi_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_signal_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Access technology related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->mode_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_mode_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Connection status related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->dsflowrpt_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_status_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Other unsolicited events to always ignore */
        if (!enable)
            mm_at_serial_port_add_unsolicited_msg_handler (
                ports[i],
                self->priv->boot_regex,
                NULL, NULL, NULL);
    }
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_huawei_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);
}

/*****************************************************************************/

MMBroadbandModemHuawei *
mm_broadband_modem_huawei_new (const gchar *device,
                               const gchar *driver,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_HUAWEI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_huawei_init (MMBroadbandModemHuawei *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_HUAWEI,
                                              MMBroadbandModemHuaweiPrivate);
    /* Prepare regular expressions to setup */
    self->priv->rssi_regex = g_regex_new ("\\r\\n\\^RSSI:(\\d+)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->mode_regex = g_regex_new ("\\r\\n\\^MODE:(\\d),(\\d)\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->dsflowrpt_regex = g_regex_new ("\\r\\n\\^DSFLOWRPT:(.+)\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->boot_regex = g_regex_new ("\\r\\n\\^BOOT:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (object);

    g_regex_unref (self->priv->rssi_regex);
    g_regex_unref (self->priv->mode_regex);
    g_regex_unref (self->priv->dsflowrpt_regex);
    g_regex_unref (self->priv->boot_regex);

    G_OBJECT_CLASS (mm_broadband_modem_huawei_parent_class)->finalize (object);
}

static void
mm_broadband_modem_huawei_class_init (MMBroadbandModemHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemHuaweiPrivate));

    object_class->finalize = finalize;

    broadband_modem_class->setup_ports = setup_ports;
}
