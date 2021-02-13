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
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-altair-lte.h"
#include "mm-broadband-modem-altair-lte.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-messaging.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-altair-lte.h"
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
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init))

struct _MMBroadbandModemAltairLtePrivate {
    /* Regex for SIM refresh notifications */
    GRegex *sim_refresh_regex;
    /* Timer that goes off 10s after the last SIM refresh notification.
     * This indicates that there are no more SIM refreshes and we should
     * reregister the device.*/
    guint sim_refresh_timer_id;
    /* Flag indicating that we are detaching from the network to process SIM
     * refresh.  This is used to prevent connect requests while we're in this
     * state.*/
    gboolean sim_refresh_detach_in_progress;
    /* Regex for bearer related notifications */
    GRegex *statcm_regex;
    /* Regex for PCO notifications */
    GRegex *pcoinfo_regex;

    GList *pco_list;
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
                              20,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_altair_lte_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* We just create a MMBroadbandBearer */
    mm_broadband_bearer_altair_lte_new (MM_BROADBAND_MODEM_ALTAIR_LTE (self),
                                        properties,
                                        NULL, /* cancellable */
                                        (GAsyncReadyCallback)broadband_bearer_new_ready,
                                        task);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_unlock_retries_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    gint pin1, puk1, pin2, puk2;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
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
        g_task_return_pointer (task, retries, g_object_unref);
    } else {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Invalid unlock retries response: '%s'",
                                 response);
    }
    g_object_unref (task);
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "%CPININFO",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)load_unlock_retries_ready,
                              task);
}

/*****************************************************************************/
/* Load current capabilities (Modem interface) */

static MMModemCapability
load_current_capabilities_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_CAPABILITY_NONE;
    }
    return (MMModemCapability)value;
}

static void
load_current_capabilities (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    /* This modem is LTE only.*/
    g_task_return_int (task, MM_MODEM_CAPABILITY_LTE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static GArray *
load_supported_bands_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

#define BANDCAP_TAG "%BANDCAP:  "

static void
load_supported_bands_done (MMIfaceModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /*
     * Response is "%BANDCAP: <band>,[<band>...]"
     */
    response = mm_strip_tag (response, BANDCAP_TAG);

    bands = mm_altair_parse_bands_response (response);
    if (!bands) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse supported bands response");
        g_object_unref (task);
        return;
    }

    g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

static void
load_supported_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%BANDCAP=",
        3,
        FALSE,
        (GAsyncReadyCallback)load_supported_bands_done,
        task);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

#define CFGBANDS_TAG "Bands:  "

static void
load_current_bands_done (MMIfaceModem *self,
                         GAsyncResult *res,
                         GTask *task)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /*
     * Response is "Bands: <band>,[<band>...]"
     */
    response = mm_strip_tag (response, CFGBANDS_TAG);

    bands = mm_altair_parse_bands_response (response);
    if (!bands) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse current bands response");
        g_object_unref (task);
        return;
    }

    g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

static void
load_current_bands (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%GETCFG=\"BAND\"",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_done,
        task);
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
/* Run registration checks (3GPP interface) */

static gboolean
modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
simulate_unprovisioned_subscription_pco_update (MMBroadbandModemAltairLte *self)
{
    MMPco *pco;

    /* Simulate a %PCOINFO notification issued to the IMS PDN that indicates an
     * unprovisioned Verizon SIM. See mm_altair_parse_vendor_pco_info() for the
     * detailed format of a %PCOINFO response.
     *
     * 1,FF00,13018405 is constructed as follows:
     *
     *     1: CID for IMS PDN
     *     FF 00: Container ID for the Verizon-specific PCO content
     *     13 01 84: Binary coded decimal representation of Verizon MCC/MNC 311/084
     *     05: Value indicating an unprovisioned SIM
     */
    pco = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,FF00,13018405", NULL);
    g_assert (pco != NULL);
    self->priv->pco_list = mm_pco_list_add (self->priv->pco_list, pco);
    mm_iface_modem_3gpp_update_pco_list (MM_IFACE_MODEM_3GPP (self), self->priv->pco_list);
    g_object_unref (pco);
}

