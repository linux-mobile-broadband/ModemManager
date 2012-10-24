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
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-simple.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-messaging.h"
#include "mm-iface-modem-time.h"
#include "mm-iface-modem-firmware.h"
#include "mm-broadband-bearer.h"
#include "mm-bearer-list.h"
#include "mm-sms-list.h"
#include "mm-sim.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-qcdm-serial-port.h"
#include "libqcdm/src/errors.h"
#include "libqcdm/src/commands.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_simple_init (MMIfaceModemSimple *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_time_init (MMIfaceModemTime *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModem, mm_broadband_modem, MM_TYPE_BASE_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIMPLE, iface_modem_simple_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init))

enum {
    PROP_0,
    PROP_MODEM_DBUS_SKELETON,
    PROP_MODEM_3GPP_DBUS_SKELETON,
    PROP_MODEM_3GPP_USSD_DBUS_SKELETON,
    PROP_MODEM_CDMA_DBUS_SKELETON,
    PROP_MODEM_SIMPLE_DBUS_SKELETON,
    PROP_MODEM_LOCATION_DBUS_SKELETON,
    PROP_MODEM_MESSAGING_DBUS_SKELETON,
    PROP_MODEM_TIME_DBUS_SKELETON,
    PROP_MODEM_FIRMWARE_DBUS_SKELETON,
    PROP_MODEM_SIM,
    PROP_MODEM_BEARER_LIST,
    PROP_MODEM_STATE,
    PROP_MODEM_3GPP_REGISTRATION_STATE,
    PROP_MODEM_3GPP_CS_NETWORK_SUPPORTED,
    PROP_MODEM_3GPP_PS_NETWORK_SUPPORTED,
    PROP_MODEM_CDMA_CDMA1X_REGISTRATION_STATE,
    PROP_MODEM_CDMA_EVDO_REGISTRATION_STATE,
    PROP_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED,
    PROP_MODEM_CDMA_EVDO_NETWORK_SUPPORTED,
    PROP_MODEM_MESSAGING_SMS_LIST,
    PROP_MODEM_MESSAGING_SMS_PDU_MODE,
    PROP_MODEM_MESSAGING_SMS_DEFAULT_STORAGE,
    PROP_MODEM_SIMPLE_STATUS,
    PROP_LAST
};

/* When CIND is supported, invalid indicators are marked with this value */
#define CIND_INDICATOR_INVALID 255
#define CIND_INDICATOR_IS_VALID(u) (u != CIND_INDICATOR_INVALID)

typedef struct _PortsContext PortsContext;

struct _MMBroadbandModemPrivate {
    /* Broadband modem specific implementation */
    PortsContext *enabled_ports_ctx;

    /*<--- Modem interface --->*/
    /* Properties */
    GObject *modem_dbus_skeleton;
    MMSim *modem_sim;
    MMBearerList *modem_bearer_list;
    MMModemState modem_state;
    /* Implementation helpers */
    MMModemCharset modem_current_charset;
    gboolean modem_cind_support_checked;
    gboolean modem_cind_supported;
    guint modem_cind_indicator_signal_quality;
    guint modem_cind_min_signal_quality;
    guint modem_cind_max_signal_quality;
    guint modem_cind_indicator_roaming;
    guint modem_cind_indicator_service;

    /*<--- Modem 3GPP interface --->*/
    /* Properties */
    GObject *modem_3gpp_dbus_skeleton;
    MMModem3gppRegistrationState modem_3gpp_registration_state;
    gboolean modem_3gpp_cs_network_supported;
    gboolean modem_3gpp_ps_network_supported;
    /* Implementation helpers */
    GPtrArray *modem_3gpp_registration_regex;

    /*<--- Modem 3GPP USSD interface --->*/
    /* Properties */
    GObject *modem_3gpp_ussd_dbus_skeleton;
    /* Implementation helpers */
    gboolean use_unencoded_ussd;
    GSimpleAsyncResult *pending_ussd_action;

    /*<--- Modem CDMA interface --->*/
    /* Properties */
    GObject *modem_cdma_dbus_skeleton;
    MMModemCdmaRegistrationState modem_cdma_cdma1x_registration_state;
    MMModemCdmaRegistrationState modem_cdma_evdo_registration_state;
    gboolean modem_cdma_cdma1x_network_supported;
    gboolean modem_cdma_evdo_network_supported;
    GCancellable *modem_cdma_pending_registration_cancellable;
    /* Implementation helpers */
    gboolean checked_sprint_support;
    gboolean has_spservice;
    gboolean has_speri;

    /*<--- Modem Simple interface --->*/
    /* Properties */
    GObject *modem_simple_dbus_skeleton;
    MMSimpleStatus *modem_simple_status;

    /*<--- Modem Location interface --->*/
    /* Properties */
    GObject *modem_location_dbus_skeleton;

    /*<--- Modem Messaging interface --->*/
    /* Properties */
    GObject *modem_messaging_dbus_skeleton;
    MMSmsList *modem_messaging_sms_list;
    gboolean modem_messaging_sms_pdu_mode;
    MMSmsStorage modem_messaging_sms_default_storage;
    /* Implementation helpers */
    gboolean sms_supported_modes_checked;
    gboolean mem1_storage_locked;
    MMSmsStorage current_sms_mem1_storage;
    gboolean mem2_storage_locked;
    MMSmsStorage current_sms_mem2_storage;


    /*<--- Modem Time interface --->*/
    /* Properties */
    GObject *modem_time_dbus_skeleton;

    /*<--- Modem Firmware interface --->*/
    /* Properties */
    GObject *modem_firmware_dbus_skeleton;
};

/*****************************************************************************/

static gboolean
response_processor_string_ignore_at_errors (MMBaseModem *self,
                                            gpointer none,
                                            const gchar *command,
                                            const gchar *response,
                                            gboolean last_command,
                                            const GError *error,
                                            GVariant **result,
                                            GError **result_error)
{
    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command)
            *result_error = g_error_copy (error);

        return FALSE;
    }

    *result = g_variant_new_string (response);
    return TRUE;
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
    mm_dbg ("New bearer created at DBus path '%s'", mm_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_new_finish (res, &error);
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
    mm_dbg ("Creating Broadband bearer in broadband modem");
    mm_broadband_bearer_new (MM_BROADBAND_MODEM (self),
                             properties,
                             NULL, /* cancellable */
                             (GAsyncReadyCallback)broadband_bearer_new_ready,
                             result);
}

/*****************************************************************************/
/* Create SIM (Modem inteface) */

static MMSim *
modem_create_sim_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMSim *sim;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    sim = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    return (sim ? g_object_ref (sim) : NULL);
}

static void
modem_create_sim_ready (GObject *source,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    MMSim *sim;
    GError *error = NULL;

    sim = mm_sim_new_finish (res, &error);
    if (!sim)
        g_simple_async_result_take_error (simple, error);
    else {
        mm_dbg ("New SIM created at DBus path '%s'",
                mm_sim_get_path (sim));
        g_simple_async_result_set_op_res_gpointer (
            simple,
            sim,
            (GDestroyNotify)g_object_unref);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_create_sim (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_sim);

    /* CDMA-only modems don't need this */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_dbg ("Skipping SIM creation in CDMA-only modem...");
        g_simple_async_result_set_op_res_gpointer (result, NULL, NULL);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* New generic SIM */
    mm_sim_new (MM_BASE_MODEM (self),
                NULL, /* cancellable */
                (GAsyncReadyCallback)modem_create_sim_ready,
                result);
}

/*****************************************************************************/
/* Capabilities loading (Modem interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMModemCapability caps;
} LoadCapabilitiesContext;

static void
load_capabilities_context_complete_and_free (LoadCapabilitiesContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (LoadCapabilitiesContext, ctx);
}

static MMModemCapability
modem_load_current_capabilities_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    MMModemCapability caps;
    gchar *caps_str;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CAPABILITY_NONE;

    caps = (MMModemCapability) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    caps_str = mm_modem_capability_build_string_from_mask (caps);
    mm_dbg ("loaded current capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static void
current_capabilities_ws46_test_ready (MMBaseModem *self,
                                      GAsyncResult *res,
                                      LoadCapabilitiesContext *ctx)
{
    const gchar *response;

    /* Completely ignore errors in AT+WS46=? */
    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (response &&
        (strstr (response, "28") != NULL ||   /* 4G only */
         strstr (response, "30") != NULL ||   /* 2G/4G */
         strstr (response, "31") != NULL)) {  /* 3G/4G */
        /* Add LTE caps */
        ctx->caps |= MM_MODEM_CAPABILITY_LTE;
    }

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        GUINT_TO_POINTER (ctx->caps),
        NULL);
    load_capabilities_context_complete_and_free (ctx);
}

typedef struct {
    gchar *name;
    MMModemCapability bits;
} ModemCaps;

static const ModemCaps modem_caps[] = {
    { "+CGSM",     MM_MODEM_CAPABILITY_GSM_UMTS  },
    { "+CLTE2",    MM_MODEM_CAPABILITY_LTE       }, /* Novatel */
    { "+CLTE1",    MM_MODEM_CAPABILITY_LTE       }, /* Novatel */
    { "+CLTE",     MM_MODEM_CAPABILITY_LTE       },
    { "+CIS707-A", MM_MODEM_CAPABILITY_CDMA_EVDO },
    { "+CIS707A",  MM_MODEM_CAPABILITY_CDMA_EVDO }, /* Cmotech */
    { "+CIS707",   MM_MODEM_CAPABILITY_CDMA_EVDO },
    { "CIS707",    MM_MODEM_CAPABILITY_CDMA_EVDO }, /* Qualcomm Gobi */
    { "+CIS707P",  MM_MODEM_CAPABILITY_CDMA_EVDO },
    { "CIS-856",   MM_MODEM_CAPABILITY_CDMA_EVDO },
    { "+IS-856",   MM_MODEM_CAPABILITY_CDMA_EVDO }, /* Cmotech */
    { "CIS-856-A", MM_MODEM_CAPABILITY_CDMA_EVDO },
    { "CIS-856A",  MM_MODEM_CAPABILITY_CDMA_EVDO }, /* Kyocera KPC680 */
    { "+WIRIDIUM", MM_MODEM_CAPABILITY_IRIDIUM   }, /* Iridium satellite modems */
    /* TODO: FCLASS, MS, ES, DS? */
    { NULL }
};

static gboolean
parse_caps_gcap (MMBaseModem *self,
                 gpointer none,
                 const gchar *command,
                 const gchar *response,
                 gboolean last_command,
                 const GError *error,
                 GVariant **variant,
                 GError **result_error)
{
    const ModemCaps *cap = modem_caps;
    guint32 ret = 0;

    /* Some modems (Huawei E160g) won't respond to +GCAP with no SIM, but
     * will respond to ATI.  Ignore the error and continue.
     */
    if (response && strstr (response, "+CME ERROR:"))
        return FALSE;

    while (cap->name) {
        if (strstr (response, cap->name))
            ret |= cap->bits;
        cap++;
    }

    /* No result built? */
    if (ret == 0)
        return FALSE;

    *variant = g_variant_new_uint32 (ret);
    return TRUE;
}

static gboolean
parse_caps_cpin (MMBaseModem *self,
                 gpointer none,
                 const gchar *command,
                 const gchar *response,
                 gboolean last_command,
                 const GError *error,
                 GVariant **result,
                 GError **result_error)
{
    if (strcasestr (response, "SIM PIN") ||
        strcasestr (response, "SIM PUK") ||
        strcasestr (response, "PH-SIM PIN") ||
        strcasestr (response, "PH-FSIM PIN") ||
        strcasestr (response, "PH-FSIM PUK") ||
        strcasestr (response, "SIM PIN2") ||
        strcasestr (response, "SIM PUK2") ||
        strcasestr (response, "PH-NET PIN") ||
        strcasestr (response, "PH-NET PUK") ||
        strcasestr (response, "PH-NETSUB PIN") ||
        strcasestr (response, "PH-NETSUB PUK") ||
        strcasestr (response, "PH-SP PIN") ||
        strcasestr (response, "PH-SP PUK") ||
        strcasestr (response, "PH-CORP PIN") ||
        strcasestr (response, "PH-CORP PUK") ||
        strcasestr (response, "READY")) {
        /* At least, it's a GSM modem */
        *result = g_variant_new_uint32 (MM_MODEM_CAPABILITY_GSM_UMTS);
        return TRUE;
    }
    return FALSE;
}

static gboolean
parse_caps_cgmm (MMBaseModem *self,
                 gpointer none,
                 const gchar *command,
                 const gchar *response,
                 gboolean last_command,
                 const GError *error,
                 GVariant **result,
                 GError **result_error)
{
    /* This check detects some really old Motorola GPRS dongles and phones */
    if (strstr (response, "GSM900") ||
        strstr (response, "GSM1800") ||
        strstr (response, "GSM1900") ||
        strstr (response, "GSM850")) {
        /* At least, it's a GSM modem */
        *result = g_variant_new_uint32 (MM_MODEM_CAPABILITY_GSM_UMTS);
        return TRUE;
    }
    return FALSE;
}

static const MMBaseModemAtCommand capabilities[] = {
    { "+GCAP",  2, TRUE,  parse_caps_gcap },
    { "I",      1, TRUE,  parse_caps_gcap }, /* yes, really parse as +GCAP */
    { "+CPIN?", 1, FALSE, parse_caps_cpin },
    { "+CGMM",  1, TRUE,  parse_caps_cgmm },
    { NULL }
};

static void
capabilities_sequence_ready (MMBaseModem *self,
                             GAsyncResult *res,
                             LoadCapabilitiesContext *ctx)
{
    GError *error = NULL;
    GVariant *result;

    result = mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (!result) {
        g_simple_async_result_take_error (ctx->result, error);
        load_capabilities_context_complete_and_free (ctx);
        return;
    }

    ctx->caps = (MMModemCapability)g_variant_get_uint32 (result);

    /* Some modems (e.g. Sierra Wireless MC7710 or ZTE MF820D) won't report LTE
     * capabilities even if they have them. So just run AT+WS46=? as well to see
     * if the current supported modes includes any LTE-specific mode.
     * This is not a big deal, as the AT+WS46=? command is a test command with a
     * cache-able result.
     *
     * E.g.:
     *  AT+WS46=?
     *   +WS46: (12,22,25,28,29)
     *   OK
     *
     */
    if (ctx->caps & MM_MODEM_CAPABILITY_GSM_UMTS &&
        !(ctx->caps & MM_MODEM_CAPABILITY_LTE)) {
        mm_base_modem_at_command (
            MM_BASE_MODEM (ctx->self),
            "+WS46=?",
            3,
            TRUE, /* allow caching, it's a test command */
            (GAsyncReadyCallback)current_capabilities_ws46_test_ready,
            ctx);
        return;
    }

    /* Otherwise, just set the already retrieved capabilities */
    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        GUINT_TO_POINTER (ctx->caps),
        NULL);
    load_capabilities_context_complete_and_free (ctx);
}

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    LoadCapabilitiesContext *ctx;

    mm_dbg ("loading current capabilities...");

    ctx = g_slice_new0 (LoadCapabilitiesContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_current_capabilities);

    /* Launch sequence, we will expect a "u" GVariant */
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        capabilities,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        (GAsyncReadyCallback)capabilities_sequence_ready,
        ctx);
}

/*****************************************************************************/
/* Manufacturer loading (Modem interface) */

static gchar *
modem_load_manufacturer_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    GVariant *result;
    gchar *manufacturer;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result)
        return NULL;

    manufacturer = g_strstrip (g_variant_dup_string (result, NULL));
    mm_dbg ("loaded manufacturer: %s", manufacturer);
    return manufacturer;
}

static const MMBaseModemAtCommand manufacturers[] = {
    { "+CGMI",  3, TRUE, response_processor_string_ignore_at_errors },
    { "+GMI",   3, TRUE, response_processor_string_ignore_at_errors },
    { NULL }
};

static void
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    mm_dbg ("loading manufacturer...");
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        manufacturers,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    GVariant *result;
    const gchar *p;
    gchar *model;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result)
        return NULL;

    p = g_variant_get_string (result, NULL);

    /* Some devices (e.g. ZTE MF820D) seem to include the command prefix */
    p = mm_strip_tag (p, "+CGMM:");
    p = mm_strip_tag (p, "+GMM:");
    model = g_strdup (p);

    /* Stripping quotes modifies string in place */
    model = mm_strip_quotes (model);

    mm_dbg ("loaded model: %s", model);
    return model;
}

static const MMBaseModemAtCommand models[] = {
    { "+CGMM",  3, TRUE, response_processor_string_ignore_at_errors },
    { "+GMM",   3, TRUE, response_processor_string_ignore_at_errors },
    { NULL }
};

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    mm_dbg ("loading model...");
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        models,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Revision loading */

static gchar *
modem_load_revision_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    GVariant *result;
    gchar *revision;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result)
        return NULL;

    revision = g_strstrip (g_variant_dup_string (result, NULL));
    mm_dbg ("loaded revision: %s", revision);
    return revision;
}

static const MMBaseModemAtCommand revisions[] = {
    { "+CGMR",  3, TRUE, response_processor_string_ignore_at_errors },
    { "+GMR",   3, TRUE, response_processor_string_ignore_at_errors },
    { NULL }
};

static void
modem_load_revision (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_dbg ("loading revision...");
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        revisions,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Equipment ID loading (Modem interface) */

static gchar *
modem_load_equipment_identifier_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    GVariant *result;
    gchar *equipment_identifier;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result)
        return NULL;

    equipment_identifier = g_variant_dup_string (result, NULL);
    mm_dbg ("loaded equipment identifier: %s", equipment_identifier);
    return equipment_identifier;
}

static const MMBaseModemAtCommand equipment_identifiers[] = {
    { "+CGSN",  3, TRUE, response_processor_string_ignore_at_errors },
    { "+GSN",   3, TRUE, response_processor_string_ignore_at_errors },
    { NULL }
};

static void
modem_load_equipment_identifier (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    const MMBaseModemAtCommand *commands = equipment_identifiers;

    mm_dbg ("loading equipment identifier...");

    /* On CDMA-only (non-3GPP) modems, just try +GSN */
    if (mm_iface_modem_is_cdma_only (self))
        commands++;

    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        commands,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Device identifier loading (Modem interface) */

typedef struct {
    gchar *ati;
    gchar *ati1;
} DeviceIdentifierContext;

static void
device_identifier_context_free (DeviceIdentifierContext *ctx)
{
    g_free (ctx->ati);
    g_free (ctx->ati1);
    g_free (ctx);
}

static gchar *
modem_load_device_identifier_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    GError *inner_error = NULL;
    gpointer ctx = NULL;
    gchar *device_identifier;

    mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, &ctx, &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    g_assert (ctx != NULL);
    device_identifier = (mm_broadband_modem_create_device_identifier (
                             MM_BROADBAND_MODEM (self),
                             ((DeviceIdentifierContext *)ctx)->ati,
                             ((DeviceIdentifierContext *)ctx)->ati1));
    mm_dbg ("loaded device identifier: %s", device_identifier);
    return device_identifier;
}

static gboolean
parse_ati_reply (MMBaseModem *self,
                 DeviceIdentifierContext *ctx,
                 const gchar *command,
                 const gchar *response,
                 gboolean last_command,
                 const GError *error,
                 GVariant **result,
                 GError **result_error)
{
    /* Store the proper string in the proper place */
    if (!error) {
        if (g_str_equal (command, "ATI1"))
            ctx->ati1 = g_strdup (response);
        else
            ctx->ati = g_strdup (response);
    }

    /* Always keep on, this is a sequence where all the steps should be taken */
    return TRUE;
}

static const MMBaseModemAtCommand device_identifier_steps[] = {
    { "ATI",  3, TRUE, (MMBaseModemAtResponseProcessor)parse_ati_reply },
    { "ATI1", 3, TRUE, (MMBaseModemAtResponseProcessor)parse_ati_reply },
    { NULL }
};

static void
modem_load_device_identifier (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    mm_dbg ("loading device identifier...");
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        device_identifier_steps,
        g_new0 (DeviceIdentifierContext, 1),
        (GDestroyNotify)device_identifier_context_free,
        callback,
        user_data);
}

/*****************************************************************************/
/* Load own numbers (Modem interface) */

static GStrv
modem_load_own_numbers_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    const gchar *result;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    return mm_3gpp_parse_cnum_exec_response (result, error);
}

static void
modem_load_own_numbers (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    mm_dbg ("loading own numbers...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CNUM",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Check if unlock required (Modem interface) */

typedef struct {
    const gchar *result;
    MMModemLock code;
} CPinResult;

static CPinResult unlock_results[] = {
    /* Longer entries first so we catch the correct one with strcmp() */
    { "READY",         MM_MODEM_LOCK_NONE           },
    { "SIM PIN2",      MM_MODEM_LOCK_SIM_PIN2       },
    { "SIM PUK2",      MM_MODEM_LOCK_SIM_PUK2       },
    { "SIM PIN",       MM_MODEM_LOCK_SIM_PIN        },
    { "SIM PUK",       MM_MODEM_LOCK_SIM_PUK        },
    { "PH-NETSUB PIN", MM_MODEM_LOCK_PH_NETSUB_PIN  },
    { "PH-NETSUB PUK", MM_MODEM_LOCK_PH_NETSUB_PUK  },
    { "PH-FSIM PIN",   MM_MODEM_LOCK_PH_FSIM_PIN    },
    { "PH-FSIM PUK",   MM_MODEM_LOCK_PH_FSIM_PUK    },
    { "PH-CORP PIN",   MM_MODEM_LOCK_PH_CORP_PIN    },
    { "PH-CORP PUK",   MM_MODEM_LOCK_PH_CORP_PUK    },
    { "PH-SIM PIN",    MM_MODEM_LOCK_PH_SIM_PIN     },
    { "PH-NET PIN",    MM_MODEM_LOCK_PH_NET_PIN     },
    { "PH-NET PUK",    MM_MODEM_LOCK_PH_NET_PUK     },
    { "PH-SP PIN",     MM_MODEM_LOCK_PH_SP_PIN      },
    { "PH-SP PUK",     MM_MODEM_LOCK_PH_SP_PUK      },
    { NULL }
};

static MMModemLock
modem_load_unlock_required_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCK_UNKNOWN;

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                               G_SIMPLE_ASYNC_RESULT (res)));
}

static void
cpin_query_ready (MMIfaceModem *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *simple)
{

    MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;
    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    if (result &&
        strstr (result, "+CPIN:")) {
        CPinResult *iter = &unlock_results[0];
        const gchar *str;

        str = strstr (result, "+CPIN:") + 6;
        /* Skip possible whitespaces after '+CPIN:' and before the response */
        while (*str == ' ')
            str++;

        /* Some phones (Motorola EZX models) seem to quote the response */
        if (str[0] == '"')
            str++;

        /* Translate the reply */
        while (iter->result) {
            if (g_str_has_prefix (str, iter->result)) {
                lock = iter->code;
                break;
            }
            iter++;
        }
    }

    g_simple_async_result_set_op_res_gpointer (simple,
                                               GUINT_TO_POINTER (lock),
                                               NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_unlock_required (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_unlock_required);

    /* CDMA-only modems don't need this */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_dbg ("Skipping unlock check in CDMA-only modem...");
        g_simple_async_result_set_op_res_gpointer (result,
                                                   GUINT_TO_POINTER (MM_MODEM_LOCK_NONE),
                                                   NULL);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_dbg ("checking if unlock required...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPIN?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)cpin_query_ready,
                              result);
}

/*****************************************************************************/
/* Supported modes loading (Modem interface) */

typedef struct {
    GSimpleAsyncResult *result;
    MMBroadbandModem *self;
    MMModemMode mode;
    gboolean run_ws46;
    gboolean run_gcap;
} LoadSupportedModesContext;

static void
load_supported_modes_context_complete_and_free (LoadSupportedModesContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static MMModemMode
modem_load_supported_modes_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_MODE_NONE;

    return (MMModemMode)GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                              G_SIMPLE_ASYNC_RESULT (res)));
}

static void load_supported_modes_step (LoadSupportedModesContext *ctx);

static void
supported_modes_gcap_ready (MMBaseModem *self,
                            GAsyncResult *res,
                            LoadSupportedModesContext *ctx)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!error) {
        MMModemMode mode = MM_MODEM_MODE_NONE;

        if (strstr (response, "IS")) {
            /* IS-856 is the EV-DO family */
            if (strstr (response, "856")) {
                if (!ctx->self->priv->modem_cdma_evdo_network_supported) {
                    ctx->self->priv->modem_cdma_evdo_network_supported = TRUE;
                    g_object_notify (G_OBJECT (ctx->self), MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED);
                }
                mm_dbg ("Device allows (CDMA) 3G network mode");
                mode |= MM_MODEM_MODE_3G;
            }
            /* IS-707 is the 1xRTT family, which we consider as 2G */
            if (strstr (response, "707")) {
                if (!ctx->self->priv->modem_cdma_cdma1x_network_supported) {
                    ctx->self->priv->modem_cdma_cdma1x_network_supported = TRUE;
                    g_object_notify (G_OBJECT (ctx->self), MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED);
                }
                mm_dbg ("Device allows (CDMA) 2G network mode");
                mode |= MM_MODEM_MODE_2G;
            }
        }

        /* If no expected mode found, error */
        if (mode == MM_MODEM_MODE_NONE) {
            /* This should really never happen in the default implementation. */
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't find specific CDMA mode in capabilities string: '%s'",
                                 response);
        } else {
            /* Keep our results */
            ctx->mode |= mode;
        }
    }

    if (error) {
        mm_dbg ("Generic query of supported CDMA networks failed: '%s'", error->message);
        g_error_free (error);

        /* Use defaults */
        if (ctx->self->priv->modem_cdma_cdma1x_network_supported) {
            mm_dbg ("Assuming device allows (CDMA) 2G network mode");
            ctx->mode |= MM_MODEM_MODE_2G;
        }
        if (ctx->self->priv->modem_cdma_evdo_network_supported) {
            mm_dbg ("Assuming device allows (CDMA) 3G network mode");
            ctx->mode |= MM_MODEM_MODE_3G;
        }
    }

    /* Now keep on with the loading, we're probably finishing now */
    ctx->run_gcap = FALSE;
    load_supported_modes_step (ctx);
}

