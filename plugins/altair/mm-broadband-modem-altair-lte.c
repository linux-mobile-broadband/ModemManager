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
 * Copyright (C) 2013 Altair Semiconductor
 *
 * Author: Ori Inbar <ori.inbar@altair-semi.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-altair-lte.h"
#include "mm-broadband-modem-altair-lte.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-messaging.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-serial-parsers.h"
#include "mm-bearer-list.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemAltairLte, mm_broadband_modem_altair_lte, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init));

struct _MMBroadbandModemAltairLtePrivate {
    /* Regex for bearer related notifications */
    GRegex *statcm_regex;
};

static MMIfaceModem3gpp *iface_modem_3gpp_parent;


/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              6,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBearer *bearer;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    return g_object_ref (bearer);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_altair_lte_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    /* We just create a MMBroadbandBearer */
    mm_broadband_bearer_altair_lte_new (MM_BROADBAND_MODEM_ALTAIR_LTE (self),
                                        properties,
                                        NULL, /* cancellable */
                                        (GAsyncReadyCallback)broadband_bearer_new_ready,
                                        result);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    return (MMUnlockRetries *) g_object_ref (g_simple_async_result_get_op_res_gpointer (
                                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_unlock_retries_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    gint pin1, puk1, pin2, puk2;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query unlock retries: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    response = mm_strip_tag (response, "%CPININFO:");
    if (sscanf (response, " %d, %d, %d, %d", &pin1, &puk1, &pin2, &puk2) == 4) {
        MMUnlockRetries *retries;

        retries = mm_unlock_retries_new ();
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, pin1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, puk1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, pin2);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, puk2);
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   retries,
                                                   (GDestroyNotify)g_object_unref);
    } else {
        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Invalid unlock retries response: '%s'",
                                         response);
    }
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%CPININFO",
        3,
        FALSE,
        (GAsyncReadyCallback)load_unlock_retries_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_unlock_retries));
}

/*****************************************************************************/
/* Load current capabilities (Modem interface) */

static MMModemCapability
load_current_capabilities_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    MMModemCapability caps;
    gchar *caps_str;

    /* This modem is LTE only.*/
    caps = MM_MODEM_CAPABILITY_LTE;
    caps_str = mm_modem_capability_build_string_from_mask (caps);
    mm_dbg ("Loaded current capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static void
load_current_capabilities (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    mm_dbg ("Loading (Altair LTE) current capabilities...");

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_current_capabilities);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* supported/current Bands helpers*/

static GArray *
parse_bands_response (const gchar *response)
{
    guint32 bandval;
    MMModemBand band;
    gchar **split;
    guint i, num_of_bands;
    GArray *bands;

    split = g_strsplit_set (response, ",", -1);
    if (!split)
        return NULL;

    num_of_bands = g_strv_length (split);

    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), num_of_bands);

    for (i = 0; split[i]; i++) {
        bandval = (guint32)strtoul (split[i], NULL, 10);
        band = MM_MODEM_BAND_EUTRAN_I - 1 + bandval;
        g_array_append_val (bands, band);
    }

    g_strfreev (split);

    return bands;
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static GArray *
load_supported_bands_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                   G_SIMPLE_ASYNC_RESULT (res)));
}

#define BANDCAP_TAG "%BANDCAP:  "

static void
load_supported_bands_done (MMIfaceModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *operation_result)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query supported bands: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete_in_idle (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /*
     * Response is "%BANDCAP: <band>,[<band>...]"
     */
    response = mm_strip_tag (response, BANDCAP_TAG);

    bands = parse_bands_response (response);
    if (!bands) {
        mm_dbg ("Failed to parse supported bands response");
        g_simple_async_result_set_error (
            operation_result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse supported bands response");
        g_simple_async_result_complete_in_idle (operation_result);
        g_object_unref (operation_result);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (operation_result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete_in_idle (operation_result);
    g_object_unref (operation_result);
}

static void
load_supported_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_bands);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%BANDCAP=",
        3,
        FALSE,
        (GAsyncReadyCallback)load_supported_bands_done,
        result);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                   G_SIMPLE_ASYNC_RESULT (res)));
}

#define CFGBANDS_TAG "Bands:  "

