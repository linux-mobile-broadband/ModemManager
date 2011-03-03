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
 * Copyright 2011 by Samsung Electronics, Inc.,
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

static void modem_init (MMModem *modem_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);
static void modem_simple_init (MMModemSimple *class);


G_DEFINE_TYPE_EXTENDED (MMModemSamsungGsm, mm_modem_samsung_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init))

#define MM_MODEM_SAMSUNG_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_SAMSUNG_GSM, MMModemSamsungGsmPrivate))

#define SAMSUNG_IPDPACT_DISCONNECTED 0
#define SAMSUNG_IPDPACT_CONNECTED    1
#define SAMSUNG_IPDPACT_CONNECTING   2
#define SAMSUNG_IPDPACT_CONNECTED_FAILED    3

typedef struct {
    char * band;
    MMCallbackInfo *connect_pending_data;
    gboolean init_retried;

    char *username;
    char *password;

    MMModemGsmAccessTech last_act;

} MMModemSamsungGsmPrivate;

#define IPDPADDR_TAG "%IPDPADDR: "


MMModem *
mm_modem_samsung_gsm_new (const char *device,
                         const char *driver,
                         const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_SAMSUNG_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_DHCP,
                                   NULL));
}

static void
connect_pending_done (MMModemSamsungGsm *self)
{
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);
    GError *error = NULL;

    if (priv->connect_pending_data) {
        if (priv->connect_pending_data->error) {
            error = priv->connect_pending_data->error;
            priv->connect_pending_data->error = NULL;
        }

        /* Complete the connect */
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (self), error, priv->connect_pending_data);
        priv->connect_pending_data = NULL;
    }
}

void
mm_modem_samsung_cleanup (MMModemSamsungGsm *self)
{
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);

    /* Clear the pending connection if necessary */
    connect_pending_done (self);
    g_free (priv->username);
    g_free (priv->password);
    memset (priv, 0, sizeof (MMModemSamsungGsmPrivate));
}

void
mm_modem_samsung_change_unsolicited_messages (MMModemSamsungGsm *self, gboolean enabled)
{
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_at_serial_port_queue_command (primary, enabled ? "%NWSTATE=1" : "%NWSTATE=0", 3, NULL, NULL);
}

typedef struct {
    MMModemGsmBand mm;
    char band[50];
} BandTable;

static BandTable bands[12] = {
    /* Sort 3G first since it's preferred */
    { MM_MODEM_GSM_BAND_U2100, "FDD_BAND_I" },
    { MM_MODEM_GSM_BAND_U1900, "FDD_BAND_II" },
    { MM_MODEM_GSM_BAND_U1800, "FDD_BAND_III" },
    { MM_MODEM_GSM_BAND_U17IV, "FDD_BAND_IV" },
    { MM_MODEM_GSM_BAND_U850,  "FDD_BAND_V" },
    { MM_MODEM_GSM_BAND_U800,  "FDD_BAND_VI" },
    { MM_MODEM_GSM_BAND_U900,  "FDD_BAND_VIII" },
    { MM_MODEM_GSM_BAND_G850, "G850" },
    /* 2G second */
    { MM_MODEM_GSM_BAND_DCS,   "DCS" },
    { MM_MODEM_GSM_BAND_EGSM,  "EGSM" }, /* 0x100 = Extended GSM, 0x200 = Primary GSM */
    { MM_MODEM_GSM_BAND_PCS,   "PCS" },
    /* And ANY last since it's most inclusive */
    { MM_MODEM_GSM_BAND_ANY,   "ANY" },
};

static gboolean
band_mm_to_samsung (MMModemGsmBand band, MMModemGsmNetwork *modem)
{
    int i;
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem);

    for (i = 0; i < sizeof (bands) / sizeof (BandTable); i++) {
        if (bands[i].mm == band) {
            priv->band = bands[i].band;
            return TRUE;
        }
    }
    return FALSE;
}

static gint samsung_get_cid (MMModemSamsungGsm *self)
{
    gint cid;

    cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (self));
    if (cid < 0) {
        g_warn_if_fail (cid >= 0);
        cid = 0;
    }

    return cid;
}