static void
run_registration_checks_subscription_state_ready (MMIfaceModem3gpp *self,
                                                  GAsyncResult *res,
                                                  GTask *task)
{
    GError *error = NULL;
    const gchar *at_response;
    gchar *ceer_response;

    /* If the AT+CEER command fails, or we fail to obtain a valid result, we
     * ignore the error. This allows the registration attempt to continue.
     * So, the async response from this function is *always* True.
     */

    at_response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!at_response) {
        g_assert (error);
        mm_obj_warn (self, "AT+CEER failed: %s", error->message);
        g_error_free (error);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ceer_response = mm_altair_parse_ceer_response (at_response, &error);
    if (!ceer_response) {
        g_assert (error);
        mm_obj_warn (self, "Failed to parse AT+CEER response: %s", error->message);
        g_error_free (error);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if (g_strcmp0 ("EPS_AND_NON_EPS_SERVICES_NOT_ALLOWED", ceer_response) == 0) {
        mm_obj_dbg (self, "registration failed due to unprovisioned SIM");
        simulate_unprovisioned_subscription_pco_update (MM_BROADBAND_MODEM_ALTAIR_LTE (self));
    } else {
        mm_obj_dbg (self, "failed to find a better reason for registration failure");
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    g_free (ceer_response);
}

static void
run_registration_checks_ready (MMIfaceModem3gpp *self,
                               GAsyncResult *res,
                               GTask *task)
{
    GError *error = NULL;
    gboolean success;

    g_assert (iface_modem_3gpp_parent->run_registration_checks_finish);
    success = iface_modem_3gpp_parent->run_registration_checks_finish (self, res, &error);
    if (!success) {
        g_assert (error);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "checking if SIM is unprovisioned (ignoring registration state)");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CEER",
                              6,
                              FALSE,
                              (GAsyncReadyCallback) run_registration_checks_subscription_state_ready,
                              task);
}

static void
modem_3gpp_run_registration_checks (MMIfaceModem3gpp    *self,
                                    gboolean             is_cs_supported,
                                    gboolean             is_ps_supported,
                                    gboolean             is_eps_supported,
                                    gboolean             is_5gs_supported,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    g_assert (iface_modem_3gpp_parent->run_registration_checks);
    iface_modem_3gpp_parent->run_registration_checks (self,
                                                      is_cs_supported,
                                                      is_ps_supported,
                                                      is_eps_supported,
                                                      is_5gs_supported,
                                                      (GAsyncReadyCallback) run_registration_checks_ready,
                                                      task);
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

static gboolean
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp  *self,
                                       GAsyncResult      *res,
                                       GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
cmatt_ready (MMBaseModem  *self,
             GAsyncResult *res,
             GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_register_in_network (MMIfaceModem3gpp    *self,
                                const gchar         *operator_id,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    if (operator_id) {
        /* Currently only VZW is supported */
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Setting a specific operator ID is not supported");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "%CMATT=1",
                              3,
                              FALSE, /* allow cached */
                              (GAsyncReadyCallback)cmatt_ready,
                              task);
}

/*****************************************************************************/
/* SIMREFRESH unsolicited event handler */

static void
altair_reregister_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         gpointer user_data)
{
    if (!mm_base_modem_at_command_finish (self, res, NULL))
        mm_obj_dbg (self, "failed to reregister modem");
    else
        mm_obj_dbg (self, "modem reregistered successfully");
    MM_BROADBAND_MODEM_ALTAIR_LTE (self)->priv->sim_refresh_detach_in_progress = FALSE;
}

static void
altair_deregister_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         gpointer user_data)
{
    if (!mm_base_modem_at_command_finish (self, res, NULL)) {
        mm_obj_dbg (self, "deregister modem failed");
        MM_BROADBAND_MODEM_ALTAIR_LTE (self)->priv->sim_refresh_detach_in_progress = FALSE;
        return;
    }

    mm_obj_dbg (self, "deregistered modem, now reregistering");

    /* Register */
    mm_base_modem_at_command (
        self,
        "%CMATT=1",
        10,
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)altair_reregister_ready,
        NULL);
}

static void
altair_load_own_numbers_ready (MMIfaceModem *iface_modem,
                               GAsyncResult *res,
                               MMBroadbandModemAltairLte *self)
{
    GError *error = NULL;
    GStrv str_list;

    str_list = MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers_finish (MM_IFACE_MODEM (self), res, &error);
    if (error) {
        mm_obj_warn (self, "Couldn't reload Own Numbers: '%s'", error->message);
        g_error_free (error);
    }
    if (str_list) {
        mm_iface_modem_update_own_numbers (iface_modem, str_list);
        g_strfreev (str_list);
    }

    /* Set this flag to prevent connect requests from being processed while we
     * detach from the network.*/
    self->priv->sim_refresh_detach_in_progress = TRUE;

    /* Deregister */
    mm_obj_dbg (self, "reregistering modem");
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%CMATT=0",
        10,
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)altair_deregister_ready,
        NULL);
}

