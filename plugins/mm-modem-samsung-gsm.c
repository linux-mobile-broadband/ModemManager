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
 * Copyright (C) 2011 Samsung Electronics, Inc.,
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-samsung-gsm.h"
#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-gsm-card.h"
#include "mm-log.h"
#include "mm-modem-icera.h"
#include "mm-modem-time.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);
static void modem_simple_init (MMModemSimple *class);
static void modem_gsm_card_init (MMModemGsmCard *class);
static void modem_icera_init (MMModemIcera *icera_class);
static void modem_time_init (MMModemTime *class);

G_DEFINE_TYPE_EXTENDED (MMModemSamsungGsm, mm_modem_samsung_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_ICERA, modem_icera_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_CARD, modem_gsm_card_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_TIME, modem_time_init))

#define MM_MODEM_SAMSUNG_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_SAMSUNG_GSM, MMModemSamsungGsmPrivate))

typedef struct {
    gboolean disposed;
    MMModemIceraPrivate *icera;
} MMModemSamsungGsmPrivate;

#define IPDPADDR_TAG "%IPDPADDR: "


MMModem *
mm_modem_samsung_gsm_new (const char *device,
                         const char *driver,
                         const char *plugin)
{
    MMModem *modem;

    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    modem = MM_MODEM (g_object_new (MM_TYPE_MODEM_SAMSUNG_GSM,
                                    MM_MODEM_MASTER_DEVICE, device,
                                    MM_MODEM_DRIVER, driver,
                                    MM_MODEM_PLUGIN, plugin,
                                    MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_DHCP,
                                    NULL));
    if (modem)
        MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem)->icera = mm_modem_icera_init_private ();

    return modem;
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    mm_modem_icera_get_allowed_mode (MM_MODEM_ICERA (gsm), callback, user_data);
}

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    mm_modem_icera_set_allowed_mode (MM_MODEM_ICERA (gsm), mode, callback, user_data);
}

static void
set_band (MMModemGsmNetwork *modem,
          MMModemGsmBand band,
          MMModemFn callback,
          gpointer user_data)
{
    mm_modem_icera_set_band (MM_MODEM_ICERA (modem), band, callback, user_data);
}

static void
get_band (MMModemGsmNetwork *modem,
          MMModemUIntFn callback,
          gpointer user_data)
{
    mm_modem_icera_get_band (MM_MODEM_ICERA (modem), callback, user_data);
}

static void
reset (MMModem *modem,
       MMModemFn callback,
       gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (port)
        mm_at_serial_port_queue_command (port, "%IRESET", 3, NULL, NULL);

    mm_callback_info_schedule (info);
}

static void
get_unlock_retries (MMModemGsmCard *modem,
                    MMModemArrayFn callback,
                    gpointer user_data)
{
    mm_modem_icera_get_unlock_retries (MM_MODEM_ICERA (modem), callback, user_data);
}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    mm_modem_icera_get_access_technology (MM_MODEM_ICERA (gsm), callback, user_data);
}

/*****************************************************************************/

static void
disable_unsolicited_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)

{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* Ignore all errors */
    mm_callback_info_schedule (info);
}

static void
invoke_call_parent_disable_fn (MMCallbackInfo *info)
{
    /* Note: we won't call the parent disable if info->modem is no longer
     * valid. The invoke is called always once the info gets scheduled, which
     * may happen during removed modem detection. */
    if (info->modem) {
        MMModem *parent_modem_iface;

        parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
        parent_modem_iface->disable (info->modem, (MMModemFn)info->callback, info->user_data);
    }
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMAtSerialPort *primary;
    MMCallbackInfo *info;

    mm_modem_icera_cleanup (MM_MODEM_ICERA (modem));
    mm_modem_icera_change_unsolicited_messages (MM_MODEM_ICERA (modem), FALSE);

    info = mm_callback_info_new_full (modem,
                                      invoke_call_parent_disable_fn,
                                      (GCallback)callback,
                                      user_data);

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_AT_PORT_FLAG_PRIMARY);
    g_assert (primary);

    /*
     * Command to ensure unsolicited message disable completes.
     * Turns the radios off, which seems like a reasonable
     * think to do when disabling.
     */
    mm_at_serial_port_queue_command (primary, "AT+CFUN=4", 5, disable_unsolicited_done, info);
}

static void
init_all_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (!error)
        mm_modem_icera_change_unsolicited_messages (MM_MODEM_ICERA (self), TRUE);

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
}

static void
init2_done (MMAtSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    else {
        /* Finish the initialization */
        mm_at_serial_port_queue_command (port, "E0 V1 X4 &C1", 3, init_all_done, info);
    }
}