static gboolean
parse_ipsys (MMModemSamsungGsm *self,
              const char *reply,
              int *mode,
              int *domain,
              MMModemGsmAllowedMode *out_mode)
{
    if(reply == NULL || !g_str_has_prefix(reply, "%IPSYS:"))
        return FALSE;

    if (sscanf (reply + 7, "%d,%d", mode, domain) == 2) {
        MMModemGsmAllowedMode new_mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;

        /* Network mode */
        if (*mode == 2)
            new_mode = MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED;
        else if (*mode == 3)
            new_mode = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
        else if (*mode == 0)
            new_mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
        else if (*mode == 1)
            new_mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;

        if (out_mode)
            *out_mode = new_mode;

        return TRUE;
    }

    return FALSE;
}


static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);
    int mode, domain;
    MMModemGsmAllowedMode mode_out = MM_MODEM_GSM_ALLOWED_MODE_ANY;

    if (error)
        info->error = g_error_copy (error);
    else if (parse_ipsys (self, response->str, &mode, &domain, &mode_out))
        mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);

    mm_callback_info_schedule (info);
}


static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "AT%IPSYS?", 3, get_allowed_mode_done, info);
}

static void
set_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}


static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    int i;
    char *command;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        i = 0;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        i = 1;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
        i = 2;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
        i = 3;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        i = 5;
        break;
    }

    command = g_strdup_printf ("AT%%IPSYS=%d,3",i);

    mm_at_serial_port_queue_command (port, command, 3, set_allowed_mode_done, info);
    g_free (command);
}

static void
set_band_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
set_band (MMModemGsmNetwork *modem,
          MMModemGsmBand band,
          MMModemFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command;
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem);

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    if (!band_mm_to_samsung (band, modem)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid band.");
        mm_callback_info_schedule (info);
    } else {
        mm_callback_info_set_data (info, "band", g_strdup(priv->band), NULL);
        command = g_strdup_printf ("AT%%IPBM=\"%s\",1", priv->band);
        mm_at_serial_port_queue_command (port, command, 3, set_band_done, info);
        g_free (command);
     priv->band = NULL;
    }
}

static gboolean parse_ipbm(MMModemSamsungGsm *self,
              const char *reply,
              MMModemGsmBand *band)
{
    int enable[12];

    g_assert(band != NULL);

    if (reply == NULL)
        return FALSE;

    if (sscanf (reply, "\"ANY\": %d\r\n\"EGSM\": %d\r\n\"DCS\": %d\r\n\"PCS\": %d\r\n\"G850\": %d\r\n\"FDD_BAND_I\": %d\r\n\"FDD_BAND_II\": %d\r\n\"FDD_BAND_III\": %d\r\n\"FDD_BAND_IV\": %d\r\n\"FDD_BAND_V\": %d\r\n\"FDD_BAND_VI\": %d\r\n\"FDD_BAND_VIII\": %d", &enable[0], &enable[1], &enable[2], &enable[3], &enable[4], &enable[5], &enable[6], &enable[7], &enable[8], &enable[9], &enable[10], &enable[11])== 12) {

    if(enable[5] == 1) {
        *band = MM_MODEM_GSM_BAND_U2100;
        return TRUE;}
    else if(enable[6] == 1) {
        *band = MM_MODEM_GSM_BAND_U1900;
        return TRUE;}
    else if(enable[7] == 1) {
        *band = MM_MODEM_GSM_BAND_U1800;
        return TRUE;}
    else if(enable[8] == 1) {
        *band = MM_MODEM_GSM_BAND_U17IV;
        return TRUE;}
    else if(enable[9] == 1) {
        *band = MM_MODEM_GSM_BAND_U850;
        return TRUE;}
    else if(enable[10] == 1) {
        *band = MM_MODEM_GSM_BAND_U800;
        return TRUE;}
    else if(enable[11] == 1) {
        *band = MM_MODEM_GSM_BAND_U900;
        return TRUE;}
    else if(enable[1] == 1) {
        *band = MM_MODEM_GSM_BAND_EGSM;
        return TRUE;}
    else if(enable[2] == 1) {
        *band = MM_MODEM_GSM_BAND_DCS;
        return TRUE;}
    else if(enable[3] == 1) {
        *band = MM_MODEM_GSM_BAND_PCS;
        return TRUE;}
    else if(enable[4] == 1) {
        *band = MM_MODEM_GSM_BAND_G850;
        return TRUE;}
    else{
        *band = MM_MODEM_GSM_BAND_ANY;}

        return TRUE;
    }

    return FALSE;
}

static void
get_band_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);
    MMModemGsmBand mm_band = MM_MODEM_GSM_BAND_ANY;

    if (error)
        info->error = g_error_copy (error);
    else if (parse_ipbm (self, response->str, &mm_band)) {

        mm_callback_info_set_result (info, GUINT_TO_POINTER (mm_band), NULL);
    }

    mm_callback_info_schedule (info);
}