static void
supported_modes_ws46_test_ready (MMBroadbandModem *self,
                                 GAsyncResult *res,
                                 LoadSupportedModesContext *ctx)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!error) {
        MMModemMode mode = MM_MODEM_MODE_NONE;

        /*
         * More than one numeric ID may appear in the list, that's why
         * they are checked separately.
         *
         * NOTE: Do not skip WS46 prefix; it would break Cinterion handling.
         *
         * From 3GPP TS 27.007 v.11.2.0, section 5.9
         * 12	GSM Digital Cellular Systems (GERAN only)
         * 22	UTRAN only
         * 25	3GPP Systems (GERAN, UTRAN and E-UTRAN)
         * 28	E-UTRAN only
         * 29	GERAN and UTRAN
         * 30	GERAN and E-UTRAN
         * 31	UTRAN and E-UTRAN
         */

        if (strstr (response, "12") != NULL) {
            mm_dbg ("Device allows (3GPP) 2G-only network mode");
            mode |= MM_MODEM_MODE_2G;
        }

        if (strstr (response, "22") != NULL) {
            mm_dbg ("Device allows (3GPP) 3G-only network mode");
            mode |= MM_MODEM_MODE_3G;
        }

        if (strstr (response, "28") != NULL) {
            mm_dbg ("Device allows (3GPP) 4G-only network mode");
            mode |= MM_MODEM_MODE_4G;
        }

        if (strstr (response, "29") != NULL) {
            mm_dbg ("Device allows (3GPP) 2G/3G network mode");
            mode |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        }

        if (strstr (response, "30") != NULL) {
            mm_dbg ("Device allows (3GPP) 2G/4G network mode");
            mode |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G);
        }

        if (strstr (response, "31") != NULL) {
            mm_dbg ("Device allows (3GPP) 3G/4G network mode");
            mode |= (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        }

        if (strstr (response, "25") != NULL) {
            if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self))) {
                mm_dbg ("Device allows every supported 3GPP network mode (2G/3G/4G)");
                mode |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
            } else {
                mm_dbg ("Device allows every supported 3GPP network mode (2G/3G)");
                mode |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            }
        }

        /* If no expected ID found, error */
        if (mode == MM_MODEM_MODE_NONE) {
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Invalid list of supported networks: '%s'",
                                 response);
        } else {
            /* Keep our results */
            ctx->mode |= mode;
        }
    }

    /* Process error, which may come either directly from the AT command or from
     * our parsing logic. In this case, always fallback to some default guesses. */
    if (error) {
        mm_dbg ("Generic query of supported 3GPP networks failed: '%s'", error->message);
        g_error_free (error);

        /* If PS supported, assume we can do both 2G and 3G, even if it may not really
         * be true. This is the generic implementation anyway, plugins can use modem
         * specific commands to check which technologies are supported. */
        if (ctx->self->priv->modem_3gpp_ps_network_supported) {
            mm_dbg ("Assuming device allows (3GPP) 2G/3G network modes");
            ctx->mode |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        }
    }

    /* We'll assume CS supported if we have 2G */
    if (!(ctx->mode & MM_MODEM_MODE_CS) &&
        ctx->mode & MM_MODEM_MODE_2G) {
        mm_dbg ("Assuming device allows (3GPP) 2G/3G network modes");
        ctx->mode |= MM_MODEM_MODE_CS;
    }

    /* Now keep on with the loading, we may need CDMA-specific checks */
    ctx->run_ws46 = FALSE;
    load_supported_modes_step (ctx);
}

static void
load_supported_modes_step (LoadSupportedModesContext *ctx)
{
    if (ctx->run_ws46) {
        mm_base_modem_at_command (
            MM_BASE_MODEM (ctx->self),
            "+WS46=?",
            3,
            TRUE, /* allow caching, it's a test command */
            (GAsyncReadyCallback)supported_modes_ws46_test_ready,
            ctx);
        return;
    }

    if (ctx->run_gcap) {
        mm_base_modem_at_command (
            MM_BASE_MODEM (ctx->self),
            "+GCAP",
            3,
            TRUE, /* allow caching */
            (GAsyncReadyCallback)supported_modes_gcap_ready,
            ctx);
        return;
    }

    /* All done.
     * If no mode found, error */
    if (ctx->mode == MM_MODEM_MODE_NONE)
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't retrieve supported modes");
    else
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   GUINT_TO_POINTER (ctx->mode),
                                                   NULL);
    load_supported_modes_context_complete_and_free (ctx);
}

static void
modem_load_supported_modes (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    LoadSupportedModesContext *ctx;

    mm_dbg ("loading initial supported modes...");
    ctx = g_new0 (LoadSupportedModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_supported_modes);
    ctx->mode = MM_MODEM_MODE_NONE;

    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self))) {
        /* Run +WS46=? in order to know if the modem is 2G-only or 2G/3G */
        ctx->run_ws46 = TRUE;

        /* If the modem has LTE caps, it does 4G */
        if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
            ctx->mode |= MM_MODEM_MODE_4G;
    }

    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self))) {
        /* Run +GCAP in order to know if the modem is CDMA1x only or CDMA1x/EV-DO */
        ctx->run_gcap = TRUE;
    }

    load_supported_modes_step (ctx);
}

/*****************************************************************************/
/* Signal quality loading (Modem interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMSerialPort *port;
} SignalQualityContext;

static void
signal_quality_context_complete_and_free (SignalQualityContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    if (ctx->port)
        g_object_unref (ctx->port);
    g_free (ctx);
}

static guint
modem_load_signal_quality_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return 0;

    return GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
signal_quality_csq_ready (MMBroadbandModem *self,
                          GAsyncResult *res,
                          SignalQualityContext *ctx)
{
    GError *error = NULL;
    GVariant *result;
    const gchar *result_str;

    result = mm_base_modem_at_sequence_full_finish (MM_BASE_MODEM (self), res, NULL, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        signal_quality_context_complete_and_free (ctx);
        return;
    }

    result_str = g_variant_get_string (result, NULL);
    if (result_str &&
        !strncmp (result_str, "+CSQ: ", 6)) {
        /* Got valid reply */
        int quality;
        int ber;

        if (sscanf (result_str + 6, "%d, %d", &quality, &ber)) {
            /* 99 means unknown */
            if (quality == 99) {
                g_simple_async_result_take_error (
                    ctx->result,
                    mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK));
            } else {
                /* Normalize the quality */
                quality = CLAMP (quality, 0, 31) * 100 / 31;
                g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                           GUINT_TO_POINTER (quality),
                                                           NULL);
            }

            signal_quality_context_complete_and_free (ctx);
            return;
        }
    }

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Could not parse signal quality results");
    signal_quality_context_complete_and_free (ctx);
}

/* Some modems want +CSQ, others want +CSQ?, and some of both types
 * will return ERROR if they don't get the command they want.  So
 * try the other command if the first one fails.
 */
static const MMBaseModemAtCommand signal_quality_csq_sequence[] = {
    { "+CSQ",  3, TRUE, response_processor_string_ignore_at_errors },
    { "+CSQ?", 3, TRUE, response_processor_string_ignore_at_errors },
    { NULL }
};

static void
signal_quality_csq (SignalQualityContext *ctx)
{
    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (ctx->self),
        MM_AT_SERIAL_PORT (ctx->port),
        signal_quality_csq_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)signal_quality_csq_ready,
        ctx);
}

static guint
normalize_ciev_cind_signal_quality (guint quality,
                                    guint min,
                                    guint max)
{
    if (!max &&
        quality >= 0) {
        /* If we didn't get a max, assume it was 5. Note that we do allow
         * 0, meaning no signal at all. */
        return (quality * 20);
    }

    if (quality >= min &&
        quality <= max)
        return ((100 * (quality - min)) / (max - min));

    /* Value out of range, assume no signal here. Some modems (Cinterion
     * for example) will send out-of-range values when they cannot get
     * the signal strength. */
    return 0;
}

static void
signal_quality_cind_ready (MMBroadbandModem *self,
                           GAsyncResult *res,
                           SignalQualityContext *ctx)
{
    GError *error = NULL;
    const gchar *result;
    GByteArray *indicators;
    guint quality;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!error)
        indicators = mm_3gpp_parse_cind_read_response (result, &error);

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        signal_quality_context_complete_and_free (ctx);
        return;
    }

    if (indicators->len < self->priv->modem_cind_indicator_signal_quality) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Could not parse CIND signal quality results "
                                         "signal index (%u) outside received range (0-%u)",
                                         self->priv->modem_cind_indicator_signal_quality,
                                         indicators->len);
        g_byte_array_free (indicators, TRUE);
        signal_quality_context_complete_and_free (ctx);
        return;
    }

    quality = g_array_index (indicators,
                             guint8,
                             self->priv->modem_cind_indicator_signal_quality);
    g_byte_array_free (indicators, TRUE);

    quality = normalize_ciev_cind_signal_quality (quality,
                                                  self->priv->modem_cind_min_signal_quality,
                                                  self->priv->modem_cind_max_signal_quality);

    /* Some LTE devices say they support signal via CIND,
     * but always report zero even though they have signal.  So
     * if we get zero signal, try CSQ too.  (bgo #636040)
     */
    if (quality == 0) {
        signal_quality_csq (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (quality),
                                               NULL);
    signal_quality_context_complete_and_free (ctx);
}

static void
signal_quality_cind (SignalQualityContext *ctx)
{
    mm_base_modem_at_command_full (MM_BASE_MODEM (ctx->self),
                                   MM_AT_SERIAL_PORT (ctx->port),
                                   "+CIND?",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)signal_quality_cind_ready,
                                   ctx);
}

static void
signal_quality_qcdm_ready (MMQcdmSerialPort *port,
                           GByteArray *response,
                           GError *error,
                           SignalQualityContext *ctx)
{
    QcdmResult *result;
    guint32 num = 0, quality = 0, i;
    gfloat best_db = -28;
    gint err = QCDM_SUCCESS;

    if (error) {
        g_simple_async_result_set_from_error (ctx->result, error);
        signal_quality_context_complete_and_free (ctx);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_pilot_sets_result ((const gchar *) response->data,
                                         response->len,
                                         &err);
    if (!result) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Failed to parse pilot sets command result: %d",
                                         err);
        signal_quality_context_complete_and_free (ctx);
        return;
    }

    qcdm_cmd_pilot_sets_result_get_num (result, QCDM_CMD_PILOT_SETS_TYPE_ACTIVE, &num);
    for (i = 0; i < num; i++) {
        guint32 pn_offset = 0, ecio = 0;
        gfloat db = 0;

        qcdm_cmd_pilot_sets_result_get_pilot (result,
                                              QCDM_CMD_PILOT_SETS_TYPE_ACTIVE,
                                              i,
                                              &pn_offset,
                                              &ecio,
                                              &db);
        best_db = MAX (db, best_db);
    }
    qcdm_result_unref (result);

    if (num > 0) {
        #define BEST_ECIO 3
        #define WORST_ECIO 25

        /* EC/IO dB ranges from roughly 0 to -31 dB.  Lower == worse.  We
         * really only care about -3 to -25 dB though, since that's about what
         * you'll see in real-world usage.
         */
        best_db = CLAMP (ABS (best_db), BEST_ECIO, WORST_ECIO) - BEST_ECIO;
        quality = (guint32) (100 - (best_db * 100 / (WORST_ECIO - BEST_ECIO)));
    }

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (quality),
                                               NULL);
    signal_quality_context_complete_and_free (ctx);
}

static void
signal_quality_qcdm (SignalQualityContext *ctx)
{
    GByteArray *pilot_sets;

    /* Use CDMA1x pilot EC/IO if we can */
    pilot_sets = g_byte_array_sized_new (25);
    pilot_sets->len = qcdm_cmd_pilot_sets_new ((char *) pilot_sets->data, 25);
    g_assert (pilot_sets->len);

    mm_qcdm_serial_port_queue_command (MM_QCDM_SERIAL_PORT (ctx->port),
                                       pilot_sets,
                                       3,
                                       NULL,
                                       (MMQcdmSerialResponseFn)signal_quality_qcdm_ready,
                                       ctx);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    SignalQualityContext *ctx;
    GError *error = NULL;

    mm_dbg ("loading signal quality...");
    ctx = g_new0 (SignalQualityContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_signal_quality);

    /* Check whether we can get a non-connected AT port */
    ctx->port = (MMSerialPort *)mm_base_modem_get_best_at_port (MM_BASE_MODEM (self), &error);
    if (ctx->port) {
        if (MM_BROADBAND_MODEM (self)->priv->modem_cind_supported)
            signal_quality_cind (ctx);
        else
            signal_quality_csq (ctx);
        return;
    }

    /* If no best AT port available (all connected), try with QCDM ports */
    ctx->port = (MMSerialPort *)mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));
    if (ctx->port) {
        g_error_free (error);
        signal_quality_qcdm (ctx);
        return;
    }

    /* Return the error we got when getting best AT port */
    g_simple_async_result_take_error (ctx->result, error);
    signal_quality_context_complete_and_free (ctx);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
ciev_received (MMAtSerialPort *port,
               GMatchInfo *info,
               MMBroadbandModem *self)
{
    gint ind = 0;
    gchar *item;

    item = g_match_info_fetch (info, 1);
    if (item)
        ind = atoi (item);

    /* Handle signal quality change indication */
    if (ind == self->priv->modem_cind_indicator_signal_quality ||
        g_str_equal (item, "signal")) {
        gchar *value;

        value = g_match_info_fetch (info, 2);
        if (value) {
            gint quality = 0;

            quality = atoi (value);

            mm_iface_modem_update_signal_quality (
                MM_IFACE_MODEM (self),
                normalize_ciev_cind_signal_quality (quality,
                                                    self->priv->modem_cind_min_signal_quality,
                                                    self->priv->modem_cind_max_signal_quality));
            g_free (value);
        }
    }

    g_free (item);

    /* FIXME: handle roaming and service indicators.
     * ... wait, arent these already handle by unsolicited CREG responses? */
}

static void
set_unsolicited_events_handlers (MMBroadbandModem *self,
                                 gboolean enable)
{
    MMAtSerialPort *ports[2];
    GRegex *ciev_regex;
    guint i;

    ciev_regex = mm_3gpp_ciev_regex_get ();
    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Set/unset unsolicited CIEV event handler */
        mm_dbg ("(%s) %s 3GPP unsolicited events handlers",
                mm_port_get_device (MM_PORT (ports[i])),
                enable ? "Setting" : "Removing");
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            ciev_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn) ciev_received : NULL,
            enable ? self : NULL,
            NULL);
    }

    g_regex_unref (ciev_regex);
}

static void
cind_format_check_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GHashTable *indicators = NULL;
    GError *error = NULL;
    const gchar *result;
    MM3gppCindResponse *r;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error ||
        !(indicators = mm_3gpp_parse_cind_test_response (result, &error))) {
        /* unsupported indications */
        mm_dbg ("Marking indications as unsupported: '%s'", error->message);
        g_error_free (error);
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Mark CIND as being supported and find the proper indexes for the
     * indicators. */
    self->priv->modem_cind_supported = TRUE;

    /* Check if we support signal quality indications */
    r = g_hash_table_lookup (indicators, "signal");
    if (r) {
        self->priv->modem_cind_indicator_signal_quality = mm_3gpp_cind_response_get_index (r);
        self->priv->modem_cind_min_signal_quality = mm_3gpp_cind_response_get_min (r);
        self->priv->modem_cind_max_signal_quality = mm_3gpp_cind_response_get_max (r);

        mm_dbg ("Modem supports signal quality indications via CIND at index '%u'"
                "(min: %u, max: %u)",
                self->priv->modem_cind_indicator_signal_quality,
                self->priv->modem_cind_min_signal_quality,
                self->priv->modem_cind_max_signal_quality);
    } else
        self->priv->modem_cind_indicator_signal_quality = CIND_INDICATOR_INVALID;

    /* Check if we support roaming indications */
    r = g_hash_table_lookup (indicators, "roam");
    if (r) {
        self->priv->modem_cind_indicator_roaming = mm_3gpp_cind_response_get_index (r);
        mm_dbg ("Modem supports roaming indications via CIND at index '%u'",
                self->priv->modem_cind_indicator_roaming);
    } else
        self->priv->modem_cind_indicator_roaming = CIND_INDICATOR_INVALID;

    /* Check if we support service indications */
    r = g_hash_table_lookup (indicators, "service");
    if (r) {
        self->priv->modem_cind_indicator_service = mm_3gpp_cind_response_get_index (r);
        mm_dbg ("Modem supports service indications via CIND at index '%u'",
                self->priv->modem_cind_indicator_service);
    } else
        self->priv->modem_cind_indicator_service = CIND_INDICATOR_INVALID;

    g_hash_table_destroy (indicators);

    /* Now, keep on setting up the ports */
    set_unsolicited_events_handlers (self, TRUE);

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *_self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_setup_unsolicited_events);

    /* Load supported indicators */
    if (!self->priv->modem_cind_support_checked) {
        mm_dbg ("Checking indicator support...");
        self->priv->modem_cind_support_checked = TRUE;
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+CIND=?",
                                  3,
                                  TRUE,
                                  (GAsyncReadyCallback)cind_format_check_ready,
                                  result);
        return;
    }

    /* If supported, go on */
    if (self->priv->modem_cind_supported)
        set_unsolicited_events_handlers (self, TRUE);

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *_self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_cleanup_unsolicited_events);

    /* If supported, go on */
    if (self->priv->modem_cind_support_checked && self->priv->modem_cind_supported)
        set_unsolicited_events_handlers (self, FALSE);

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Enabling/disabling unsolicited events (3GPP interface) */

typedef struct {
    MMBroadbandModem *self;
    gchar *command;
    gboolean enable;
    GSimpleAsyncResult *result;
    gboolean cmer_primary_done;
    gboolean cmer_secondary_done;
} UnsolicitedEventsContext;

static void
unsolicited_events_context_complete_and_free (UnsolicitedEventsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx->command);
    g_free (ctx);
}

static gboolean
modem_3gpp_enable_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void run_unsolicited_events_setup (UnsolicitedEventsContext *ctx);

static void
unsolicited_events_setup_ready (MMBroadbandModem *self,
                                GAsyncResult *res,
                                UnsolicitedEventsContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!error) {
        /* Run on next port, if any */
        run_unsolicited_events_setup (ctx);
        return;
    }

    mm_dbg ("Couldn't %s event reporting: '%s'",
            ctx->enable ? "enable" : "disable",
            error->message);
    g_error_free (error);
    /* Consider this operation complete, ignoring errors */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    unsolicited_events_context_complete_and_free (ctx);
}

static void
run_unsolicited_events_setup (UnsolicitedEventsContext *ctx)
{
    MMAtSerialPort *port = NULL;

    if (!ctx->cmer_primary_done) {
        ctx->cmer_primary_done = TRUE;
        port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (ctx->self));
    } else if (!ctx->cmer_secondary_done) {
        ctx->cmer_secondary_done = TRUE;
        port = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (ctx->self));
    }

    /* Enable unsolicited events in given port */
    if (port) {
        mm_base_modem_at_command_full (MM_BASE_MODEM (ctx->self),
                                       port,
                                       ctx->command,
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)unsolicited_events_setup_ready,
                                       ctx);
        return;
    }

    /* If no more ports, we're fully done now */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    unsolicited_events_context_complete_and_free (ctx);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_enable_unsolicited_events);

    /* If supported, go on */
    if (self->priv->modem_cind_support_checked && self->priv->modem_cind_supported) {
        UnsolicitedEventsContext *ctx;

        ctx = g_new0 (UnsolicitedEventsContext, 1);
        ctx->self = g_object_ref (self);
        ctx->enable = TRUE;
        ctx->command = g_strdup ("+CMER=3,0,0,1");
        ctx->result = result;
        run_unsolicited_events_setup (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *_self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_disable_unsolicited_events);

    /* If supported, go on */
    if (self->priv->modem_cind_support_checked && self->priv->modem_cind_supported) {
        UnsolicitedEventsContext *ctx;

        ctx = g_new0 (UnsolicitedEventsContext, 1);
        ctx->self = g_object_ref (self);
        ctx->command = g_strdup ("+CMER=0");
        ctx->result = result;
        run_unsolicited_events_setup (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Setting modem charset (Modem interface) */

typedef struct {
    GSimpleAsyncResult *result;
    MMModemCharset charset;
    /* Commands to try in the sequence:
     *  First one with quotes
     *  Second without.
     *  + last NUL */
    MMBaseModemAtCommand charset_commands[3];
} SetupCharsetContext;

static void
setup_charset_context_free (SetupCharsetContext *ctx)
{
    g_object_unref (ctx->result);
    g_free (ctx->charset_commands[0].command);
    g_free (ctx->charset_commands[1].command);
    g_free (ctx);
}

static gboolean
modem_setup_charset_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    return TRUE;
}

static void
current_charset_query_ready (MMBroadbandModem *self,
                             GAsyncResult *res,
                             SetupCharsetContext *ctx)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response)
        g_simple_async_result_take_error (ctx->result, error);
    else {
        MMModemCharset current;
        const gchar *p;

        p = response;
        if (g_str_has_prefix (p, "+CSCS:"))
            p += 6;
        while (*p == ' ')
            p++;

        current = mm_modem_charset_from_string (p);
        if (ctx->charset != current)
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Modem failed to change character set to %s",
                                             mm_modem_charset_to_string (ctx->charset));
        else {
            /* We'll keep track ourselves of the current charset.
             * TODO: Make this a property so that plugins can also store it. */
            self->priv->modem_current_charset = current;
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        }
    }

    g_simple_async_result_complete (ctx->result);
    setup_charset_context_free (ctx);
}

static void
charset_change_ready (MMBroadbandModem *self,
                      GAsyncResult *res,
                      SetupCharsetContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete (ctx->result);
        setup_charset_context_free (ctx);
        return;
    }

    /* Check whether we did properly set the charset */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CSCS?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)current_charset_query_ready,
                              ctx);
}

static void
modem_setup_charset (MMIfaceModem *self,
                     MMModemCharset charset,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    SetupCharsetContext *ctx;
    const gchar *charset_str;

    /* NOTE: we already notified that CDMA-only modems couldn't load supported
     * charsets, so we'll never get here in such a case */
    g_assert (mm_iface_modem_is_cdma_only (self) == FALSE);

    /* Build charset string to use */
    charset_str = mm_modem_charset_to_string (charset);
    if (!charset_str) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Unhandled character set 0x%X",
                                             charset);
        return;
    }

    /* Setup context, including commands to try */
    ctx = g_new0 (SetupCharsetContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_setup_charset);
    ctx->charset = charset;
    /* First try, with quotes */
    ctx->charset_commands[0].command = g_strdup_printf ("+CSCS=\"%s\"", charset_str);
    ctx->charset_commands[0].timeout = 3;
    ctx->charset_commands[0].allow_cached = FALSE;
    ctx->charset_commands[0].response_processor = mm_base_modem_response_processor_no_result;
    /* Second try.
     * Some modems puke if you include the quotes around the character
     * set name, so lets try it again without them.
     */
    ctx->charset_commands[1].command = g_strdup_printf ("+CSCS=%s", charset_str);
    ctx->charset_commands[1].timeout = 3;
    ctx->charset_commands[1].allow_cached = FALSE;
    ctx->charset_commands[1].response_processor = mm_base_modem_response_processor_no_result;

    /* Launch sequence */
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        ctx->charset_commands,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        (GAsyncReadyCallback)charset_change_ready,
        ctx);
}

/*****************************************************************************/
/* Loading supported charsets (Modem interface) */

static MMModemCharset
modem_load_supported_charsets_finish (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CHARSET_UNKNOWN;

    return GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
cscs_format_check_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    MMModemCharset charsets = MM_MODEM_CHARSET_UNKNOWN;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else if (!mm_3gpp_parse_cscs_test_response (response, &charsets))
        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse the supported character "
            "sets response");
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (charsets),
                                                   NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_supported_charsets (MMIfaceModem *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_supported_charsets);

    /* CDMA-only modems don't need this */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_dbg ("Skipping supported charset loading in CDMA-only modem...");
        g_simple_async_result_set_op_res_gpointer (result,
                                                   GUINT_TO_POINTER (MM_MODEM_CHARSET_UNKNOWN),
                                                   NULL);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CSCS=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cscs_format_check_ready,
                              result);
}

/*****************************************************************************/
/* configuring flow control (Modem interface) */

static gboolean
modem_setup_flow_control_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    /* Completely ignore errors */
    return TRUE;
}

static void
modem_setup_flow_control (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* By default, try to set XOFF/XON flow control */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+IFC=1,1",
                              3,
                              FALSE,
                              NULL,
                              NULL);

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_setup_flow_control);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Powering up the modem (Modem interface) */

