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

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-modem-huawei-gsm.h"
#include "mm-modem-gsm-network.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-port.h"
#include "mm-serial-parsers.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);

G_DEFINE_TYPE_EXTENDED (MMModemHuaweiGsm, mm_modem_huawei_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init))


#define MM_MODEM_HUAWEI_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_HUAWEI_GSM, MMModemHuaweiGsmPrivate))

typedef struct {
    /* Cached state */
    guint signal_quality;
    MMModemGsmMode mode;
    MMModemGsmBand band;
} MMModemHuaweiGsmPrivate;

MMModem *
mm_modem_huawei_gsm_new (const char *device,
                         const char *driver,
                         const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_HUAWEI_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

static gboolean
parse_syscfg (MMModemHuaweiGsm *self,
              const char *reply,
              int *mode_a,
              int *mode_b,
              guint32 *band,
              int *unknown1,
              int *unknown2)
{
    if (reply == NULL || strncmp (reply, "^SYSCFG:", 8))
        return FALSE;

    if (sscanf (reply + 8, "%d,%d,%x,%d,%d", mode_a, mode_b, band, unknown1, unknown2)) {
        MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (self);
                
        /* Network mode */
        if (*mode_a == 2 && *mode_b == 1)
            priv->mode = MM_MODEM_GSM_MODE_2G_PREFERRED;
        else if (*mode_a == 2 && *mode_b == 2)
            priv->mode = MM_MODEM_GSM_MODE_3G_PREFERRED;
        else if (*mode_a == 13 && *mode_b == 1)
            priv->mode = MM_MODEM_GSM_MODE_2G_ONLY;
        else if (*mode_a == 14 && *mode_b == 2)
            priv->mode = MM_MODEM_GSM_MODE_3G_ONLY;

        /* Band */
        if (*band == 0x3FFFFFFF)
            priv->band = MM_MODEM_GSM_BAND_ANY;
        else if (*band == 0x400380)
            priv->band = MM_MODEM_GSM_BAND_DCS;
        else if (*band == 0x200000)
            priv->band = MM_MODEM_GSM_BAND_PCS;

        return TRUE;
    }

    return FALSE;
}

static void
set_network_mode_done (MMSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHuaweiGsm *self = MM_MODEM_HUAWEI_GSM (info->modem);
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (self);

    if (error)
        info->error = g_error_copy (error);
    else
        /* Success, cache the value */
        priv->mode = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "mode"));

    mm_callback_info_schedule (info);
}

static void
set_network_mode_get_done (MMSerialPort *port,
                           GString *response,
                           GError *error,
                           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        int a, b, u1, u2;
        guint32 band;

        if (parse_syscfg (MM_MODEM_HUAWEI_GSM (info->modem), response->str, &a, &b, &band, &u1, &u2)) {
            char *command;

            switch (GPOINTER_TO_UINT (mm_callback_info_get_data (info, "mode"))) {
            case MM_MODEM_GSM_MODE_ANY:
                a = 2;
                b = 0;
                break;
            case MM_MODEM_GSM_MODE_GPRS:
            case MM_MODEM_GSM_MODE_EDGE:
            case MM_MODEM_GSM_MODE_2G_ONLY:
                a = 13;
                b = 1;
                break;
            case MM_MODEM_GSM_MODE_UMTS:
            case MM_MODEM_GSM_MODE_HSDPA:
            case MM_MODEM_GSM_MODE_HSUPA:
            case MM_MODEM_GSM_MODE_HSPA:
            case MM_MODEM_GSM_MODE_3G_ONLY:
                a = 14;
                b = 2;
                break;
            case MM_MODEM_GSM_MODE_2G_PREFERRED:
                a = 2;
                b = 1;
                break;
            case MM_MODEM_GSM_MODE_3G_PREFERRED:
                a = 2;
                b = 2;
                break;
            default:
                break;
            }

            command = g_strdup_printf ("AT^SYSCFG=%d,%d,%X,%d,%d", a, b, band, u1, u2);
            mm_serial_port_queue_command (port, command, 3, set_network_mode_done, info);
            g_free (command);
        }
    }
}