static void
get_band (MMModemGsmNetwork *modem,
          MMModemUIntFn callback,
          gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    /* Otherwise ask the modem */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "AT%IPBM?", 3, get_band_done, info);
}

static void
get_nwstate_done (MMAtSerialPort *port,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);
        MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);

        /* The unsolicited message handler will already have run and
         * removed the NWSTATE response, so we have to work around that.
         */
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->last_act), NULL);
        priv->last_act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    }

    mm_callback_info_schedule (info);
}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (gsm);
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "%NWSTATE=1", 3, get_nwstate_done, info);
}

typedef struct {
    MMModem *modem;
    MMModemFn callback;
    gpointer user_data;
} DisableInfo;

static void
disable_unsolicited_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)

{
    MMModem *parent_modem_iface;
    DisableInfo *info = user_data;

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
    parent_modem_iface->disable (info->modem, info->callback, info->user_data);
    g_free (info);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem);
    MMAtSerialPort *primary;
    DisableInfo *info;

    priv->init_retried = FALSE;

    info = g_malloc0 (sizeof (DisableInfo));
    info->callback = callback;
    info->user_data = user_data;
    info->modem = modem;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    /* Turn off unsolicited responses */
    mm_modem_samsung_cleanup (MM_MODEM_SAMSUNG_GSM (modem));
    mm_modem_samsung_change_unsolicited_messages (MM_MODEM_SAMSUNG_GSM (modem), FALSE);

    /* Random command to ensure unsolicited message disable completes */
    mm_at_serial_port_queue_command (primary, "AT+CFUN=0", 5, disable_unsolicited_done, info);
}

static void
init_modem_done (MMAtSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    mm_at_serial_port_queue_command (port, "ATE0;+CFUN=1", 5, NULL, NULL);

    mm_modem_samsung_change_unsolicited_messages (MM_MODEM_SAMSUNG_GSM (info->modem), TRUE);

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}

static void enable_flash_done (MMSerialPort *port,
                               GError *error,
                               gpointer user_data);

static void
pre_init_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);

    if (error) {
        /* Retry the init string one more time; the modem sometimes throws it away */
        if (   !priv->init_retried
            && g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            priv->init_retried = TRUE;
            enable_flash_done (MM_SERIAL_PORT (port), NULL, user_data);
        } else
            mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    } else {
        /* Finish the initialization */
        mm_at_serial_port_queue_command (port, "Z E0 V1 X4 &C1 +CMEE=1;+CFUN=1;", 10, init_modem_done, info);
    }
}

static void
enable_flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
    else
        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "E0 V1", 3, pre_init_done, user_data);
}

static void
do_enable (MMGenericGsm *modem, MMModemFn callback, gpointer user_data)
{
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMAtSerialPort *primary;

    priv->init_retried = FALSE;

    primary = mm_generic_gsm_get_at_port (modem, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_flash (MM_SERIAL_PORT (primary), 100, FALSE, enable_flash_done, info);
}

static void
Samsung_call_control (MMModemSamsungGsm *self,
                    gboolean activate,
                    MMAtSerialResponseFn callback,
                    gpointer user_data)
{
    char *command;
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    command = g_strdup_printf ("%%IPDPACT=%d,%d", samsung_get_cid(self), activate ? 1 : 0);
    mm_at_serial_port_queue_command (primary, command, 3, callback, user_data);
    g_free (command);
}

static void
Samsung_enabled (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), error, info);
    } else {
        MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);
        MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);

        priv->connect_pending_data = info;
    }
}

static void
auth_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), error, info);
    else {
        /* Activate the PDP context and start the data session */
    Samsung_call_control (MM_MODEM_SAMSUNG_GSM (info->modem), TRUE, Samsung_enabled, info);
    }
}

static void
old_context_clear_done (MMAtSerialPort *port,
                        GString *response,
                        GError *error,
                        gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gint cid;
    char *command;
    MMAtSerialPort *primary;

    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);

    cid = samsung_get_cid (self);

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    /* Both user and password are required; otherwise firmware returns an error */
    if (!priv->username || !priv->password)
        command = g_strdup_printf ("%%IPDPCFG=%d,0,0,\"\",\"\"", cid);
    else {
        command = g_strdup_printf ("%%IPDPCFG=%d,0,1,\"%s\",\"%s\"",
                                   cid,
                                   priv->password ? priv->password : "",
                                   priv->username ? priv->username : "");

    }

    mm_at_serial_port_queue_command (primary, command, 3, auth_done, info);
    g_free (command);
}