static gboolean
modem_power_up_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    /* By default, errors in the power up command are ignored.
     * Plugins wanting to treat power up errors should subclass the power up
     * handling. */
    return TRUE;
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* CDMA-only modems don't need this */
    if (mm_iface_modem_is_cdma_only (self))
        mm_dbg ("Skipping Power-up in CDMA-only modem...");
    else
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+CFUN=1",
                                  5,
                                  FALSE,
                                  NULL,
                                  NULL);

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_up);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Sending a command to the modem (Modem interface) */

static const gchar *
modem_command_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    return mm_base_modem_at_command_finish (MM_BASE_MODEM (self),
                                            res,
                                            error);
}

static void
modem_command (MMIfaceModem *self,
               const gchar *cmd,
               guint timeout,
               GAsyncReadyCallback callback,
               gpointer user_data)
{

    mm_base_modem_at_command (MM_BASE_MODEM (self), cmd, timeout,
                              FALSE,
                              callback,
                              user_data);
}


/*****************************************************************************/
/* Initializing the modem (Modem interface) */

static gboolean
modem_init_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
modem_init_sequence_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (MM_BASE_MODEM (self), res, NULL, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static const MMBaseModemAtCommand modem_init_sequence[] = {
    /* Init command. ITU rec v.250 (6.1.1) says:
     *   The DTE should not include additional commands on the same command line
     *   after the Z command because such commands may be ignored.
     * So run ATZ alone.
     */
    { "Z", 6, FALSE, mm_base_modem_response_processor_no_result_continue },

    /* Ensure echo is off after the init command */
    { "E0 V1",      3, FALSE, NULL },

    /* Some phones (like Blackberries) don't support +CMEE=1, so make it
     * optional.  It completely violates 3GPP TS 27.007 (9.1) but what can we do...
     */
    { "+CMEE=1", 3, FALSE, NULL },

    /* Additional OPTIONAL initialization */
    { "X4 &C1",  3, FALSE, NULL },

    { NULL }
};

static void
modem_init (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMAtSerialPort *primary;
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_init);

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Need primary AT port to run modem init sequence");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_sequence_full (MM_BASE_MODEM (self),
                                    primary,
                                    modem_init_sequence,
                                    NULL,  /* response_processor_context */
                                    NULL,  /* response_processor_context_free */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback)modem_init_sequence_ready,
                                    result);
}

/*****************************************************************************/
/* IMEI loading (3GPP interface) */

static gchar *
modem_3gpp_load_imei_finish (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             GError **error)
{
    gchar *imei;

    imei = g_strdup (mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error));
    if (!imei)
        return NULL;

    mm_dbg ("loaded IMEI: %s", imei);
    return imei;
}

static void
modem_3gpp_load_imei (MMIfaceModem3gpp *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    mm_dbg ("loading IMEI...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGSN",
                              3,
                              TRUE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Facility locks status loading (3GPP interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    guint current;
    MMModem3gppFacility facilities;
    MMModem3gppFacility locks;
} LoadEnabledFacilityLocksContext;

static void get_next_facility_lock_status (LoadEnabledFacilityLocksContext *ctx);

static void
load_enabled_facility_locks_context_complete_and_free (LoadEnabledFacilityLocksContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static MMModem3gppFacility
modem_3gpp_load_enabled_facility_locks_finish (MMIfaceModem3gpp *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_3GPP_FACILITY_NONE;

    return ((MMModem3gppFacility) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

static void
clck_single_query_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         LoadEnabledFacilityLocksContext *ctx)
{
    const gchar *response;
    gboolean enabled = FALSE;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (response &&
        mm_3gpp_parse_clck_write_response (response, &enabled) &&
        enabled) {
        ctx->locks |= (1 << ctx->current);
    } else {
        /* On errors, we'll just assume disabled */
        ctx->locks &= ~(1 << ctx->current);
    }

    /* And go on with the next one */
    ctx->current++;
    get_next_facility_lock_status (ctx);
}

static void
get_next_facility_lock_status (LoadEnabledFacilityLocksContext *ctx)
{
    guint i;

    for (i = ctx->current; i < sizeof (MMModem3gppFacility) * 8; i++) {
        guint32 facility = 1 << i;

        /* Found the next one to query! */
        if (ctx->facilities & facility) {
            gchar *cmd;

            /* Keep the current one */
            ctx->current = i;

            /* Query current */
            cmd = g_strdup_printf ("+CLCK=\"%s\",2",
                                   mm_3gpp_facility_to_acronym (facility));
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      cmd,
                                      3,
                                      FALSE,
                                      (GAsyncReadyCallback)clck_single_query_ready,
                                      ctx);
            g_free (cmd);
            return;
        }
    }

    /* No more facilities to query, all done */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (ctx->locks),
                                               NULL);
    load_enabled_facility_locks_context_complete_and_free (ctx);
}

static void
clck_test_ready (MMBaseModem *self,
                 GAsyncResult *res,
                 LoadEnabledFacilityLocksContext *ctx)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_simple_async_result_take_error (ctx->result, error);
        load_enabled_facility_locks_context_complete_and_free (ctx);
        return;
    }

    if (!mm_3gpp_parse_clck_test_response (response, &ctx->facilities)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse list of available lock facilities: '%s'",
                                         response);
        load_enabled_facility_locks_context_complete_and_free (ctx);
        return;
    }

    /* Go on... */
    get_next_facility_lock_status (ctx);
}

static void
modem_3gpp_load_enabled_facility_locks (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    LoadEnabledFacilityLocksContext *ctx;

    ctx = g_new (LoadEnabledFacilityLocksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_load_enabled_facility_locks);
    ctx->facilities = MM_MODEM_3GPP_FACILITY_NONE;
    ctx->locks = MM_MODEM_3GPP_FACILITY_NONE;
    ctx->current = 0;

    mm_dbg ("loading enabled facility locks...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CLCK=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)clck_test_ready,
                              ctx);
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
                              "+COPS=3,2;+COPS?",
                              3,
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

    operator_name = mm_3gpp_parse_operator (result, MM_BROADBAND_MODEM (self)->priv->modem_current_charset);
    if (operator_name)
        mm_dbg ("loaded Operator Name: %s", operator_name);

    return operator_name;
}

static void
modem_3gpp_load_operator_name (MMIfaceModem3gpp *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    mm_dbg ("loading Operator Name...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+COPS=3,0;+COPS?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Unsolicited registration messages handling (3GPP interface) */

static gboolean
modem_3gpp_setup_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                         GAsyncResult *res,
                                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
registration_state_changed (MMAtSerialPort *port,
                            GMatchInfo *match_info,
                            MMBroadbandModem *self)
{
    MMModem3gppRegistrationState state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    gulong lac = 0, cell_id = 0;
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gboolean cgreg = FALSE;
    GError *error = NULL;

    if (!mm_3gpp_parse_creg_response (match_info,
                                      &state,
                                      &lac,
                                      &cell_id,
                                      &act,
                                      &cgreg,
                                      &error)) {
        mm_warn ("error parsing unsolicited registration: %s",
                 error && error->message ? error->message : "(unknown)");
        g_clear_error (&error);
        return;
    }

    /* Report new registration state */
    if (cgreg)
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), state);
    else
        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), state);

    mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), act);
    mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, cell_id);
}

static void
modem_3gpp_setup_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *ports[2];
    GPtrArray *array;
    guint i;
    guint j;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_setup_unsolicited_registration_events);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Set up CREG unsolicited message handlers in both ports */
    array = mm_3gpp_creg_regex_get (FALSE);
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        mm_dbg ("(%s) setting up 3GPP unsolicited registration messages handlers",
                mm_port_get_device (MM_PORT (ports[i])));
        for (j = 0; j < array->len; j++) {
            mm_at_serial_port_add_unsolicited_msg_handler (
                MM_AT_SERIAL_PORT (ports[i]),
                (GRegex *) g_ptr_array_index (array, j),
                (MMAtSerialUnsolicitedMsgFn)registration_state_changed,
                self,
                NULL);
        }
    }
    mm_3gpp_creg_regex_destroy (array);

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Unsolicited registration messages cleaning up (3GPP interface) */

static gboolean
modem_3gpp_cleanup_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                           GAsyncResult *res,
                                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
modem_3gpp_cleanup_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *ports[2];
    GPtrArray *array;
    guint i;
    guint j;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_cleanup_unsolicited_registration_events);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Set up CREG unsolicited message handlers in both ports */
    array = mm_3gpp_creg_regex_get (FALSE);
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        mm_dbg ("(%s) cleaning up unsolicited registration messages handlers",
                mm_port_get_device (MM_PORT (ports[i])));

        for (j = 0; j < array->len; j++) {
            mm_at_serial_port_add_unsolicited_msg_handler (
                MM_AT_SERIAL_PORT (ports[i]),
                (GRegex *) g_ptr_array_index (array, j),
                NULL,
                NULL,
                NULL);
        }
    }
    mm_3gpp_creg_regex_destroy (array);

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Scan networks (3GPP interface) */

static GList *
modem_3gpp_scan_networks_finish (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    const gchar *result;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    return mm_3gpp_parse_cops_test_response (result, error);
}

static void
modem_3gpp_scan_networks (MMIfaceModem3gpp *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+COPS=?",
                              120,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

static gboolean
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !!mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                const gchar *operator_id,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    gchar *command;

    /* If the user sent a specific network to use, lock it in. */
    if (operator_id)
        command = g_strdup_printf ("+COPS=1,2,\"%s\"", operator_id);
    /* If no specific network was given, and the modem is not registered and not
     * searching, kick it to search for a network. Also do auto registration if
     * the modem had been set to manual registration last time but now is not.
     */
    else
        /* Note that '+COPS=0,,' (same but with commas) won't work in some Nokia
         * phones */
        command = g_strdup ("+COPS=0");

    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   mm_base_modem_peek_best_at_port (MM_BASE_MODEM (self), NULL),
                                   command,
                                   120,
                                   FALSE,
                                   FALSE, /* raw */
                                   cancellable,
                                   callback,
                                   user_data);
}

/*****************************************************************************/
/* Registration checks (3GPP interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    gboolean cs_supported;
    gboolean ps_supported;
    gboolean run_cs;
    gboolean run_ps;
    gboolean running_cs;
    gboolean running_ps;
    GError *cs_error;
    GError *ps_error;
} RunRegistrationChecksContext;

static void
run_registration_checks_context_complete_and_free (RunRegistrationChecksContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->cs_error)
        g_error_free (ctx->cs_error);
    if (ctx->ps_error)
        g_error_free (ctx->ps_error);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void run_registration_checks_context_step (RunRegistrationChecksContext *ctx);

static void
registration_status_check_ready (MMBroadbandModem *self,
                                 GAsyncResult *res,
                                 RunRegistrationChecksContext *ctx)
{
    const gchar *response;
    GError *error = NULL;
    GMatchInfo *match_info;
    guint i;
    gboolean parsed;
    gboolean cgreg;
    MMModem3gppRegistrationState state;
    MMModemAccessTechnology act;
    gulong lac;
    gulong cid;

    /* Only one must be running */
    g_assert (!(ctx->running_ps && ctx->running_cs) &&
              (ctx->running_cs || ctx->running_ps));

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_assert (error != NULL);
        if (ctx->running_cs)
            ctx->cs_error = error;
        else
            ctx->ps_error = error;

        run_registration_checks_context_step (ctx);
        return;
    }

    /* Unsolicited registration status handlers will usually process the
     * response for us, but just in case they don't, do that here.
     */
    if (!response[0]) {
        /* Done */
        run_registration_checks_context_step (ctx);
        return;
    }

    /* Try to match the response */
    for (i = 0;
         i < self->priv->modem_3gpp_registration_regex->len;
         i++) {
        if (g_regex_match ((GRegex *)g_ptr_array_index (
                               self->priv->modem_3gpp_registration_regex, i),
                           response,
                           0,
                           &match_info))
            break;
        g_match_info_free (match_info);
        match_info = NULL;
    }

    if (!match_info) {
        error = g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Unknown registration status response: '%s'",
                             response);
        if (ctx->running_cs)
            ctx->cs_error = error;
        else
            ctx->ps_error = error;

        run_registration_checks_context_step (ctx);
        return;
    }

    cgreg = FALSE;
    state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    lac = 0;
    cid = 0;
    parsed = mm_3gpp_parse_creg_response (match_info,
                                          &state,
                                          &lac,
                                          &cid,
                                          &act,
                                          &cgreg,
                                          &error);
    g_match_info_free (match_info);

    if (!parsed) {
        if (!error)
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Error parsing registration response: '%s'",
                                 response);
        if (ctx->running_cs)
            ctx->cs_error = error;
        else
            ctx->ps_error = error;
        run_registration_checks_context_step (ctx);
        return;
    }

    /* Report new registration state */
    if (cgreg) {
        if (ctx->running_cs)
            mm_dbg ("Got PS registration state when checking CS registration state");
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), state);
    } else {
        if (ctx->running_ps)
            mm_dbg ("Got CS registration state when checking PS registration state");
        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), state);
    }

    mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), act);
    mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, cid);

    run_registration_checks_context_step (ctx);
}

static void
run_registration_checks_context_step (RunRegistrationChecksContext *ctx)
{
    ctx->running_cs = FALSE;
    ctx->running_ps = FALSE;

    if (ctx->run_cs) {
        ctx->running_cs = TRUE;
        ctx->run_cs = FALSE;
        /* Check current CS-registration state. */
        mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                  "+CREG?",
                                  10,
                                  FALSE,
                                  (GAsyncReadyCallback)registration_status_check_ready,
                                  ctx);
        return;
    }

    if (ctx->run_ps) {
        ctx->running_ps = TRUE;
        ctx->run_ps = FALSE;
        /* Check current PS-registration state. */
        mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                  "+CGREG?",
                                  10,
                                  FALSE,
                                  (GAsyncReadyCallback)registration_status_check_ready,
                                  ctx);
        return;
    }

    /* If all run checks returned errors we fail */
    if ((ctx->ps_supported && ctx->ps_error && ctx->cs_supported && ctx->cs_error) ||
        (ctx->ps_supported && ctx->ps_error && !ctx->cs_supported) ||
        (ctx->cs_supported && ctx->cs_error && !ctx->ps_supported)) {
        /* Prefer the PS error if any */
        if (ctx->ps_error) {
            g_simple_async_result_set_from_error (ctx->result, ctx->ps_error);
            ctx->ps_error = NULL;
        } else if (ctx->cs_error) {
            g_simple_async_result_set_from_error (ctx->result, ctx->cs_error);
            ctx->cs_error = NULL;
        } else
            g_assert_not_reached ();
    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    run_registration_checks_context_complete_and_free (ctx);
}

static void
modem_3gpp_run_registration_checks (MMIfaceModem3gpp *self,
                                    gboolean cs_supported,
                                    gboolean ps_supported,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    RunRegistrationChecksContext *ctx;

    ctx = g_new0 (RunRegistrationChecksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_run_registration_checks);
    ctx->cs_supported = cs_supported;
    ctx->ps_supported = ps_supported;
    ctx->run_cs = cs_supported;
    ctx->run_ps = ps_supported;

    run_registration_checks_context_step (ctx);
}

/*****************************************************************************/
/* Enable/Disable unsolicited registration events (3GPP interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    gboolean enable; /* TRUE for enabling, FALSE for disabling */
    gboolean run_cs;
    gboolean run_ps;
    gboolean running_cs;
    gboolean running_ps;
    GError *cs_error;
    GError *ps_error;
    gboolean secondary_sequence;
    gboolean secondary_done;
} UnsolicitedRegistrationEventsContext;

static void
unsolicited_registration_events_context_complete_and_free (UnsolicitedRegistrationEventsContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->cs_error)
        g_error_free (ctx->cs_error);
    if (ctx->ps_error)
        g_error_free (ctx->ps_error);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static UnsolicitedRegistrationEventsContext *
unsolicited_registration_events_context_new (MMBroadbandModem *self,
                                             gboolean enable,
                                             gboolean cs_supported,
                                             gboolean ps_supported,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    UnsolicitedRegistrationEventsContext *ctx;

    ctx = g_new0 (UnsolicitedRegistrationEventsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             unsolicited_registration_events_context_new);
    ctx->enable = FALSE;
    ctx->run_cs = cs_supported;
    ctx->run_ps = ps_supported;

    return ctx;
}

static gboolean
modem_3gpp_enable_disable_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                                  GAsyncResult *res,
                                                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
parse_registration_setup_reply (MMBaseModem *self,
                                gpointer none,
                                const gchar *command,
                                const gchar *response,
                                gboolean last_command,
                                const GError *error,
                                GVariant **result,
                                GError **result_error)
{
    /* If error, try next command */
    if (error)
        return FALSE;

    /* Set COMMAND as result! */
    *result = g_variant_new_string (command);
    return TRUE;
}

static const MMBaseModemAtCommand cs_registration_sequence[] = {
    /* Enable unsolicited registration notifications in CS network, with location */
    { "+CREG=2", 3, FALSE, parse_registration_setup_reply },
    /* Enable unsolicited registration notifications in CS network, without location */
    { "+CREG=1", 3, FALSE, parse_registration_setup_reply },
    { NULL }
};

static const MMBaseModemAtCommand cs_unregistration_sequence[] = {
    /* Disable unsolicited registration notifications in CS network */
    { "+CREG=0", 3, FALSE, parse_registration_setup_reply },
    { NULL }
};

static const MMBaseModemAtCommand ps_registration_sequence[] = {
    /* Enable unsolicited registration notifications in PS network, with location */
    { "+CGREG=2", 3, FALSE, parse_registration_setup_reply },
    /* Enable unsolicited registration notifications in PS network, without location */
    { "+CGREG=1", 3, FALSE, parse_registration_setup_reply },
    { NULL }
};

static const MMBaseModemAtCommand ps_unregistration_sequence[] = {
    /* Disable unsolicited registration notifications in PS network */
    { "+CGREG=0", 3, FALSE, parse_registration_setup_reply },
    { NULL }
};

static void unsolicited_registration_events_context_step (UnsolicitedRegistrationEventsContext *ctx);

static void
unsolicited_registration_events_sequence_ready (MMBroadbandModem *self,
                                                GAsyncResult *res,
                                                UnsolicitedRegistrationEventsContext *ctx)
{
    GError *error = NULL;
    GVariant *command;
    MMAtSerialPort *secondary;

    /* Only one must be running */
    g_assert (!(ctx->running_ps && ctx->running_cs) &&
              (ctx->running_cs || ctx->running_ps));

    if (ctx->secondary_done) {
        if (ctx->secondary_sequence)
            mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, &error);
        else
            mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, &error);

        if (error) {
            mm_dbg ("%s unsolicited registration events in secondary port failed: '%s'",
                    ctx->enable ? "Enabling" : "Disabling",
                    error->message);
            /* Keep errors reported */
            if (ctx->running_cs && !ctx->cs_error)
                ctx->cs_error = error;
            else if (ctx->running_ps && !ctx->ps_error)
                ctx->ps_error = error;
            else
                g_error_free (error);
        } else {
            /* If successful in secondary port, cleanup primary error if any */
            if (ctx->running_cs && ctx->cs_error) {
                g_error_free (ctx->cs_error);
                ctx->cs_error = NULL;
            }
            else if (ctx->running_ps && ctx->ps_error) {
                g_error_free (ctx->ps_error);
                ctx->ps_error = NULL;
            }
        }

        /* Done with primary and secondary, keep on */
        unsolicited_registration_events_context_step (ctx);
        return;
    }

    /*  We just run the sequence in the primary port */
    command = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, &error);
    if (!command) {
        if (!error)
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "AT sequence failed");
        mm_dbg ("%s unsolicited registration events in primary port failed: '%s'",
                ctx->enable ? "Enabling" : "Disabling",
                error->message);
        /* Keep errors reported */
        if (ctx->running_cs)
            ctx->cs_error = error;
        else
            ctx->ps_error = error;
        /* Even if primary failed, go on and try to enable in secondary port */
    }

    secondary = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));
    if (secondary) {
        ctx->secondary_done = TRUE;

        /* Now use the same registration setup in secondary port, if any */
        if (command) {
            mm_base_modem_at_command_full (
                MM_BASE_MODEM (self),
                secondary,
                g_variant_get_string (command, NULL),
                3,
                FALSE,
                FALSE, /* raw */
                NULL, /* cancellable */
                (GAsyncReadyCallback)unsolicited_registration_events_sequence_ready,
                ctx);
            return;
        }

        /* If primary failed, run the whole sequence in secondary */
        ctx->secondary_sequence = TRUE;
        mm_base_modem_at_sequence_full (
            MM_BASE_MODEM (self),
            secondary,
            (ctx->running_cs ?
             (ctx->enable ? cs_registration_sequence : cs_unregistration_sequence) :
             (ctx->enable ? ps_registration_sequence : ps_unregistration_sequence)),
            NULL,  /* response processor context */
            NULL,  /* response processor context free */
            NULL, /* cancellable */
            (GAsyncReadyCallback)unsolicited_registration_events_sequence_ready,
            ctx);
        return;
    }

    /* We're done */
    unsolicited_registration_events_context_step (ctx);
}

static void
unsolicited_registration_events_context_step (UnsolicitedRegistrationEventsContext *ctx)
{
    ctx->running_cs = FALSE;
    ctx->running_ps = FALSE;
    ctx->secondary_done = FALSE;

    if (ctx->run_cs) {
        ctx->running_cs = TRUE;
        ctx->run_cs = FALSE;
        mm_base_modem_at_sequence_full (
            MM_BASE_MODEM (ctx->self),
            mm_base_modem_peek_port_primary (MM_BASE_MODEM (ctx->self)),
            cs_registration_sequence,
            NULL,  /* response processor context */
            NULL,  /* response processor context free */
            NULL, /* cancellable */
            (GAsyncReadyCallback)unsolicited_registration_events_sequence_ready,
            ctx);
        return;
    }

    if (ctx->run_ps) {
        ctx->running_ps = TRUE;
        ctx->run_ps = FALSE;
        mm_base_modem_at_sequence_full (
            MM_BASE_MODEM (ctx->self),
            mm_base_modem_peek_port_primary (MM_BASE_MODEM (ctx->self)),
            ps_registration_sequence,
            NULL,  /* response processor context */
            NULL,  /* response processor context free */
            NULL, /* cancellable */
            (GAsyncReadyCallback)unsolicited_registration_events_sequence_ready,
            ctx);
        return;
    }

    /* All done!
     * If we have any error reported, we'll propagate it. PS errors take
     * precendence over CS errors. */
    if (ctx->ps_error) {
        g_simple_async_result_take_error (ctx->result, ctx->ps_error);
        ctx->ps_error = NULL;
    } else if (ctx->cs_error) {
        g_simple_async_result_take_error (ctx->result, ctx->cs_error);
        ctx->cs_error = NULL;
    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    unsolicited_registration_events_context_complete_and_free (ctx);
}

static void
modem_3gpp_disable_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                    gboolean cs_supported,
                                                    gboolean ps_supported,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    unsolicited_registration_events_context_step (
        unsolicited_registration_events_context_new (MM_BROADBAND_MODEM (self),
                                                     FALSE,
                                                     cs_supported,
                                                     ps_supported,
                                                     callback,
                                                     user_data));
}

static void
modem_3gpp_enable_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                   gboolean cs_supported,
                                                   gboolean ps_supported,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    unsolicited_registration_events_context_step (
        unsolicited_registration_events_context_new (MM_BROADBAND_MODEM (self),
                                                     TRUE,
                                                     cs_supported,
                                                     ps_supported,
                                                     callback,
                                                     user_data));
}

/*****************************************************************************/
/* Cancel USSD (3GPP/USSD interface) */

static gboolean
modem_3gpp_ussd_cancel_finish (MMIfaceModem3gppUssd *self,
                               GAsyncResult *res,
                               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cancel_command_ready (MMBroadbandModem *self,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);

    /* Complete the pending action, if any */
    if (self->priv->pending_ussd_action) {
        g_simple_async_result_set_error (self->priv->pending_ussd_action,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "USSD session was cancelled");
        g_simple_async_result_complete_in_idle (self->priv->pending_ussd_action);
        g_object_unref (self->priv->pending_ussd_action);
        self->priv->pending_ussd_action = NULL;
    }

    mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self),
                                           MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE);
}

static void
modem_3gpp_ussd_cancel (MMIfaceModem3gppUssd *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_ussd_cancel);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CUSD=2",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cancel_command_ready,
                              result);
}

/*****************************************************************************/
/* Send command (3GPP/USSD interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    gchar *command;
    gboolean current_is_unencoded;
    gboolean encoded_used;
    gboolean unencoded_used;
} Modem3gppUssdSendContext;

static void
modem_3gpp_ussd_send_context_complete_and_free (Modem3gppUssdSendContext *ctx)
{
    /* We check for result, as we may have already set it in
     * priv->pending_ussd_request */
    if (ctx->result) {
        g_simple_async_result_complete_in_idle (ctx->result);
        g_object_unref (ctx->result);
    }
    g_object_unref (ctx->self);
    g_free (ctx->command);
    g_free (ctx);
}

static const gchar *
modem_3gpp_ussd_send_finish (MMIfaceModem3gppUssd *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* We can return the string as constant because it is owned by the async
     * result, which will be valid during the whole call of its callback, which
     * is when we're actually calling finish() */
    return (const gchar *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void modem_3gpp_ussd_context_step (Modem3gppUssdSendContext *ctx);

static void
ussd_send_command_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         Modem3gppUssdSendContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* Some immediate error happened when sending the USSD request */
        mm_dbg ("Error sending USSD request: '%s'", error->message);
        g_error_free (error);

        modem_3gpp_ussd_context_step (ctx);
        return;
    }

    /* Cache the hint for the next time we send something */
    if (!ctx->self->priv->use_unencoded_ussd &&
        ctx->current_is_unencoded) {
        mm_dbg ("Will assume we want unencoded USSD commands");
        ctx->self->priv->use_unencoded_ussd = TRUE;
    } else if (ctx->self->priv->use_unencoded_ussd &&
               !ctx->current_is_unencoded) {
        mm_dbg ("Will assume we want encoded USSD commands");
        ctx->self->priv->use_unencoded_ussd = FALSE;
    }

    /* Cache the action, as it will be completed via URCs.
     * There shouldn't be any previous action pending. */
    g_warn_if_fail (self->priv->pending_ussd_action == NULL);
    self->priv->pending_ussd_action = ctx->result;

    /* Reset result so that it doesn't get completed */
    ctx->result = NULL;
    modem_3gpp_ussd_send_context_complete_and_free (ctx);
}