static void
load_current_bands_done (MMIfaceModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *operation_result)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query current bands: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete_in_idle (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /*
     * Response is "Bands: <band>,[<band>...]"
     */
    response = mm_strip_tag (response, CFGBANDS_TAG);

    bands = parse_bands_response (response);
    if (!bands) {
        mm_dbg ("Failed to parse current bands response");
        g_simple_async_result_set_error (
            operation_result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse current bands response");
        g_simple_async_result_complete_in_idle (operation_result);
        g_object_unref (operation_result);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (operation_result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete_in_idle (operation_result);
    g_object_unref (operation_result);
}

static void
load_current_bands (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_current_bands);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%GETCFG=\"BAND\"",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_done,
        result);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
reset_finish (MMIfaceModem *self,
              GAsyncResult *res,
              GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
reset (MMIfaceModem *self,
       GAsyncReadyCallback callback,
       gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "ATZ",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

static void
modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                const gchar *operator_id,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    if (operator_id) {
        /* Currently only VZW is supported */
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Setting a specific operator Id is not supported");
        return;
    }

    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   mm_base_modem_peek_best_at_port (MM_BASE_MODEM (self), NULL),
                                   "%CMATT=1",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   cancellable,
                                   callback,
                                   user_data);
}

static gboolean
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !!mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, error);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

typedef enum {
    MM_STATCM_ALTAIR_LTE_DEREGISTERED = 0,
    MM_STATCM_ALTAIR_LTE_REGISTERED = 1,
    MM_STATCM_ALTAIR_PDN_CONNECTED = 3,
    MM_STATCM_ALTAIR_PDN_DISCONNECTED = 4,
} MMStatcmAltair;

static void
bearer_list_report_disconnect_status_foreach (MMBearer *bearer,
                                              gpointer *user_data)
{
    mm_bearer_report_disconnection (bearer);
}

static void
altair_statcm_changed (MMAtSerialPort *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemAltairLte *self)
{
    gint pdn_event = 0;
    MMBearerList *list = NULL;

    mm_get_int_from_match_info (match_info, 1, &pdn_event);

    mm_dbg ("altair_statcm_changed %d", pdn_event);

    /* Currently we only care about bearer disconnection */

    if (pdn_event == MM_STATCM_ALTAIR_PDN_DISCONNECTED) {
        /* If empty bearer list, nothing else to do */
        g_object_get (self,
                      MM_IFACE_MODEM_BEARER_LIST, &list,
                      NULL);
        if (!list)
            return;

        mm_bearer_list_foreach (list,
                                (MMBearerListForeachFunc)bearer_list_report_disconnect_status_foreach,
                                NULL);

        g_object_unref (list);
    }

}

static void
set_3gpp_unsolicited_events_handlers (MMBroadbandModemAltairLte *self,
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

        /* bearer mode related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->statcm_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)altair_statcm_changed : NULL,
            enable ? self : NULL,
            NULL);
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
parent_3gpp_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                            GAsyncResult *res,
                                            GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else {
        /* Our own setup now */
        set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_ALTAIR_LTE (self), TRUE);
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
        (GAsyncReadyCallback)parent_3gpp_setup_unsolicited_events_ready,
        result);
}

static void
parent_3gpp_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
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
    set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_ALTAIR_LTE (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_3gpp_cleanup_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
own_enable_unsolicited_events_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Our own enable now */
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        "%STATCM=1",
        6,
        FALSE, /* allow_cached */
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_enable_unsolicited_events);

    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Disabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
