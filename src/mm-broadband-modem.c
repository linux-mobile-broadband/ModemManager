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
 * Copyright (C) 2011 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-simple.h"
#include "mm-bearer-3gpp.h"
#include "mm-bearer-cdma.h"
#include "mm-bearer-list.h"
#include "mm-sim.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-qcdm-serial-port.h"
#include "libqcdm/src/errors.h"
#include "libqcdm/src/commands.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_simple_init (MMIfaceModemSimple *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModem, mm_broadband_modem, MM_TYPE_BASE_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIMPLE, iface_modem_simple_init));

enum {
    PROP_0,
    PROP_MODEM_DBUS_SKELETON,
    PROP_MODEM_3GPP_DBUS_SKELETON,
    PROP_MODEM_CDMA_DBUS_SKELETON,
    PROP_MODEM_SIMPLE_DBUS_SKELETON,
    PROP_MODEM_SIM,
    PROP_MODEM_BEARER_LIST,
    PROP_MODEM_STATE,
    PROP_MODEM_CURRENT_CAPABILITIES,
    PROP_MODEM_3GPP_REGISTRATION_STATE,
    PROP_MODEM_3GPP_CS_NETWORK_SUPPORTED,
    PROP_MODEM_3GPP_PS_NETWORK_SUPPORTED,
    PROP_MODEM_CDMA_CDMA1X_REGISTRATION_STATE,
    PROP_MODEM_CDMA_EVDO_REGISTRATION_STATE,
    PROP_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED,
    PROP_MODEM_CDMA_EVDO_NETWORK_SUPPORTED,
    PROP_MODEM_SIMPLE_STATUS,
    PROP_LAST
};

/* When CIND is supported, invalid indicators are marked with this value */
#define CIND_INDICATOR_INVALID 255
#define CIND_INDICATOR_IS_VALID(u) (u != CIND_INDICATOR_INVALID)

struct _MMBroadbandModemPrivate {
    /*<--- Modem interface --->*/
    /* Properties */
    GObject *modem_dbus_skeleton;
    MMSim *modem_sim;
    MMBearerList *modem_bearer_list;
    MMModemState modem_state;
    MMModemCapability modem_current_capabilities;
    /* Implementation helpers */
    MMModemCharset modem_current_charset;
    gboolean modem_cind_supported;
    guint modem_cind_indicator_signal_quality;
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
    gboolean modem_3gpp_manual_registration;
    GCancellable *modem_3gpp_pending_registration_cancellable;

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
    MMCommonSimpleProperties *modem_simple_status;
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
modem_cdma_create_bearer_ready (MMIfaceModemCdma *self,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_iface_modem_cdma_create_bearer_finish (self, res, &error);
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
modem_3gpp_create_bearer_ready (MMIfaceModem3gpp *self,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_iface_modem_3gpp_create_bearer_finish (self, res, &error);
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
                     MMCommonBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;


    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    /* On 3GPP-only modems, new 3GPP bearer */
    if (mm_iface_modem_is_3gpp_only (self)) {
        mm_dbg ("Creating 3GPP Bearer in 3GPP-only modem");
        mm_iface_modem_3gpp_create_bearer (MM_IFACE_MODEM_3GPP (self),
                                           properties,
                                           (GAsyncReadyCallback)modem_3gpp_create_bearer_ready,
                                           result);
        return;
    }

    /* On CDMA-only modems, new CDMA bearer */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_dbg ("Creating CDMA Bearer in CDMA-only modem");
        mm_iface_modem_cdma_create_bearer (MM_IFACE_MODEM_CDMA (self),
                                           properties,
                                           (GAsyncReadyCallback)modem_cdma_create_bearer_ready,
                                           result);
        return;
    }

    /* On mixed LTE and CDMA modems, we'll default to building a 3GPP bearer.
     * Plugins supporting mixed LTE+CDMA modems can override this and provide
     * their own specific and detailed logic. */
    if (mm_iface_modem_is_cdma (self) &&
        mm_iface_modem_is_3gpp_lte (self)) {
        mm_dbg ("Creating 3GPP Bearer in mixed CDMA+LTE modem");
        mm_iface_modem_3gpp_create_bearer (MM_IFACE_MODEM_3GPP (self),
                                           properties,
                                           (GAsyncReadyCallback)modem_3gpp_create_bearer_ready,
                                           result);
        return;
    }

    g_simple_async_result_set_error (
        result,
        MM_CORE_ERROR,
        MM_CORE_ERROR_UNSUPPORTED,
        "Cannot create bearer in modem of unknown type. "
        "CDMA: %s, 3GPP: %s (LTE: %s)",
        mm_iface_modem_is_cdma (self) ? "yes" : "no",
        mm_iface_modem_is_3gpp (self) ? "yes" : "no",
        mm_iface_modem_is_3gpp_lte (self) ? "yes" : "no");
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
modem_create_sim_ready (GAsyncInitable *initable,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    MMSim *sim;
    GError *error = NULL;

    sim = mm_sim_new_finish (initable, res, &error);
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
	gchar *name;
	MMModemCapability bits;
} ModemCaps;

static const ModemCaps modem_caps[] = {
	{ "+CGSM",     MM_MODEM_CAPABILITY_GSM_UMTS  },
	{ "+CLTE2",    MM_MODEM_CAPABILITY_LTE       }, /* Novatel */
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

static MMModemCapability
modem_load_current_capabilities_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    GVariant *result;
    MMModemCapability caps;
    gchar *caps_str;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result)
        return MM_MODEM_CAPABILITY_NONE;

    caps = (MMModemCapability)g_variant_get_uint32 (result);
    caps_str = mm_common_get_capabilities_string (caps);
    mm_dbg ("loaded current capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static const MMBaseModemAtCommand capabilities[] = {
    { "+GCAP",  2, TRUE,  parse_caps_gcap },
    { "I",      1, TRUE,  parse_caps_gcap }, /* yes, really parse as +GCAP */
    { "+CPIN?", 1, FALSE, parse_caps_cpin },
    { "+CGMM",  1, TRUE,  parse_caps_cgmm },
    { NULL }
};

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    mm_dbg ("loading current capabilities...");

    /* Launch sequence, we will expect a "u" GVariant */
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        capabilities,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        callback,
        user_data);
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