static void
modem_3gpp_ussd_context_send_encoded (Modem3gppUssdSendContext *ctx)
{
    gchar *at_command = NULL;
    GError *error = NULL;
    guint scheme = 0;
    gchar *encoded;

    /* Encode USSD command */
    encoded = mm_iface_modem_3gpp_ussd_encode (MM_IFACE_MODEM_3GPP_USSD (ctx->self),
                                               ctx->command,
                                               &scheme,
                                               &error);
    if (!encoded) {
        g_simple_async_result_take_error (ctx->result, error);
        modem_3gpp_ussd_send_context_complete_and_free (ctx);
        mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (ctx->self),
                                               MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE);
        return;
    }

    /* Build AT command */
    ctx->encoded_used = TRUE;
    ctx->current_is_unencoded = FALSE;
    at_command = g_strdup_printf ("+CUSD=1,\"%s\",%d", encoded, scheme);
    g_free (encoded);

    mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                              at_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)ussd_send_command_ready,
                              ctx);
    g_free (at_command);
}

static void
modem_3gpp_ussd_context_send_unencoded (Modem3gppUssdSendContext *ctx)
{
    gchar *at_command = NULL;

    /* Build AT command with action unencoded */
    ctx->unencoded_used = TRUE;
    ctx->current_is_unencoded = TRUE;
    at_command = g_strdup_printf ("+CUSD=1,\"%s\",%d",
                                  ctx->command,
                                  MM_MODEM_GSM_USSD_SCHEME_7BIT);
    mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                              at_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)ussd_send_command_ready,
                              ctx);
    g_free (at_command);
}

static void
modem_3gpp_ussd_context_step (Modem3gppUssdSendContext *ctx)
{
    if (ctx->encoded_used &&
        ctx->unencoded_used) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Sending USSD command failed");
        modem_3gpp_ussd_send_context_complete_and_free (ctx);

        mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (ctx->self),
                                               MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE);
        return;
    }

    if (ctx->self->priv->use_unencoded_ussd) {
        if (!ctx->unencoded_used)
            modem_3gpp_ussd_context_send_unencoded (ctx);
        else if (!ctx->encoded_used)
            modem_3gpp_ussd_context_send_encoded (ctx);
        else
            g_assert_not_reached ();
    } else {
        if (!ctx->encoded_used)
            modem_3gpp_ussd_context_send_encoded (ctx);
        else if (!ctx->unencoded_used)
            modem_3gpp_ussd_context_send_unencoded (ctx);
        else
            g_assert_not_reached ();
    }
}

static void
modem_3gpp_ussd_send (MMIfaceModem3gppUssd *self,
                      const gchar *command,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    Modem3gppUssdSendContext *ctx;

    ctx = g_new0 (Modem3gppUssdSendContext, 1);
    /* We're going to steal the string result in finish() so we must have a
     * callback specified. */
    g_assert (callback != NULL);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_ussd_send);
    ctx->self = g_object_ref (self);
    ctx->command = g_strdup (command);

    modem_3gpp_ussd_context_step (ctx);

    mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self),
                                           MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE);
}

/*****************************************************************************/
/* USSD Encode/Decode (3GPP/USSD interface) */

static gchar *
modem_3gpp_ussd_encode (MMIfaceModem3gppUssd *self,
                        const gchar *command,
                        guint *scheme,
                        GError **error)
{
    MMBroadbandModem *broadband = MM_BROADBAND_MODEM (self);
    GByteArray *ussd_command;
    gchar *hex = NULL;

    ussd_command = g_byte_array_new ();

    /* encode to the current charset */
    if (mm_modem_charset_byte_array_append (ussd_command,
                                            command,
                                            FALSE,
                                            broadband->priv->modem_current_charset)) {
        *scheme = MM_MODEM_GSM_USSD_SCHEME_7BIT;
        /* convert to hex representation */
        hex = mm_utils_bin2hexstr (ussd_command->data, ussd_command->len);
    }

    g_byte_array_free (ussd_command, TRUE);

    return hex;
}

static gchar *
modem_3gpp_ussd_decode (MMIfaceModem3gppUssd *self,
                        const gchar *reply,
                        GError **error)
{
    MMBroadbandModem *broadband = MM_BROADBAND_MODEM (self);

    return mm_modem_charset_hex_to_utf8 (reply,
                                         broadband->priv->modem_current_charset);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited result codes (3GPP/USSD interface) */

static gboolean
modem_3gpp_ussd_setup_cleanup_unsolicited_result_codes_finish (MMIfaceModem3gppUssd *self,
                                                               GAsyncResult *res,
                                                               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gchar *
decode_ussd_response (MMBroadbandModem *self,
                      const gchar *reply,
                      GError **error)
{
    gchar **items, **iter, *p;
    gchar *str = NULL;
    gint encoding = -1;
    gchar *decoded;

    /* Look for the first ',' */
    p = strchr (reply, ',');
    if (!p) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot decode USSD response (%s): missing field separator",
                     reply);
        return NULL;
    }

    items = g_strsplit_set (p + 1, " ,", -1);
    for (iter = items; iter && *iter; iter++) {
        if (*iter[0] == '\0')
            continue;
        if (str == NULL)
            str = *iter;
        else if (encoding == -1) {
            encoding = atoi (*iter);
            mm_dbg ("USSD data coding scheme %d", encoding);
            break;  /* All done */
        }
    }

    if (!str) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot decode USSD response (%s): not enough fields",
                     reply);
        return NULL;
    }

    /* Strip quotes */
    if (str[0] == '"')
        str++;
    p = strchr (str, '"');
    if (p)
        *p = '\0';

    decoded = mm_iface_modem_3gpp_ussd_decode (MM_IFACE_MODEM_3GPP_USSD (self), str, error);
    g_strfreev (items);
    return decoded;
}

static void
cusd_received (MMAtSerialPort *port,
               GMatchInfo *info,
               MMBroadbandModem *self)
{
    gchar *str;
    MMModem3gppUssdSessionState ussd_state = MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE;

    str = g_match_info_fetch (info, 1);
    if (!str || !isdigit (*str)) {
        if (self->priv->pending_ussd_action)
            g_simple_async_result_set_error (self->priv->pending_ussd_action,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Invalid USSD response received: '%s'",
                                             str ? str : "(none)");
        else
            mm_warn ("Received invalid USSD network-initiated request: '%s'",
                     str ? str : "(none)");
    } else {
        gint status;

        status = g_ascii_digit_value (*str);
        switch (status) {
        case 0: /* no further action required */ {
            gchar *converted;
            GError *error = NULL;

            converted = decode_ussd_response (self, str, &error);
            if (self->priv->pending_ussd_action) {
                /* Response to the user's request */
                if (error)
                    g_simple_async_result_take_error (self->priv->pending_ussd_action, error);
                else
                    g_simple_async_result_set_op_res_gpointer (self->priv->pending_ussd_action,
                                                               converted,
                                                               g_free);
            } else {
                if (error) {
                    mm_warn ("Invalid network initiated USSD notification: %s",
                             error->message);
                    g_error_free (error);
                } else {
                    /* Network-initiated USSD-Notify */
                    mm_iface_modem_3gpp_ussd_update_network_notification (
                        MM_IFACE_MODEM_3GPP_USSD (self),
                        converted);
                    g_free (converted);
                }
            }
            break;
        }

        case 1: /* further action required */ {
            gchar *converted;
            GError *error = NULL;

            ussd_state = MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE;
            converted = decode_ussd_response (self, str, &error);
            if (self->priv->pending_ussd_action) {
                if (error)
                    g_simple_async_result_take_error (self->priv->pending_ussd_action, error);
                else
                    g_simple_async_result_set_op_res_gpointer (self->priv->pending_ussd_action,
                                                               converted,
                                                               g_free);
            } else {
                if (error) {
                    mm_warn ("Invalid network initiated USSD request: %s",
                             error->message);
                    g_error_free (error);
                } else {
                    /* Network-initiated USSD-Request */
                    mm_iface_modem_3gpp_ussd_update_network_request (
                        MM_IFACE_MODEM_3GPP_USSD (self),
                        converted);
                    g_free (converted);
                }
            }
            break;
        }

        case 2:
            if (self->priv->pending_ussd_action)
                g_simple_async_result_set_error (self->priv->pending_ussd_action,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_CANCELLED,
                                                 "USSD terminated by network.");
            break;

        case 4:
            if (self->priv->pending_ussd_action)
                g_simple_async_result_set_error (self->priv->pending_ussd_action,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_UNSUPPORTED,
                                                 "Operation not supported.");
            break;

        default:
            if (self->priv->pending_ussd_action)
                g_simple_async_result_set_error (self->priv->pending_ussd_action,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_FAILED,
                                                 "Unhandled USSD reply: %s (%d)",
                                                 str,
                                                 status);
            break;
        }
    }

    mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self),
                                           ussd_state);

    /* Complete the pending action */
    if (self->priv->pending_ussd_action) {
        g_simple_async_result_complete_in_idle (self->priv->pending_ussd_action);
        g_object_unref (self->priv->pending_ussd_action);
        self->priv->pending_ussd_action = NULL;
    }

    g_free (str);
}

static void
set_unsolicited_result_code_handlers (MMIfaceModem3gppUssd *self,
                                      gboolean enable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *ports[2];
    GRegex *cusd_regex;
    guint i;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_unsolicited_events_handlers);

    cusd_regex = mm_3gpp_cusd_regex_get ();
    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited result codes in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;
        /* Set/unset unsolicited CUSD event handler */
        mm_dbg ("(%s) %s unsolicited result code handlers",
                mm_port_get_device (MM_PORT (ports[i])),
                enable ? "Setting" : "Removing");
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            cusd_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn) cusd_received : NULL,
            enable ? self : NULL,
            NULL);
    }

    g_regex_unref (cusd_regex);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

static void
modem_3gpp_ussd_setup_unsolicited_result_codes (MMIfaceModem3gppUssd *self,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data)
{
    set_unsolicited_result_code_handlers (self, TRUE, callback, user_data);
}

static void
modem_3gpp_ussd_cleanup_unsolicited_result_codes (MMIfaceModem3gppUssd *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data)
{
    set_unsolicited_result_code_handlers (self, FALSE, callback, user_data);
}

/*****************************************************************************/
/* Enable/Disable URCs (3GPP/USSD interface) */

static gboolean
modem_3gpp_ussd_enable_disable_unsolicited_result_codes_finish (MMIfaceModem3gppUssd *self,
                                                                GAsyncResult *res,
                                                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
urc_enable_disable_ready (MMBroadbandModem *self,
                          GAsyncResult *res,
                          GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_ussd_disable_unsolicited_result_codes (MMIfaceModem3gppUssd *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_ussd_disable_unsolicited_result_codes);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CUSD=0",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)urc_enable_disable_ready,
                              result);
}

static void
modem_3gpp_ussd_enable_unsolicited_result_codes (MMIfaceModem3gppUssd *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_ussd_enable_unsolicited_result_codes);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CUSD=1",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)urc_enable_disable_ready,
                              result);
}

/*****************************************************************************/
/* Check if USSD supported (3GPP/USSD interface) */

static gboolean
modem_3gpp_ussd_check_support_finish (MMIfaceModem3gppUssd *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cusd_format_check_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_ussd_check_support (MMIfaceModem3gppUssd *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_ussd_check_support);

    /* Check USSD support */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CUSD=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cusd_format_check_ready,
                              result);
}

/*****************************************************************************/
/* Check if Messaging supported (Messaging interface) */

static gboolean
modem_messaging_check_support_finish (MMIfaceModemMessaging *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cnmi_format_check_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* CNMI command is supported; assume we have full messaging capabilities */
    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_messaging_check_support (MMIfaceModemMessaging *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_messaging_check_support);

    /* We assume that CDMA-only modems don't have messaging capabilities */
    if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (self))) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "CDMA-only modems don't have messaging capabilities");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Check CNMI support */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CNMI=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cnmi_format_check_ready,
                              result);
}

/*****************************************************************************/
/* Load supported SMS storages (Messaging interface) */

typedef struct {
    GArray *mem1;
    GArray *mem2;
    GArray *mem3;
} SupportedStoragesResult;

static void
supported_storages_result_free (SupportedStoragesResult *result)
{
    if (result->mem1)
        g_array_unref (result->mem1);
    if (result->mem2)
        g_array_unref (result->mem2);
    if (result->mem3)
        g_array_unref (result->mem3);
    g_free (result);
}

static gboolean
modem_messaging_load_supported_storages_finish (MMIfaceModemMessaging *self,
                                                GAsyncResult *res,
                                                GArray **mem1,
                                                GArray **mem2,
                                                GArray **mem3,
                                                GError **error)
{
    SupportedStoragesResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = (SupportedStoragesResult *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *mem1 = g_array_ref (result->mem1);
    *mem2 = g_array_ref (result->mem2);
    *mem3 = g_array_ref (result->mem3);

    return TRUE;
}

static void
cpms_format_check_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    const gchar *response;
    GError *error = NULL;
    SupportedStoragesResult *result;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    result = g_new0 (SupportedStoragesResult, 1);

    /* Parse reply */
    if (!mm_3gpp_parse_cpms_test_response (response,
                                           &result->mem1,
                                           &result->mem2,
                                           &result->mem3)) {
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse supported storages reply: '%s'",
                                         response);
        supported_storages_result_free (result);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (simple,
                                               result,
                                               (GDestroyNotify)supported_storages_result_free);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_messaging_load_supported_storages (MMIfaceModemMessaging *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_messaging_load_supported_storages);

    /* Check support storages */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPMS=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cpms_format_check_ready,
                              result);
}

/*****************************************************************************/
/* Lock/unlock SMS storage (Messaging interface implementation helper)
 *
 * The basic commands to work with SMS storages play with AT+CPMS and three
 * different storages: mem1, mem2 and mem3.
 *   'mem1' is the storage for reading, listing and deleting.
 *   'mem2' is the storage for writing and sending from storage.
 *   'mem3' is the storage for receiving.
 *
 * When a command is to be issued for a specific storage, we need a way to
 * lock the access so that other actions are forbidden until the current one
 * finishes. Just think of two sequential actions to store two different
 * SMS into 2 different storages. If the second action is run while the first
 * one is still running, we should issue a RETRY error.
 *
 * Note that mem3 cannot be locked; we just set the default mem3 and that's it.
 *
 * When we unlock the storage, we don't go back to the default storage
 * automatically, we just keep track of which is the current one and only go to
 * the default one if needed.
 */

void
mm_broadband_modem_unlock_sms_storages (MMBroadbandModem *self,
                                        gboolean mem1,
                                        gboolean mem2)
{
    if (mem1) {
        g_assert (self->priv->mem1_storage_locked);
        self->priv->mem1_storage_locked = FALSE;
    }

    if (mem2) {
        g_assert (self->priv->mem2_storage_locked);
        self->priv->mem2_storage_locked = FALSE;
    }
}

gboolean
mm_broadband_modem_lock_sms_storages_finish (MMBroadbandModem *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

typedef struct {
    GSimpleAsyncResult *result;
    MMBroadbandModem *self;
    MMSmsStorage previous_mem1;
    gboolean mem1_locked;
    MMSmsStorage previous_mem2;
    gboolean mem2_locked;
} LockSmsStoragesContext;

static void
lock_sms_storages_context_complete_and_free (LockSmsStoragesContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (LockSmsStoragesContext, ctx);
}

static void
lock_storages_cpms_set_ready (MMBaseModem *self,
                              GAsyncResult *res,
                              LockSmsStoragesContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        /* Reset previous storages and set unlocked */
        if (ctx->mem1_locked) {
            ctx->self->priv->current_sms_mem1_storage = ctx->previous_mem1;
            ctx->self->priv->mem1_storage_locked = FALSE;
        }
        if (ctx->mem2_locked) {
            ctx->self->priv->current_sms_mem2_storage = ctx->previous_mem2;
            ctx->self->priv->mem2_storage_locked = FALSE;
        }
    }
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    lock_sms_storages_context_complete_and_free (ctx);
}

void
mm_broadband_modem_lock_sms_storages (MMBroadbandModem *self,
                                      MMSmsStorage mem1, /* reading/listing/deleting */
                                      MMSmsStorage mem2, /* storing/sending */
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    LockSmsStoragesContext *ctx;
    gchar *cmd;
    gchar *mem1_str = NULL;
    gchar *mem2_str = NULL;

    /* If storages are currently locked by someone else, just return an
     * error */
    if ((mem1 != MM_SMS_STORAGE_UNKNOWN && self->priv->mem1_storage_locked) ||
        (mem2 != MM_SMS_STORAGE_UNKNOWN && self->priv->mem2_storage_locked)) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_RETRY,
            "SMS storage currently locked, try again later");
        return;
    }

    /* We allow locking either just one or both */
    g_assert (mem1 != MM_SMS_STORAGE_UNKNOWN ||
              mem2 != MM_SMS_STORAGE_UNKNOWN);

    ctx = g_slice_new0 (LockSmsStoragesContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_broadband_modem_lock_sms_storages);

    if (mem1 != MM_SMS_STORAGE_UNKNOWN) {
        ctx->mem1_locked = TRUE;
        ctx->previous_mem1 = self->priv->current_sms_mem1_storage;
        self->priv->mem1_storage_locked = TRUE;
        self->priv->current_sms_mem1_storage = mem1;
        mem1_str = g_ascii_strup (mm_sms_storage_get_string (self->priv->current_sms_mem1_storage), -1);
    }

    if (mem2 != MM_SMS_STORAGE_UNKNOWN) {
        ctx->mem2_locked = TRUE;
        ctx->previous_mem2 = self->priv->current_sms_mem2_storage;
        self->priv->mem2_storage_locked = TRUE;
        self->priv->current_sms_mem2_storage = mem2;
        mem2_str = g_ascii_strup (mm_sms_storage_get_string (self->priv->current_sms_mem2_storage), -1);
    }

    /* We don't touch 'mem3' here */

    mm_dbg ("Locking SMS storages to: mem1 (%s), mem2 (%s)...",
            mem1_str ? mem1_str : "none",
            mem2_str ? mem2_str : "none");

    if (mem2_str)
        cmd = g_strdup_printf ("+CPMS=\"%s\",\"%s\"",
                               mem1_str ? mem1_str : "",
                               mem2_str);
    else if (mem1_str)
        cmd = g_strdup_printf ("+CPMS=\"%s\"", mem1_str);
    else
        g_assert_not_reached ();

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)lock_storages_cpms_set_ready,
                              ctx);
    g_free (mem1_str);
    g_free (mem2_str);
    g_free (cmd);
}

/*****************************************************************************/
/* Set default SMS storage (Messaging interface) */

static gboolean
modem_messaging_set_default_storage_finish (MMIfaceModemMessaging *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cpms_set_ready (MMBroadbandModem *self,
                GAsyncResult *res,
                GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_messaging_set_default_storage (MMIfaceModemMessaging *_self,
                                     MMSmsStorage storage,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (_self);
    gchar *cmd;
    GSimpleAsyncResult *result;
    gchar *mem_str;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_messaging_set_default_storage);

    /* Set defaults as current */
    self->priv->current_sms_mem2_storage = storage;

    mem_str = g_ascii_strup (mm_sms_storage_get_string (storage), -1);
    cmd = g_strdup_printf ("+CPMS=\"\",\"%s\",\"%s\"", mem_str, mem_str);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)cpms_set_ready,
                              result);
    g_free (mem_str);
    g_free (cmd);
}

/*****************************************************************************/
/* Setup SMS format (Messaging interface) */

static gboolean
modem_messaging_setup_sms_format_finish (MMIfaceModemMessaging *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cmgf_set_ready (MMBroadbandModem *self,
                GAsyncResult *res,
                GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        mm_dbg ("Failed to set preferred SMS mode: '%s'; assuming text mode'",
                error->message);
        g_error_free (error);
        self->priv->modem_messaging_sms_pdu_mode = FALSE;
    } else
        mm_info ("Successfully set preferred SMS mode: '%s'",
                 self->priv->modem_messaging_sms_pdu_mode ? "PDU" : "text");

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
set_preferred_sms_format (MMBroadbandModem *self,
                          GSimpleAsyncResult *result)
{
    gchar *cmd;

    cmd = g_strdup_printf ("+CMGF=%s",
                           self->priv->modem_messaging_sms_pdu_mode ? "0" : "1");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cmgf_set_ready,
                              result);
    g_free (cmd);
}

static void
cmgf_format_check_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *response;
    gboolean sms_pdu_supported = FALSE;
    gboolean sms_text_supported = FALSE;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error ||
        !mm_3gpp_parse_cmgf_test_response (response,
                                           &sms_pdu_supported,
                                           &sms_text_supported,
                                           &error)) {
        mm_dbg ("Failed to query supported SMS modes: '%s'",
                error->message);
        g_error_free (error);
    }

    /* Only use text mode if PDU mode not supported */
    self->priv->modem_messaging_sms_pdu_mode = TRUE;
    if (!sms_pdu_supported) {
        if (sms_text_supported) {
            mm_dbg ("PDU mode not supported, will try to use Text mode");
            self->priv->modem_messaging_sms_pdu_mode = FALSE;
        } else
            mm_dbg ("Neither PDU nor Text modes are reported as supported; "
                    "will anyway default to PDU mode");
    }

    self->priv->sms_supported_modes_checked = TRUE;

    set_preferred_sms_format (self, simple);
}

static void
modem_messaging_setup_sms_format (MMIfaceModemMessaging *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_messaging_setup_sms_format);

    /* If we already checked for supported SMS types, go on to select the
     * preferred format. */
    if (MM_BROADBAND_MODEM (self)->priv->sms_supported_modes_checked) {
        set_preferred_sms_format (MM_BROADBAND_MODEM (self), result);
        return;
    }

    /* Check supported SMS formats */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CMGF=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cmgf_format_check_ready,
                              result);
}

/*****************************************************************************/
/* Setup/cleanup messaging related unsolicited events (Messaging interface) */

static gboolean
modem_messaging_setup_cleanup_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                                         GAsyncResult *res,
                                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    guint idx;
} SmsPartContext;

static void
sms_part_context_complete_and_free (SmsPartContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
sms_part_ready (MMBroadbandModem *self,
                GAsyncResult *res,
                SmsPartContext *ctx)
{
    MMSmsPart *part;
    gint rv, status, tpdu_len;
    gchar pdu[SMS_MAX_PDU_LEN + 1];
    const gchar *response;
    GError *error = NULL;

    /* Always always always unlock mem1 storage. Warned you've been. */
    mm_broadband_modem_unlock_sms_storages (self, TRUE, FALSE);

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* We're really ignoring this error afterwards, as we don't have a callback
         * passed to the async operation, so just log the error here. */
        mm_warn ("Couldn't retrieve SMS part: '%s'",
                 error->message);
        g_simple_async_result_take_error (ctx->result, error);
        sms_part_context_complete_and_free (ctx);
        return;
    }

    rv = sscanf (response, "+CMGR: %d,,%d %" G_STRINGIFY (SMS_MAX_PDU_LEN) "s",
                 &status, &tpdu_len, pdu);
    if (rv != 3) {
        error = g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Failed to parse CMGR response (parsed %d items)", rv);
        mm_warn ("Couldn't retrieve SMS part: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        sms_part_context_complete_and_free (ctx);
        return;
    }

    part = mm_sms_part_new_from_pdu (ctx->idx, pdu, &error);
    if (part) {
        mm_dbg ("Correctly parsed PDU (%d)", ctx->idx);
        mm_iface_modem_messaging_take_part (MM_IFACE_MODEM_MESSAGING (self),
                                            part,
                                            MM_SMS_STATE_RECEIVED,
                                            self->priv->modem_messaging_sms_default_storage);
    } else {
        /* Don't treat the error as critical */
        mm_dbg ("Error parsing PDU (%d): %s", ctx->idx, error->message);
        g_error_free (error);
    }

    /* All done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    sms_part_context_complete_and_free (ctx);
}

static void
indication_lock_storages_ready (MMBroadbandModem *self,
                                GAsyncResult *res,
                                SmsPartContext *ctx)
{
    gchar *command;
    GError *error = NULL;

    if (!mm_broadband_modem_lock_sms_storages_finish (self, res, &error)) {
        /* TODO: we should either make this lock() never fail, by automatically
         * retrying after some time, or otherwise retry here. */
        g_simple_async_result_take_error (ctx->result, error);
        sms_part_context_complete_and_free (ctx);
        return;
    }

    /* Storage now set and locked */

    /* Retrieve the message */
    command = g_strdup_printf ("+CMGR=%d", ctx->idx);
    mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                              command,
                              10,
                              FALSE,
                              (GAsyncReadyCallback)sms_part_ready,
                              ctx);
    g_free (command);
}

