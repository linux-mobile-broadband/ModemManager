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
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-novatel-lte.h"
#include "mm-broadband-modem-novatel-lte.h"
#include "mm-sim-novatel-lte.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-messaging.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-serial-parsers.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemNovatelLte, mm_broadband_modem_novatel_lte, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

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

    bearer = mm_broadband_bearer_novatel_lte_new_finish (res, &error);
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
    /* We just create a MMBroadbandBearer */
    mm_broadband_bearer_novatel_lte_new (MM_BROADBAND_MODEM_NOVATEL_LTE (self),
                                     properties,
                                     NULL, /* cancellable */
                                     (GAsyncReadyCallback)broadband_bearer_new_ready,
                                     g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
modem_create_sim_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return mm_sim_novatel_lte_new_finish (res, error);
}

static void
modem_create_sim (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    /* New Novatel LTE SIM */
    mm_sim_novatel_lte_new (MM_BASE_MODEM (self),
                            NULL, /* cancellable */
                            callback,
                            user_data);
}

/*****************************************************************************/
/* After SIM unlock (Modem interface) */

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_sim_unlock_wait_cb (GTask *task)
{
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
modem_after_sim_unlock (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* A 3-second wait is necessary for SIM to become ready.
     * Otherwise, a subsequent AT+CRSM command will likely fail. */
    g_timeout_add_seconds (3, (GSourceFunc)after_sim_unlock_wait_cb, task);
}

/*****************************************************************************/
/* Load own numbers (Modem interface) */

static GStrv
load_own_numbers_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    GVariant *result;
    GStrv own_numbers;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result)
        return NULL;

    own_numbers = (GStrv) g_variant_dup_strv (result, NULL);
    return own_numbers;
}

static MMBaseModemAtResponseProcessorResult
response_processor_cnum_ignore_at_errors (MMBaseModem   *self,
                                          gpointer       none,
                                          const gchar   *command,
                                          const gchar   *response,
                                          gboolean       last_command,
                                          const GError  *error,
                                          GVariant     **result,
                                          GError       **result_error)
{
    GStrv own_numbers;

    *result = NULL;
    *result_error = NULL;

    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command) {
            *result_error = g_error_copy (error);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
        }

        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    }

    own_numbers = mm_3gpp_parse_cnum_exec_response (response);
    if (!own_numbers)
        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;

    *result = g_variant_new_strv ((const gchar *const *) own_numbers, -1);
    g_strfreev (own_numbers);
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
}

static MMBaseModemAtResponseProcessorResult
response_processor_nwmdn_ignore_at_errors (MMBaseModem   *self,
                                           gpointer       none,
                                           const gchar   *command,
                                           const gchar   *response,
                                           gboolean       last_command,
                                           const GError  *error,
                                           GVariant     **result,
                                           GError       **result_error)
{
    g_auto(GStrv)  own_numbers = NULL;
    GPtrArray     *array;
    gchar         *mdn;

    *result = NULL;
    *result_error = NULL;

    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command) {
            *result_error = g_error_copy (error);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
        }

        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    }

    mdn = g_strdup (mm_strip_tag (response, "$NWMDN:"));

    array = g_ptr_array_new ();
    g_ptr_array_add (array, mdn);
    g_ptr_array_add (array, NULL);
    own_numbers = (GStrv) g_ptr_array_free (array, FALSE);

    *result = g_variant_new_strv ((const gchar *const *) own_numbers, -1);
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
}

static const MMBaseModemAtCommand own_numbers_commands[] = {
    { "+CNUM",  3, TRUE, response_processor_cnum_ignore_at_errors },
    { "$NWMDN", 3, TRUE, response_processor_nwmdn_ignore_at_errors },
    { NULL }
};