    manufacturer = g_variant_dup_string (result, NULL);
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
        NULL, /* cancellable */
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
    gchar *model;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result)
        return NULL;

    model = g_variant_dup_string (result, NULL);
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
        NULL, /* cancellable */
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

    revision = g_variant_dup_string (result, NULL);
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
        NULL, /* cancellable */
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
        NULL, /* cancellable */
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
    device_identifier = mm_create_device_identifier (
        mm_base_modem_get_vendor_id (MM_BASE_MODEM (self)),
        mm_base_modem_get_product_id (MM_BASE_MODEM (self)),
        ((DeviceIdentifierContext *)ctx)->ati,
        ((DeviceIdentifierContext *)ctx)->ati1,
        mm_gdbus_modem_get_equipment_identifier (
            MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton)),
        mm_gdbus_modem_get_revision (
            MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton)),
        mm_gdbus_modem_get_model (
            MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton)),
        mm_gdbus_modem_get_manufacturer (
            MM_GDBUS_MODEM (MM_BROADBAND_MODEM (self)->priv->modem_dbus_skeleton)));

    mm_dbg ("loaded device identifier: %s", device_identifier);
    return device_identifier;
}

static gboolean
parse_ati_reply (MMBaseModem *self,
                 DeviceIdentifierContext *ctx,
                 const gchar *command,
                 const gchar *response,
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
        NULL, /* cancellable */
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
        strstr (result, "+CPIN: ")) {
        CPinResult *iter = &unlock_results[0];
        const gchar *str;

        str = strstr (result, "+CPIN: ") + 7;

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
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)cpin_query_ready,
                              result);
}

/*****************************************************************************/
/* Supported modes loading (Modem interface) */

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

static void
modem_load_supported_modes (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    MMBroadbandModem *broadband = MM_BROADBAND_MODEM (self);
    GSimpleAsyncResult *result;
    MMModemMode mode;

    mm_dbg ("loading supported modes...");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_supported_modes);

    mode = MM_MODEM_MODE_NONE;

    /* If the modem has +GSM caps, assume it does CS, 2G and 3G */
    if (broadband->priv->modem_current_capabilities & MM_MODEM_CAPABILITY_GSM_UMTS) {
        mode |= (MM_MODEM_MODE_CS | MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    }
    /* If the modem has CDMA caps, assume it does 3G */
    else if (broadband->priv->modem_current_capabilities & MM_MODEM_CAPABILITY_CDMA_EVDO) {
        mode |= MM_MODEM_MODE_3G;
    }

    /* If the modem has LTE caps, it does 4G */
    if (broadband->priv->modem_current_capabilities & MM_MODEM_CAPABILITY_LTE ||
        broadband->priv->modem_current_capabilities & MM_MODEM_CAPABILITY_LTE_ADVANCED) {
        mode |= MM_MODEM_MODE_4G;
    }

    g_simple_async_result_set_op_res_gpointer (result,
                                               GUINT_TO_POINTER (mode),
                                               NULL);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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

    result = mm_base_modem_at_sequence_in_port_finish (MM_BASE_MODEM (self), res, NULL, &error);
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
    mm_base_modem_at_sequence_in_port (
        MM_BASE_MODEM (ctx->self),
        MM_AT_SERIAL_PORT (ctx->port),
        signal_quality_csq_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)signal_quality_csq_ready,
        ctx);
}

static void
signal_quality_cind_ready (MMBroadbandModem *self,
                           GAsyncResult *res,
                           SignalQualityContext *ctx)
{
    GError *error = NULL;
    const gchar *result;
    GByteArray *indicators;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!error)
        indicators = mm_parse_cind_query_response (result, &error);

    if (error)
        g_simple_async_result_take_error (ctx->result, error);
    else if (indicators->len >= self->priv->modem_cind_indicator_signal_quality) {
        guint quality;

        quality = g_array_index (indicators,
                                 guint8,
                                 self->priv->modem_cind_indicator_signal_quality);
        quality = CLAMP (quality, 0, 5) * 20;
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   GUINT_TO_POINTER (quality),
                                                   NULL);

        g_byte_array_free (indicators, TRUE);
    } else
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Could not parse CIND signal quality results "
                                         "signal index (%u) outside received range (0-%u)",
                                         self->priv->modem_cind_indicator_signal_quality,
                                         indicators->len);

    signal_quality_context_complete_and_free (ctx);
}

static void
signal_quality_cind (SignalQualityContext *ctx)
{
    mm_base_modem_at_command_in_port (MM_BASE_MODEM (ctx->self),
                                      MM_AT_SERIAL_PORT (ctx->port),
                                      "+CIND?",
                                      3,
                                      FALSE,
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
                                       (MMQcdmSerialResponseFn)signal_quality_qcdm_ready,
                                       ctx);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    MMSerialPort *port;
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
    port = (MMSerialPort *)mm_base_modem_get_best_at_port (MM_BASE_MODEM (self), &error);
    if (port) {
        ctx->port = g_object_ref (port);
        if (MM_BROADBAND_MODEM (self)->priv->modem_cind_supported)
            signal_quality_cind (ctx);
        else
            signal_quality_csq (ctx);
        return;
    }

    /* If no best AT port available (all connected), try with QCDM ports */
    port = (MMSerialPort *)mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));
    if (port) {
        g_error_free (error);
        ctx->port = g_object_ref (port);
        signal_quality_qcdm (ctx);
        return;
    }

    /* Return the error we got when getting best AT port */
    g_simple_async_result_take_error (ctx->result, error);
    signal_quality_context_complete_and_free (ctx);
}

/*****************************************************************************/
/* Setting up indicators (3GPP interface) */

static gboolean
modem_3gpp_setup_indicators_finish (MMIfaceModem3gpp *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cind_format_check_ready (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GHashTable *indicators = NULL;
    GError *error = NULL;
    const gchar *result;
    CindResponse *r;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error ||
        !(indicators = mm_parse_cind_test_response (result, &error))) {
        /* quit with error */
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Mark CIND as being supported and find the proper indexes for the
     * indicators. */
    self->priv->modem_cind_supported = TRUE;

#define FIND_INDEX(indicator_str,var_suffix) do {                       \
        r = g_hash_table_lookup (indicators, indicator_str);            \
        self->priv->modem_cind_indicator_##var_suffix = (r ?            \
                                                   cind_response_get_index (r) : \
                                                   CIND_INDICATOR_INVALID); \
    } while (0)

    FIND_INDEX ("signal", signal_quality);
    FIND_INDEX ("roam", roaming);
    FIND_INDEX ("service", service);

#undef FIND_INDEX

    g_hash_table_destroy (indicators);

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_setup_indicators (MMIfaceModem3gpp *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_setup_indicators);

    /* Load supported indicators */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CIND=?",
                              3,
                              TRUE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)cind_format_check_ready,
                              result);
}

/*****************************************************************************/
/* Enabling/disabling unsolicited events (3GPP interface) */