static void
cmti_received (MMAtSerialPort *port,
               GMatchInfo *info,
               MMBroadbandModem *self)
{
    SmsPartContext *ctx;
    guint idx = 0;
    MMSmsStorage storage;
    gchar *str;

    if (!mm_get_uint_from_match_info (info, 2, &idx))
        return;

    /* The match info gives us in which storage the index applies */
    str = mm_get_string_unquoted_from_match_info (info, 1);
    storage = mm_common_get_sms_storage_from_string (str, NULL);
    if (storage == MM_SMS_STORAGE_UNKNOWN) {
        mm_dbg ("Skipping CMTI indication, unknown storage '%s' reported", str);
        g_free (str);
        return;
    }
    g_free (str);

    /* Don't signal multiple times if there are multiple CMTI notifications for a message */
    if (mm_sms_list_has_part (self->priv->modem_messaging_sms_list,
                              storage,
                              idx)) {
        mm_dbg ("Skipping CMTI indication, part already processed");
        return;
    }

    ctx = g_new0 (SmsPartContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self), NULL, NULL, cmti_received);
    ctx->idx = idx;

    /* First, request to set the proper storage to read from */
    mm_broadband_modem_lock_sms_storages (ctx->self,
                                          storage,
                                          MM_SMS_STORAGE_UNKNOWN,
                                          (GAsyncReadyCallback)indication_lock_storages_ready,
                                          ctx);
}

static void
cds_received (MMAtSerialPort *port,
              GMatchInfo *info,
              MMBroadbandModem *self)
{
    GError *error = NULL;
    MMSmsPart *part;
    guint length;
    gchar *pdu;

    mm_dbg ("Got new non-stored message indication");

    if (!mm_get_uint_from_match_info (info, 1, &length))
        return;

    pdu = g_match_info_fetch (info, 2);
    if (!pdu)
        return;

    part = mm_sms_part_new_from_pdu (SMS_PART_INVALID_INDEX, pdu, &error);
    if (part) {
        mm_dbg ("Correctly parsed non-stored PDU");
        mm_iface_modem_messaging_take_part (MM_IFACE_MODEM_MESSAGING (self),
                                            part,
                                            MM_SMS_STATE_RECEIVED,
                                            MM_SMS_STORAGE_UNKNOWN);
    } else {
        /* Don't treat the error as critical */
        mm_dbg ("Error parsing non-stored PDU: %s", error->message);
        g_error_free (error);
    }
}

static void
set_messaging_unsolicited_events_handlers (MMIfaceModemMessaging *self,
                                           gboolean enable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *ports[2];
    GRegex *cmti_regex;
    GRegex *cds_regex;
    guint i;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_messaging_unsolicited_events_handlers);

    cmti_regex = mm_3gpp_cmti_regex_get ();
    cds_regex = mm_3gpp_cds_regex_get ();
    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Set/unset unsolicited CMTI event handler */
        mm_dbg ("(%s) %s messaging unsolicited events handlers",
                mm_port_get_device (MM_PORT (ports[i])),
                enable ? "Setting" : "Removing");
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            cmti_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn) cmti_received : NULL,
            enable ? self : NULL,
            NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            cds_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn) cds_received : NULL,
            enable ? self : NULL,
            NULL);
    }

    g_regex_unref (cmti_regex);
    g_regex_unref (cds_regex);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

static void
modem_messaging_setup_unsolicited_events (MMIfaceModemMessaging *self,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
    set_messaging_unsolicited_events_handlers (self, TRUE, callback, user_data);
}

static void
modem_messaging_cleanup_unsolicited_events (MMIfaceModemMessaging *self,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data)
{
    set_messaging_unsolicited_events_handlers (self, FALSE, callback, user_data);
}

/*****************************************************************************/
/* Enable unsolicited events (SMS indications) (Messaging interface) */

static gboolean
modem_messaging_enable_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                                  GAsyncResult *res,
                                                  GError **error)
{
    GError *inner_error = NULL;

    mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

static gboolean
cnmi_response_processor (MMBaseModem *self,
                         gpointer none,
                         const gchar *command,
                         const gchar *response,
                         gboolean last_command,
                         const GError *error,
                         GVariant **result,
                         GError **result_error)
{
    if (error) {
        /* If we get a not-supported error and we're not in the last command, we
         * won't set 'result_error', so we'll keep on the sequence */
        if (!g_error_matches (error, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_NOT_SUPPORTED) ||
            last_command)
            *result_error = g_error_copy (error);

        return FALSE;
    }

    *result = NULL;
    return TRUE;
}

/*
 * Many devices based on Qualcomm chipsets don't support a <ds> value
 * of '1', despite saying they do in the AT+CNMI=? response.  But they
 * do accept '2'.  Since we're not doing much with delivery status
 * reports yet, if we get a CME 303 (not supported) error when setting
 * the message indication parameters via CNMI, fall back to the
 * Qualcomm-compatible CNMI parameters.
 */
static const MMBaseModemAtCommand cnmi_sequence[] = {
    { "+CNMI=2,1,2,1,0", 3, TRUE, cnmi_response_processor },
    { "+CNMI=2,1,2,2,0", 3, TRUE, cnmi_response_processor },
    { NULL }
};

static void
modem_messaging_enable_unsolicited_events (MMIfaceModemMessaging *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        cnmi_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Load initial list of SMS parts (Messaging interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMSmsStorage list_storage;
} ListPartsContext;

static void
list_parts_context_complete_and_free (ListPartsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_messaging_load_initial_sms_parts_finish (MMIfaceModemMessaging *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static MMSmsState
sms_state_from_str (const gchar *str)
{
    /* We merge unread and read messages in the same state */
    if (strstr (str, "REC"))
        return MM_SMS_STATE_RECEIVED;

    /* look for 'unsent' BEFORE looking for 'sent' */
    if (strstr (str, "UNSENT"))
        return MM_SMS_STATE_STORED;

    if (strstr (str, "SENT"))
        return MM_SMS_STATE_SENT;

    return MM_SMS_STATE_UNKNOWN;
}

static MMSmsPduType
sms_pdu_type_from_str (const gchar *str)
{
    /* We merge unread and read messages in the same state */
    if (strstr (str, "REC"))
        return MM_SMS_PDU_TYPE_DELIVER;

    if (strstr (str, "STO"))
        return MM_SMS_PDU_TYPE_SUBMIT;

    return MM_SMS_PDU_TYPE_UNKNOWN;
}

static void
sms_text_part_list_ready (MMBroadbandModem *self,
                          GAsyncResult *res,
                          ListPartsContext *ctx)
{
    GRegex *r;
    GMatchInfo *match_info = NULL;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        list_parts_context_complete_and_free (ctx);
        return;
    }

    /* +CMGL: <index>,<stat>,<oa/da>,[alpha],<scts><CR><LF><data><CR><LF> */
    r = g_regex_new ("\\+CMGL:\\s*(\\d+)\\s*,\\s*([^,]*),\\s*([^,]*),\\s*([^,]*),\\s*([^\\r\\n]*)\\r\\n([^\\r\\n]*)",
                     0, 0, NULL);
    g_assert (r);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, NULL)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_INVALID_ARGS,
                                         "Couldn't parse SMS list response");
        list_parts_context_complete_and_free (ctx);
        g_regex_unref (r);
        return;
    }

    while (g_match_info_matches (match_info)) {
        MMSmsPart *part;
        guint matches, idx;
        gchar *number, *timestamp, *text, *ucs2_text, *stat;
        gsize ucs2_len = 0;
        GByteArray *raw;

        matches = g_match_info_get_match_count (match_info);
        if (matches != 7) {
            mm_dbg ("Failed to match entire CMGL response (count %d)", matches);
            goto next;
        }

        if (!mm_get_uint_from_match_info (match_info, 1, &idx)) {
            mm_dbg ("Failed to convert message index");
            goto next;
        }

        /* Get part state */
        stat = mm_get_string_unquoted_from_match_info (match_info, 2);
        if (!stat) {
            mm_dbg ("Failed to get part status");
            goto next;
        }

        /* Get and parse number */
        number = mm_get_string_unquoted_from_match_info (match_info, 3);
        if (!number) {
            mm_dbg ("Failed to get message sender number");
            g_free (stat);
            goto next;
        }

        number = mm_broadband_modem_take_and_convert_to_utf8 (MM_BROADBAND_MODEM (self),
                                                              number);

        /* Get and parse timestamp (always expected in ASCII) */
        timestamp = mm_get_string_unquoted_from_match_info (match_info, 5);

        /* Get and parse text */
        text = mm_broadband_modem_take_and_convert_to_utf8 (MM_BROADBAND_MODEM (self),
                                                            g_match_info_fetch (match_info, 6));

        /* The raw SMS data can only be GSM, UCS2, or unknown (8-bit), so we
         * need to convert to UCS2 here.
         */
        ucs2_text = g_convert (text, -1, "UCS-2BE//TRANSLIT", "UTF-8", NULL, &ucs2_len, NULL);
        g_assert (ucs2_text);
        raw = g_byte_array_sized_new (ucs2_len);
        g_byte_array_append (raw, (const guint8 *) ucs2_text, ucs2_len);
        g_free (ucs2_text);

        /* all take() methods pass ownership of the value as well */
        part = mm_sms_part_new (idx,
                                sms_pdu_type_from_str (stat));
        mm_sms_part_take_number (part, number);
        mm_sms_part_take_timestamp (part, timestamp);
        mm_sms_part_take_text (part, text);
        mm_sms_part_take_data (part, raw);
        mm_sms_part_set_class (part, 0);

        mm_dbg ("Correctly parsed SMS list entry (%d)", idx);
        mm_iface_modem_messaging_take_part (MM_IFACE_MODEM_MESSAGING (self),
                                            part,
                                            sms_state_from_str (stat),
                                            ctx->list_storage);
        g_free (stat);
next:
        g_match_info_next (match_info, NULL);
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    /* We consider all done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    list_parts_context_complete_and_free (ctx);
}

static MMSmsState
sms_state_from_index (guint index)
{
    /* We merge unread and read messages in the same state */
    switch (index) {
    case 0: /* received, unread */
    case 1: /* received, read */
        return MM_SMS_STATE_RECEIVED;
    case 2:
        return MM_SMS_STATE_STORED;
    case 3:
        return MM_SMS_STATE_SENT;
    default:
        return MM_SMS_STATE_UNKNOWN;
    }
}

static void
sms_pdu_part_list_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         ListPartsContext *ctx)
{
    const gchar *response;
    GError *error = NULL;

    /* Always always always unlock mem1 storage. Warned you've been. */
    mm_broadband_modem_unlock_sms_storages (self, TRUE, FALSE);

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        list_parts_context_complete_and_free (ctx);
        return;
    }

    while (*response) {
        MMSmsPart *part;
        gint idx;
        gint status;
        gint tpdu_len;
        gchar pdu[SMS_MAX_PDU_LEN + 1];
        gint offset;
        gint rv;

        rv = sscanf (response,
                     "+CMGL: %d,%d,,%d %" G_STRINGIFY (SMS_MAX_PDU_LEN) "s %n",
                     &idx, &status, &tpdu_len, pdu, &offset);
        if (4 != rv) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_INVALID_ARGS,
                                             "Couldn't parse SMS list response: "
                                             "only %d fields parsed",
                                             rv);
            list_parts_context_complete_and_free (ctx);
            return;
        }

        /* Will try to keep on the loop */
        response += offset;

        part = mm_sms_part_new_from_pdu (idx, pdu, &error);
        if (part) {
            mm_dbg ("Correctly parsed PDU (%d)", idx);
            mm_iface_modem_messaging_take_part (MM_IFACE_MODEM_MESSAGING (self),
                                                part,
                                                sms_state_from_index (status),
                                                ctx->list_storage);
        } else {
            /* Don't treat the error as critical */
            mm_dbg ("Error parsing PDU (%d): %s", idx, error->message);
            g_clear_error (&error);
        }
    }

    /* We consider all done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    list_parts_context_complete_and_free (ctx);
}

static void
list_parts_lock_storages_ready (MMBroadbandModem *self,
                                GAsyncResult *res,
                                ListPartsContext *ctx)
{
    GError *error = NULL;

    if (!mm_broadband_modem_lock_sms_storages_finish (self, res, &error)) {
        /* TODO: we should either make this lock() never fail, by automatically
         * retrying after some time, or otherwise retry here. */
        g_simple_async_result_take_error (ctx->result, error);
        list_parts_context_complete_and_free (ctx);
        return;
    }

    /* Storage now set and locked */

    /* Get SMS parts from ALL types.
     * Different command to be used if we are on Text or PDU mode */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              (MM_BROADBAND_MODEM (self)->priv->modem_messaging_sms_pdu_mode ?
                               "+CMGL=4" :
                               "+CMGL=\"ALL\""),
                              20,
                              FALSE,
                              (GAsyncReadyCallback) (MM_BROADBAND_MODEM (self)->priv->modem_messaging_sms_pdu_mode ?
                                                     sms_pdu_part_list_ready :
                                                     sms_text_part_list_ready),
                              ctx);
}

static void
modem_messaging_load_initial_sms_parts (MMIfaceModemMessaging *self,
                                        MMSmsStorage storage,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    ListPartsContext *ctx;

    ctx = g_new0 (ListPartsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_messaging_load_initial_sms_parts);
    ctx->list_storage = storage;

    mm_dbg ("Listing SMS parts in storage '%s'",
            mm_sms_storage_get_string (storage));

    /* First, request to set the proper storage to read from */
    mm_broadband_modem_lock_sms_storages (ctx->self,
                                          storage,
                                          MM_SMS_STORAGE_UNKNOWN,
                                          (GAsyncReadyCallback)list_parts_lock_storages_ready,
                                          ctx);
}

/*****************************************************************************/
/* Create SMS (Messaging interface) */

static MMSms *
modem_messaging_create_sms (MMIfaceModemMessaging *self)
{
    return mm_sms_new (MM_BASE_MODEM (self));
}

/*****************************************************************************/
/* ESN loading (CDMA interface) */

static gchar *
modem_cdma_load_esn_finish (MMIfaceModemCdma *self,
                            GAsyncResult *res,
                            GError **error)
{
    gchar *esn;

    esn = g_strdup (mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error));
    if (!esn)
        return NULL;

    mm_dbg ("loaded ESN: %s", esn);
    return esn;
}

static void
modem_cdma_load_esn (MMIfaceModemCdma *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_dbg ("loading ESN...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+GSN",
                              3,
                              TRUE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* HDR state check (CDMA interface) */

typedef struct {
    guint8 hybrid_mode;
    guint8 session_state;
    guint8 almp_state;
} HdrStateResults;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMQcdmSerialPort *qcdm;
} HdrStateContext;

static void
hdr_state_context_complete_and_free (HdrStateContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->qcdm);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_cdma_get_hdr_state_finish (MMIfaceModemCdma *self,
                                 GAsyncResult *res,
                                 guint8 *hybrid_mode,
                                 guint8 *session_state,
                                 guint8 *almp_state,
                                 GError **error)
{
    HdrStateResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *hybrid_mode = results->hybrid_mode;
    *session_state = results->session_state;
    *almp_state = results->almp_state;
    return TRUE;
}

static void
hdr_subsys_state_info_ready (MMQcdmSerialPort *port,
                             GByteArray *response,
                             GError *error,
                             HdrStateContext *ctx)
{
    QcdmResult *result;
    HdrStateResults *results;
    gint err = QCDM_SUCCESS;

    if (error) {
        g_simple_async_result_set_from_error (ctx->result, error);
        hdr_state_context_complete_and_free (ctx);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_hdr_subsys_state_info_result ((const gchar *) response->data,
                                                    response->len,
                                                    &err);
    if (!result) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Failed to parse HDR subsys state info command result: %d",
                                         err);
        hdr_state_context_complete_and_free (ctx);
        return;
    }

    /* Build results */
    results = g_new0 (HdrStateResults, 1);
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_HDR_HYBRID_MODE, &results->hybrid_mode);
    results->session_state = QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSED;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE, &results->session_state);
    results->almp_state = QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INACTIVE;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE, &results->almp_state);
    qcdm_result_unref (result);

    g_simple_async_result_set_op_res_gpointer (ctx->result, results, (GDestroyNotify)g_free);
    hdr_state_context_complete_and_free (ctx);
}

static void
modem_cdma_get_hdr_state (MMIfaceModemCdma *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MMQcdmSerialPort *qcdm;
    HdrStateContext *ctx;
    GByteArray *hdrstate;

    qcdm = mm_base_modem_peek_port_qcdm (MM_BASE_MODEM (self));
    if (!qcdm) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Cannot get HDR state without a QCDM port");
        return;
    }

    /* Setup context */
    ctx = g_new0 (HdrStateContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_get_hdr_state);
    ctx->qcdm = g_object_ref (qcdm);

    /* Setup command */
    hdrstate = g_byte_array_sized_new (25);
    hdrstate->len = qcdm_cmd_hdr_subsys_state_info_new ((gchar *) hdrstate->data, 25);
    g_assert (hdrstate->len);

    mm_qcdm_serial_port_queue_command (ctx->qcdm,
                                       hdrstate,
                                       3,
                                       NULL,
                                       (MMQcdmSerialResponseFn)hdr_subsys_state_info_ready,
                                       ctx);
}

/*****************************************************************************/
/* Call Manager state check (CDMA interface) */

typedef struct {
    guint system_mode;
    guint operating_mode;
} CallManagerStateResults;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMQcdmSerialPort *qcdm;
} CallManagerStateContext;

static void
call_manager_state_context_complete_and_free (CallManagerStateContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->qcdm);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_cdma_get_call_manager_state_finish (MMIfaceModemCdma *self,
                                          GAsyncResult *res,
                                          guint *system_mode,
                                          guint *operating_mode,
                                          GError **error)
{
    CallManagerStateResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *system_mode = results->system_mode;
    *operating_mode = results->operating_mode;
    return TRUE;
}

static void
cm_subsys_state_info_ready (MMQcdmSerialPort *port,
                            GByteArray *response,
                            GError *error,
                            CallManagerStateContext *ctx)
{
    QcdmResult *result;
    CallManagerStateResults *results;
    gint err = QCDM_SUCCESS;

    if (error) {
        g_simple_async_result_set_from_error (ctx->result, error);
        call_manager_state_context_complete_and_free (ctx);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_cm_subsys_state_info_result ((const gchar *) response->data,
                                                   response->len,
                                                   &err);
    if (!result) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Failed to parse CM subsys state info command result: %d",
                                         err);
        call_manager_state_context_complete_and_free (ctx);
        return;
    }

    /* Build results */
    results = g_new0 (CallManagerStateResults, 1);
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE, &results->operating_mode);
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE, &results->system_mode);
    qcdm_result_unref (result);

    g_simple_async_result_set_op_res_gpointer (ctx->result, results, (GDestroyNotify)g_free);
    call_manager_state_context_complete_and_free (ctx);
}

static void
modem_cdma_get_call_manager_state (MMIfaceModemCdma *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    MMQcdmSerialPort *qcdm;
    CallManagerStateContext *ctx;
    GByteArray *cmstate;

    qcdm = mm_base_modem_peek_port_qcdm (MM_BASE_MODEM (self));
    if (!qcdm) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Cannot get call manager state without a QCDM port");
        return;
    }

    /* Setup context */
    ctx = g_new0 (CallManagerStateContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_get_call_manager_state);
    ctx->qcdm = g_object_ref (qcdm);

    /* Setup command */
    cmstate = g_byte_array_sized_new (25);
    cmstate->len = qcdm_cmd_cm_subsys_state_info_new ((gchar *) cmstate->data, 25);
    g_assert (cmstate->len);

    mm_qcdm_serial_port_queue_command (ctx->qcdm,
                                       cmstate,
                                       3,
                                       NULL,
                                       (MMQcdmSerialResponseFn)cm_subsys_state_info_ready,
                                       ctx);
}

/*****************************************************************************/
/* Serving System check (CDMA interface) */

typedef struct {
    guint sid;
    guint nid;
    guint class;
    guint band;
} Cdma1xServingSystemResults;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMQcdmSerialPort *qcdm;
} Cdma1xServingSystemContext;

static void
cdma1x_serving_system_context_complete_and_free (Cdma1xServingSystemContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    if (ctx->qcdm)
        g_object_unref (ctx->qcdm);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static GError *
cdma1x_serving_system_no_service_error (void)
{
    /* NOTE: update get_cdma1x_serving_system_ready() in mm-iface-modem-cdma.c
     * if this error changes */
    return g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR,
                                MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK,
                                "No CDMA service");
}

static gboolean
modem_cdma_get_cdma1x_serving_system_finish (MMIfaceModemCdma *self,
                                             GAsyncResult *res,
                                             guint *class,
                                             guint *band,
                                             guint *sid,
                                             guint *nid,
                                             GError **error)
{
    Cdma1xServingSystemResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = (Cdma1xServingSystemResults *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *sid = results->sid;
    *nid = results->nid;
    *class = results->class;
    *band = results->band;
    return TRUE;
}

static void
css_query_ready (MMIfaceModemCdma *self,
                 GAsyncResult *res,
                 Cdma1xServingSystemContext *ctx)
{
    GError *error = NULL;
    const gchar *result;
    gint class = 0;
    gint sid = MM_MODEM_CDMA_SID_UNKNOWN;
    gint num;
    guchar band = 'Z';
    gboolean class_ok = FALSE;
    gboolean band_ok = FALSE;
    gboolean success = FALSE;
    Cdma1xServingSystemResults *results;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        cdma1x_serving_system_context_complete_and_free (ctx);
        return;
    }

    /* Strip any leading command tag and spaces */
    result = mm_strip_tag (result, "+CSS:");
    num = sscanf (result, "? , %d", &sid);
    if (num == 1) {
        /* UTStarcom and Huawei modems that use IS-707-A format; note that
         * this format obviously doesn't have other indicators like band and
         * class and thus SID 0 will be reported as "no service" (see below).
         */
        class = 0;
        band = 'Z';
        success = TRUE;
    } else {
        GRegex *r;
        GMatchInfo *match_info;

        /* Format is "<band_class>,<band>,<sid>" */
        r = g_regex_new ("\\s*([^,]*?)\\s*,\\s*([^,]*?)\\s*,\\s*(\\d+)", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        if (!r) {
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_FAILED,
                "Could not parse Serving System results (regex creation failed).");
            cdma1x_serving_system_context_complete_and_free (ctx);
            return;
        }

        g_regex_match (r, result, 0, &match_info);
        if (g_match_info_get_match_count (match_info) >= 3) {
            gint override_class = 0;
            gchar *str;

            /* band class */
            str = g_match_info_fetch (match_info, 1);
            class = mm_cdma_normalize_class (str);
            g_free (str);

            /* band */
            str = g_match_info_fetch (match_info, 2);
            band = mm_cdma_normalize_band (str, &override_class);
            if (override_class)
                class = override_class;
            g_free (str);

            /* sid */
            str = g_match_info_fetch (match_info, 3);
            if (!mm_get_int_from_str (str, &sid))
                sid = MM_MODEM_CDMA_SID_UNKNOWN;
            g_free (str);

            success = TRUE;
        }

        g_match_info_free (match_info);
        g_regex_unref (r);
    }

    if (!success) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Could not parse Serving System results");
        cdma1x_serving_system_context_complete_and_free (ctx);
        return;
    }

    /* Normalize the SID */
    if (sid < 0 || sid > 32767)
        sid = MM_MODEM_CDMA_SID_UNKNOWN;

    if (class == 1 || class == 2)
        class_ok = TRUE;
    if (band != 'Z')
        band_ok = TRUE;

    /* Return 'no service' if none of the elements of the +CSS response
     * indicate that the modem has service.  Note that this allows SID 0
     * when at least one of the other elements indicates service.
     * Normally we'd treat SID 0 as 'no service' but some modems
     * (Sierra 5725) sometimes return SID 0 even when registered.
     */
    if (sid == 0 && !class_ok && !band_ok)
        sid = MM_MODEM_CDMA_SID_UNKNOWN;

    /* 99999 means unknown/no service */
    if (sid == MM_MODEM_CDMA_SID_UNKNOWN) {
        g_simple_async_result_take_error (ctx->result,
                                          cdma1x_serving_system_no_service_error ());
        cdma1x_serving_system_context_complete_and_free (ctx);
        return;
    }

    results = g_new0 (Cdma1xServingSystemResults, 1);
    results->sid = sid;
    results->band = band;
    results->class = class;
    /* No means to get NID with AT commands right now */
    results->nid = MM_MODEM_CDMA_NID_UNKNOWN;

    g_simple_async_result_set_op_res_gpointer (ctx->result, results, (GDestroyNotify)g_free);
    cdma1x_serving_system_context_complete_and_free (ctx);
}

static void
qcdm_cdma_status_ready (MMQcdmSerialPort *port,
                        GByteArray *response,
                        GError *error,
                        Cdma1xServingSystemContext *ctx)
{
    Cdma1xServingSystemResults *results;
    QcdmResult *result;
    guint32 sid = MM_MODEM_CDMA_SID_UNKNOWN;
    guint32 nid = MM_MODEM_CDMA_NID_UNKNOWN;
    guint32 rxstate = 0;
    gint err = QCDM_SUCCESS;

    if (error ||
        (result = qcdm_cmd_cdma_status_result ((const gchar *) response->data,
                                               response->len,
                                               &err)) == NULL) {
        if (err != QCDM_SUCCESS)
            mm_dbg ("Failed to parse cdma status command result: %d", err);
        /* If there was some error, fall back to use +CSS like we did before QCDM */
        mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                  "+CSS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)css_query_ready,
                                  ctx);
        return;
    }

    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE, &rxstate);
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_SID, &sid);
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_NID, &nid);
    qcdm_result_unref (result);

    /* 99999 means unknown/no service */
    if (rxstate == QCDM_CMD_CDMA_STATUS_RX_STATE_ENTERING_CDMA) {
        sid = MM_MODEM_CDMA_SID_UNKNOWN;
        nid = MM_MODEM_CDMA_NID_UNKNOWN;
    }

    results = g_new0 (Cdma1xServingSystemResults, 1);
    results->sid = sid;
    results->nid = nid;
    if (sid != MM_MODEM_CDMA_SID_UNKNOWN) {
        results->band = 'Z';
        results->class = 0;
    }

    g_simple_async_result_set_op_res_gpointer (ctx->result, results, (GDestroyNotify)g_free);
    cdma1x_serving_system_context_complete_and_free (ctx);
}