void
mm_modem_samsung_do_connect (MMModemSamsungGsm *self,
                           const char *number,
                           MMModemFn callback,
                           gpointer user_data)
{
    MMModem *modem = MM_MODEM (self);
    MMCallbackInfo *info;

    mm_modem_set_state (modem, MM_MODEM_STATE_CONNECTING, MM_MODEM_STATE_REASON_NONE);

    info = mm_callback_info_new (modem, callback, user_data);


    /* Ensure the PDP context is deactivated */
    Samsung_call_control (MM_MODEM_SAMSUNG_GSM (info->modem), FALSE, old_context_clear_done, info);

}

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{

    mm_modem_samsung_do_connect (MM_MODEM_SAMSUNG_GSM (modem), number, callback, user_data);

}

static void
do_disconnect (MMGenericGsm *gsm,
               gint cid,
               MMModemFn callback,
               gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;
    char *command;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    command = g_strdup_printf ("AT%%IPDPACT=%d,0", cid);

    mm_at_serial_port_queue_command (primary, command, 3, NULL, NULL);
    g_free (command);

    MM_GENERIC_GSM_CLASS (mm_modem_samsung_gsm_parent_class)->do_disconnect (gsm, cid, callback, user_data);

}

static void
Samsung_disconnect_done (MMModem *modem,
                       GError *error,
                       gpointer user_data)
{
    g_message ("Modem signaled disconnection from the network");
}

static void
connection_enabled (MMAtSerialPort *port,
                    GMatchInfo *match_info,
                    gpointer user_data)
{
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (user_data);
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info = priv->connect_pending_data;
    char *str;
    int status, cid, tmp;

    cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (self));
    if (cid < 0)
        return;

    str = g_match_info_fetch (match_info, 1);
    g_return_if_fail (str != NULL);
    tmp = atoi (str);
    g_free (str);

    /* Make sure the unsolicited message's CID matches the current CID */
    if (tmp != cid)
        return;

    str = g_match_info_fetch (match_info, 2);
    g_return_if_fail (str != NULL);
    status = atoi (str);
    g_free (str);

    switch (status) {
    case 0:
        /* Disconnected */
        if (mm_modem_get_state (MM_MODEM (self)) >= MM_MODEM_STATE_CONNECTED)
            mm_modem_disconnect (MM_MODEM (self), Samsung_disconnect_done, NULL);
        break;
    case 1:
        /* Connected */
        connect_pending_done (self);
        break;
    case 2:
        /* Connecting */
        break;
    case 3:
        /* Call setup failure? */
        if (info) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_GENERAL,
                                               "Call setup failed");
        }
        connect_pending_done (self);
        break;
    default:
        g_warning ("Unknown Samsung connect status %d", status);
        break;
    }
}