static void
ciev_received (MMAtSerialPort *port,
               GMatchInfo *info,
               MMBroadbandModem *self)
{
    gint ind = 0;
    gchar *str;

    str = g_match_info_fetch (info, 1);
    if (str) {
        ind = atoi (str);
        g_free (str);
    }

    /* Handle signal quality change indication */
    if (ind == self->priv->modem_cind_indicator_signal_quality) {
        str = g_match_info_fetch (info, 2);
        if (str) {
            gint quality = 0;

            quality = atoi (str);
            if (quality > 0)
                mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self),
                                                      (guint) (quality * 20));
            g_free (str);
        }
    }

    /* FIXME: handle roaming and service indicators.
     * ... wait, arent these already handle by unsolicited CREG responses? */
}

typedef struct {
    MMBroadbandModem *self;
    gchar *command;
    gboolean enable;
    GSimpleAsyncResult *result;
    gboolean cmer_primary_done;
    gboolean cmer_secondary_done;
    GRegex *ciev_regex;
} UnsolicitedEventsContext;

static void
unsolicited_events_context_complete_and_free (UnsolicitedEventsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_regex_unref (ctx->ciev_regex);
    g_free (ctx->command);
    g_free (ctx);
}

static gboolean
modem_3gpp_unsolicited_events_finish (MMIfaceModem3gpp *self,
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

    if (!ctx->ciev_regex)
        ctx->ciev_regex = mm_3gpp_ciev_regex_get ();

    if (!ctx->cmer_primary_done) {
        ctx->cmer_primary_done = TRUE;
        port = mm_base_modem_get_port_primary (MM_BASE_MODEM (ctx->self));
    } else if (!ctx->cmer_secondary_done) {
        ctx->cmer_secondary_done = TRUE;
        port = mm_base_modem_get_port_secondary (MM_BASE_MODEM (ctx->self));
    }

    /* Enable unsolicited events in given port */
    if (port) {
        if (ctx->enable)
            /* When enabling, setup unsolicited CIEV event handler */
            mm_at_serial_port_add_unsolicited_msg_handler (
                port,
                ctx->ciev_regex,
                (MMAtSerialUnsolicitedMsgFn) ciev_received,
                ctx->self,
                NULL);
        else
            /* When disabling, remove unsolicited CIEV event handler */
            mm_at_serial_port_add_unsolicited_msg_handler (
                port,
                ctx->ciev_regex,
                NULL,
                NULL,
                NULL);

        mm_base_modem_at_command_in_port (MM_BASE_MODEM (ctx->self),
                                          port,
                                          ctx->command,
                                          3,
                                          FALSE,
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
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    UnsolicitedEventsContext *ctx;

    ctx = g_new0 (UnsolicitedEventsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->enable = TRUE;
    ctx->command = g_strdup ("+CMER=3,0,0,1");
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_enable_unsolicited_events);

    run_unsolicited_events_setup (ctx);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    UnsolicitedEventsContext *ctx;

    ctx = g_new0 (UnsolicitedEventsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->command = g_strdup ("+CMER=0");
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_disable_unsolicited_events);

    run_unsolicited_events_setup (ctx);
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
                              NULL, /* cancellable */
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
        NULL, /* cancellable */
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
    else if (!mm_gsm_parse_cscs_support_response (response, &charsets))
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
                              NULL,  /* cancellable */
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
    mm_base_modem_at_command_ignore_reply (MM_BASE_MODEM (self),
                                           "+IFC=1,1",
                                           3);

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
        mm_base_modem_at_command_ignore_reply (MM_BASE_MODEM (self),
                                               "+CFUN=1",
                                               5);

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_up);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Initializing the modem (Modem interface) */

static gboolean
modem_init_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
}

static const MMBaseModemAtCommand modem_init_sequence[] = {
    /* Init command */
    { "Z E0 V1", 3, FALSE, mm_base_modem_response_processor_no_result_continue },

    /* Ensure echo is off after the init command; some modems ignore the
     * E0 when it's in the same line as ATZ (Option GIO322).
     */
    { "E0",      3, FALSE, NULL },

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
    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               modem_init_sequence,
                               NULL,  /* response_processor_context */
                               NULL,  /* response_processor_context_free */
                               NULL,  /* cancellable */
                               callback,
                               user_data);
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
                              NULL, /* cancellable */
                              callback,
                              user_data);
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
                              NULL, /* cancellable */
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
    mm_dbg ("loading Operator Name...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+COPS=3,0;+COPS?",
                              3,
                              FALSE,
                              NULL, /* cancellable */
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Unsolicited registration messages handling (3GPP interface) */

static gboolean
modem_3gpp_setup_unsolicited_registration_finish (MMIfaceModem3gpp *self,
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
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          state,
                                                          act);
    else
        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          state,
                                                          act);

    /* TODO: report LAC/CI location */
    /* update_lac_ci (self, lac, cell_id, cgreg ? 1 : 0); */
}