static gboolean
altair_sim_refresh_timer_expired (MMBroadbandModemAltairLte *self)
{
    mm_obj_dbg (self, "no more SIM refreshes, reloading own numbers and reregistering modem");

    g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers);
    g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers_finish);
    MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)altair_load_own_numbers_ready,
        self);
    self->priv->sim_refresh_timer_id = 0;

    return G_SOURCE_REMOVE;
}

static void
altair_sim_refresh_changed (MMPortSerialAt *port,
                            GMatchInfo *match_info,
                            MMBroadbandModemAltairLte *self)
{
    mm_obj_dbg (self, "received SIM refresh notification");
    if (self->priv->sim_refresh_timer_id) {
        g_source_remove (self->priv->sim_refresh_timer_id);
    }
    self->priv->sim_refresh_timer_id =
        g_timeout_add_seconds(10,
                              (GSourceFunc)altair_sim_refresh_timer_expired,
                              self);
}

typedef enum {
    MM_STATCM_ALTAIR_LTE_DEREGISTERED = 0,
    MM_STATCM_ALTAIR_LTE_REGISTERED = 1,
    MM_STATCM_ALTAIR_PDN_CONNECTED = 3,
    MM_STATCM_ALTAIR_PDN_DISCONNECTED = 4,
} MMStatcmAltair;

static void
bearer_list_report_disconnect_status_foreach (MMBaseBearer *bearer,
                                              gpointer *user_data)
{
    mm_base_bearer_report_connection_status (bearer,
                                             MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
}

/*****************************************************************************/
/* STATCM unsolicited event handler */

static void
altair_statcm_changed (MMPortSerialAt *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemAltairLte *self)
{
    gint pdn_event = 0;
    MMBearerList *list = NULL;

    mm_get_int_from_match_info (match_info, 1, &pdn_event);

    mm_obj_dbg (self, "PDN event detected: %d", pdn_event);

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

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static void
altair_pco_info_changed (MMPortSerialAt *port,
                         GMatchInfo *match_info,
                         MMBroadbandModemAltairLte *self);

static void
set_3gpp_unsolicited_events_handlers (MMBroadbandModemAltairLte *self,
                                      gboolean enable)
{
    MMPortSerialAt *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable/disable unsolicited events in given port */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        /* SIM refresh handler */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->sim_refresh_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)altair_sim_refresh_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* bearer mode related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->statcm_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)altair_statcm_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* PCO info handler */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->pcoinfo_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)altair_pco_info_changed : NULL,
            enable ? self : NULL,
            NULL);
    }
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_3gpp_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                            GAsyncResult *res,
                                            GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* Our own setup now */
        set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_ALTAIR_LTE (self), TRUE);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_3gpp_setup_unsolicited_events_ready,
        task);
}

static void
parent_3gpp_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Our own cleanup first */
    set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_ALTAIR_LTE (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_3gpp_cleanup_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

static const MMBaseModemAtCommand unsolicited_events_enable_sequence[] = {
    { "%STATCM=1",                  10, FALSE, mm_base_modem_response_processor_no_result_continue },
    { "%NOTIFYEV=\"SIMREFRESH\",1", 10, FALSE, NULL },
    { "%PCOINFO=1",                 10, FALSE, NULL },
    { NULL }
};

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
own_enable_unsolicited_events_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Our own enable now */
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        unsolicited_events_enable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
        task);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Disabling unsolicited events (3GPP interface) */