static void
set_network_mode (MMModemGsmNetwork *modem,
                  MMModemGsmMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    switch (mode) {
    case MM_MODEM_GSM_MODE_ANY:
    case MM_MODEM_GSM_MODE_GPRS:
    case MM_MODEM_GSM_MODE_EDGE:
    case MM_MODEM_GSM_MODE_UMTS:
    case MM_MODEM_GSM_MODE_HSDPA:
    case MM_MODEM_GSM_MODE_HSUPA:
    case MM_MODEM_GSM_MODE_HSPA:
    case MM_MODEM_GSM_MODE_2G_PREFERRED:
    case MM_MODEM_GSM_MODE_3G_PREFERRED:
    case MM_MODEM_GSM_MODE_2G_ONLY:
    case MM_MODEM_GSM_MODE_3G_ONLY:
        /* Allowed values */
        mm_callback_info_set_data (info, "mode", GUINT_TO_POINTER (mode), NULL);
        primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
        g_assert (primary);
        mm_serial_port_queue_command (primary, "AT^SYSCFG?", 3, set_network_mode_get_done, info);
        return;
    default:
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid mode.");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
get_network_mode_done (MMSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHuaweiGsm *self = MM_MODEM_HUAWEI_GSM (info->modem);
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (self);
    int mode_a, mode_b, u1, u2;
    guint32 band;

    if (error)
        info->error = g_error_copy (error);
    else if (parse_syscfg (self, response->str, &mode_a, &mode_b, &band, &u1, &u2))
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->mode), NULL);

    mm_callback_info_schedule (info);
}

static void
get_network_mode (MMModemGsmNetwork *modem,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (modem);

    if (priv->mode != MM_MODEM_GSM_MODE_ANY) {
        /* have cached mode (from an unsolicited message). Use that */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->mode), NULL);
        mm_callback_info_schedule (info);
    } else {
        /* Get it from modem */
        MMCallbackInfo *info;
        MMSerialPort *primary;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
        g_assert (primary);
        mm_serial_port_queue_command (primary, "AT^SYSCFG?", 3, get_network_mode_done, info);
    }
}

static void
set_band_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHuaweiGsm *self = MM_MODEM_HUAWEI_GSM (info->modem);
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (self);

    if (error)
        info->error = g_error_copy (error);
    else
        /* Success, cache the value */
        priv->band = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "band"));

    mm_callback_info_schedule (info);
}

static void
set_band_get_done (MMSerialPort *port,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHuaweiGsm *self = MM_MODEM_HUAWEI_GSM (info->modem);

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        int a, b, u1, u2;
        guint32 band;

        if (parse_syscfg (self, response->str, &a, &b, &band, &u1, &u2)) {
            char *command;

            switch (GPOINTER_TO_UINT (mm_callback_info_get_data (info, "band"))) {
            case MM_MODEM_GSM_BAND_ANY:
                band = 0x3FFFFFFF;
                break;
            case MM_MODEM_GSM_BAND_EGSM:
                band = 0x100;
                break;
            case MM_MODEM_GSM_BAND_DCS:
                band = 0x80;
                break;
            case MM_MODEM_GSM_BAND_U2100:
                band = 0x400000;
                break;
            case MM_MODEM_GSM_BAND_PCS:
                band = 0x200000;
                break;
            case MM_MODEM_GSM_BAND_G850:
                band = 0x80000;
                break;
            default:
                break;
            }

            command = g_strdup_printf ("AT^SYSCFG=%d,%d,%X,%d,%d", a, b, band, u1, u2);
            mm_serial_port_queue_command (port, command, 3, set_band_done, info);
            g_free (command);
        }
    }
}