static void
modem_cdma_get_cdma1x_serving_system (MMIfaceModemCdma *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    Cdma1xServingSystemContext *ctx;

    /* Setup context */
    ctx = g_new0 (Cdma1xServingSystemContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_get_cdma1x_serving_system);
    ctx->qcdm = mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));

    if (ctx->qcdm) {
        GByteArray *cdma_status;

        /* Setup command */
        cdma_status = g_byte_array_sized_new (25);
        cdma_status->len = qcdm_cmd_cdma_status_new ((char *) cdma_status->data, 25);
        g_assert (cdma_status->len);
        mm_qcdm_serial_port_queue_command (ctx->qcdm,
                                           cdma_status,
                                           3,
                                           NULL,
                                           (MMQcdmSerialResponseFn)qcdm_cdma_status_ready,
                                           ctx);
        return;
    }

    /* Try with AT if we don't have QCDM */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CSS?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)css_query_ready,
                              ctx);
}

/*****************************************************************************/
/* Service status, analog/digital check (CDMA interface) */

static gboolean
modem_cdma_get_service_status_finish (MMIfaceModemCdma *self,
                                      GAsyncResult *res,
                                      gboolean *has_cdma_service,
                                      GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    *has_cdma_service = g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res));
    return TRUE;
}

static void
cad_query_ready (MMIfaceModemCdma *self,
                 GAsyncResult *res,
                 GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *result;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else {
        guint cad;

        /* Strip any leading command tag and spaces */
        result = mm_strip_tag (result, "+CAD:");
        if (!mm_get_uint_from_str (result, &cad))
            g_simple_async_result_set_error (simple,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Failed to parse +CAD response '%s'",
                                             result);
        else
            /* 1 == CDMA service */
            g_simple_async_result_set_op_res_gboolean (simple, (cad == 1));
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_cdma_get_service_status (MMIfaceModemCdma *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_cdma_get_service_status);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CAD?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)cad_query_ready,
                              result);
}

/*****************************************************************************/
/* Detailed registration state (CDMA interface) */

typedef struct {
    MMModemCdmaRegistrationState detailed_cdma1x_state;
    MMModemCdmaRegistrationState detailed_evdo_state;
} DetailedRegistrationStateResults;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMAtSerialPort *port;
    MMModemCdmaRegistrationState cdma1x_state;
    MMModemCdmaRegistrationState evdo_state;
    GError *error;
} DetailedRegistrationStateContext;

static void
detailed_registration_state_context_complete_and_free (DetailedRegistrationStateContext *ctx)
{
    if (ctx->error)
        g_simple_async_result_take_error (ctx->result, ctx->error);
    else {
        DetailedRegistrationStateResults *results;

        results = g_new (DetailedRegistrationStateResults, 1);
        results->detailed_cdma1x_state = ctx->cdma1x_state;
        results->detailed_evdo_state = ctx->evdo_state;
        g_simple_async_result_set_op_res_gpointer (ctx->result, results, g_free);
    }

    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_cdma_get_detailed_registration_state_finish (MMIfaceModemCdma *self,
                                                   GAsyncResult *res,
                                                   MMModemCdmaRegistrationState *detailed_cdma1x_state,
                                                   MMModemCdmaRegistrationState *detailed_evdo_state,
                                                   GError **error)
{
    DetailedRegistrationStateResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *detailed_cdma1x_state = results->detailed_cdma1x_state;
    *detailed_evdo_state = results->detailed_evdo_state;
    return TRUE;
}

static void
speri_ready (MMIfaceModemCdma *self,
             GAsyncResult *res,
             DetailedRegistrationStateContext *ctx)
{
    gboolean roaming = FALSE;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* silently discard SPERI errors */
        g_error_free (error);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    /* Try to parse the results */
    response = mm_strip_tag (response, "$SPERI:");
    if (!response ||
        !mm_cdma_parse_eri (response, &roaming, NULL, NULL)) {
        mm_warn ("Couldn't parse SPERI response '%s'", response);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    if (roaming) {
        /* Change the 1x and EVDO registration states to roaming if they were
         * anything other than UNKNOWN.
         */
        if (ctx->cdma1x_state > MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
            ctx->cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
        if (ctx->evdo_state > MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
            ctx->evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
    } else {
        /* Change 1x and/or EVDO registration state to home if home/roaming wasn't previously known */
        if (ctx->cdma1x_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED)
            ctx->cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
        if (ctx->evdo_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED)
            ctx->evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
    }

    detailed_registration_state_context_complete_and_free (ctx);
}

static void
spservice_ready (MMIfaceModemCdma *self,
                 GAsyncResult *res,
                 DetailedRegistrationStateContext *ctx)
{
    const gchar *response;
    MMModemCdmaRegistrationState cdma1x_state;
    MMModemCdmaRegistrationState evdo_state;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &ctx->error);
    if (ctx->error) {
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    /* Try to parse the results */
    cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    if (!mm_cdma_parse_spservice_read_response (response,
                                                &cdma1x_state,
                                                &evdo_state)) {
        ctx->error = g_error_new (MM_CORE_ERROR,
                                  MM_CORE_ERROR_FAILED,
                                  "Couldn't parse SPSERVICE response '%s'",
                                  response);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    /* Store new intermediate results */
    ctx->cdma1x_state = cdma1x_state;
    ctx->evdo_state = evdo_state;

    /* If SPERI not supported, we're done */
    if (!ctx->self->priv->has_speri) {
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    /* Get roaming status to override generic registration state */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$SPERI?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)speri_ready,
                              ctx);
}

static void
modem_cdma_get_detailed_registration_state (MMIfaceModemCdma *self,
                                            MMModemCdmaRegistrationState cdma1x_state,
                                            MMModemCdmaRegistrationState evdo_state,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data)
{
    MMAtSerialPort *port;
    GError *error = NULL;
    DetailedRegistrationStateContext *ctx;

    /* The default implementation to get detailed registration state
     * requires the use of an AT port; so if we cannot get any, just
     * return the error */
    port = mm_base_modem_peek_best_at_port (MM_BASE_MODEM (self), &error);
    if (!port) {
        g_simple_async_report_take_gerror_in_idle (G_OBJECT (self),
                                                   callback,
                                                   user_data,
                                                   error);
        return;
    }

    /* Setup context */
    ctx = g_new0 (DetailedRegistrationStateContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_get_detailed_registration_state);
    ctx->port = g_object_ref (port);
    ctx->cdma1x_state = cdma1x_state;
    ctx->evdo_state = evdo_state;

    /* NOTE: If we get this generic implementation of getting detailed
     * registration state called, we DO know that we have Sprint commands
     * supported, we checked it in setup_registration_checks() */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+SPSERVICE?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)spservice_ready,
                              ctx);
}

/*****************************************************************************/
/* Setup registration checks (CDMA interface) */

typedef struct {
    gboolean skip_qcdm_call_manager_step;
    gboolean skip_qcdm_hdr_step;
    gboolean skip_at_cdma_service_status_step;
    gboolean skip_at_cdma1x_serving_system_step;
    gboolean skip_detailed_registration_state;
} SetupRegistrationChecksResults;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    GError *error;
    gboolean has_qcdm_port;
    gboolean has_sprint_commands;
} SetupRegistrationChecksContext;

static void
setup_registration_checks_context_complete_and_free (SetupRegistrationChecksContext *ctx)
{
    if (ctx->error)
        g_simple_async_result_take_error (ctx->result, ctx->error);
    else {
        SetupRegistrationChecksResults *results;

        results = g_new0 (SetupRegistrationChecksResults, 1);

        /* Skip QCDM steps if no QCDM port */
        if (!ctx->has_qcdm_port) {
            mm_dbg ("Will skip all QCDM-based registration checks");
            results->skip_qcdm_call_manager_step = TRUE;
            results->skip_qcdm_hdr_step = TRUE;
        }

        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_detailed_registration_state ==
            modem_cdma_get_detailed_registration_state) {
            /* Skip CDMA1x Serving System check if we have Sprint specific
             * commands AND if the default detailed registration checker
             * is the generic one. Implementations knowing that their
             * CSS response is undesired, should either setup NULL callbacks
             * for the specific step, or subclass this setup and return
             * FALSE themselves. */
            if (ctx->has_sprint_commands) {
                mm_dbg ("Will skip CDMA1x Serving System check, "
                        "we do have Sprint commands");
                results->skip_at_cdma1x_serving_system_step = TRUE;
            } else {
                /* If there aren't Sprint specific commands, and the detailed
                 * registration state getter wasn't subclassed, skip the step */
                mm_dbg ("Will skip generic detailed registration check, we "
                        "don't have Sprint commands");
                results->skip_detailed_registration_state = TRUE;
            }
        }

        g_simple_async_result_set_op_res_gpointer (ctx->result, results, g_free);
    }

    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_cdma_setup_registration_checks_finish (MMIfaceModemCdma *self,
                                             GAsyncResult *res,
                                             gboolean *skip_qcdm_call_manager_step,
                                             gboolean *skip_qcdm_hdr_step,
                                             gboolean *skip_at_cdma_service_status_step,
                                             gboolean *skip_at_cdma1x_serving_system_step,
                                             gboolean *skip_detailed_registration_state,
                                             GError **error)
{
    SetupRegistrationChecksResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *skip_qcdm_call_manager_step = results->skip_qcdm_call_manager_step;
    *skip_qcdm_hdr_step = results->skip_qcdm_hdr_step;
    *skip_at_cdma_service_status_step = results->skip_at_cdma_service_status_step;
    *skip_at_cdma1x_serving_system_step = results->skip_at_cdma1x_serving_system_step;
    *skip_detailed_registration_state = results->skip_detailed_registration_state;
    return TRUE;
}

static void
speri_check_ready (MMIfaceModemCdma *self,
                   GAsyncResult *res,
                   SetupRegistrationChecksContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_error_free (error);
    else
        /* We DO have SPERI */
        ctx->self->priv->has_speri = TRUE;

    /* All done */
    ctx->self->priv->checked_sprint_support = TRUE;
    setup_registration_checks_context_complete_and_free (ctx);
}

static void
spservice_check_ready (MMIfaceModemCdma *self,
                       GAsyncResult *res,
                       SetupRegistrationChecksContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_error_free (error);
        ctx->self->priv->checked_sprint_support = TRUE;
        setup_registration_checks_context_complete_and_free (ctx);
        return;
    }

    /* We DO have SPSERVICE, look for SPERI */
    ctx->has_sprint_commands = TRUE;
    ctx->self->priv->has_spservice = TRUE;
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$SPERI?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)speri_check_ready,
                              ctx);
}

static void
modem_cdma_setup_registration_checks (MMIfaceModemCdma *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    SetupRegistrationChecksContext *ctx;

    ctx = g_new0 (SetupRegistrationChecksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_setup_registration_checks);

    /* Check if we have a QCDM port */
    ctx->has_qcdm_port = !!mm_base_modem_peek_port_qcdm (MM_BASE_MODEM (self));

    /* If we have cached results of Sprint command checking, use them */
    if (ctx->self->priv->checked_sprint_support) {
        ctx->has_sprint_commands = ctx->self->priv->has_spservice;

        /* Completes in idle */
        setup_registration_checks_context_complete_and_free (ctx);
        return;
    }

    /* Otherwise, launch Sprint command checks. */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+SPSERVICE?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)spservice_check_ready,
                              ctx);
}

/*****************************************************************************/
/* Register in network (CDMA interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    GTimer *timer;
    guint max_registration_time;
} RegisterInCdmaNetworkContext;

static void
register_in_cdma_network_context_complete_and_free (RegisterInCdmaNetworkContext *ctx)
{
    /* If our cancellable reference is still around, clear it */
    if (ctx->self->priv->modem_cdma_pending_registration_cancellable ==
        ctx->cancellable) {
        g_clear_object (&ctx->self->priv->modem_cdma_pending_registration_cancellable);
    }

    if (ctx->timer)
        g_timer_destroy (ctx->timer);

    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_cdma_register_in_network_finish (MMIfaceModemCdma *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef REG_IS_IDLE
#define REG_IS_IDLE(state)                              \
    (state == MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)

#undef REG_IS_DONE
#define REG_IS_DONE(state)                                  \
    (state == MM_MODEM_CDMA_REGISTRATION_STATE_HOME ||      \
     state == MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING ||   \
     state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED)

static void run_cdma_registration_checks_ready (MMBroadbandModem *self,
                                                GAsyncResult *res,
                                                RegisterInCdmaNetworkContext *ctx);

static gboolean
run_cdma_registration_checks_again (RegisterInCdmaNetworkContext *ctx)
{
    /* Get fresh registration state */
    mm_iface_modem_cdma_run_registration_checks (
        MM_IFACE_MODEM_CDMA (ctx->self),
        (GAsyncReadyCallback)run_cdma_registration_checks_ready,
        ctx);
    return FALSE;
}

static void
run_cdma_registration_checks_ready (MMBroadbandModem *self,
                                    GAsyncResult *res,
                                    RegisterInCdmaNetworkContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_cdma_run_registration_checks_finish (MM_IFACE_MODEM_CDMA (self), res, &error);

    if (error) {
        mm_dbg ("CDMA registration check failed: '%s'", error->message);
        mm_iface_modem_cdma_update_cdma1x_registration_state (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
            MM_MODEM_CDMA_SID_UNKNOWN,
            MM_MODEM_CDMA_NID_UNKNOWN);
        mm_iface_modem_cdma_update_evdo_registration_state (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        mm_iface_modem_cdma_update_access_technologies (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

        g_simple_async_result_take_error (ctx->result, error);
        register_in_cdma_network_context_complete_and_free (ctx);
        return;
    }

    /* If we got registered in at least one CDMA network, end registration checks */
    if (REG_IS_DONE (self->priv->modem_cdma_cdma1x_registration_state) ||
        REG_IS_DONE (self->priv->modem_cdma_evdo_registration_state)) {
        mm_dbg ("Modem is currently registered in a CDMA network "
                "(CDMA1x: '%s', EV-DO: '%s')",
                REG_IS_DONE (self->priv->modem_cdma_cdma1x_registration_state) ? "yes" : "no",
                REG_IS_DONE (self->priv->modem_cdma_evdo_registration_state) ? "yes" : "no");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        register_in_cdma_network_context_complete_and_free (ctx);
        return;
    }

    /* Don't spend too much time waiting to get registered */
    if (g_timer_elapsed (ctx->timer, NULL) > ctx->max_registration_time) {
        mm_dbg ("CDMA registration check timed out");
        mm_iface_modem_cdma_update_cdma1x_registration_state (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
            MM_MODEM_CDMA_SID_UNKNOWN,
            MM_MODEM_CDMA_NID_UNKNOWN);
        mm_iface_modem_cdma_update_evdo_registration_state (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        mm_iface_modem_cdma_update_access_technologies (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_simple_async_result_take_error (
            ctx->result,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT));
        register_in_cdma_network_context_complete_and_free (ctx);
        return;
    }

    /* Check again in a few seconds. */
    mm_dbg ("Modem not yet registered in a CDMA network... will recheck soon");
    g_timeout_add_seconds (3,
                           (GSourceFunc)run_cdma_registration_checks_again,
                           ctx);
}

static void
modem_cdma_register_in_network (MMIfaceModemCdma *self,
                                guint max_registration_time,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MMBroadbandModem *broadband = MM_BROADBAND_MODEM (self);
    RegisterInCdmaNetworkContext *ctx;

    /* (Try to) cancel previous registration request */
    if (broadband->priv->modem_cdma_pending_registration_cancellable) {
        g_cancellable_cancel (broadband->priv->modem_cdma_pending_registration_cancellable);
        g_clear_object (&broadband->priv->modem_cdma_pending_registration_cancellable);
    }

    ctx = g_new0 (RegisterInCdmaNetworkContext, 1);
    ctx->self = g_object_ref (self);
    ctx->max_registration_time = max_registration_time;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_register_in_network);
    ctx->cancellable = g_cancellable_new ();

    /* Keep an accessible reference to the cancellable, so that we can cancel
     * previous request when needed */
    broadband->priv->modem_cdma_pending_registration_cancellable =
        g_object_ref (ctx->cancellable);

    /* Get fresh registration state */
    ctx->timer = g_timer_new ();
    mm_iface_modem_cdma_run_registration_checks (
        MM_IFACE_MODEM_CDMA (self),
        (GAsyncReadyCallback)run_cdma_registration_checks_ready,
        ctx);
}

/*****************************************************************************/
/* Load location capabilities (Location interface) */

static MMModemLocationSource
modem_location_load_capabilities_finish (MMIfaceModemLocation *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCATION_SOURCE_NONE;

    return (MMModemLocationSource) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                                         G_SIMPLE_ASYNC_RESULT (res)));
}

static void
modem_location_load_capabilities (MMIfaceModemLocation *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_location_load_capabilities);

    /* Default location capabilities supported by the generic broadband
     * implementation are only LAC-CI in 3GPP-enabled modems. And even this,
     * will only be true if the modem supports CREG/CGREG=2 */
    if (!mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        g_simple_async_result_set_op_res_gpointer (result,
                                                   GUINT_TO_POINTER (MM_MODEM_LOCATION_SOURCE_NONE),
                                                   NULL);
    else
        g_simple_async_result_set_op_res_gpointer (result,
                                                   GUINT_TO_POINTER (MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI),
                                                   NULL);

    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
enable_location_gathering (MMIfaceModemLocation *self,
                           MMModemLocationSource source,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        enable_location_gathering);

    /* 3GPP modems need to re-run registration checks when enabling the 3GPP
     * location source, so that we get up to date LAC/CI location information.
     * Note that we don't care for when the registration checks get finished.
     */
    if (source == MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI &&
        mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self))) {
        /* Reload registration to get LAC/CI */
        mm_iface_modem_3gpp_run_registration_checks (MM_IFACE_MODEM_3GPP (self), NULL, NULL);
        /* Reload operator to get MCC/MNC */
        if (MM_BROADBAND_MODEM (self)->priv->modem_state >= MM_MODEM_STATE_REGISTERED)
            mm_iface_modem_3gpp_reload_current_operator (MM_IFACE_MODEM_3GPP (self), NULL, NULL);
    }

    /* Done we are */
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

static void
setup_ports (MMBroadbandModem *self)
{
    MMAtSerialPort *ports[2];
    GRegex *regex;
    GPtrArray *array;
    gint i, j;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Cleanup all unsolicited message handlers in all AT ports */

    /* Set up CREG unsolicited message handlers, with NULL callbacks */
    array = mm_3gpp_creg_regex_get (FALSE);
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        for (j = 0; j < array->len; j++) {
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (ports[i]),
                                                           (GRegex *)g_ptr_array_index (array, j),
                                                           NULL,
                                                           NULL,
                                                           NULL);
        }
    }
    mm_3gpp_creg_regex_destroy (array);

    /* Set up CIEV unsolicited message handler, with NULL callback */
    regex = mm_3gpp_ciev_regex_get ();
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (ports[i]),
                                                       regex,
                                                       NULL,
                                                       NULL,
                                                       NULL);
    }
    g_regex_unref (regex);

    /* Set up CMTI unsolicited message handler, with NULL callback */
    regex = mm_3gpp_cmti_regex_get ();
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (ports[i]),
                                                       regex,
                                                       NULL,
                                                       NULL,
                                                       NULL);
    }
    g_regex_unref (regex);

    /* Set up CUSD unsolicited message handler, with NULL callback */
    regex = mm_3gpp_cusd_regex_get ();
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (ports[i]),
                                                       regex,
                                                       NULL,
                                                       NULL,
                                                       NULL);
    }
    g_regex_unref (regex);
}

/*****************************************************************************/
/* Generic ports open/close context */

struct _PortsContext {
    volatile gint ref_count;

    MMAtSerialPort *primary;
    gboolean primary_open;
    MMAtSerialPort *secondary;
    gboolean secondary_open;
    MMQcdmSerialPort *qcdm;
    gboolean qcdm_open;
};

static PortsContext *
ports_context_ref (PortsContext *ctx)
{
    g_atomic_int_inc (&ctx->ref_count);
    return ctx;
}

static void
ports_context_unref (PortsContext *ctx)
{
    if (g_atomic_int_dec_and_test (&ctx->ref_count)) {
        if (ctx->primary) {
            if (ctx->primary_open)
                mm_serial_port_close (MM_SERIAL_PORT (ctx->primary));
            g_object_unref (ctx->primary);
        }
        if (ctx->secondary) {
            if (ctx->secondary_open)
                mm_serial_port_close (MM_SERIAL_PORT (ctx->secondary));
            g_object_unref (ctx->secondary);
        }
        if (ctx->qcdm) {
            if (ctx->qcdm_open)
                mm_serial_port_close (MM_SERIAL_PORT (ctx->qcdm));
            g_object_unref (ctx->qcdm);
        }
        g_free (ctx);
    }
}

/*****************************************************************************/
/* Initialization started/stopped */

static gboolean
initialization_stopped (MMBroadbandModem *self,
                        gpointer user_data,
                        GError **error)
{
    PortsContext *ctx = (PortsContext *)user_data;

    ports_context_unref (ctx);
    return TRUE;
}

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    PortsContext *ports;
} InitializationStartedContext;

static void
initialization_started_context_complete_and_free (InitializationStartedContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    ports_context_unref (ctx->ports);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return ((gpointer) ports_context_ref (
                g_simple_async_result_get_op_res_gpointer (
                    G_SIMPLE_ASYNC_RESULT (res))));
}

static gboolean
open_ports_initialization (MMBroadbandModem *self,
                           PortsContext *ctx,
                           GError **error)
{
    ctx->primary = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    if (!ctx->primary) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't get primary port");
        return FALSE;
    }

    /* Open and send first commands to the primary serial port.
     * We do keep the primary port open during the whole initialization
     * sequence. Note that this port is not really passed to the interfaces,
     * they will get the primary port themselves. */
    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->primary), error)) {
        g_prefix_error (error, "Couldn't open primary port: ");
        return FALSE;
    }

    ctx->primary_open = TRUE;

    /* Try to disable echo */
    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   ctx->primary,
                                   "E0", 3,
                                   FALSE, FALSE, NULL, NULL, NULL);
    /* Try to get extended errors */
    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   ctx->primary,
                                   "+CMEE=1", 3,
                                   FALSE, FALSE, NULL, NULL, NULL);

    return TRUE;
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GError *error = NULL;
    InitializationStartedContext *ctx;

    ctx = g_new0 (InitializationStartedContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_started);
    ctx->ports = g_new0 (PortsContext, 1);
    ctx->ports->ref_count = 1;

    if (!open_ports_initialization (self, ctx->ports, &error)) {
        g_prefix_error (&error, "Couldn't open ports during modem initialization: ");
        g_simple_async_result_take_error (ctx->result, error);
    } else
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   ports_context_ref (ctx->ports),
                                                   (GDestroyNotify)ports_context_unref);

    initialization_started_context_complete_and_free (ctx);
}

/*****************************************************************************/
/* Disabling stopped */

static gboolean
disabling_stopped (MMBroadbandModem *self,
                   GError **error)
{
    if (self->priv->enabled_ports_ctx) {
        ports_context_unref (self->priv->enabled_ports_ctx);
        self->priv->enabled_ports_ctx = NULL;
    }
    return TRUE;
}

/*****************************************************************************/
/* Enabling started */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    PortsContext *ports;
} EnablingStartedContext;

static void
enabling_started_context_complete_and_free (EnablingStartedContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    ports_context_unref (ctx->ports);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
enabling_started_finish (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
open_ports_enabling (MMBroadbandModem *self,
                     PortsContext *ctx,
                     GError **error)
{
    /* Open primary */
    ctx->primary = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    if (!ctx->primary) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't get primary port");
        return FALSE;
    }

    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->primary), error)) {
        g_prefix_error (error, "Couldn't open primary port: ");
        return FALSE;
    }

    ctx->primary_open = TRUE;

    /* Open secondary (optional) */
    ctx->secondary = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));
    if (ctx->secondary) {
        if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->secondary), error)) {
            g_prefix_error (error, "Couldn't open secondary port: ");
            return FALSE;
        }
        ctx->secondary_open = TRUE;
    }

    /* Open qcdm (optional) */
    ctx->qcdm = mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));
    if (ctx->qcdm) {
        if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->qcdm), error)) {
            g_prefix_error (error, "Couldn't open QCDM port: ");
            return FALSE;
        }
        ctx->qcdm_open = TRUE;
    }

    return TRUE;
}