static void
modem_3gpp_setup_unsolicited_registration (MMIfaceModem3gpp *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *ports[2];
    GPtrArray *array;
    guint i;

    mm_dbg ("setting up unsolicited registration messages handling");

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_setup_unsolicited_registration);

    ports[0] = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));

    /* Set up CREG unsolicited message handlers in both ports */
    array = mm_3gpp_creg_regex_get (FALSE);
    for (i = 0; i < 2; i++) {
        if (ports[i]) {
            guint j;

            for (j = 0; j < array->len; j++) {
                mm_at_serial_port_add_unsolicited_msg_handler (
                    MM_AT_SERIAL_PORT (ports[i]),
                    (GRegex *) g_ptr_array_index (array, j),
                    (MMAtSerialUnsolicitedMsgFn)registration_state_changed,
                    self,
                    NULL);
            }
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
modem_3gpp_cleanup_unsolicited_registration_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
modem_3gpp_cleanup_unsolicited_registration (MMIfaceModem3gpp *self,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *ports[2];
    GPtrArray *array;
    guint i;

    mm_dbg ("cleaning up unsolicited registration messages handling");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_cleanup_unsolicited_registration);

    ports[0] = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));

    /* Set up CREG unsolicited message handlers in both ports */
    array = mm_3gpp_creg_regex_get (FALSE);
    for (i = 0; i < 2; i++) {
        if (ports[i]) {
            guint j;

            for (j = 0; j < array->len; j++) {
                mm_at_serial_port_add_unsolicited_msg_handler (
                    MM_AT_SERIAL_PORT (ports[i]),
                    (GRegex *) g_ptr_array_index (array, j),
                    NULL,
                    NULL,
                    NULL);
            }
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

    return mm_3gpp_parse_scan_response (result, error);
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
                              NULL, /* cancellable */
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    GTimer *timer;
    guint max_registration_time;
} RegisterIn3gppNetworkContext;

static void
register_in_3gpp_network_context_complete_and_free (RegisterIn3gppNetworkContext *ctx)
{
    /* If our cancellable reference is still around, clear it */
    if (ctx->self->priv->modem_3gpp_pending_registration_cancellable ==
        ctx->cancellable) {
        g_clear_object (&ctx->self->priv->modem_3gpp_pending_registration_cancellable);
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
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef REG_IS_IDLE
#define REG_IS_IDLE(state)                                  \
    (state != MM_MODEM_3GPP_REGISTRATION_STATE_HOME &&      \
     state != MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING && \
     state != MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)

#undef REG_IS_DONE
#define REG_IS_DONE(state)                                  \
    (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||      \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING ||   \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED)

static void run_all_3gpp_registration_checks_ready (MMBroadbandModem *self,
                                                    GAsyncResult *res,
                                                    RegisterIn3gppNetworkContext *ctx);

static gboolean
run_all_3gpp_registration_checks_again (RegisterIn3gppNetworkContext *ctx)
{
    /* Get fresh registration state */
    mm_iface_modem_3gpp_run_all_registration_checks (
        MM_IFACE_MODEM_3GPP (ctx->self),
        (GAsyncReadyCallback)run_all_3gpp_registration_checks_ready,
        ctx);
    return FALSE;
}

static void
run_all_3gpp_registration_checks_ready (MMBroadbandModem *self,
                                        GAsyncResult *res,
                                        RegisterIn3gppNetworkContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (MM_IFACE_MODEM_3GPP (self),
                                                            res,
                                                            &error);

    if (error) {
        mm_dbg ("3GPP registration check failed: '%s'", error->message);
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                          MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                          MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_simple_async_result_take_error (ctx->result, error);
        register_in_3gpp_network_context_complete_and_free (ctx);
        return;
    }

    /* If we got registered, end registration checks */
    if (REG_IS_DONE (self->priv->modem_3gpp_registration_state)) {
        mm_dbg ("Modem is currently registered in a 3GPP network");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        register_in_3gpp_network_context_complete_and_free (ctx);
        return;
    }

    /* Don't spend too much time waiting to get registered */
    if (g_timer_elapsed (ctx->timer, NULL) > ctx->max_registration_time) {
        mm_dbg ("3GPP registration check timed out");
        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                          MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                          MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_simple_async_result_take_error (
            ctx->result,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT));
        register_in_3gpp_network_context_complete_and_free (ctx);
        return;
    }

    /* If we're still waiting for automatic registration to complete or
     * fail, check again in a few seconds.
     *
     * This 3s timeout will catch results from automatic registrations as
     * well.
     */
    mm_dbg ("Modem not yet registered in a 3GPP network... will recheck soon");
    g_timeout_add_seconds (3,
                           (GSourceFunc)run_all_3gpp_registration_checks_again,
                           ctx);
}

static void
register_in_3gpp_network_ready (MMBroadbandModem *self,
                                GAsyncResult *res,
                                RegisterIn3gppNetworkContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);

    if (error) {
        /* Propagate error in COPS, if any */
        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                          MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                          MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                          MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_simple_async_result_take_error (ctx->result, error);
        register_in_3gpp_network_context_complete_and_free (ctx);
        return;
    }

    /* Get fresh registration state */
    ctx->timer = g_timer_new ();
    mm_iface_modem_3gpp_run_all_registration_checks (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)run_all_3gpp_registration_checks_ready,
        ctx);
}

static void
modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                const gchar *operator_id,
                                guint max_registration_time,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MMBroadbandModem *broadband = MM_BROADBAND_MODEM (self);
    RegisterIn3gppNetworkContext *ctx;
    gchar *command = NULL;

    /* (Try to) cancel previous registration request */
    if (broadband->priv->modem_3gpp_pending_registration_cancellable) {
        g_cancellable_cancel (broadband->priv->modem_3gpp_pending_registration_cancellable);
        g_clear_object (&broadband->priv->modem_3gpp_pending_registration_cancellable);
    }

    ctx = g_new0 (RegisterIn3gppNetworkContext, 1);
    ctx->self = g_object_ref (self);
    ctx->max_registration_time = max_registration_time;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_register_in_network);
    ctx->cancellable = g_cancellable_new ();

    /* Keep an accessible reference to the cancellable, so that we can cancel
     * previous request when needed */
    broadband->priv->modem_3gpp_pending_registration_cancellable =
        g_object_ref (ctx->cancellable);

    /* If the user sent a specific network to use, lock it in. */
    if (operator_id && operator_id[0]) {
        command = g_strdup_printf ("+COPS=1,2,\"%s\"", operator_id);
        broadband->priv->modem_3gpp_manual_registration = TRUE;
    }
    /* If no specific network was given, and the modem is not registered and not
     * searching, kick it to search for a network. Also do auto registration if
     * the modem had been set to manual registration last time but now is not.
     */
    else if (REG_IS_IDLE (broadband->priv->modem_3gpp_registration_state) ||
             broadband->priv->modem_3gpp_manual_registration) {
        /* Note that '+COPS=0,,' (same but with commas) won't work in some Nokia
         * phones */
        command = g_strdup ("+COPS=0");
        broadband->priv->modem_3gpp_manual_registration = FALSE;
    }

    if (command) {
        /* Don't setup an additional timeout to handle registration timeouts. We
         * already do this with the 120s timeout in the AT command: if that times
         * out, we can consider the registration itself timed out. */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  command,
                                  120,
                                  FALSE,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)register_in_3gpp_network_ready,
                                  ctx);
        g_free (command);
        return;
    }

    /* Just rely on the unsolicited registration and periodic registration checks */
    mm_dbg ("Not launching any new network selection request");

    /* Get fresh registration state */
    ctx->timer = g_timer_new ();
    mm_iface_modem_3gpp_run_all_registration_checks (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)run_all_3gpp_registration_checks_ready,
        ctx);
}

/*****************************************************************************/
/* CS and PS Registration checks (3GPP interface) */