static void
load_own_numbers (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        own_numbers_commands,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

/*
 * Mapping from bits set in response of $NWBAND? command to MMModemBand values.
 * The bit positions and band names on the right come from the response to $NWBAND=?
 */
static MMModemBand bandbits[] = {
    MM_MODEM_BAND_CDMA_BC0,  /* "00 CDMA2000 Band Class 0, A-System" */
    MM_MODEM_BAND_CDMA_BC0,  /* "01 CDMA2000 Band Class 0, B-System" */
    MM_MODEM_BAND_CDMA_BC1,  /* "02 CDMA2000 Band Class 1, all blocks" */
    MM_MODEM_BAND_CDMA_BC2,  /* "03 CDMA2000 Band Class 2, place holder" */
    MM_MODEM_BAND_CDMA_BC3,  /* "04 CDMA2000 Band Class 3, A-System" */
    MM_MODEM_BAND_CDMA_BC4,  /* "05 CDMA2000 Band Class 4, all blocks" */
    MM_MODEM_BAND_CDMA_BC5,  /* "06 CDMA2000 Band Class 5, all blocks" */
    MM_MODEM_BAND_DCS,       /* "07 GSM DCS band" */
    MM_MODEM_BAND_EGSM,      /* "08 GSM Extended GSM (E-GSM) band" */
    MM_MODEM_BAND_UNKNOWN,   /* "09 GSM Primary GSM (P-GSM) band" */
    MM_MODEM_BAND_CDMA_BC6,  /* "10 CDMA2000 Band Class 6" */
    MM_MODEM_BAND_CDMA_BC7,  /* "11 CDMA2000 Band Class 7" */
    MM_MODEM_BAND_CDMA_BC8,  /* "12 CDMA2000 Band Class 8" */
    MM_MODEM_BAND_CDMA_BC9,  /* "13 CDMA2000 Band Class 9" */
    MM_MODEM_BAND_CDMA_BC10, /* "14 CDMA2000 Band Class 10 */
    MM_MODEM_BAND_CDMA_BC11, /* "15 CDMA2000 Band Class 11 */
    MM_MODEM_BAND_G450,      /* "16 GSM 450 band" */
    MM_MODEM_BAND_G480,      /* "17 GSM 480 band" */
    MM_MODEM_BAND_G750,      /* "18 GSM 750 band" */
    MM_MODEM_BAND_G850,      /* "19 GSM 850 band" */
    MM_MODEM_BAND_UNKNOWN,   /* "20 GSM 900 Railways band" */
    MM_MODEM_BAND_PCS,       /* "21 GSM PCS band" */
    MM_MODEM_BAND_UTRAN_1,   /* "22 WCDMA I IMT 2000 band" */
    MM_MODEM_BAND_UTRAN_2,   /* "23 WCDMA II PCS band" */
    MM_MODEM_BAND_UTRAN_3,   /* "24 WCDMA III 1700 band" */
    MM_MODEM_BAND_UTRAN_4,   /* "25 WCDMA IV 1700 band" */
    MM_MODEM_BAND_UTRAN_5,   /* "26 WCDMA V US850 band" */
    MM_MODEM_BAND_UTRAN_6,   /* "27 WCDMA VI JAPAN 800 band" */
    MM_MODEM_BAND_UNKNOWN,   /* "28 Reserved for BC12/BC14 */
    MM_MODEM_BAND_UNKNOWN,   /* "29 Reserved for BC12/BC14 */
    MM_MODEM_BAND_UNKNOWN,   /* "30 Reserved" */
    MM_MODEM_BAND_UNKNOWN,   /* "31 Reserved" */
};

static GArray *
load_supported_bands_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GTask *task;
    GArray *bands;
    guint i;

    task = g_task_new (self, NULL, callback, user_data);

    /*
     * The modem doesn't support telling us what bands are supported;
     * list everything we know about.
     */
    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 23);
    for (i = 0 ; i < G_N_ELEMENTS (bandbits) ; i++) {
        if (bandbits[i] != MM_MODEM_BAND_UNKNOWN)
            g_array_append_val(bands, bandbits[i]);
    }

    g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
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

static void
load_current_bands_done (MMIfaceModem *self,
                         GAsyncResult *res,
                         GTask *task)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;
    guint i;
    guint32 bandval;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /*
     * Response is "$NWBAND: <hex value>", where the hex value is a
     * bitmask of supported bands.
     */
    bandval = (guint32)strtoul(response + 9, NULL, 16);

    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
    for (i = 0 ; i < G_N_ELEMENTS (bandbits) ; i++) {
        if ((bandval & (1 << i)) && bandbits[i] != MM_MODEM_BAND_UNKNOWN)
            g_array_append_val(bands, bandbits[i]);
    }

    g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