static void
enabling_flash_done (MMSerialPort *port,
                     GError *error,
                     EnablingStartedContext *ctx)
{
    if (error) {
        g_prefix_error (&error, "Primary port flashing failed: ");
        g_simple_async_result_set_from_error (ctx->result, error);
    } else {
        ctx->self->priv->enabled_ports_ctx = ports_context_ref (ctx->ports);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    enabling_started_context_complete_and_free (ctx);
}

static void
enabling_started (MMBroadbandModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GError *error = NULL;
    EnablingStartedContext *ctx;

    ctx = g_new0 (EnablingStartedContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enabling_started);
    ctx->ports = g_new0 (PortsContext, 1);
    ctx->ports->ref_count = 1;

    /* Enabling */
    if (!open_ports_enabling (self, ctx->ports, &error)) {
        g_prefix_error (&error, "Couldn't open ports during modem enabling: ");
        g_simple_async_result_take_error (ctx->result, error);
        enabling_started_context_complete_and_free (ctx);
        return;
    }

    /* Ports were correctly opened, now flash the primary port */
    mm_dbg ("Flashing primary port before enabling...");
    mm_serial_port_flash (MM_SERIAL_PORT (ctx->ports->primary),
                          100,
                          FALSE,
                          (MMSerialFlashFn)enabling_flash_done,
                          ctx);
}

/*****************************************************************************/

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISCONNECT_BEARERS,
    DISABLING_STEP_IFACE_SIMPLE,
    DISABLING_STEP_IFACE_FIRMWARE,
    DISABLING_STEP_IFACE_TIME,
    DISABLING_STEP_IFACE_MESSAGING,
    DISABLING_STEP_IFACE_LOCATION,
    DISABLING_STEP_IFACE_CONTACTS,
    DISABLING_STEP_IFACE_CDMA,
    DISABLING_STEP_IFACE_3GPP_USSD,
    DISABLING_STEP_IFACE_3GPP,
    DISABLING_STEP_IFACE_MODEM,
    DISABLING_STEP_LAST,
} DisablingStep;

typedef struct {
    MMBroadbandModem *self;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    DisablingStep step;
} DisablingContext;

static void disabling_step (DisablingContext *ctx);

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    GError *error = NULL;

    g_simple_async_result_complete_in_idle (ctx->result);

    if (MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->disabling_stopped &&
        !MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->disabling_stopped (ctx->self, &error)) {
        mm_warn ("Error when stopping the disabling sequence: %s", error->message);
        g_error_free (error);
    }

    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
disabling_context_complete_and_free_if_cancelled (DisablingContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Disabling cancelled");
    disabling_context_complete_and_free (ctx);
    return TRUE;
}

static gboolean
disable_finish (MMBaseModem *self,
               GAsyncResult *res,
               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef INTERFACE_DISABLE_READY_FN
#define INTERFACE_DISABLE_READY_FN(NAME,TYPE,FATAL_ERRORS)              \
    static void                                                         \
    NAME##_disable_ready (MMBroadbandModem *self,                       \
                          GAsyncResult *result,                         \
                          DisablingContext *ctx)                        \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        if (!mm_##NAME##_disable_finish (TYPE (self),                   \
                                         result,                        \
                                         &error)) {                     \
            if (FATAL_ERRORS) {                                         \
                g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (ctx->result), error); \
                disabling_context_complete_and_free (ctx);              \
                return;                                                 \
            }                                                           \
                                                                        \
            mm_dbg ("Couldn't disable interface: '%s'",                 \
                    error->message);                                    \
            g_error_free (error);                                       \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        disabling_step (ctx);                                           \
    }

INTERFACE_DISABLE_READY_FN (iface_modem,           MM_IFACE_MODEM,           TRUE)
INTERFACE_DISABLE_READY_FN (iface_modem_3gpp,      MM_IFACE_MODEM_3GPP,      TRUE)
INTERFACE_DISABLE_READY_FN (iface_modem_3gpp_ussd, MM_IFACE_MODEM_3GPP_USSD, TRUE)
INTERFACE_DISABLE_READY_FN (iface_modem_cdma,      MM_IFACE_MODEM_CDMA,      TRUE)
INTERFACE_DISABLE_READY_FN (iface_modem_location,  MM_IFACE_MODEM_LOCATION,  FALSE)
INTERFACE_DISABLE_READY_FN (iface_modem_messaging, MM_IFACE_MODEM_MESSAGING, FALSE)
INTERFACE_DISABLE_READY_FN (iface_modem_time,      MM_IFACE_MODEM_TIME,      FALSE)

static void
bearer_list_disconnect_all_bearers_ready (MMBearerList *list,
                                          GAsyncResult *res,
                                          DisablingContext *ctx)
{
    GError *error = NULL;

    if (!mm_bearer_list_disconnect_all_bearers_finish (list, res, &error)) {
        g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (ctx->result), error);
        disabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    disabling_step (ctx);
}