static gboolean
modem_3gpp_run_cs_registration_check_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
modem_3gpp_run_ps_registration_check_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
registration_status_check_ready (MMBroadbandModem *self,
                                 GAsyncResult *res,
                                 GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_assert (error != NULL);
        g_simple_async_result_take_error (operation_result, error);
    }
    /* Unsolicited registration status handlers will usually process the
     * response for us, but just in case they don't, do that here.
     */
    else if (!response[0])
        /* Done */
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    else {
        GMatchInfo *match_info;
        guint i;

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
            g_simple_async_result_set_error (operation_result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Unknown registration status response: '%s'",
                                             response);
        } else {
            GError *inner_error = NULL;
            gboolean parsed;
            gboolean cgreg = FALSE;
            MMModem3gppRegistrationState state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
            MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
            gulong lac = 0;
            gulong cid = 0;

            parsed = mm_3gpp_parse_creg_response (match_info,
                                                  &state,
                                                  &lac,
                                                  &cid,
                                                  &act,
                                                  &cgreg,
                                                  &inner_error);
            g_match_info_free (match_info);

            if (!parsed) {
                if (inner_error)
                    g_simple_async_result_take_error (operation_result, inner_error);
                else
                    g_simple_async_result_set_error (operation_result,
                                                     MM_CORE_ERROR,
                                                     MM_CORE_ERROR_FAILED,
                                                     "Error parsing registration response: '%s'",
                                                     response);
            } else {
                /* Report new registration state */
                if (cgreg)
                    mm_iface_modem_3gpp_update_ps_registration_state (
                        MM_IFACE_MODEM_3GPP (self),
                        state,
                        act);
                else
                    mm_iface_modem_3gpp_update_cs_registration_state (
                        MM_IFACE_MODEM_3GPP (self),
                        state,
                        act);

                /* TODO: report LAC/CI location */

                g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
            }
        }
    }

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
modem_3gpp_run_cs_registration_check (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_run_cs_registration_check);

    /* Check current CS-registration state. */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CREG?",
                              10,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)registration_status_check_ready,
                              result);
}

static void
modem_3gpp_run_ps_registration_check (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_run_ps_registration_check);

    /* Check current PS-registration state. */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGREG?",
                              10,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)registration_status_check_ready,
                              result);
}

/*****************************************************************************/
/* CS and PS Registrations cleanup (3GPP interface) */

typedef struct {
    GSimpleAsyncResult *result;
    gchar *command;
    gboolean secondary_done;
} CleanupRegistrationContext;

static void
cleanup_registration_context_free (CleanupRegistrationContext *ctx)
{
    g_object_unref (ctx->result);
    g_free (ctx->command);
    g_free (ctx);
}

static gboolean
modem_3gpp_cleanup_cs_registration_finish (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
modem_3gpp_cleanup_ps_registration_finish (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cleanup_registration_sequence_ready (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     CleanupRegistrationContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete (ctx->result);
        cleanup_registration_context_free (ctx);
        return;
    }

    if (!ctx->secondary_done) {
        MMAtSerialPort *secondary;

        secondary = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));
        if (secondary) {
            /* Now use the same registration setup in secondary port, if any */
            ctx->secondary_done = TRUE;
            mm_base_modem_at_command_in_port (
                MM_BASE_MODEM (self),
                secondary,
                ctx->command,
                10,
                FALSE,
                NULL, /* cancellable */
                (GAsyncReadyCallback)cleanup_registration_sequence_ready,
                ctx);
            return;
        }
    }

    /* Update registration state(s) */
    if (g_str_has_prefix (ctx->command, "+CREG"))
        mm_iface_modem_3gpp_update_cs_registration_state (
            MM_IFACE_MODEM_3GPP (self),
            MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
            MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
    else
        mm_iface_modem_3gpp_update_ps_registration_state (
            MM_IFACE_MODEM_3GPP (self),
            MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
            MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    /* We're done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    g_simple_async_result_complete (ctx->result);
    cleanup_registration_context_free (ctx);
}

static void
modem_3gpp_cleanup_cs_registration (MMIfaceModem3gpp *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    CleanupRegistrationContext *ctx;

    ctx = g_new0 (CleanupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_cleanup_cs_registration);
    ctx->command = g_strdup ("+CREG=0");

    mm_base_modem_at_command_in_port (
        MM_BASE_MODEM (self),
        mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
        ctx->command,
        10,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)cleanup_registration_sequence_ready,
        ctx);
}

static void
modem_3gpp_cleanup_ps_registration (MMIfaceModem3gpp *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    CleanupRegistrationContext *ctx;

    ctx = g_new0 (CleanupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_cleanup_cs_registration);
    ctx->command = g_strdup ("+CGREG=0");

    mm_base_modem_at_command_in_port (
        MM_BASE_MODEM (self),
        mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
        ctx->command,
        10,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)cleanup_registration_sequence_ready,
        ctx);
}

/*****************************************************************************/
/* CS and PS Registrations setup (3GPP interface) */

typedef struct {
    GSimpleAsyncResult *result;
    gboolean secondary_done;
} SetupRegistrationContext;

static void
setup_registration_context_free (SetupRegistrationContext *ctx)
{
    g_object_unref (ctx->result);
    g_free (ctx);
}

static gboolean
modem_3gpp_setup_cs_registration_finish (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
modem_3gpp_setup_ps_registration_finish (MMIfaceModem3gpp *self,
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

static const MMBaseModemAtCommand ps_registration_sequence[] = {
    /* Enable unsolicited registration notifications in PS network, with location */
    { "+CGREG=2", 3, FALSE, parse_registration_setup_reply },
    /* Enable unsolicited registration notifications in PS network, without location */
    { "+CGREG=1", 3, FALSE, parse_registration_setup_reply },
    { NULL }
};

static void
setup_registration_sequence_ready (MMBroadbandModem *self,
                                   GAsyncResult *res,
                                   SetupRegistrationContext *ctx)
{
    GError *error = NULL;

    if (ctx->secondary_done) {
        mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
        if (error) {
            g_simple_async_result_take_error (ctx->result, error);
            g_simple_async_result_complete (ctx->result);
            setup_registration_context_free (ctx);
            return;
        }
        /* success */
    } else {
        GVariant *command;
        MMAtSerialPort *secondary;

        command = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, &error);
        if (!command) {
            g_assert (error != NULL);
            g_simple_async_result_take_error (ctx->result, error);
            g_simple_async_result_complete (ctx->result);
            setup_registration_context_free (ctx);
            return;
        }

        secondary = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));
        if (secondary) {
            /* Now use the same registration setup in secondary port, if any */
            ctx->secondary_done = TRUE;
            mm_base_modem_at_command_in_port (
                MM_BASE_MODEM (self),
                mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                g_variant_get_string (command, NULL),
                3,
                FALSE,
                NULL,  /* cancellable */
                (GAsyncReadyCallback)setup_registration_sequence_ready,
                ctx);
            return;
        }
        /* success */
    }

    /* We're done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    g_simple_async_result_complete (ctx->result);
    setup_registration_context_free (ctx);
}

static void
modem_3gpp_setup_cs_registration (MMIfaceModem3gpp *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    SetupRegistrationContext *ctx;

    ctx = g_new0 (SetupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_setup_cs_registration);
    mm_base_modem_at_sequence_in_port (
        MM_BASE_MODEM (self),
        mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
        cs_registration_sequence,
        NULL,  /* response processor context */
        NULL,  /* response processor context free */
        NULL,  /* cancellable */
        (GAsyncReadyCallback)setup_registration_sequence_ready,
        ctx);
}