own_disable_unsolicited_events_ready (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Next, chain up parent's disable */
    iface_modem_3gpp_parent->disable_unsolicited_events (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_disable_unsolicited_events);

    /* Our own disable first */
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        "%STATCM=0",
        5,
        FALSE, /* allow_cached */
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Operator Code loading (3GPP interface) */

static gchar *
modem_3gpp_load_operator_code_finish (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    const gchar *result;
    gchar *operator_code;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    operator_code = mm_3gpp_parse_operator (result, MM_MODEM_CHARSET_UNKNOWN);
    if (operator_code)
        mm_dbg ("loaded Operator Code: %s", operator_code);

    return operator_code;
}

static void
modem_3gpp_load_operator_code (MMIfaceModem3gpp *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    mm_dbg ("loading Operator Code...");

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+COPS=3,2",
                              6,
                              FALSE,
                              NULL,
                              NULL);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+COPS?",
                              6,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Operator Name loading (3GPP interface) */

static gchar *
modem_3gpp_load_operator_name_finish (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    const gchar *result;
    gchar *operator_name;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    operator_name = mm_3gpp_parse_operator (result, MM_MODEM_CHARSET_UNKNOWN);
    if (operator_name)
        mm_dbg ("loaded Operator Name: %s", operator_name);

    return operator_name;
}

static void
modem_3gpp_load_operator_name (MMIfaceModem3gpp *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    mm_dbg ("Loading Operator Name...");

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+COPS=3,0",
                              6,
                              FALSE,
                              NULL,
                              NULL);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+COPS?",
                              6,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Generic ports open/close context */

static const gchar *primary_init_sequence[] = {
    /* Extended numeric codes */
    "+CMEE=1",
    NULL
};


static void
setup_ports (MMBroadbandModem *self)
{
    MMAtSerialPort *primary;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_altair_lte_parent_class)->setup_ports (self);

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary)
        return;

    g_object_set (primary,
                  MM_SERIAL_PORT_SEND_DELAY, (guint64) 0,
                  MM_AT_SERIAL_PORT_SEND_LF, TRUE,
                  MM_AT_SERIAL_PORT_INIT_SEQUENCE, primary_init_sequence,
                  NULL);
}

/*****************************************************************************/

MMBroadbandModemAltairLte *
mm_broadband_modem_altair_lte_new (const gchar *device,
                                   const gchar **drivers,
                                   const gchar *plugin,
                                   guint16 vendor_id,
                                   guint16 product_id)
{

    return g_object_new (MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Since this is an LTE-only modem - don't bother query
                          * anything else */
                         MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_altair_lte_init (MMBroadbandModemAltairLte *self)
{

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE,
                                              MMBroadbandModemAltairLtePrivate);

    self->priv->statcm_regex = g_regex_new ("\\r\\n\\%STATCM:\\s*(\\d*),?(\\d*)\\r+\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    iface->load_current_capabilities = load_current_capabilities;
    iface->load_current_capabilities_finish = load_current_capabilities_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;

    iface->load_access_technologies = NULL;
    iface->load_access_technologies_finish = NULL;

    iface->reset = reset;
    iface->reset_finish = reset_finish;

    iface->load_supported_charsets = NULL;
    iface->load_supported_charsets_finish = NULL;
    iface->setup_charset = NULL;
    iface->setup_charset_finish = NULL;
    iface->setup_flow_control = NULL;
    iface->setup_flow_control_finish = NULL;

}

static void
iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface)
{
    /* we don't have USSD support */
    iface->check_support = NULL;
    iface->check_support_finish = NULL;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{

    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_disable_unsolicited_events_finish;

    iface->register_in_network = modem_3gpp_register_in_network;
    iface->register_in_network_finish = modem_3gpp_register_in_network_finish;

    /* Scanning is not currently supported */
    iface->scan_networks = NULL;
    iface->scan_networks_finish = NULL;

    /* Additional actions */
    iface->load_operator_code = modem_3gpp_load_operator_code;
    iface->load_operator_code_finish = modem_3gpp_load_operator_code_finish;
    iface->load_operator_name = modem_3gpp_load_operator_name;
    iface->load_operator_name_finish = modem_3gpp_load_operator_name_finish;

}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    /* Currently no messaging is implemented - so skip checking*/
    iface->check_support = NULL;
    iface->check_support_finish = NULL;
}

static void
mm_broadband_modem_altair_lte_class_init (MMBroadbandModemAltairLteClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemAltairLtePrivate));

    broadband_modem_class->setup_ports = setup_ports;

    /* The Altair LTE modem reboots itself upon receiving an ATZ command. We
     * need to skip the default implementation in MMBroadbandModem to prevent
     * an ATZ command from being issued as part of the modem initialization
     * sequence when enabling the modem. */
    broadband_modem_class->enabling_modem_init = NULL;
    broadband_modem_class->enabling_modem_init_finish = NULL;
}