static void
load_current_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$NWBAND?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_done,
        g_task_new (self, NULL, callback, user_data));
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
    gint pin_num, pin_value;
    int scan_count;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    response = mm_strip_tag (response, "$NWPINR:");

    scan_count = sscanf (response, "PIN%d, %d", &pin_num, &pin_value);
    if (scan_count != 2 || (pin_num != 1 && pin_num != 2)) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Invalid unlock retries response: '%s'",
                                 response);
    } else {
        MMUnlockRetries *retries;

        retries = mm_unlock_retries_new ();
        mm_unlock_retries_set (retries,
                               pin_num == 1 ? MM_MODEM_LOCK_SIM_PIN : MM_MODEM_LOCK_SIM_PIN2,
                               pin_value);
        g_task_return_pointer (task, retries, g_object_unref);
    }
    g_object_unref (task);
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWPINR?",
                              20,
                              FALSE,
                              (GAsyncReadyCallback)load_unlock_retries_ready,
                              g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *access_technologies = (MMModemAccessTechnology) value;
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
load_access_technologies_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                GTask *task)
{
    const gchar *response;
    MMModemAccessTechnology act;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    if (strstr (response, "LTE"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    if (strstr (response, "WCDMA"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    if (strstr (response, "EV-DO Rev 0"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    if (strstr (response, "EV-DO Rev A"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
    if (strstr (response, "CDMA 1X"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    if (strstr (response, "GSM"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM;

    g_task_return_int (task, act);
    g_object_unref (task);
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$NWSYSMODE",
        3,
        FALSE,
        (GAsyncReadyCallback)load_access_technologies_ready,
        g_task_new (self, NULL, callback, user_data));
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
                              "+CFUN=6",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Scan networks (3GPP interface) */

static GList *
scan_networks_finish (MMIfaceModem3gpp *self,
                      GAsyncResult *res,
                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_scan_networks_ready (MMIfaceModem3gpp *self,
                            GAsyncResult     *res,
                            GTask            *task)
{
    GError *error = NULL;
    GList  *scan_result;

    scan_result = iface_modem_3gpp_parent->scan_networks_finish (self, res, &error);
    if (!scan_result)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task,
                               scan_result,
                               (GDestroyNotify)mm_3gpp_network_info_list_free);
    g_object_unref (task);
}

static void
scan_networks (MMIfaceModem3gpp *self,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    GTask *task;
    MMModemAccessTechnology access_tech;

    mm_obj_dbg (self, "scanning for networks (Novatel LTE)...");

    task = g_task_new (self, NULL, callback, user_data);

    /* The Novatel LTE modem does not properly support AT+COPS=? in LTE mode.
     * Thus, do not try to scan networks when the current access technologies
     * include LTE.
     */
    access_tech = mm_iface_modem_get_access_technologies (MM_IFACE_MODEM (self));
    if (access_tech & MM_MODEM_ACCESS_TECHNOLOGY_LTE) {
        g_autofree gchar *access_tech_string = NULL;

        access_tech_string = mm_modem_access_technology_build_string_from_mask (access_tech);
        mm_obj_warn (self, "couldn't scan for networks with access technologies: %s", access_tech_string);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Couldn't scan for networks with access technologies: %s",
                                 access_tech_string);
        g_object_unref (task);
        return;
    }

    /* Otherwise, just fallback to the generic scan method */
    iface_modem_3gpp_parent->scan_networks (self,
                                            (GAsyncReadyCallback)parent_scan_networks_ready,
                                            task);
}

/*****************************************************************************/

MMBroadbandModemNovatelLte *
mm_broadband_modem_novatel_lte_new (const gchar *device,
                                    const gchar **drivers,
                                    const gchar *plugin,
                                    guint16 vendor_id,
                                    guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_NOVATEL_LTE,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_novatel_lte_init (MMBroadbandModemNovatelLte *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->create_sim = modem_create_sim;
    iface->create_sim_finish = modem_create_sim_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->load_own_numbers = load_own_numbers;
    iface->load_own_numbers_finish = load_own_numbers_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    /* No support for setting bands, as it destabilizes the modem. */
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->reset = reset;
    iface->reset_finish = reset_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->scan_networks = scan_networks;
    iface->scan_networks_finish = scan_networks_finish;
}

static void
mm_broadband_modem_novatel_lte_class_init (MMBroadbandModemNovatelLteClass *klass)
{
}