static void
set_band (MMModemGsmNetwork *modem,
          MMModemGsmBand band,
          MMModemFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    switch (band) {
    case MM_MODEM_GSM_BAND_ANY:
    case MM_MODEM_GSM_BAND_EGSM:
    case MM_MODEM_GSM_BAND_DCS:
    case MM_MODEM_GSM_BAND_U2100:
    case MM_MODEM_GSM_BAND_PCS:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (band), NULL);
        primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
        g_assert (primary);
        mm_serial_port_queue_command (primary, "AT^SYSCFG?", 3, set_band_get_done, info);
        return;
    default:
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid band.");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
get_band_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHuaweiGsm *self = MM_MODEM_HUAWEI_GSM (info->modem);
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (self);
    int mode_a, mode_b, u1, u2;
    guint32 band;

    if (error)
        info->error = g_error_copy (error);
    else if (parse_syscfg (self, response->str, &mode_a, &mode_b, &band, &u1, &u2))
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->band), NULL);

    mm_callback_info_schedule (info);
}

static void
get_band (MMModemGsmNetwork *modem,
          MMModemUIntFn callback,
          gpointer user_data)
{
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (modem);
    MMSerialPort *primary;

    if (priv->band != MM_MODEM_GSM_BAND_ANY) {
        /* have cached mode (from an unsolicited message). Use that */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->band), NULL);
        mm_callback_info_schedule (info);
    } else {
        /* Get it from modem */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
        g_assert (primary);
        mm_serial_port_queue_command (primary, "AT^SYSCFG?", 3, get_band_done, info);
    }
}

static void
get_signal_quality (MMModemGsmNetwork *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (modem);

    if (priv->signal_quality) {
        /* have cached signal quality (from an unsolicited message). Use that */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->signal_quality), NULL);
        mm_callback_info_schedule (info);
    } else {
        /* Use the generic implementation */
        MMModemGsmNetwork *parent_gsm_network_iface;

        parent_gsm_network_iface = g_type_interface_peek_parent (MM_MODEM_GSM_NETWORK_GET_INTERFACE (modem));
        parent_gsm_network_iface->get_signal_quality (modem, callback, user_data);
    }
}

/* Unsolicited message handlers */

static void
handle_signal_quality_change (MMSerialPort *port,
                              GMatchInfo *match_info,
                              gpointer user_data)
{
    MMModemHuaweiGsm *self = MM_MODEM_HUAWEI_GSM (user_data);
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (self);
    char *str;
    int quality;

    str = g_match_info_fetch (match_info, 1);
    quality = atoi (str);
    g_free (str);

    if (quality == 99)
        /* 99 means unknown */
        quality = 0;
    else
        /* Normalize the quality */
        quality = quality * 100 / 31;

    g_debug ("Signal quality: %d", quality);
    priv->signal_quality = (guint32) quality;
    mm_modem_gsm_network_signal_quality (MM_MODEM_GSM_NETWORK (self), (guint32) quality);
}

static void
handle_mode_change (MMSerialPort *port,
                    GMatchInfo *match_info,
                    gpointer user_data)
{
    MMModemHuaweiGsm *self = MM_MODEM_HUAWEI_GSM (user_data);
    MMModemHuaweiGsmPrivate *priv = MM_MODEM_HUAWEI_GSM_GET_PRIVATE (self);
    char *str;
    int a;
    int b;

    str = g_match_info_fetch (match_info, 1);
    a = atoi (str);
    g_free (str);

    str = g_match_info_fetch (match_info, 2);
    b = atoi (str);
    g_free (str);

    if (a == 3 && b == 2)
        priv->mode = MM_MODEM_GSM_MODE_GPRS;
    else if (a == 3 && b == 3)
        priv->mode = MM_MODEM_GSM_MODE_EDGE;
    else if (a == 5 && b == 4)
        priv->mode = MM_MODEM_GSM_MODE_UMTS;
    else if (a == 5 && b == 5)
        priv->mode = MM_MODEM_GSM_MODE_HSDPA;
    else if (a == 5 && b == 6)
        priv->mode = MM_MODEM_GSM_MODE_HSUPA;
    else if (a == 5 && b == 7)
        priv->mode = MM_MODEM_GSM_MODE_HSPA;
    else {
        g_warning ("Couldn't parse mode change value: '%s'", str);
        return;
    }

    g_debug ("Mode: %d", priv->mode);
    mm_modem_gsm_network_mode (MM_MODEM_GSM_NETWORK (self), priv->mode);
}