static void
init_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    else {
        /* Power up the modem */
        mm_at_serial_port_queue_command (port, "+CMEE=1", 2, NULL, NULL);
        mm_at_serial_port_queue_command (port, "+CFUN=1", 10, init2_done, info);
    }
}

static void
init_reset_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    else
        mm_at_serial_port_queue_command (port, "E0 V1", 3, init_done, info);
}

static void
do_enable (MMGenericGsm *modem, MMModemFn callback, gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    primary = mm_generic_gsm_get_at_port (modem, MM_AT_PORT_FLAG_PRIMARY);
    g_assert (primary);
    mm_at_serial_port_queue_command (primary, "Z", 3, init_reset_done, info);
}

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{
    mm_modem_icera_do_connect (MM_MODEM_ICERA (modem), number, callback, user_data);
}

static void
do_disconnect (MMGenericGsm *gsm,
               gint cid,
               MMModemFn callback,
               gpointer user_data)
{
    mm_modem_icera_do_disconnect (gsm, cid, callback, user_data);
}

static void
get_ip4_config (MMModem *modem,
                MMModemIp4Fn callback,
                gpointer user_data)
{
    mm_modem_icera_get_ip4_config (MM_MODEM_ICERA (modem), callback, user_data);
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMModemSimple *parent_iface;
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Let icera cache user & password */
    mm_modem_icera_simple_connect (MM_MODEM_ICERA (simple), properties);

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);
}

static void
port_grabbed (MMGenericGsm *gsm,
              MMPort *port,
              MMAtPortFlags pflags,
              gpointer user_data)
{
    if (MM_IS_AT_SERIAL_PORT (port)) {
        g_object_set (port,
                      MM_PORT_CARRIER_DETECT, FALSE,
                      MM_SERIAL_PORT_SEND_DELAY, (guint64) 0,
                      NULL);

        /* Add Icera-specific handlers */
        mm_modem_icera_register_unsolicted_handlers (MM_MODEM_ICERA (gsm), MM_AT_SERIAL_PORT (port));
    }
}

static void
poll_timezone_done (MMModemIcera *modem,
                    MMModemIceraTimestamp *timestamp,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gint offset;

    if (error || !timestamp) {
        return;
    }

    mm_info ("setting timezone from local timestamp "
             "%02d/%02d/%02d %02d:%02d:%02d %+02d.",
            timestamp->year, timestamp->month, timestamp->day,
            timestamp->hour, timestamp->minute, timestamp->second,
            timestamp->tz_offset);

    // Offset is in 15-minute intervals, as provided by GSM network
    offset = 15 * timestamp->tz_offset;

    mm_modem_base_set_network_timezone (MM_MODEM_BASE (modem),
                                        &offset, NULL, NULL);

    mm_callback_info_schedule (info);
}

static gboolean
poll_timezone (MMModemTime *self, MMModemFn callback, gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    mm_modem_icera_get_local_timestamp (MM_MODEM_ICERA (self),
                                        poll_timezone_done,
                                        info);
    return TRUE;
}

static MMModemIceraPrivate *
get_icera_private (MMModemIcera *icera)
{
    return MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (icera)->icera;
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->reset = reset;
    modem_class->disable = disable;
    modem_class->connect = do_connect;
    modem_class->get_ip4_config = get_ip4_config;
}

static void
modem_icera_init (MMModemIcera *icera)
{
    icera->get_private = get_icera_private;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->set_band = set_band;
    class->get_band = get_band;
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_unlock_retries = get_unlock_retries;
}

static void
modem_time_init (MMModemTime *class)
{
    class->poll_network_timezone = poll_timezone;
}

static void
mm_modem_samsung_gsm_init (MMModemSamsungGsm *self)
{
}

static void
dispose (GObject *object)
{
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (object);
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);

    if (priv->disposed == FALSE) {
        priv->disposed = TRUE;

        mm_modem_icera_dispose_private (MM_MODEM_ICERA (self));
    }

    G_OBJECT_CLASS (mm_modem_samsung_gsm_parent_class)->dispose (object);
}

static void
mm_modem_samsung_gsm_class_init (MMModemSamsungGsmClass *klass)
{

    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_samsung_gsm_parent_class = g_type_class_peek_parent (klass);

    g_type_class_add_private (object_class, sizeof (MMModemSamsungGsmPrivate));

    object_class->dispose = dispose;

    gsm_class->port_grabbed = port_grabbed;
    gsm_class->do_disconnect = do_disconnect;
    gsm_class->do_enable = do_enable;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}