static void
modem_3gpp_setup_ps_registration (MMIfaceModem3gpp *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    SetupRegistrationContext *ctx;

    ctx = g_new0 (SetupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_setup_ps_registration);
    mm_base_modem_at_sequence_in_port (
        MM_BASE_MODEM (self),
        mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
        ps_registration_sequence,
        NULL,  /* response processor context */
        NULL,  /* response processor context free */
        NULL,  /* cancellable */
        (GAsyncReadyCallback)setup_registration_sequence_ready,
        ctx);
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
                              NULL, /* cancellable */
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

    qcdm = mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));
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

    qcdm = mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));
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
            sid = mm_cdma_convert_sid (str);
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
                                  NULL, /* cancellable */
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

        g_object_ref (ctx->qcdm);

        /* Setup command */
        cdma_status = g_byte_array_sized_new (25);
        cdma_status->len = qcdm_cmd_cdma_status_new ((char *) cdma_status->data, 25);
        g_assert (cdma_status->len);
        mm_qcdm_serial_port_queue_command (ctx->qcdm,
                                           cdma_status,
                                           3,
                                           (MMQcdmSerialResponseFn)qcdm_cdma_status_ready,
                                           ctx);
        return;
    }

    /* Try with AT if we don't have QCDM */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CSS?",
                              3,
                              FALSE,
                              NULL, /* cancellable */
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
        gulong int_cad;

        /* Strip any leading command tag and spaces */
        result = mm_strip_tag (result, "+CAD:");
        errno = 0;
        int_cad = strtol (result, NULL, 10);
        if ((errno == EINVAL) || (errno == ERANGE))
            g_simple_async_result_set_error (simple,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Failed to parse +CAD response '%s'",
                                             result);
        else
            /* 1 == CDMA service */
            g_simple_async_result_set_op_res_gboolean (simple, (int_cad == 1));
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
                              NULL, /* cancellable */
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
    if (!mm_cdma_parse_spservice_response (response,
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
                              NULL, /* cancellable */
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
    port = mm_base_modem_get_best_at_port (MM_BASE_MODEM (self), &error);
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
                              NULL, /* cancellable */
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
                              NULL, /* cancellable */
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
    ctx->has_qcdm_port = !!mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));

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
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)spservice_check_ready,
                              ctx);
}

/*****************************************************************************/
/* Register in network (CDMA interface) */

/* Maximum time to wait for a successful registration when polling
 * periodically */
#define MAX_CDMA_REGISTRATION_CHECK_WAIT_TIME 60

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    GTimer *timer;
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

static void run_all_cdma_registration_checks_ready (MMBroadbandModem *self,
                                                    GAsyncResult *res,
                                                    RegisterInCdmaNetworkContext *ctx);

static gboolean
run_all_cdma_registration_checks_again (RegisterInCdmaNetworkContext *ctx)
{
    /* Get fresh registration state */
    mm_iface_modem_cdma_run_all_registration_checks (
        MM_IFACE_MODEM_CDMA (ctx->self),
        (GAsyncReadyCallback)run_all_cdma_registration_checks_ready,
        ctx);
    return FALSE;
}

static void
run_all_cdma_registration_checks_ready (MMBroadbandModem *self,
                                        GAsyncResult *res,
                                        RegisterInCdmaNetworkContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_cdma_run_all_registration_checks_finish (MM_IFACE_MODEM_CDMA (self),
                                                            res,
                                                            &error);

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
    if (g_timer_elapsed (ctx->timer, NULL) > MAX_CDMA_REGISTRATION_CHECK_WAIT_TIME) {
        mm_dbg ("CDMA registration check timed out");
        mm_iface_modem_cdma_update_cdma1x_registration_state (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
            MM_MODEM_CDMA_SID_UNKNOWN,
            MM_MODEM_CDMA_NID_UNKNOWN);
        mm_iface_modem_cdma_update_evdo_registration_state (
            MM_IFACE_MODEM_CDMA (self),
            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        g_simple_async_result_take_error (
            ctx->result,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT));
        register_in_cdma_network_context_complete_and_free (ctx);
        return;
    }

    /* Check again in a few seconds. */
    mm_dbg ("Modem not yet registered in a CDMA network... will recheck soon");
    g_timeout_add_seconds (3,
                           (GSourceFunc)run_all_cdma_registration_checks_again,
                           ctx);
}

static void
modem_cdma_register_in_network (MMIfaceModemCdma *self,
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
    mm_iface_modem_cdma_run_all_registration_checks (
        MM_IFACE_MODEM_CDMA (self),
        (GAsyncReadyCallback)run_all_cdma_registration_checks_ready,
        ctx);
}

/*****************************************************************************/

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_IFACE_SIMPLE,
    DISABLING_STEP_IFACE_MESSAGING,
    DISABLING_STEP_IFACE_LOCATION,
    DISABLING_STEP_IFACE_FIRMWARE,
    DISABLING_STEP_IFACE_CONTACTS,
    DISABLING_STEP_IFACE_CDMA,
    DISABLING_STEP_IFACE_3GPP_USSD,
    DISABLING_STEP_IFACE_3GPP,
    DISABLING_STEP_IFACE_MODEM,
    DISABLING_STEP_LAST,
} DisablingStep;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    DisablingStep step;
} DisablingContext;

static void disabling_step (DisablingContext *ctx);

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
disable_finish (MMBaseModem *self,
               GAsyncResult *res,
               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    return TRUE;
}

#undef INTERFACE_DISABLE_READY_FN
#define INTERFACE_DISABLE_READY_FN(NAME,TYPE)                           \
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
            g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (ctx->result), error); \
            disabling_context_complete_and_free (ctx);                  \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        disabling_step (ctx);                                           \
    }

INTERFACE_DISABLE_READY_FN (iface_modem, MM_IFACE_MODEM)
INTERFACE_DISABLE_READY_FN (iface_modem_3gpp, MM_IFACE_MODEM_3GPP)
INTERFACE_DISABLE_READY_FN (iface_modem_cdma, MM_IFACE_MODEM_CDMA)