static void
handle_status_change (MMSerialPort *port,
                      GMatchInfo *match_info,
                      gpointer user_data)
{
    char *str;
    int n1, n2, n3, n4, n5, n6, n7;

    str = g_match_info_fetch (match_info, 1);
    if (sscanf (str, "%x,%x,%x,%x,%x,%x,%x", &n1, &n2, &n3, &n4, &n5, &n6, &n7)) {
        g_debug ("Duration: %d Up: %d Kbps Down: %d Kbps Total: %d Total: %d\n",
                 n1, n2 * 8 / 1000, n3  * 8 / 1000, n4 / 1024, n5 / 1024);
    }
    g_free (str);
}

/*****************************************************************************/

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
    const char *sys[] = { "tty", NULL };
    GUdevClient *client;
    GUdevDevice *device = NULL;
    MMPort *port = NULL;
    int usbif;

    client = g_udev_client_new (sys);
    if (!client) {
        g_set_error (error, 0, 0, "Could not get udev client.");
        return FALSE;
    }

    device = g_udev_client_query_by_subsystem_and_name (client, subsys, name);
    if (!device) {
        g_set_error (error, 0, 0, "Could not get udev device.");
        goto out;
    }

    usbif = g_udev_device_get_property_as_int (device, "ID_USB_INTERFACE_NUM");
    if (usbif < 0) {
        g_set_error (error, 0, 0, "Could not get USB device interface number.");
        goto out;
    }

    if (usbif == 0) {
        if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_PRIMARY))
            ptype = MM_PORT_TYPE_PRIMARY;
    } else if (suggested_type == MM_PORT_TYPE_SECONDARY) {
        if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    }

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);

    if (port && MM_IS_SERIAL_PORT (port)) {
        g_object_set (G_OBJECT (port), MM_PORT_CARRIER_DETECT, FALSE, NULL);
        if (ptype == MM_PORT_TYPE_SECONDARY) {
            GRegex *regex;

            mm_generic_gsm_set_unsolicited_registration (MM_GENERIC_GSM (modem), TRUE);

            regex = g_regex_new ("\\r\\n\\^RSSI:(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, handle_signal_quality_change, modem, NULL);
            g_regex_unref (regex);

            regex = g_regex_new ("\\r\\n\\^MODE:(\\d),(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, handle_mode_change, modem, NULL);
            g_regex_unref (regex);

            regex = g_regex_new ("\\r\\n\\^DSFLOWRPT:(.+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, handle_status_change, modem, NULL);
            g_regex_unref (regex);

            regex = g_regex_new ("\\r\\n\\^BOOT:.+\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, modem, NULL);
            g_regex_unref (regex);
        }
    }

out:
    if (device)
        g_object_unref (device);
    g_object_unref (client);
    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->set_network_mode = set_network_mode;
    class->get_network_mode = get_network_mode;
    class->set_band = set_band;
    class->get_band = get_band;
    class->get_signal_quality = get_signal_quality;
}

static void
mm_modem_huawei_gsm_init (MMModemHuaweiGsm *self)
{
}

static void
mm_modem_huawei_gsm_class_init (MMModemHuaweiGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_modem_huawei_gsm_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemHuaweiGsmPrivate));
}