static void
disabling_step (DisablingContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (disabling_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISCONNECT_BEARERS:
        if (ctx->self->priv->modem_bearer_list) {
            mm_bearer_list_disconnect_all_bearers (
                ctx->self->priv->modem_bearer_list,
                (GAsyncReadyCallback)bearer_list_disconnect_all_bearers_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_SIMPLE:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_FIRMWARE:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_TIME:
        if (ctx->self->priv->modem_time_dbus_skeleton) {
            mm_dbg ("Modem has time capabilities, disabling the Time interface...");
            /* Disabling the Modem Time interface */
            mm_iface_modem_time_disable (MM_IFACE_MODEM_TIME (ctx->self),
                                         (GAsyncReadyCallback)iface_modem_time_disable_ready,
                                         ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_MESSAGING:
        if (ctx->self->priv->modem_messaging_dbus_skeleton) {
            mm_dbg ("Modem has messaging capabilities, disabling the Messaging interface...");
            /* Disabling the Modem Messaging interface */
            mm_iface_modem_messaging_disable (MM_IFACE_MODEM_MESSAGING (ctx->self),
                                              (GAsyncReadyCallback)iface_modem_messaging_disable_ready,
                                              ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_LOCATION:
        if (ctx->self->priv->modem_location_dbus_skeleton) {
            mm_dbg ("Modem has location capabilities, disabling the Location interface...");
            /* Disabling the Modem Location interface */
            mm_iface_modem_location_disable (MM_IFACE_MODEM_LOCATION (ctx->self),
                                             (GAsyncReadyCallback)iface_modem_location_disable_ready,
                                             ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_CONTACTS:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_CDMA:
        if (ctx->self->priv->modem_cdma_dbus_skeleton) {
            mm_dbg ("Modem has CDMA capabilities, disabling the Modem CDMA interface...");
            /* Disabling the Modem CDMA interface */
            mm_iface_modem_cdma_disable (MM_IFACE_MODEM_CDMA (ctx->self),
                                        (GAsyncReadyCallback)iface_modem_cdma_disable_ready,
                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_3GPP_USSD:
        if (ctx->self->priv->modem_3gpp_ussd_dbus_skeleton) {
            mm_dbg ("Modem has 3GPP/USSD capabilities, disabling the Modem 3GPP/USSD interface...");
            /* Disabling the Modem 3GPP USSD interface */
            mm_iface_modem_3gpp_ussd_disable (MM_IFACE_MODEM_3GPP_USSD (ctx->self),
                                              (GAsyncReadyCallback)iface_modem_3gpp_ussd_disable_ready,
                                              ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_3GPP:
        if (ctx->self->priv->modem_3gpp_dbus_skeleton) {
            mm_dbg ("Modem has 3GPP capabilities, disabling the Modem 3GPP interface...");
            /* Disabling the Modem 3GPP interface */
            mm_iface_modem_3gpp_disable (MM_IFACE_MODEM_3GPP (ctx->self),
                                        (GAsyncReadyCallback)iface_modem_3gpp_disable_ready,
                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_MODEM:
        /* This skeleton may be NULL when mm_base_modem_disable() gets called at
         * the same time as modem object disposal. */
        if (ctx->self->priv->modem_dbus_skeleton) {
            /* Disabling the Modem interface */
            mm_iface_modem_disable (MM_IFACE_MODEM (ctx->self),
                                    (GAsyncReadyCallback)iface_modem_disable_ready,
                                    ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* All disabled without errors! */
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (ctx->result), TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static void
disable (MMBaseModem *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, disable);

    /* Check state before launching modem disabling */
    switch (MM_BROADBAND_MODEM (self)->priv->modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_FAILED:
    case MM_MODEM_STATE_INITIALIZING:
    case MM_MODEM_STATE_LOCKED:
    case MM_MODEM_STATE_DISABLED:
        /* Just return success, don't relaunch disabling.
         * Note that we do consider here UNKNOWN and FAILED status on purpose,
         * as the MMManager will try to disable every modem before removing
         * it. */
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        break;

    case MM_MODEM_STATE_DISABLING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot disable modem: "
                                         "already being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot disable modem: "
                                         "currently being enabled");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED: {
        DisablingContext *ctx;

        ctx = g_new0 (DisablingContext, 1);
        ctx->self = g_object_ref (self);
        ctx->result = result;
        ctx->cancellable = (cancellable ? g_object_ref (cancellable) : NULL);
        ctx->step = DISABLING_STEP_FIRST;

        disabling_step (ctx);
        return;
      }
    }

    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_STARTED,
    ENABLING_STEP_IFACE_MODEM,
    ENABLING_STEP_IFACE_3GPP,
    ENABLING_STEP_IFACE_3GPP_USSD,
    ENABLING_STEP_IFACE_CDMA,
    ENABLING_STEP_IFACE_CONTACTS,
    ENABLING_STEP_IFACE_LOCATION,
    ENABLING_STEP_IFACE_MESSAGING,
    ENABLING_STEP_IFACE_TIME,
    ENABLING_STEP_IFACE_FIRMWARE,
    ENABLING_STEP_IFACE_SIMPLE,
    ENABLING_STEP_LAST,
} EnablingStep;

typedef struct {
    MMBroadbandModem *self;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    EnablingStep step;
} EnablingContext;

static void enabling_step (EnablingContext *ctx);

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
enabling_context_complete_and_free_if_cancelled (EnablingContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Enabling cancelled");
    enabling_context_complete_and_free (ctx);
    return TRUE;
}

static gboolean
enable_finish (MMBaseModem *self,
               GAsyncResult *res,
               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    return TRUE;
}

static void
enabling_started_ready (MMBroadbandModem *self,
                        GAsyncResult *result,
                        EnablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_BROADBAND_MODEM_GET_CLASS (self)->enabling_started_finish (self, result, &error)) {
        g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (ctx->result), error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    enabling_step (ctx);
}

#undef INTERFACE_ENABLE_READY_FN
#define INTERFACE_ENABLE_READY_FN(NAME,TYPE,FATAL_ERRORS)               \
    static void                                                         \
    NAME##_enable_ready (MMBroadbandModem *self,                        \
                         GAsyncResult *result,                          \
                         EnablingContext *ctx)                          \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        if (!mm_##NAME##_enable_finish (TYPE (self),                    \
                                        result,                         \
                                        &error)) {                      \
            if (FATAL_ERRORS) {                                         \
                g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (ctx->result), error); \
                enabling_context_complete_and_free (ctx);               \
                return;                                                 \
            }                                                           \
                                                                        \
            mm_dbg ("Couldn't enable interface: '%s'",                  \
                    error->message);                                    \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        enabling_step (ctx);                                            \
    }

INTERFACE_ENABLE_READY_FN (iface_modem,           MM_IFACE_MODEM,           TRUE)
INTERFACE_ENABLE_READY_FN (iface_modem_3gpp,      MM_IFACE_MODEM_3GPP,      TRUE)
INTERFACE_ENABLE_READY_FN (iface_modem_3gpp_ussd, MM_IFACE_MODEM_3GPP_USSD, TRUE)
INTERFACE_ENABLE_READY_FN (iface_modem_cdma,      MM_IFACE_MODEM_CDMA,      TRUE)
INTERFACE_ENABLE_READY_FN (iface_modem_location,  MM_IFACE_MODEM_LOCATION,  FALSE)
INTERFACE_ENABLE_READY_FN (iface_modem_messaging, MM_IFACE_MODEM_MESSAGING, FALSE)
INTERFACE_ENABLE_READY_FN (iface_modem_time,      MM_IFACE_MODEM_TIME,      FALSE)

static void
enabling_step (EnablingContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (enabling_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_STARTED:
        if (MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->enabling_started &&
            MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->enabling_started_finish) {
            MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->enabling_started (ctx->self,
                                                                        (GAsyncReadyCallback)enabling_started_ready,
                                                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_MODEM:
        g_assert (ctx->self->priv->modem_dbus_skeleton != NULL);
        /* Enabling the Modem interface */
        mm_iface_modem_enable (MM_IFACE_MODEM (ctx->self),
                               ctx->cancellable,
                               (GAsyncReadyCallback)iface_modem_enable_ready,
                               ctx);
        return;

    case ENABLING_STEP_IFACE_3GPP:
        if (ctx->self->priv->modem_3gpp_dbus_skeleton) {
            mm_dbg ("Modem has 3GPP capabilities, enabling the Modem 3GPP interface...");
            /* Enabling the Modem 3GPP interface */
            mm_iface_modem_3gpp_enable (MM_IFACE_MODEM_3GPP (ctx->self),
                                        ctx->cancellable,
                                        (GAsyncReadyCallback)iface_modem_3gpp_enable_ready,
                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_3GPP_USSD:
        if (ctx->self->priv->modem_3gpp_ussd_dbus_skeleton) {
            mm_dbg ("Modem has 3GPP/USSD capabilities, enabling the Modem 3GPP/USSD interface...");
            mm_iface_modem_3gpp_ussd_enable (MM_IFACE_MODEM_3GPP_USSD (ctx->self),
                                             (GAsyncReadyCallback)iface_modem_3gpp_ussd_enable_ready,
                                             ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_CDMA:
        if (ctx->self->priv->modem_cdma_dbus_skeleton) {
            mm_dbg ("Modem has CDMA capabilities, enabling the Modem CDMA interface...");
            /* Enabling the Modem CDMA interface */
            mm_iface_modem_cdma_enable (MM_IFACE_MODEM_CDMA (ctx->self),
                                        ctx->cancellable,
                                        (GAsyncReadyCallback)iface_modem_cdma_enable_ready,
                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_CONTACTS:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_LOCATION:
        if (ctx->self->priv->modem_location_dbus_skeleton) {
            mm_dbg ("Modem has location capabilities, enabling the Location interface...");
            /* Enabling the Modem Location interface */
            mm_iface_modem_location_enable (MM_IFACE_MODEM_LOCATION (ctx->self),
                                            ctx->cancellable,
                                            (GAsyncReadyCallback)iface_modem_location_enable_ready,
                                            ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_MESSAGING:
        if (ctx->self->priv->modem_messaging_dbus_skeleton) {
            mm_dbg ("Modem has messaging capabilities, enabling the Messaging interface...");
            /* Enabling the Modem Messaging interface */
            mm_iface_modem_messaging_enable (MM_IFACE_MODEM_MESSAGING (ctx->self),
                                             ctx->cancellable,
                                             (GAsyncReadyCallback)iface_modem_messaging_enable_ready,
                                             ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_TIME:
        if (ctx->self->priv->modem_time_dbus_skeleton) {
            mm_dbg ("Modem has time capabilities, enabling the Time interface...");
            /* Enabling the Modem Time interface */
            mm_iface_modem_time_enable (MM_IFACE_MODEM_TIME (ctx->self),
                                        ctx->cancellable,
                                        (GAsyncReadyCallback)iface_modem_time_enable_ready,
                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_FIRMWARE:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_SIMPLE:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* All enabled without errors! */
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (ctx->result), TRUE);
        enabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static void
enable (MMBaseModem *self,
        GCancellable *cancellable,
        GAsyncReadyCallback callback,
        gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, enable);

    /* Check state before launching modem enabling */
    switch (MM_BROADBAND_MODEM (self)->priv->modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_FAILED:
        /* We should never have a UNKNOWN|FAILED->ENABLED transition */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_INITIALIZING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot enable modem: "
                                         "device not fully initialized yet");
        break;

    case MM_MODEM_STATE_LOCKED:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot enable modem: device locked");
        break;

    case MM_MODEM_STATE_DISABLED: {
        EnablingContext *ctx;

        ctx = g_new0 (EnablingContext, 1);
        ctx->self = g_object_ref (self);
        ctx->result = result;
        ctx->cancellable = g_object_ref (cancellable);
        ctx->step = ENABLING_STEP_FIRST;
        enabling_step (ctx);
        return;
    }

    case MM_MODEM_STATE_DISABLING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot enable modem: "
                                         "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot enable modem: "
                                         "already being enabled");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        /* Just return success, don't relaunch enabling */
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        break;
    }

    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

typedef enum {
    INITIALIZE_STEP_FIRST,
    INITIALIZE_STEP_SETUP_PORTS,
    INITIALIZE_STEP_STARTED,
    INITIALIZE_STEP_SETUP_SIMPLE_STATUS,
    INITIALIZE_STEP_IFACE_MODEM,
    INITIALIZE_STEP_IFACE_3GPP,
    INITIALIZE_STEP_IFACE_3GPP_USSD,
    INITIALIZE_STEP_IFACE_CDMA,
    INITIALIZE_STEP_IFACE_CONTACTS,
    INITIALIZE_STEP_IFACE_LOCATION,
    INITIALIZE_STEP_IFACE_MESSAGING,
    INITIALIZE_STEP_IFACE_TIME,
    INITIALIZE_STEP_IFACE_FIRMWARE,
    INITIALIZE_STEP_IFACE_SIMPLE,
    INITIALIZE_STEP_LAST,
} InitializeStep;

typedef struct {
    MMBroadbandModem *self;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    InitializeStep step;
    gpointer ports_ctx;
} InitializeContext;

static void initialize_step (InitializeContext *ctx);

static void
initialize_context_complete_and_free (InitializeContext *ctx)
{
    GError *error = NULL;

    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->ports_ctx &&
        MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->initialization_stopped &&
        !MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->initialization_stopped (ctx->self, ctx->ports_ctx, &error)) {
        mm_warn ("Error when stopping the initialization sequence: %s", error->message);
        g_error_free (error);
    }

    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
initialize_context_complete_and_free_if_cancelled (InitializeContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Initialization cancelled");
    initialize_context_complete_and_free (ctx);
    return TRUE;
}

static gboolean
initialize_finish (MMBaseModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    return TRUE;
}

static void
initialization_started_ready (MMBroadbandModem *self,
                              GAsyncResult *result,
                              InitializeContext *ctx)
{
    GError *error = NULL;
    gpointer ports_ctx;

    ports_ctx = MM_BROADBAND_MODEM_GET_CLASS (self)->initialization_started_finish (self, result, &error);
    if (!ports_ctx) {
        mm_warn ("Couldn't start initialization: %s", error->message);
        g_error_free (error);

        mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                     MM_MODEM_STATE_FAILED,
                                     MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);

        /* Just jump to the last step */
        ctx->step = INITIALIZE_STEP_LAST;
        initialize_step (ctx);
        return;
    }

    /* Keep the ctx for later use when stopping initialization */
    ctx->ports_ctx = ports_ctx;

    /* Go on to next step */
    ctx->step++;
    initialize_step (ctx);
}

static void
iface_modem_initialize_ready (MMBroadbandModem *self,
                              GAsyncResult *result,
                              InitializeContext *ctx)
{
    GError *error = NULL;

    /* If the modem interface fails to get initialized, we will move the modem
     * to a FAILED state. Note that in this case we still export the interface. */
    if (!mm_iface_modem_initialize_finish (MM_IFACE_MODEM (self), result, &error)) {
        /* Report the new FAILED state */
        mm_warn ("Modem couldn't be initialized: %s", error->message);
        g_error_free (error);

        mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                     MM_MODEM_STATE_FAILED,
                                     MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);

        /* Just jump to the last step */
        ctx->step = INITIALIZE_STEP_LAST;
        initialize_step (ctx);
        return;
    }

    /* bind simple properties */
    mm_iface_modem_bind_simple_status (MM_IFACE_MODEM (self),
                                       self->priv->modem_simple_status);

    /* If we find ourselves in a LOCKED state, we shouldn't keep on
     * the initialization sequence. Instead, we will re-initialize once
     * we are unlocked. */
    if (ctx->self->priv->modem_state == MM_MODEM_STATE_LOCKED) {
        /* Jump to the Firmware interface. We do allow modems to export
         * both the Firmware and Simple interfaces when locked. */
        ctx->step = INITIALIZE_STEP_IFACE_FIRMWARE;
        initialize_step (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    initialize_step (ctx);
}

#undef INTERFACE_INIT_READY_FN
#define INTERFACE_INIT_READY_FN(NAME,TYPE,FATAL_ERRORS)                 \
    static void                                                         \
    NAME##_initialize_ready (MMBroadbandModem *self,                    \
                             GAsyncResult *result,                      \
                             InitializeContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        if (!mm_##NAME##_initialize_finish (TYPE (self), result, &error)) { \
            if (FATAL_ERRORS) {                                         \
                mm_warn ("Couldn't initialize interface: '%s'",         \
                         error->message);                               \
                g_error_free (error);                                   \
                                                                        \
                /* Report the new FAILED state */                       \
                mm_iface_modem_update_state (MM_IFACE_MODEM (self),     \
                                             MM_MODEM_STATE_FAILED,     \
                                             MM_MODEM_STATE_CHANGE_REASON_UNKNOWN); \
                                                                        \
                /* Just jump to the last step */                        \
                ctx->step = INITIALIZE_STEP_LAST;                       \
                initialize_step (ctx);                                  \
                return;                                                 \
            }                                                           \
                                                                        \
            mm_dbg ("Couldn't initialize interface: '%s'",              \
                    error->message);                                    \
            /* Just shutdown this interface */                          \
            mm_##NAME##_shutdown (TYPE (self));                         \
            g_error_free (error);                                       \
        } else {                                                        \
            /* bind simple properties */                                \
            mm_##NAME##_bind_simple_status (TYPE (self), self->priv->modem_simple_status); \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        initialize_step (ctx);                                          \
    }

INTERFACE_INIT_READY_FN (iface_modem_3gpp,      MM_IFACE_MODEM_3GPP,      TRUE)
INTERFACE_INIT_READY_FN (iface_modem_3gpp_ussd, MM_IFACE_MODEM_3GPP_USSD, FALSE)
INTERFACE_INIT_READY_FN (iface_modem_cdma,      MM_IFACE_MODEM_CDMA,      TRUE)
INTERFACE_INIT_READY_FN (iface_modem_location,  MM_IFACE_MODEM_LOCATION,  FALSE)
INTERFACE_INIT_READY_FN (iface_modem_messaging, MM_IFACE_MODEM_MESSAGING, FALSE)
INTERFACE_INIT_READY_FN (iface_modem_time,      MM_IFACE_MODEM_TIME,      FALSE)
INTERFACE_INIT_READY_FN (iface_modem_firmware,  MM_IFACE_MODEM_FIRMWARE,  FALSE)

static void
initialize_step (InitializeContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (initialize_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case INITIALIZE_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_SETUP_PORTS:
        if (MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->setup_ports)
            MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->setup_ports (ctx->self);
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_STARTED:
        if (MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->initialization_started &&
            MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->initialization_started_finish) {
            MM_BROADBAND_MODEM_GET_CLASS (ctx->self)->initialization_started (ctx->self,
                                                                              (GAsyncReadyCallback)initialization_started_ready,
                                                                              ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_SETUP_SIMPLE_STATUS:
        /* Simple status must be created before any interface initialization,
         * so that interfaces add and bind the properties they want to export.
         */
        if (!ctx->self->priv->modem_simple_status)
            ctx->self->priv->modem_simple_status = mm_simple_status_new ();
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_MODEM:
        /* Initialize the Modem interface */
        mm_iface_modem_initialize (MM_IFACE_MODEM (ctx->self),
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)iface_modem_initialize_ready,
                                   ctx);
        return;

    case INITIALIZE_STEP_IFACE_3GPP:
        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self))) {
            /* Initialize the 3GPP interface */
            mm_iface_modem_3gpp_initialize (MM_IFACE_MODEM_3GPP (ctx->self),
                                            ctx->cancellable,
                                            (GAsyncReadyCallback)iface_modem_3gpp_initialize_ready,
                                            ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_3GPP_USSD:
        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self))) {
            /* Initialize the 3GPP/USSD interface */
            mm_iface_modem_3gpp_ussd_initialize (MM_IFACE_MODEM_3GPP_USSD (ctx->self),
                                                 (GAsyncReadyCallback)iface_modem_3gpp_ussd_initialize_ready,
                                                 ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_CDMA:
        if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self))) {
            /* Initialize the CDMA interface */
            mm_iface_modem_cdma_initialize (MM_IFACE_MODEM_CDMA (ctx->self),
                                            ctx->cancellable,
                                            (GAsyncReadyCallback)iface_modem_cdma_initialize_ready,
                                            ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_CONTACTS:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_LOCATION:
        /* Initialize the Location interface */
        mm_iface_modem_location_initialize (MM_IFACE_MODEM_LOCATION (ctx->self),
                                            ctx->cancellable,
                                            (GAsyncReadyCallback)iface_modem_location_initialize_ready,
                                            ctx);
        return;

    case INITIALIZE_STEP_IFACE_MESSAGING:
        /* Initialize the Messaging interface */
        mm_iface_modem_messaging_initialize (MM_IFACE_MODEM_MESSAGING (ctx->self),
                                             ctx->cancellable,
                                             (GAsyncReadyCallback)iface_modem_messaging_initialize_ready,
                                             ctx);
        return;

    case INITIALIZE_STEP_IFACE_TIME:
        /* Initialize the Time interface */
        mm_iface_modem_time_initialize (MM_IFACE_MODEM_TIME (ctx->self),
                                        ctx->cancellable,
                                        (GAsyncReadyCallback)iface_modem_time_initialize_ready,
                                        ctx);
        return;

    case INITIALIZE_STEP_IFACE_FIRMWARE:
        /* Initialize the Firmware interface */
        mm_iface_modem_firmware_initialize (MM_IFACE_MODEM_FIRMWARE (ctx->self),
                                            ctx->cancellable,
                                            (GAsyncReadyCallback)iface_modem_firmware_initialize_ready,
                                            ctx);
        return;

    case INITIALIZE_STEP_IFACE_SIMPLE:
        mm_iface_modem_simple_initialize (MM_IFACE_MODEM_SIMPLE (ctx->self));
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_LAST:
        if (ctx->self->priv->modem_state == MM_MODEM_STATE_FAILED) {
            /* Fatal SIM failure :-( */
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_WRONG_STATE,
                                             "Modem is unusable, "
                                             "cannot fully initialize");
            /* Ensure we only leave the Modem interface around */
            mm_iface_modem_3gpp_shutdown (MM_IFACE_MODEM_3GPP (ctx->self));
            mm_iface_modem_3gpp_ussd_shutdown (MM_IFACE_MODEM_3GPP_USSD (ctx->self));
            mm_iface_modem_cdma_shutdown (MM_IFACE_MODEM_CDMA (ctx->self));
            mm_iface_modem_location_shutdown (MM_IFACE_MODEM_LOCATION (ctx->self));
            mm_iface_modem_messaging_shutdown (MM_IFACE_MODEM_MESSAGING (ctx->self));
            mm_iface_modem_time_shutdown (MM_IFACE_MODEM_TIME (ctx->self));
            mm_iface_modem_simple_shutdown (MM_IFACE_MODEM_SIMPLE (ctx->self));
            initialize_context_complete_and_free (ctx);
            return;
        }

        if (ctx->self->priv->modem_state == MM_MODEM_STATE_LOCKED) {
            /* We're locked :-/ */
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_WRONG_STATE,
                                             "Modem is currently locked, "
                                             "cannot fully initialize");
            initialize_context_complete_and_free (ctx);
            return;
        }

        /* All initialized without errors!
         * Set as disabled (a.k.a. initialized) */
        mm_iface_modem_update_state (MM_IFACE_MODEM (ctx->self),
                                     MM_MODEM_STATE_DISABLED,
                                     MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);

        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (ctx->result), TRUE);
        initialize_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static void
initialize (MMBaseModem *self,
            GCancellable *cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, initialize);

    /* Check state before launching modem initialization */
    switch (MM_BROADBAND_MODEM (self)->priv->modem_state) {
    case MM_MODEM_STATE_FAILED:
        /* NOTE: this will only happen if we ever support hot-plugging SIMs */
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot initialize modem: "
                                         "device is unusable");
        break;

    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED: {
        InitializeContext *ctx;

        ctx = g_new0 (InitializeContext, 1);
        ctx->self = g_object_ref (self);
        ctx->cancellable = g_object_ref (cancellable);
        ctx->result = result;
        ctx->step = INITIALIZE_STEP_FIRST;

        /* Set as being initialized, even if we were locked before */
        mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                     MM_MODEM_STATE_INITIALIZING,
                                     MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);

        initialize_step (ctx);
        return;
    }

    case MM_MODEM_STATE_INITIALIZING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cannot initialize modem: "
                                         "already being initialized");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        /* Just return success, don't relaunch initialization */
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        break;
    }

    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

gchar *
mm_broadband_modem_take_and_convert_to_utf8 (MMBroadbandModem *self,
                                             gchar *str)
{
    /* should only be used AFTER current charset is set */
    if (self->priv->modem_current_charset == MM_MODEM_CHARSET_UNKNOWN)
        return str;

    return mm_charset_take_and_convert_to_utf8 (str,
                                                self->priv->modem_current_charset);
}

gchar *
mm_broadband_modem_take_and_convert_to_current_charset (MMBroadbandModem *self,
                                                        gchar *str)
{
    /* should only be used AFTER current charset is set */
    if (self->priv->modem_current_charset == MM_MODEM_CHARSET_UNKNOWN)
        return str;

    return mm_utf8_take_and_convert_to_charset (str, self->priv->modem_current_charset);
}

gchar *
mm_broadband_modem_create_device_identifier (MMBroadbandModem *self,
                                             const gchar *ati,
                                             const gchar *ati1)
{
    return (mm_create_device_identifier (
                mm_base_modem_get_vendor_id (MM_BASE_MODEM (self)),
                mm_base_modem_get_product_id (MM_BASE_MODEM (self)),
                ati,
                ati1,
                mm_gdbus_modem_get_equipment_identifier (
                    MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton)),
                mm_gdbus_modem_get_revision (
                    MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton)),
                mm_gdbus_modem_get_model (
                    MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton)),
                mm_gdbus_modem_get_manufacturer (
                    MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton))));
}

/*****************************************************************************/

MMBroadbandModem *
mm_broadband_modem_new (const gchar *device,
                        const gchar **drivers,
                        const gchar *plugin,
                        guint16 vendor_id,
                        guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

    switch (prop_id) {
    case PROP_MODEM_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_dbus_skeleton);
        self->priv->modem_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_3GPP_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_3gpp_dbus_skeleton);
        self->priv->modem_3gpp_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_3GPP_USSD_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_3gpp_ussd_dbus_skeleton);
        self->priv->modem_3gpp_ussd_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_CDMA_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_cdma_dbus_skeleton);
        self->priv->modem_cdma_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_SIMPLE_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_simple_dbus_skeleton);
        self->priv->modem_simple_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_LOCATION_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_location_dbus_skeleton);
        self->priv->modem_location_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_MESSAGING_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_messaging_dbus_skeleton);
        self->priv->modem_messaging_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_TIME_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_time_dbus_skeleton);
        self->priv->modem_time_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_FIRMWARE_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_firmware_dbus_skeleton);
        self->priv->modem_firmware_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_SIM:
        g_clear_object (&self->priv->modem_sim);
        self->priv->modem_sim = g_value_dup_object (value);
        break;
    case PROP_MODEM_BEARER_LIST:
        g_clear_object (&self->priv->modem_bearer_list);
        self->priv->modem_bearer_list = g_value_dup_object (value);
        break;
    case PROP_MODEM_STATE:
        self->priv->modem_state = g_value_get_enum (value);
        break;
    case PROP_MODEM_3GPP_REGISTRATION_STATE:
        self->priv->modem_3gpp_registration_state = g_value_get_enum (value);
        break;
    case PROP_MODEM_3GPP_CS_NETWORK_SUPPORTED:
        self->priv->modem_3gpp_cs_network_supported = g_value_get_boolean (value);
        break;
    case PROP_MODEM_3GPP_PS_NETWORK_SUPPORTED:
        self->priv->modem_3gpp_ps_network_supported = g_value_get_boolean (value);
        break;
    case PROP_MODEM_CDMA_CDMA1X_REGISTRATION_STATE:
        self->priv->modem_cdma_cdma1x_registration_state = g_value_get_enum (value);
        break;
    case PROP_MODEM_CDMA_EVDO_REGISTRATION_STATE:
        self->priv->modem_cdma_evdo_registration_state = g_value_get_enum (value);
        break;
    case PROP_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED:
        self->priv->modem_cdma_cdma1x_network_supported = g_value_get_boolean (value);
        break;
    case PROP_MODEM_CDMA_EVDO_NETWORK_SUPPORTED:
        self->priv->modem_cdma_evdo_network_supported = g_value_get_boolean (value);
        break;
    case PROP_MODEM_MESSAGING_SMS_LIST:
        g_clear_object (&self->priv->modem_messaging_sms_list);
        self->priv->modem_messaging_sms_list = g_value_dup_object (value);
        break;
    case PROP_MODEM_MESSAGING_SMS_PDU_MODE:
        self->priv->modem_messaging_sms_pdu_mode = g_value_get_boolean (value);
        break;
    case PROP_MODEM_MESSAGING_SMS_DEFAULT_STORAGE:
        self->priv->modem_messaging_sms_default_storage = g_value_get_enum (value);
        break;
    case PROP_MODEM_SIMPLE_STATUS:
        g_clear_object (&self->priv->modem_simple_status);
        self->priv->modem_simple_status = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

    switch (prop_id) {
    case PROP_MODEM_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_dbus_skeleton);
        break;
    case PROP_MODEM_3GPP_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_3gpp_dbus_skeleton);
        break;
    case PROP_MODEM_3GPP_USSD_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_3gpp_ussd_dbus_skeleton);
        break;
    case PROP_MODEM_CDMA_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_cdma_dbus_skeleton);
        break;
    case PROP_MODEM_SIMPLE_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_simple_dbus_skeleton);
        break;
    case PROP_MODEM_LOCATION_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_location_dbus_skeleton);
        break;
    case PROP_MODEM_MESSAGING_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_messaging_dbus_skeleton);
        break;
    case PROP_MODEM_TIME_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_time_dbus_skeleton);
        break;
    case PROP_MODEM_FIRMWARE_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_firmware_dbus_skeleton);
        break;
    case PROP_MODEM_SIM:
        g_value_set_object (value, self->priv->modem_sim);
        break;
    case PROP_MODEM_BEARER_LIST:
        g_value_set_object (value, self->priv->modem_bearer_list);
        break;
    case PROP_MODEM_STATE:
        g_value_set_enum (value, self->priv->modem_state);
        break;
    case PROP_MODEM_3GPP_REGISTRATION_STATE:
        g_value_set_enum (value, self->priv->modem_3gpp_registration_state);
        break;
    case PROP_MODEM_3GPP_CS_NETWORK_SUPPORTED:
        g_value_set_boolean (value, self->priv->modem_3gpp_cs_network_supported);
        break;
    case PROP_MODEM_3GPP_PS_NETWORK_SUPPORTED:
        g_value_set_boolean (value, self->priv->modem_3gpp_ps_network_supported);
        break;
    case PROP_MODEM_CDMA_CDMA1X_REGISTRATION_STATE:
        g_value_set_enum (value, self->priv->modem_cdma_cdma1x_registration_state);
        break;
    case PROP_MODEM_CDMA_EVDO_REGISTRATION_STATE:
        g_value_set_enum (value, self->priv->modem_cdma_evdo_registration_state);
        break;
    case PROP_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED:
        g_value_set_boolean (value, self->priv->modem_cdma_cdma1x_network_supported);
        break;
    case PROP_MODEM_CDMA_EVDO_NETWORK_SUPPORTED:
        g_value_set_boolean (value, self->priv->modem_cdma_evdo_network_supported);
        break;
    case PROP_MODEM_MESSAGING_SMS_LIST:
        g_value_set_object (value, self->priv->modem_messaging_sms_list);
        break;
    case PROP_MODEM_MESSAGING_SMS_PDU_MODE:
        g_value_set_boolean (value, self->priv->modem_messaging_sms_pdu_mode);
        break;
    case PROP_MODEM_MESSAGING_SMS_DEFAULT_STORAGE:
        g_value_set_enum (value, self->priv->modem_messaging_sms_default_storage);
        break;
    case PROP_MODEM_SIMPLE_STATUS:
        g_value_set_object (value, self->priv->modem_simple_status);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_modem_init (MMBroadbandModem *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM,
                                              MMBroadbandModemPrivate);
    self->priv->modem_state = MM_MODEM_STATE_UNKNOWN;
    self->priv->modem_3gpp_registration_regex = mm_3gpp_creg_regex_get (TRUE);
    self->priv->modem_current_charset = MM_MODEM_CHARSET_UNKNOWN;
    self->priv->modem_3gpp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_3gpp_cs_network_supported = TRUE;
    self->priv->modem_3gpp_ps_network_supported = TRUE;
    self->priv->modem_cdma_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_cdma_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_cdma_cdma1x_network_supported = TRUE;
    self->priv->modem_cdma_evdo_network_supported = TRUE;
    self->priv->modem_messaging_sms_default_storage = MM_SMS_STORAGE_UNKNOWN;
    self->priv->current_sms_mem1_storage = MM_SMS_STORAGE_UNKNOWN;
    self->priv->current_sms_mem2_storage = MM_SMS_STORAGE_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

    if (self->priv->enabled_ports_ctx)
        ports_context_unref (self->priv->enabled_ports_ctx);

    if (self->priv->modem_3gpp_registration_regex)
        mm_3gpp_creg_regex_destroy (self->priv->modem_3gpp_registration_regex);

    G_OBJECT_CLASS (mm_broadband_modem_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

    if (self->priv->modem_dbus_skeleton) {
        mm_iface_modem_shutdown (MM_IFACE_MODEM (object));
        g_clear_object (&self->priv->modem_dbus_skeleton);
    }

    if (self->priv->modem_3gpp_dbus_skeleton) {
        mm_iface_modem_3gpp_shutdown (MM_IFACE_MODEM_3GPP (object));
        g_clear_object (&self->priv->modem_3gpp_dbus_skeleton);
    }

    if (self->priv->modem_3gpp_ussd_dbus_skeleton) {
        mm_iface_modem_3gpp_ussd_shutdown (MM_IFACE_MODEM_3GPP_USSD (object));
        g_clear_object (&self->priv->modem_3gpp_ussd_dbus_skeleton);
    }

    if (self->priv->modem_cdma_dbus_skeleton) {
        mm_iface_modem_cdma_shutdown (MM_IFACE_MODEM_CDMA (object));
        g_clear_object (&self->priv->modem_cdma_dbus_skeleton);
    }

    if (self->priv->modem_location_dbus_skeleton) {
        mm_iface_modem_location_shutdown (MM_IFACE_MODEM_LOCATION (object));
        g_clear_object (&self->priv->modem_location_dbus_skeleton);
    }

    if (self->priv->modem_messaging_dbus_skeleton) {
        mm_iface_modem_messaging_shutdown (MM_IFACE_MODEM_MESSAGING (object));
        g_clear_object (&self->priv->modem_messaging_dbus_skeleton);
    }

    if (self->priv->modem_time_dbus_skeleton) {
        mm_iface_modem_time_shutdown (MM_IFACE_MODEM_TIME (object));
        g_clear_object (&self->priv->modem_time_dbus_skeleton);
    }

    if (self->priv->modem_simple_dbus_skeleton) {
        mm_iface_modem_simple_shutdown (MM_IFACE_MODEM_SIMPLE (object));
        g_clear_object (&self->priv->modem_simple_dbus_skeleton);
    }

    g_clear_object (&self->priv->modem_sim);
    g_clear_object (&self->priv->modem_bearer_list);
    g_clear_object (&self->priv->modem_messaging_sms_list);
    g_clear_object (&self->priv->modem_simple_status);

    G_OBJECT_CLASS (mm_broadband_modem_parent_class)->dispose (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Initialization steps */
    iface->load_current_capabilities = modem_load_current_capabilities;
    iface->load_current_capabilities_finish = modem_load_current_capabilities_finish;
    iface->load_manufacturer = modem_load_manufacturer;
    iface->load_manufacturer_finish = modem_load_manufacturer_finish;
    iface->load_model = modem_load_model;
    iface->load_model_finish = modem_load_model_finish;
    iface->load_revision = modem_load_revision;
    iface->load_revision_finish = modem_load_revision_finish;
    iface->load_equipment_identifier = modem_load_equipment_identifier;
    iface->load_equipment_identifier_finish = modem_load_equipment_identifier_finish;
    iface->load_device_identifier = modem_load_device_identifier;
    iface->load_device_identifier_finish = modem_load_device_identifier_finish;
    iface->load_own_numbers = modem_load_own_numbers;
    iface->load_own_numbers_finish = modem_load_own_numbers_finish;
    iface->load_unlock_required = modem_load_unlock_required;
    iface->load_unlock_required_finish = modem_load_unlock_required_finish;
    iface->create_sim = modem_create_sim;
    iface->create_sim_finish = modem_create_sim_finish;
    iface->load_supported_modes = modem_load_supported_modes;
    iface->load_supported_modes_finish = modem_load_supported_modes_finish;

    /* Enabling steps */
    iface->modem_init = modem_init;
    iface->modem_init_finish = modem_init_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->setup_flow_control = modem_setup_flow_control;
    iface->setup_flow_control_finish = modem_setup_flow_control_finish;
    iface->load_supported_charsets = modem_load_supported_charsets;
    iface->load_supported_charsets_finish = modem_load_supported_charsets_finish;
    iface->setup_charset = modem_setup_charset;
    iface->setup_charset_finish = modem_setup_charset_finish;

    /* Additional actions */
    iface->load_signal_quality = modem_load_signal_quality;
    iface->load_signal_quality_finish = modem_load_signal_quality_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->command = modem_command;
    iface->command_finish = modem_command_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Initialization steps */
    iface->load_imei = modem_3gpp_load_imei;
    iface->load_imei_finish = modem_3gpp_load_imei_finish;
    iface->load_enabled_facility_locks = modem_3gpp_load_enabled_facility_locks;
    iface->load_enabled_facility_locks_finish = modem_3gpp_load_enabled_facility_locks_finish;

    /* Enabling steps */
    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
    iface->setup_unsolicited_registration_events = modem_3gpp_setup_unsolicited_registration_events;
    iface->setup_unsolicited_registration_events_finish = modem_3gpp_setup_unsolicited_registration_events_finish;
    iface->enable_unsolicited_registration_events = modem_3gpp_enable_unsolicited_registration_events;
    iface->enable_unsolicited_registration_events_finish = modem_3gpp_enable_disable_unsolicited_registration_events_finish;

    /* Disabling steps */
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->disable_unsolicited_registration_events = modem_3gpp_disable_unsolicited_registration_events;
    iface->disable_unsolicited_registration_events_finish = modem_3gpp_enable_disable_unsolicited_registration_events_finish;
    iface->cleanup_unsolicited_registration_events = modem_3gpp_cleanup_unsolicited_registration_events;
    iface->cleanup_unsolicited_registration_events_finish = modem_3gpp_cleanup_unsolicited_registration_events_finish;

    /* Additional actions */
    iface->load_operator_code = modem_3gpp_load_operator_code;
    iface->load_operator_code_finish = modem_3gpp_load_operator_code_finish;
    iface->load_operator_name = modem_3gpp_load_operator_name;
    iface->load_operator_name_finish = modem_3gpp_load_operator_name_finish;
    iface->run_registration_checks = modem_3gpp_run_registration_checks;
    iface->run_registration_checks_finish = modem_3gpp_run_registration_checks_finish;
    iface->register_in_network = modem_3gpp_register_in_network;
    iface->register_in_network_finish = modem_3gpp_register_in_network_finish;
    iface->scan_networks = modem_3gpp_scan_networks;
    iface->scan_networks_finish = modem_3gpp_scan_networks_finish;
}

static void
iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface)
{
    /* Initialization steps */
    iface->check_support = modem_3gpp_ussd_check_support;
    iface->check_support_finish = modem_3gpp_ussd_check_support_finish;

    /* Enabling steps */
    iface->setup_unsolicited_result_codes = modem_3gpp_ussd_setup_unsolicited_result_codes;
    iface->setup_unsolicited_result_codes_finish = modem_3gpp_ussd_setup_cleanup_unsolicited_result_codes_finish;
    iface->enable_unsolicited_result_codes = modem_3gpp_ussd_enable_unsolicited_result_codes;
    iface->enable_unsolicited_result_codes_finish = modem_3gpp_ussd_enable_disable_unsolicited_result_codes_finish;

    /* Disabling steps */
    iface->cleanup_unsolicited_result_codes_finish = modem_3gpp_ussd_setup_cleanup_unsolicited_result_codes_finish;
    iface->cleanup_unsolicited_result_codes = modem_3gpp_ussd_cleanup_unsolicited_result_codes;
    iface->disable_unsolicited_result_codes = modem_3gpp_ussd_disable_unsolicited_result_codes;
    iface->disable_unsolicited_result_codes_finish = modem_3gpp_ussd_enable_disable_unsolicited_result_codes_finish;

    /* Additional actions */
    iface->encode = modem_3gpp_ussd_encode;
    iface->decode = modem_3gpp_ussd_decode;
    iface->send = modem_3gpp_ussd_send;
    iface->send_finish = modem_3gpp_ussd_send_finish;
    iface->cancel = modem_3gpp_ussd_cancel;
    iface->cancel_finish = modem_3gpp_ussd_cancel_finish;
}

static void
iface_modem_cdma_init (MMIfaceModemCdma *iface)
{
    /* Initialization steps */
    iface->load_esn = modem_cdma_load_esn;
    iface->load_esn_finish = modem_cdma_load_esn_finish;

    /* Registration check steps */
    iface->setup_registration_checks = modem_cdma_setup_registration_checks;
    iface->setup_registration_checks_finish = modem_cdma_setup_registration_checks_finish;
    iface->get_call_manager_state = modem_cdma_get_call_manager_state;
    iface->get_call_manager_state_finish = modem_cdma_get_call_manager_state_finish;
    iface->get_hdr_state = modem_cdma_get_hdr_state;
    iface->get_hdr_state_finish = modem_cdma_get_hdr_state_finish;
    iface->get_service_status = modem_cdma_get_service_status;
    iface->get_service_status_finish = modem_cdma_get_service_status_finish;
    iface->get_cdma1x_serving_system = modem_cdma_get_cdma1x_serving_system;
    iface->get_cdma1x_serving_system_finish = modem_cdma_get_cdma1x_serving_system_finish;
    iface->get_detailed_registration_state = modem_cdma_get_detailed_registration_state;
    iface->get_detailed_registration_state_finish = modem_cdma_get_detailed_registration_state_finish;

    /* Additional actions */
    iface->register_in_network = modem_cdma_register_in_network;
    iface->register_in_network_finish = modem_cdma_register_in_network_finish;
}

static void
iface_modem_simple_init (MMIfaceModemSimple *iface)
{
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface->load_capabilities = modem_location_load_capabilities;
    iface->load_capabilities_finish = modem_location_load_capabilities_finish;
    iface->enable_location_gathering = enable_location_gathering;
    iface->enable_location_gathering_finish = enable_location_gathering_finish;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->check_support = modem_messaging_check_support;
    iface->check_support_finish = modem_messaging_check_support_finish;
    iface->load_supported_storages = modem_messaging_load_supported_storages;
    iface->load_supported_storages_finish = modem_messaging_load_supported_storages_finish;
    iface->set_default_storage = modem_messaging_set_default_storage;
    iface->set_default_storage_finish = modem_messaging_set_default_storage_finish;
    iface->setup_sms_format = modem_messaging_setup_sms_format;
    iface->setup_sms_format_finish = modem_messaging_setup_sms_format_finish;
    iface->load_initial_sms_parts = modem_messaging_load_initial_sms_parts;
    iface->load_initial_sms_parts_finish = modem_messaging_load_initial_sms_parts_finish;
    iface->setup_unsolicited_events = modem_messaging_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_messaging_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_messaging_enable_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_messaging_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_messaging_setup_cleanup_unsolicited_events_finish;
    iface->create_sms = modem_messaging_create_sms;
}

static void
iface_modem_time_init (MMIfaceModemTime *iface)
{
}

static void
iface_modem_firmware_init (MMIfaceModemFirmware *iface)
{
}

static void
mm_broadband_modem_class_init (MMBroadbandModemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseModemClass *base_modem_class = MM_BASE_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;
    object_class->finalize = finalize;

    base_modem_class->initialize = initialize;
    base_modem_class->initialize_finish = initialize_finish;
    base_modem_class->enable = enable;
    base_modem_class->enable_finish = enable_finish;
    base_modem_class->disable = disable;
    base_modem_class->disable_finish = disable_finish;

    klass->setup_ports = setup_ports;
    klass->initialization_started = initialization_started;
    klass->initialization_started_finish = initialization_started_finish;
    klass->initialization_stopped = initialization_stopped;
    klass->enabling_started = enabling_started;
    klass->enabling_started_finish = enabling_started_finish;
    klass->disabling_stopped = disabling_stopped;

    g_object_class_override_property (object_class,
                                      PROP_MODEM_DBUS_SKELETON,
                                      MM_IFACE_MODEM_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_3GPP_DBUS_SKELETON,
                                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_3GPP_USSD_DBUS_SKELETON,
                                      MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CDMA_DBUS_SKELETON,
                                      MM_IFACE_MODEM_CDMA_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_SIMPLE_DBUS_SKELETON,
                                      MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_LOCATION_DBUS_SKELETON,
                                      MM_IFACE_MODEM_LOCATION_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_MESSAGING_DBUS_SKELETON,
                                      MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_TIME_DBUS_SKELETON,
                                      MM_IFACE_MODEM_TIME_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_FIRMWARE_DBUS_SKELETON,
                                      MM_IFACE_MODEM_FIRMWARE_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_SIM,
                                      MM_IFACE_MODEM_SIM);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_BEARER_LIST,
                                      MM_IFACE_MODEM_BEARER_LIST);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_STATE,
                                      MM_IFACE_MODEM_STATE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_3GPP_REGISTRATION_STATE,
                                      MM_IFACE_MODEM_3GPP_REGISTRATION_STATE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_3GPP_CS_NETWORK_SUPPORTED,
                                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_3GPP_PS_NETWORK_SUPPORTED,
                                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CDMA_CDMA1X_REGISTRATION_STATE,
                                      MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CDMA_EVDO_REGISTRATION_STATE,
                                      MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED,
                                      MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CDMA_EVDO_NETWORK_SUPPORTED,
                                      MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_MESSAGING_SMS_LIST,
                                      MM_IFACE_MODEM_MESSAGING_SMS_LIST);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_MESSAGING_SMS_PDU_MODE,
                                      MM_IFACE_MODEM_MESSAGING_SMS_PDU_MODE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_MESSAGING_SMS_DEFAULT_STORAGE,
                                      MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_SIMPLE_STATUS,
                                      MM_IFACE_MODEM_SIMPLE_STATUS);
}