static void
disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_SIMPLE:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_MESSAGING:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_LOCATION:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_IFACE_FIRMWARE:
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
        g_assert (ctx->self->priv->modem_dbus_skeleton != NULL);
        /* Disabling the Modem interface */
        mm_iface_modem_disable (MM_IFACE_MODEM (ctx->self),
                               (GAsyncReadyCallback)iface_modem_disable_ready,
                               ctx);
        return;

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
        /* We should never have a UNKNOWN->DISABLED transition requested by
         * the user. */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_LOCKED:
    case MM_MODEM_STATE_DISABLED:
        /* Just return success, don't relaunch enabling */
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
    ENABLING_STEP_IFACE_MODEM,
    ENABLING_STEP_IFACE_3GPP,
    ENABLING_STEP_IFACE_3GPP_USSD,
    ENABLING_STEP_IFACE_CDMA,
    ENABLING_STEP_IFACE_CONTACTS,
    ENABLING_STEP_IFACE_FIRMWARE,
    ENABLING_STEP_IFACE_LOCATION,
    ENABLING_STEP_IFACE_MESSAGING,
    ENABLING_STEP_IFACE_SIMPLE,
    ENABLING_STEP_LAST,
} EnablingStep;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    EnablingStep step;
} EnablingContext;

static void enabling_step (EnablingContext *ctx);

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
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

#undef INTERFACE_ENABLE_READY_FN
#define INTERFACE_ENABLE_READY_FN(NAME,TYPE)                            \
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
            g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (ctx->result), error); \
            enabling_context_complete_and_free (ctx);                   \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        enabling_step (ctx);                                            \
    }

INTERFACE_ENABLE_READY_FN (iface_modem, MM_IFACE_MODEM)
INTERFACE_ENABLE_READY_FN (iface_modem_3gpp, MM_IFACE_MODEM_3GPP)
INTERFACE_ENABLE_READY_FN (iface_modem_cdma, MM_IFACE_MODEM_CDMA)

static void
enabling_step (EnablingContext *ctx)
{
    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_MODEM:
        g_assert (ctx->self->priv->modem_dbus_skeleton != NULL);
        /* Enabling the Modem interface */
        mm_iface_modem_enable (MM_IFACE_MODEM (ctx->self),
                               (GAsyncReadyCallback)iface_modem_enable_ready,
                               ctx);
        return;

    case ENABLING_STEP_IFACE_3GPP:
        if (ctx->self->priv->modem_3gpp_dbus_skeleton) {
            mm_dbg ("Modem has 3GPP capabilities, enabling the Modem 3GPP interface...");
            /* Enabling the Modem 3GPP interface */
            mm_iface_modem_3gpp_enable (MM_IFACE_MODEM_3GPP (ctx->self),
                                        (GAsyncReadyCallback)iface_modem_3gpp_enable_ready,
                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_3GPP_USSD:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_CDMA:
        if (ctx->self->priv->modem_cdma_dbus_skeleton) {
            mm_dbg ("Modem has CDMA capabilities, enabling the Modem CDMA interface...");
            /* Enabling the Modem CDMA interface */
            mm_iface_modem_cdma_enable (MM_IFACE_MODEM_CDMA (ctx->self),
                                        (GAsyncReadyCallback)iface_modem_cdma_enable_ready,
                                        ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_CONTACTS:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_FIRMWARE:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_LOCATION:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_IFACE_MESSAGING:
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
        /* We should never have a UNKNOWN->ENABLED transition */
        g_assert_not_reached ();
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
    INITIALIZE_STEP_IFACE_MODEM,
    INITIALIZE_STEP_IFACE_3GPP,
    INITIALIZE_STEP_IFACE_3GPP_USSD,
    INITIALIZE_STEP_IFACE_CDMA,
    INITIALIZE_STEP_IFACE_CONTACTS,
    INITIALIZE_STEP_IFACE_FIRMWARE,
    INITIALIZE_STEP_IFACE_LOCATION,
    INITIALIZE_STEP_IFACE_MESSAGING,
    INITIALIZE_STEP_IFACE_SIMPLE,
    INITIALIZE_STEP_LAST,
} InitializeStep;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    InitializeStep step;
    MMAtSerialPort *port;
    gboolean close_port;
} InitializeContext;

static void initialize_step (InitializeContext *ctx);

static void
initialize_context_complete_and_free (InitializeContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    /* balance open/close count */
    if (ctx->close_port)
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
    g_object_unref (ctx->port);
    g_object_unref (ctx->self);
    g_free (ctx);
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

#undef INTERFACE_INIT_READY_FN
#define INTERFACE_INIT_READY_FN(NAME,TYPE)                              \
    static void                                                         \
    NAME##_initialize_ready (MMBroadbandModem *self,                    \
                             GAsyncResult *result,                      \
                             InitializeContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        if (!mm_##NAME##_initialize_finish (TYPE (self),                \
                                            result,                     \
                                            &error)) {                  \
            g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (ctx->result), error); \
            initialize_context_complete_and_free (ctx);                 \
            return;                                                     \
        }                                                               \
                                                                        \
        /* bind simple properties */                                    \
        mm_##NAME##_bind_simple_status (TYPE (self),                    \
                                        self->priv->modem_simple_status); \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        initialize_step (ctx);                                          \
    }

INTERFACE_INIT_READY_FN (iface_modem, MM_IFACE_MODEM)
INTERFACE_INIT_READY_FN (iface_modem_3gpp, MM_IFACE_MODEM_3GPP)
INTERFACE_INIT_READY_FN (iface_modem_cdma, MM_IFACE_MODEM_CDMA)