static void
handle_mode_change (MMAtSerialPort *port,
                    GMatchInfo *match_info,
                    gpointer user_data)
{
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (user_data);
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;
    int rssi = -1;

    str = g_match_info_fetch (match_info, 1);
    if (str) {
        rssi = atoi (str);
        rssi = CLAMP (rssi, -1, 5);
        g_free (str);
    }

    str = g_match_info_fetch (match_info, 3);

     /* Better technologies are listed first since modems sometimes say
     * stuff like "GPRS/EDGE" and that should be handled as EDGE.
     */
    g_debug ("Access Technology: %s", str);
    if (strcmp (str, "3G-HSDPA-HSUPA")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_HSPA;
    else if (strcmp (str, "3G-HSUPA")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_HSUPA;
    else if (strcmp (str, "3G-HSDPA")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
    else if (strcmp (str, "3G")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
    else if (strcmp (str, "3g")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
    else if (strcmp (str, "2G-EDGE")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_EDGE;
    else if (strcmp (str, "2G-GPRS")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_GPRS;
    else if (strcmp (str, "2g")==0)
        act = MM_MODEM_GSM_ACCESS_TECH_GSM;
    else
        act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    g_free (str);

    g_debug ("Access Technology: %d", act);
    MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self)->last_act = act;
    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (self), act);
}

static void
free_dns_array (gpointer data)
{
    g_array_free ((GArray *) data, TRUE);
}

static void
ip4_config_invoke (MMCallbackInfo *info)
{
    MMModemIp4Fn callback = (MMModemIp4Fn) info->callback;

    callback (info->modem,
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, "ip4-address")),
              (GArray *) mm_callback_info_get_data (info, "ip4-dns"),
              info->error, info->user_data);
}

static void
get_ip4_config_done (MMAtSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char **items, **iter;
    GArray *dns_array;
    int i;
    guint32 tmp;
    gint cid;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    } else if (!g_str_has_prefix (response->str, IPDPADDR_TAG)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Retrieving failed: invalid response.");
        goto out;
    }

    cid = samsung_get_cid (MM_MODEM_SAMSUNG_GSM (info->modem));
    dns_array = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 2);
    items = g_strsplit (response->str + strlen (IPDPADDR_TAG), ", ", 0);

    /* Appending data from at%IPDPADDR command
     * Skipping when i = 2. Gateway address is not what we want
     */
    for (iter = items, i = 0; *iter; iter++, i++) {
        if (i == 0) { /* CID */
            long int num;

            errno = 0;
            num = strtol (*iter, NULL, 10);
            if (errno != 0 || num < 0 || (gint) num != cid) {
                info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Unknown CID in OWANDATA response ("
                                           "got %d, expected %d)", (guint) num, cid);
                break;
            }
        } else if (i == 1) { /* IP address */
            if (inet_pton (AF_INET, *iter, &tmp) > 0)
                mm_callback_info_set_data (info, "ip4-address", GUINT_TO_POINTER (tmp), NULL);
        } else if (i == 3) { /* DNS 1 */
            if (inet_pton (AF_INET, *iter, &tmp) > 0)
                g_array_append_val (dns_array, tmp);
        } else if (i == 4) { /* DNS 2 */
            if (inet_pton (AF_INET, *iter, &tmp) > 0)
                g_array_append_val (dns_array, tmp);
        }
    }

    g_strfreev (items);
    mm_callback_info_set_data (info, "ip4-dns", dns_array, free_dns_array);

 out:
    mm_callback_info_schedule (info);
}


static void
get_ip4_config (MMModem *modem,
                MMModemIp4Fn callback,
                gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *primary;

    info = mm_callback_info_new_full (modem, ip4_config_invoke, G_CALLBACK (callback), user_data);

    command = g_strdup_printf ("AT%%IPDPADDR=%d", samsung_get_cid (MM_MODEM_SAMSUNG_GSM (modem)));

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_at_serial_port_queue_command (primary, command, 3, get_ip4_config_done, info);
    g_free (command);
}

static const char *
get_string_property (GHashTable *properties, const char *name)
{
    GValue *value;

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (value && G_VALUE_HOLDS_STRING (value))
        return g_value_get_string (value);
    return NULL;
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (simple);
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSimple *parent_iface;

    g_free (priv->username);
    priv->username = g_strdup (get_string_property (properties, "username"));
    g_free (priv->password);
    priv->password = g_strdup (get_string_property (properties, "password"));

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);

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
    MMPort *port = NULL;

    if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
    if(g_str_has_prefix(name, "usb"))
        ptype = MM_PORT_TYPE_ECM;
        else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY))
                ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_SECONDARY))
                ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_AT_SERIAL_PORT (port)) {
        GRegex *regex;

        g_object_set (port, MM_PORT_CARRIER_DETECT, FALSE, NULL);


        /* %NWSTATE: <rssi>,<mccmnc>,<tech>,<connected>,<regulation> */
        regex = g_regex_new ("\\r\\n\\%NWSTATE: (\\d),(\\d+),\\s*([^,\\s]*)\\s*,(.+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, handle_mode_change, modem, NULL);
        g_regex_unref (regex);

        /* %IPDPACT: <cid>,<status>,0 */
        regex = g_regex_new ("\\r\\n%IPDPACT:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, connection_enabled, modem, NULL);
        g_regex_unref (regex);
    }

    return !!port;
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->disable = disable;
    modem_class->connect = do_connect;
    modem_class->get_ip4_config = get_ip4_config;

    modem_class->grab_port = grab_port;
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
mm_modem_samsung_gsm_init (MMModemSamsungGsm *self)
{
}

static void
mm_modem_samsung_gsm_class_init (MMModemSamsungGsmClass *klass)
{

    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_samsung_gsm_parent_class = g_type_class_peek_parent (klass);

    g_type_class_add_private (object_class, sizeof (MMModemSamsungGsmPrivate));

    gsm_class->do_disconnect = do_disconnect;
    gsm_class->do_enable = do_enable;

    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}