static const MMBaseModemAtCommand unsolicited_events_disable_sequence[] = {
  { "%STATCM=0", 10, FALSE, NULL },
  { "%NOTIFYEV=\"SIMREFRESH\",0", 10, FALSE, NULL },
  { "%PCOINFO=0", 10, FALSE, NULL },
  { NULL }
};

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
own_disable_unsolicited_events_ready (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Next, chain up parent's disable */
    iface_modem_3gpp_parent->disable_unsolicited_events (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
        task);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Our own disable first */
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        unsolicited_events_disable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Operator Code loading (3GPP interface) */

static gchar *
modem_3gpp_load_operator_code_finish (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    const gchar *result;
    gchar *operator_code = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    if (!mm_3gpp_parse_cops_read_response (result,
                                           NULL, /* mode */
                                           NULL, /* format */
                                           &operator_code,
                                           NULL, /* act */
                                           error))
        return NULL;

    return operator_code;
}

static void
modem_3gpp_load_operator_code (MMIfaceModem3gpp *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
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
    gchar *operator_name = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    if (!mm_3gpp_parse_cops_read_response (result,
                                           NULL, /* mode */
                                           NULL, /* format */
                                           &operator_name,
                                           NULL, /* act */
                                           error))
        return NULL;

    mm_3gpp_normalize_operator (&operator_name, MM_MODEM_CHARSET_UNKNOWN, self);
    return operator_name;
}

static void
modem_3gpp_load_operator_name (MMIfaceModem3gpp *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
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
/* PCOINFO unsolicited event handler */

static void
altair_pco_info_changed (MMPortSerialAt *port,
                         GMatchInfo *match_info,
                         MMBroadbandModemAltairLte *self)
{
    const gchar *pco_info;
    MMPco *pco;
    g_autoptr(GError) error = NULL;

    pco_info = g_match_info_fetch (match_info, 0);

    /* ignore if empty */
    if (!pco_info || !pco_info[0])
        return;

    mm_obj_dbg (self, "parsing vendor PCO info: %s", pco_info);
    pco = mm_altair_parse_vendor_pco_info (pco_info, &error);
    if (!pco) {
        mm_obj_warn (self, "error parsing vendor PCO info: %s", error->message);
        return;
    }

    self->priv->pco_list = mm_pco_list_add (self->priv->pco_list, pco);
    mm_iface_modem_3gpp_update_pco_list (MM_IFACE_MODEM_3GPP (self), self->priv->pco_list);
    g_object_unref (pco);
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
    MMPortSerialAt *primary;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_altair_lte_parent_class)->setup_ports (self);

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary)
        return;

    g_object_set (primary,
                  MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                  MM_PORT_SERIAL_AT_SEND_LF, TRUE,
                  MM_PORT_SERIAL_AT_INIT_SEQUENCE, primary_init_sequence,
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

gboolean
mm_broadband_modem_altair_lte_is_sim_refresh_detach_in_progress (MMBroadbandModem *self)
{
    return MM_BROADBAND_MODEM_ALTAIR_LTE (self)->priv->sim_refresh_detach_in_progress;
}

static void
mm_broadband_modem_altair_lte_init (MMBroadbandModemAltairLte *self)
{

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE,
                                              MMBroadbandModemAltairLtePrivate);

    self->priv->sim_refresh_regex = g_regex_new ("\\r\\n\\%NOTIFYEV:\\s*\"?SIMREFRESH\"?,?(\\d*)\\r+\\n",
                                                 G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->sim_refresh_detach_in_progress = FALSE;
    self->priv->sim_refresh_timer_id = 0;
    self->priv->statcm_regex = g_regex_new ("\\r\\n\\%STATCM:\\s*(\\d*),?(\\d*)\\r+\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->pcoinfo_regex = g_regex_new ("\\r\\n\\%PCOINFO:\\s*(\\d*),([^,\\s]*),([^,\\s]*)\\r+\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemAltairLte *self = MM_BROADBAND_MODEM_ALTAIR_LTE (object);

    if (self->priv->sim_refresh_timer_id)
        g_source_remove (self->priv->sim_refresh_timer_id);
    g_regex_unref (self->priv->sim_refresh_regex);
    g_regex_unref (self->priv->statcm_regex);
    g_regex_unref (self->priv->pcoinfo_regex);
    G_OBJECT_CLASS (mm_broadband_modem_altair_lte_parent_class)->finalize (object);
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
    iface->run_registration_checks = modem_3gpp_run_registration_checks;
    iface->run_registration_checks_finish = modem_3gpp_run_registration_checks_finish;

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

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;

    /* The Altair LTE modem reboots itself upon receiving an ATZ command. We
     * need to skip the default implementation in MMBroadbandModem to prevent
     * an ATZ command from being issued as part of the modem initialization
     * sequence when enabling the modem. */
    broadband_modem_class->enabling_modem_init = NULL;
    broadband_modem_class->enabling_modem_init_finish = NULL;
}