static void
initialize_step (InitializeContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZE_STEP_FIRST: {
        GError *error = NULL;

        if (!ctx->self->priv->modem_simple_status)
            ctx->self->priv->modem_simple_status = mm_common_simple_properties_new ();

        /* Open and send first commands to the serial port */
        if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
            g_simple_async_result_take_error (ctx->result, error);
            initialize_context_complete_and_free (ctx);
            return;
        }
        ctx->close_port = TRUE;
        /* Try to disable echo */
        mm_base_modem_at_command_in_port_ignore_reply (
            MM_BASE_MODEM (ctx->self),
            ctx->port,
            "E0",
            3);
        /* Try to get extended errors */
        mm_base_modem_at_command_in_port_ignore_reply (
            MM_BASE_MODEM (ctx->self),
            ctx->port,
            "+CMEE=1",
            3);
        /* Fall down to next step */
        ctx->step++;
    }

    case INITIALIZE_STEP_IFACE_MODEM:
        /* Initialize the Modem interface */
        mm_iface_modem_initialize (MM_IFACE_MODEM (ctx->self),
                                   ctx->port,
                                   (GAsyncReadyCallback)iface_modem_initialize_ready,
                                   ctx);
        return;

    case INITIALIZE_STEP_IFACE_3GPP:
        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self))) {
            /* Initialize the 3GPP interface */
            mm_iface_modem_3gpp_initialize (MM_IFACE_MODEM_3GPP (ctx->self),
                                            ctx->port,
                                            (GAsyncReadyCallback)iface_modem_3gpp_initialize_ready,
                                            ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_3GPP_USSD:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_CDMA:
        if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self))) {
            /* Initialize the CDMA interface */
            mm_iface_modem_cdma_initialize (MM_IFACE_MODEM_CDMA (ctx->self),
                                            ctx->port,
                                            (GAsyncReadyCallback)iface_modem_cdma_initialize_ready,
                                            ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_CONTACTS:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_FIRMWARE:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_LOCATION:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_MESSAGING:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_IFACE_SIMPLE:
        mm_iface_modem_simple_initialize (MM_IFACE_MODEM_SIMPLE (ctx->self));
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZE_STEP_LAST:
        /* All initialized without errors! */
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (ctx->result), TRUE);
        initialize_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static void
initialize (MMBaseModem *self,
            MMAtSerialPort *port,
            GCancellable *cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    InitializeContext *ctx;

    ctx = g_new0 (InitializeContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (port);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialize);
    ctx->step = INITIALIZE_STEP_FIRST;

    initialize_step (ctx);
}

/*****************************************************************************/

MMBroadbandModem *
mm_broadband_modem_new (const gchar *device,
                        const gchar *driver,
                        const gchar *plugin,
                        guint16 vendor_id,
                        guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
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
    case PROP_MODEM_CDMA_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_cdma_dbus_skeleton);
        self->priv->modem_cdma_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_SIMPLE_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_simple_dbus_skeleton);
        self->priv->modem_simple_dbus_skeleton = g_value_dup_object (value);
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
    case PROP_MODEM_CURRENT_CAPABILITIES:
        self->priv->modem_current_capabilities = g_value_get_flags (value);
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
    case PROP_MODEM_CDMA_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_cdma_dbus_skeleton);
        break;
    case PROP_MODEM_SIMPLE_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_simple_dbus_skeleton);
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
    case PROP_MODEM_CURRENT_CAPABILITIES:
        g_value_set_flags (value, self->priv->modem_current_capabilities);
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
    self->priv->modem_current_capabilities = MM_MODEM_CAPABILITY_NONE;
    self->priv->modem_3gpp_registration_regex = mm_3gpp_creg_regex_get (TRUE);
    self->priv->modem_current_charset = MM_MODEM_CHARSET_UNKNOWN;
    self->priv->modem_3gpp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_3gpp_cs_network_supported = TRUE;
    self->priv->modem_3gpp_ps_network_supported = TRUE;
    self->priv->modem_cdma_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_cdma_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_cdma_cdma1x_network_supported = TRUE;
    self->priv->modem_cdma_evdo_network_supported = TRUE;
}

static void
finalize (GObject *object)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

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

    if (self->priv->modem_cdma_dbus_skeleton) {
        mm_iface_modem_cdma_shutdown (MM_IFACE_MODEM_CDMA (object));
        g_clear_object (&self->priv->modem_cdma_dbus_skeleton);
    }

    if (self->priv->modem_simple_dbus_skeleton) {
        mm_iface_modem_simple_shutdown (MM_IFACE_MODEM_SIMPLE (object));
        g_clear_object (&self->priv->modem_simple_dbus_skeleton);
    }

    g_clear_object (&self->priv->modem_sim);
    g_clear_object (&self->priv->modem_bearer_list);
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
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Initialization steps */
    iface->load_imei = modem_3gpp_load_imei;
    iface->load_imei_finish = modem_3gpp_load_imei_finish;

    /* Enabling steps */
    iface->setup_indicators = modem_3gpp_setup_indicators;
    iface->setup_indicators_finish = modem_3gpp_setup_indicators_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_unsolicited_events_finish;
    iface->setup_unsolicited_registration = modem_3gpp_setup_unsolicited_registration;
    iface->setup_unsolicited_registration_finish = modem_3gpp_setup_unsolicited_registration_finish;
    iface->setup_cs_registration = modem_3gpp_setup_cs_registration;
    iface->setup_cs_registration_finish = modem_3gpp_setup_cs_registration_finish;
    iface->setup_ps_registration = modem_3gpp_setup_ps_registration;
    iface->setup_ps_registration_finish = modem_3gpp_setup_ps_registration_finish;

    /* Disabling steps */
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_unsolicited_events_finish;
    iface->cleanup_unsolicited_registration = modem_3gpp_cleanup_unsolicited_registration;
    iface->cleanup_unsolicited_registration_finish = modem_3gpp_cleanup_unsolicited_registration_finish;
    iface->cleanup_cs_registration = modem_3gpp_cleanup_cs_registration;
    iface->cleanup_cs_registration_finish = modem_3gpp_cleanup_cs_registration_finish;
    iface->cleanup_ps_registration = modem_3gpp_cleanup_ps_registration;
    iface->cleanup_ps_registration_finish = modem_3gpp_cleanup_ps_registration_finish;

    /* Additional actions */
    iface->load_operator_code = modem_3gpp_load_operator_code;
    iface->load_operator_code_finish = modem_3gpp_load_operator_code_finish;
    iface->load_operator_name = modem_3gpp_load_operator_name;
    iface->load_operator_name_finish = modem_3gpp_load_operator_name_finish;
    iface->run_cs_registration_check = modem_3gpp_run_cs_registration_check;
    iface->run_cs_registration_check_finish = modem_3gpp_run_cs_registration_check_finish;
    iface->run_ps_registration_check = modem_3gpp_run_ps_registration_check;
    iface->run_ps_registration_check_finish = modem_3gpp_run_ps_registration_check_finish;
    iface->register_in_network = modem_3gpp_register_in_network;
    iface->register_in_network_finish = modem_3gpp_register_in_network_finish;
    iface->scan_networks = modem_3gpp_scan_networks;
    iface->scan_networks_finish = modem_3gpp_scan_networks_finish;
    iface->bearer_new = mm_bearer_3gpp_new;
    iface->bearer_new_finish = mm_bearer_3gpp_new_finish;
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
    iface->bearer_new = mm_bearer_cdma_new;
    iface->bearer_new_finish = mm_bearer_cdma_new_finish;
}

static void
iface_modem_simple_init (MMIfaceModemSimple *iface)
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

    g_object_class_override_property (object_class,
                                      PROP_MODEM_DBUS_SKELETON,
                                      MM_IFACE_MODEM_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_3GPP_DBUS_SKELETON,
                                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CDMA_DBUS_SKELETON,
                                      MM_IFACE_MODEM_CDMA_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_SIMPLE_DBUS_SKELETON,
                                      MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON);

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
                                      PROP_MODEM_CURRENT_CAPABILITIES,
                                      MM_IFACE_MODEM_CURRENT_CAPABILITIES);

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
                                      PROP_MODEM_SIMPLE_STATUS,
                                      MM_IFACE_MODEM_SIMPLE_STATUS);

}
