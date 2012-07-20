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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
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
#include "mm-modem-helpers.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-broadband-modem-zte.h"

static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemZte, mm_broadband_modem_zte, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

struct _MMBroadbandModemZtePrivate {
    /* Regex for access-technology related notifications */
    GRegex *zpasr_regex;

    /* Requests to always ignore */
    GRegex *zusimr_regex; /* SMS related */
    GRegex *zdonr_regex; /* Unsolicited operator display */
    GRegex *zpstm_regex; /* SIM request to Build Main Menu */
    GRegex *zend_regex;  /* SIM request to Rebuild Main Menu */
};

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static void
zpasr_received (MMAtSerialPort *port,
                GMatchInfo *info,
                MMBroadbandModemZte *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gchar *str;

    str = g_match_info_fetch (info, 1);
    if (str) {
        act = mm_string_to_access_tech (str);
        g_free (str);
    }

    mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                               act,
                                               MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
}

static void
set_unsolicited_events_handlers (MMBroadbandModemZte *self,
                                 gboolean enable)
{
    MMAtSerialPort *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Access technology related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->zpasr_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)zpasr_received : NULL,
            enable ? self : NULL,
            NULL);

        /* Other unsolicited events to always ignore */
        if (!enable) {
            mm_at_serial_port_add_unsolicited_msg_handler (
                ports[i],
                self->priv->zusimr_regex,
                NULL, NULL, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (
                ports[i],
                self->priv->zdonr_regex,
                NULL, NULL, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (
                ports[i],
                self->priv->zpstm_regex,
                NULL, NULL, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (
                ports[i],
                self->priv->zend_regex,
                NULL, NULL, NULL);
        }
    }
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else {
        /* Our own setup now */
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ZTE (self), TRUE);
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_setup_unsolicited_events);

    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_setup_unsolicited_events_ready,
        result);
}

static void
parent_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_cleanup_unsolicited_events);

    /* Our own cleanup first */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ZTE (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_zte_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ZTE (self), FALSE);
}

/*****************************************************************************/

MMBroadbandModemZte *
mm_broadband_modem_zte_new (const gchar *device,
                              const gchar *driver,
                              const gchar *plugin,
                              guint16 vendor_id,
                              guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_ZTE,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_zte_init (MMBroadbandModemZte *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_ZTE,
                                              MMBroadbandModemZtePrivate);

    /* Prepare regular expressions to setup */
    self->priv->zusimr_regex = g_regex_new ("\\r\\n\\+ZUSIMR:(.*)\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->zdonr_regex = g_regex_new ("\\r\\n\\+ZDONR: (.*)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->zpasr_regex = g_regex_new ("\\r\\n\\+ZPASR:\\s*(.*)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->zpstm_regex = g_regex_new ("\\r\\n\\+ZPSTM: (.*)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->zend_regex = g_regex_new ("\\r\\n\\+ZEND\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemZte *self = MM_BROADBAND_MODEM_ZTE (object);

    g_regex_unref (self->priv->zusimr_regex);
    g_regex_unref (self->priv->zdonr_regex);
    g_regex_unref (self->priv->zpasr_regex);
    g_regex_unref (self->priv->zpstm_regex);
    g_regex_unref (self->priv->zend_regex);

    G_OBJECT_CLASS (mm_broadband_modem_zte_parent_class)->finalize (object);
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
}

static void
mm_broadband_modem_zte_class_init (MMBroadbandModemZteClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemZtePrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
