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
#include <string.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-simple.h"
#include "mm-bearer-3gpp.h"
#include "mm-bearer-list.h"
#include "mm-sim.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_simple_init (MMIfaceModemSimple *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModem, mm_broadband_modem, MM_TYPE_BASE_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIMPLE, iface_modem_simple_init));

enum {
    PROP_0,
    PROP_MODEM_DBUS_SKELETON,
    PROP_MODEM_3GPP_DBUS_SKELETON,
    PROP_MODEM_SIMPLE_DBUS_SKELETON,
    PROP_MODEM_SIM,
    PROP_MODEM_BEARER_LIST,
    PROP_MODEM_STATE,
    PROP_MODEM_CURRENT_CAPABILITIES,
    PROP_MODEM_3GPP_REGISTRATION_STATE,
    PROP_MODEM_SIMPLE_STATUS,
    PROP_LAST
};

/* When CIND is supported, invalid indicators are marked with this value */
#define CIND_INDICATOR_INVALID 255
#define CIND_INDICATOR_IS_VALID(u) (u != CIND_INDICATOR_INVALID)

struct _MMBroadbandModemPrivate {
    GObject *modem_dbus_skeleton;
    GObject *modem_3gpp_dbus_skeleton;
    GObject *modem_simple_dbus_skeleton;
    MMSim *modem_sim;
    MMBearerList *modem_bearer_list;
    MMModemState modem_state;
    MMModemCapability modem_current_capabilities;
    MMModem3gppRegistrationState modem_3gpp_registration_state;
    MMCommonSimpleProperties *modem_simple_status;

    /* Modem helpers */
    MMModemCharset current_charset;
    gboolean cind_supported;
    guint cind_indicator_roaming;
    guint cind_indicator_signal_quality;
    guint cind_indicator_service;

    /* 3GPP registration helpers */
    GPtrArray *reg_regex;
    MMModem3gppRegistrationState reg_cs;
    MMModem3gppRegistrationState reg_ps;
    gboolean manual_reg;
    GCancellable *pending_reg_cancellable;
};

/*****************************************************************************/
/* CREATE BEARER */

static MMModem3gppRegistrationState get_consolidated_reg_state (MMBroadbandModem *self);

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
modem_create_bearer (MMIfaceModem *self,
                     MMCommonBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMBearer *bearer = NULL;
    GError *error = NULL;

    /* TODO: We'll need to guess the capability of the bearer, based on the
     * current capabilities that we handle, and the specific allowed modes
     * configured in the modem. Use 3GPP for testing now */

    /* New 3GPP bearer */
    if (mm_iface_modem_is_3gpp (self)) {
        bearer = mm_iface_modem_3gpp_create_bearer (MM_IFACE_MODEM_3GPP (self),
                                                    properties,
                                                    &error);
    } else {
        g_set_error (&error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot create bearer in modem of unknown type");
    }

    if (!bearer) {
        g_simple_async_report_take_gerror_in_idle (G_OBJECT (self),
                                                   callback,
                                                   user_data,
                                                   error);
            return;
    }

    /* Expose all properties used during creation */
    mm_bearer_expose_properties (bearer, properties);

    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);
    g_simple_async_result_set_op_res_gpointer (result,
                                               bearer,
                                               (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* CREATE SIM */

static MMModem3gppRegistrationState get_consolidated_reg_state (MMBroadbandModem *self);

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
/* CAPABILITIES */

typedef struct {
	gchar *name;
	MMModemCapability bits;
} ModemCaps;

static const ModemCaps modem_caps[] = {
	{ "+CGSM",     MM_MODEM_CAPABILITY_GSM_UMTS  },
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
                 const GError *error,
                 GVariant **result,
                 GError **result_error)
{
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
load_current_capabilities_finish (MMIfaceModem *self,
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
load_current_capabilities (MMIfaceModem *self,
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
/* MANUFACTURER */

static gchar *
load_manufacturer_finish (MMIfaceModem *self,
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
    { "+CGMI",  3, TRUE, mm_base_modem_response_processor_string },
    { "+GMI",   3, TRUE, mm_base_modem_response_processor_string },
    { NULL }
};

static void
load_manufacturer (MMIfaceModem *self,
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
/* MODEL */

static gchar *
load_model_finish (MMIfaceModem *self,
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
    { "+CGMM",  3, TRUE, mm_base_modem_response_processor_string },
    { "+GMM",   3, TRUE, mm_base_modem_response_processor_string },
    { NULL }
};

static void
load_model (MMIfaceModem *self,
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
/* REVISION */

static gchar *
load_revision_finish (MMIfaceModem *self,
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
    { "+CGMR",  3, TRUE, mm_base_modem_response_processor_string },
    { "+GMR",   3, TRUE, mm_base_modem_response_processor_string },
    { NULL }
};

static void
load_revision (MMIfaceModem *self,
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
/* EQUIPMENT ID */

static gchar *
load_equipment_identifier_finish (MMIfaceModem *self,
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
    { "+CGSN",  3, TRUE, mm_base_modem_response_processor_string },
    { "+GSN",   3, TRUE, mm_base_modem_response_processor_string },
    { NULL }
};

static void
load_equipment_identifier (MMIfaceModem *self,
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
/* DEVICE IDENTIFIER */

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
load_device_identifier_finish (MMIfaceModem *self,
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
load_device_identifier (MMIfaceModem *self,
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
/* UNLOCK REQUIRED */

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
load_unlock_required_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCK_UNKNOWN;

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_unlock_required_ready (MMIfaceModem *self,
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
load_unlock_required (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_unlock_required);

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
                              (GAsyncReadyCallback)load_unlock_required_ready,
                              result);
}

/*****************************************************************************/
/* SUPPORTED MODES */

static MMModemMode
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return 0;

    return (MMModemMode)GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                              G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_supported_modes (MMIfaceModem *self,
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
                                        load_supported_modes);

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
/* SIGNAL QUALITY */

static guint
load_signal_quality_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return 0;

    return GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_signal_quality_csf_ready (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *result;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    if (result &&
        !strncmp (result, "+CSQ: ", 6)) {
        /* Got valid reply */
        int quality;
        int ber;

        if (sscanf (result + 6, "%d, %d", &quality, &ber)) {
            /* 99 means unknown */
            if (quality == 99) {
                g_simple_async_result_take_error (
                    simple,
                    mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK));
            } else {
                /* Normalize the quality */
                quality = CLAMP (quality, 0, 31) * 100 / 31;
                g_simple_async_result_set_op_res_gpointer (simple,
                                                           GUINT_TO_POINTER (quality),
                                                           NULL);
            }

            g_simple_async_result_complete (simple);
            g_object_unref (simple);
            return;
        }
    }

    g_simple_async_result_set_error (simple,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Could not parse signal quality results");
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_signal_quality_csf (MMBroadbandModem *self,
                         GSimpleAsyncResult *result)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CSQ",
                              3,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)load_signal_quality_csf_ready,
                              result);
}

static void
load_signal_quality_cind_ready (MMBroadbandModem *self,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *result;
    GByteArray *indicators;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!error)
        indicators = mm_parse_cind_query_response (result, &error);

    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    if (indicators->len >= self->priv->cind_indicator_signal_quality) {
        guint quality;
        quality = g_array_index (indicators, guint8, self->priv->cind_indicator_signal_quality);
        quality = CLAMP (quality, 0, 5) * 20;
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (quality),
                                                   NULL);

        g_byte_array_free (indicators, TRUE);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    g_simple_async_result_set_error (simple,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Could not parse CIND signal quality results "
                                     "signal index (%u) outside received range (0-%u)",
                                     self->priv->cind_indicator_signal_quality,
                                     indicators->len);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_signal_quality_cind (MMBroadbandModem *self,
                          GSimpleAsyncResult *result)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CIND?",
                              3,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)load_signal_quality_cind_ready,
                              result);
}

static void
load_signal_quality (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    mm_dbg ("loading signal quality...");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_signal_quality);

    if (MM_BROADBAND_MODEM (self)->priv->cind_supported)
        load_signal_quality_cind (MM_BROADBAND_MODEM (self), result);
    else
        load_signal_quality_csf (MM_BROADBAND_MODEM (self), result);
}

/*****************************************************************************/
/* SETTING UP INDICATORS */

static gboolean
setup_indicators_finish (MMIfaceModem3gpp *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_indicators_ready (MMBroadbandModem *self,
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
    self->priv->cind_supported = TRUE;

#define FIND_INDEX(indicator_str,var_suffix) do {                       \
        r = g_hash_table_lookup (indicators, indicator_str);            \
        self->priv->cind_indicator_##var_suffix = (r ?                  \
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
setup_indicators (MMIfaceModem3gpp *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        setup_indicators);

    /* Load supported indicators */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CIND=?",
                              3,
                              TRUE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)setup_indicators_ready,
                              result);
}

/*****************************************************************************/
/* ENABLE/DISABLE UNSOLICITED EVENTS */

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
    if (ind == self->priv->cind_indicator_signal_quality) {
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
unsolicited_events_finish (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void unsolicited_events (UnsolicitedEventsContext *ctx);

static void
unsolicited_events_ready (MMBroadbandModem *self,
                          GAsyncResult *res,
                          UnsolicitedEventsContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!error) {
        /* Run on next port, if any */
        unsolicited_events (ctx);
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
unsolicited_events (UnsolicitedEventsContext *ctx)
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
                                          (GAsyncReadyCallback)unsolicited_events_ready,
                                          ctx);
        return;
    }

    /* If no more ports, we're fully done now */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    unsolicited_events_context_complete_and_free (ctx);
}

static void
enable_unsolicited_events (MMIfaceModem3gpp *self,
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
                                             enable_unsolicited_events);

    unsolicited_events (ctx);
}

static void
disable_unsolicited_events (MMIfaceModem3gpp *self,
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
                                             disable_unsolicited_events);

    unsolicited_events (ctx);
}

/*****************************************************************************/
/* SETTING MODEM CHARSET */

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
setup_charset_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    return TRUE;
}

static void
current_charset_ready (MMBroadbandModem *self,
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
            self->priv->current_charset = current;
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        }
    }

    g_simple_async_result_complete (ctx->result);
    setup_charset_context_free (ctx);
}

static void
setup_charset_ready (MMBroadbandModem *self,
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
                              (GAsyncReadyCallback)current_charset_ready,
                              ctx);
}

static void
setup_charset (MMIfaceModem *self,
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
                                             setup_charset);
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
        (GAsyncReadyCallback)setup_charset_ready,
        ctx);
}

/*****************************************************************************/
/* LOAD SUPPORTED CHARSETS */

static MMModemCharset
load_supported_charsets_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CHARSET_UNKNOWN;

    return GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_supported_charsets_ready (MMBaseModem *self,
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
load_supported_charsets (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_charsets);

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
                              (GAsyncReadyCallback)load_supported_charsets_ready,
                              result);
}

/*****************************************************************************/
/* FLOW CONTROL */

static gboolean
setup_flow_control_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    /* Completely ignore errors */
    return TRUE;
}

static void
setup_flow_control (MMIfaceModem *self,
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
                                        setup_flow_control);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* MODEM POWER UP */

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
                                        setup_flow_control);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* MODEM INITIALIZATION */

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
/* IMEI */

static gchar *
load_imei_finish (MMIfaceModem3gpp *self,
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
load_imei (MMIfaceModem3gpp *self,
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
/* Operator Code */

static gchar *
load_operator_code_finish (MMIfaceModem3gpp *self,
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
load_operator_code (MMIfaceModem3gpp *self,
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
/* Operator Name */

static gchar *
load_operator_name_finish (MMIfaceModem3gpp *self,
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
load_operator_name (MMIfaceModem3gpp *self,
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
/* Unsolicited registration messages handling (3GPP) */

/* static void clear_previous_registration_request (MMBroadbandModem *self, */
/*                                                  gboolean complete_with_cancel); */

static MMModem3gppRegistrationState
get_consolidated_reg_state (MMBroadbandModem *self)
{
    /* Some devices (Blackberries for example) will respond to +CGREG, but
     * return ERROR for +CREG, probably because their firmware is just stupid.
     * So here we prefer the +CREG response, but if we never got a successful
     * +CREG response, we'll take +CGREG instead.
     */
    if (self->priv->reg_cs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        self->priv->reg_cs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return self->priv->reg_cs;

    if (self->priv->reg_ps == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        self->priv->reg_ps == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return self->priv->reg_ps;

    if (self->priv->reg_cs == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return self->priv->reg_cs;

    if (self->priv->reg_ps == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return self->priv->reg_ps;

    return self->priv->reg_cs;
}

static gboolean
setup_unsolicited_registration_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
reg_state_changed (MMAtSerialPort *port,
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

    if (cgreg)
        self->priv->reg_ps = state;
    else
        self->priv->reg_cs = state;

    /* Report new registration state */
    state = get_consolidated_reg_state (self);
    mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                   state,
                                                   act);

    /* TODO: report LAC/CI location */
    /* update_lac_ci (self, lac, cell_id, cgreg ? 1 : 0); */
}

static void
setup_unsolicited_registration (MMIfaceModem3gpp *self,
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
                                        setup_unsolicited_registration);

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
                    (MMAtSerialUnsolicitedMsgFn) reg_state_changed,
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
/* Unsolicited registration messages cleaning up (3GPP) */

static gboolean
cleanup_unsolicited_registration_finish (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cleanup_unsolicited_registration (MMIfaceModem3gpp *self,
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
                                        cleanup_unsolicited_registration);

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
/* Scan networks (3GPP) */

static GList *
scan_networks_finish (MMIfaceModem3gpp *self,
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
scan_networks (MMIfaceModem3gpp *self,
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
/* Register in network (3GPP) */

/* Maximum time to wait for a successful registration when polling
 * periodically */
#define MAX_REGISTRATION_CHECK_WAIT_TIME 60

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    GTimer *timer;
} RegisterInNetworkContext;

static void
register_in_network_context_complete_and_free (RegisterInNetworkContext *ctx)
{
    /* If our cancellable reference is still around, clear it */
    if (ctx->self->priv->pending_reg_cancellable == ctx->cancellable) {
        g_clear_object (&ctx->self->priv->pending_reg_cancellable);
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
register_in_network_finish (MMIfaceModem3gpp *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#define REG_IS_IDLE(state)                                  \
    (state != MM_MODEM_3GPP_REGISTRATION_STATE_HOME &&      \
     state != MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING && \
     state != MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)

#define REG_IS_DONE(state)                                  \
    (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||      \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING ||   \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED)

static void run_all_registration_checks_ready (MMBroadbandModem *self,
                                               GAsyncResult *res,
                                               RegisterInNetworkContext *ctx);

static gboolean
run_all_registration_checks_again (RegisterInNetworkContext *ctx)
{
    /* Get fresh registration state */
    mm_iface_modem_3gpp_run_all_registration_checks (
        MM_IFACE_MODEM_3GPP (ctx->self),
        (GAsyncReadyCallback)run_all_registration_checks_ready,
        ctx);
    return FALSE;
}

static void
run_all_registration_checks_ready (MMBroadbandModem *self,
                                   GAsyncResult *res,
                                   RegisterInNetworkContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (MM_IFACE_MODEM_3GPP (self),
                                                            res,
                                                            &error);

    if (error) {
        mm_dbg ("Registration check failed: '%s'", error->message);
        mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                       MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                       MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_simple_async_result_take_error (ctx->result, error);
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* If we got registered, end registration checks */
    if (REG_IS_DONE (self->priv->modem_3gpp_registration_state)) {
        mm_dbg ("Modem is currently registered in 3GPP network");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* Don't spent too much time waiting to get registered */
    if (g_timer_elapsed (ctx->timer, NULL) > MAX_REGISTRATION_CHECK_WAIT_TIME) {
        mm_dbg ("Registration check timed out");
        mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                       MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                       MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_simple_async_result_take_error (
            ctx->result,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT));
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* If we're still waiting for automatic registration to complete or
     * fail, check again in a few seconds.
     *
     * This 3s timeout will catch results from automatic registrations as
     * well.
     */
    mm_dbg ("Modem not yet registered... will recheck soon");
    g_timeout_add_seconds (3,
                           (GSourceFunc)run_all_registration_checks_again,
                           ctx);
}

static void
register_in_network_ready (MMBroadbandModem *self,
                           GAsyncResult *res,
                           RegisterInNetworkContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);

    if (error) {
        /* Propagate error in COPS, if any */
        mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                       MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,
                                                       MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_simple_async_result_take_error (ctx->result, error);
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* Get fresh registration state */
    ctx->timer = g_timer_new ();
    mm_iface_modem_3gpp_run_all_registration_checks (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)run_all_registration_checks_ready,
        ctx);
}

static void
register_in_network (MMIfaceModem3gpp *self,
                     const gchar *network_id,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModem *broadband = MM_BROADBAND_MODEM (self);
    RegisterInNetworkContext *ctx;
    gchar *command = NULL;

    /* (Try to) cancel previous registration request */
    if (broadband->priv->pending_reg_cancellable) {
        g_cancellable_cancel (broadband->priv->pending_reg_cancellable);
        g_clear_object (&broadband->priv->pending_reg_cancellable);
    }

    ctx = g_new0 (RegisterInNetworkContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             register_in_network);
    ctx->cancellable = g_cancellable_new ();

    /* Keep an accessible reference to the cancellable, so that we can cancel
     * previous request when needed */
    broadband->priv->pending_reg_cancellable = g_object_ref (ctx->cancellable);

    /* If the user sent a specific network to use, lock it in. */
    if (network_id && network_id[0]) {
        command = g_strdup_printf ("+COPS=1,2,\"%s\"", network_id);
        broadband->priv->manual_reg = TRUE;
    }
    /* If no specific network was given, and the modem is not registered and not
     * searching, kick it to search for a network. Also do auto registration if
     * the modem had been set to manual registration last time but now is not.
     */
    else if (REG_IS_IDLE (broadband->priv->modem_3gpp_registration_state) ||
             broadband->priv->manual_reg) {
        command = g_strdup ("+COPS=0,,");
        broadband->priv->manual_reg = FALSE;
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
                                  (GAsyncReadyCallback)register_in_network_ready,
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
        (GAsyncReadyCallback)run_all_registration_checks_ready,
        ctx);
}

/*****************************************************************************/
/* CS and PS Registration checks (3GPP) */

static gboolean
run_cs_registration_check_finish (MMIfaceModem3gpp *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
run_ps_registration_check_finish (MMIfaceModem3gpp *self,
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
        for (i = 0; i < self->priv->reg_regex->len; i++) {
            if (g_regex_match ((GRegex *)g_ptr_array_index (self->priv->reg_regex, i),
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
                if (cgreg)
                    self->priv->reg_ps = state;
                else
                    self->priv->reg_cs = state;

                /* Report new registration state */
                mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                               get_consolidated_reg_state (self),
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
run_cs_registration_check (MMIfaceModem3gpp *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        run_cs_registration_check);

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
run_ps_registration_check (MMIfaceModem3gpp *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        run_ps_registration_check);

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
/* CS and PS Registrations cleanup (3GPP) */

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
cleanup_cs_registration_finish (MMIfaceModem3gpp *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
cleanup_ps_registration_finish (MMIfaceModem3gpp *self,
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
        self->priv->reg_cs = MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;
    else
        self->priv->reg_ps = MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;

    mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                   get_consolidated_reg_state (self),
                                                   MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    /* We're done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    g_simple_async_result_complete (ctx->result);
    cleanup_registration_context_free (ctx);
}

static void
cleanup_cs_registration (MMIfaceModem3gpp *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    CleanupRegistrationContext *ctx;

    ctx = g_new0 (CleanupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             cleanup_cs_registration);
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
cleanup_ps_registration (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    CleanupRegistrationContext *ctx;

    ctx = g_new0 (CleanupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             cleanup_cs_registration);
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
/* CS and PS Registrations (3GPP) */

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
setup_cs_registration_finish (MMIfaceModem3gpp *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
setup_ps_registration_finish (MMIfaceModem3gpp *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
parse_reg_setup_reply (MMBaseModem *self,
                       gpointer none,
                       const gchar *command,
                       const gchar *response,
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
    { "+CREG=2", 3, FALSE, parse_reg_setup_reply },
    /* Enable unsolicited registration notifications in CS network, without location */
    { "+CREG=1", 3, FALSE, parse_reg_setup_reply },
    { NULL }
};

static const MMBaseModemAtCommand ps_registration_sequence[] = {
    /* Enable unsolicited registration notifications in PS network, with location */
    { "+CGREG=2", 3, FALSE, parse_reg_setup_reply },
    /* Enable unsolicited registration notifications in PS network, without location */
    { "+CGREG=1", 3, FALSE, parse_reg_setup_reply },
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
setup_cs_registration (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    SetupRegistrationContext *ctx;

    ctx = g_new0 (SetupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             setup_cs_registration);
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
setup_ps_registration (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    SetupRegistrationContext *ctx;

    ctx = g_new0 (SetupRegistrationContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             setup_ps_registration);
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
        if (ctx->self->priv->modem_current_capabilities & MM_MODEM_CAPABILITY_CDMA_EVDO) {
            /* TODO: Expose the CDMA interface */
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
            /* Initialize the Modem interface */
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
        if (ctx->self->priv->modem_current_capabilities & MM_MODEM_CAPABILITY_CDMA_EVDO) {
            /* TODO: Expose the CDMA interface */
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
        self->priv->modem_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_3GPP_DBUS_SKELETON:
        self->priv->modem_3gpp_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_SIMPLE_DBUS_SKELETON:
        self->priv->modem_simple_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_SIM:
        self->priv->modem_sim = g_value_dup_object (value);
        break;
    case PROP_MODEM_BEARER_LIST:
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
    case PROP_MODEM_SIMPLE_STATUS:
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
    self->priv->modem_3gpp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->reg_regex = mm_3gpp_creg_regex_get (TRUE);
    self->priv->reg_cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->reg_ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->current_charset = MM_MODEM_CHARSET_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

    if (self->priv->reg_regex)
        mm_3gpp_creg_regex_destroy (self->priv->reg_regex);

    G_OBJECT_CLASS (mm_broadband_modem_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

    if (self->priv->modem_dbus_skeleton) {
        /* TODO: Cancel initialization/enabling/disabling, whatever */
        mm_iface_modem_shutdown (MM_IFACE_MODEM (object));
        g_clear_object (&self->priv->modem_dbus_skeleton);
    }

    if (self->priv->modem_3gpp_dbus_skeleton) {
        mm_iface_modem_3gpp_shutdown (MM_IFACE_MODEM_3GPP (object));
        g_clear_object (&self->priv->modem_3gpp_dbus_skeleton);
    }

    if (self->priv->modem_simple_dbus_skeleton) {
        mm_iface_modem_simple_shutdown (MM_IFACE_MODEM_SIMPLE (object));
        g_clear_object (&self->priv->modem_simple_dbus_skeleton);
    }

    if (self->priv->modem_sim)
        g_clear_object (&self->priv->modem_sim);

    if (self->priv->modem_bearer_list)
        g_clear_object (&self->priv->modem_bearer_list);

    g_clear_object (&self->priv->modem_simple_status);

    G_OBJECT_CLASS (mm_broadband_modem_parent_class)->dispose (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Initialization steps */
    iface->load_current_capabilities = load_current_capabilities;
    iface->load_current_capabilities_finish = load_current_capabilities_finish;
    iface->load_manufacturer = load_manufacturer;
    iface->load_manufacturer_finish = load_manufacturer_finish;
    iface->load_model = load_model;
    iface->load_model_finish = load_model_finish;
    iface->load_revision = load_revision;
    iface->load_revision_finish = load_revision_finish;
    iface->load_equipment_identifier = load_equipment_identifier;
    iface->load_equipment_identifier_finish = load_equipment_identifier_finish;
    iface->load_device_identifier = load_device_identifier;
    iface->load_device_identifier_finish = load_device_identifier_finish;
    iface->load_unlock_required = load_unlock_required;
    iface->load_unlock_required_finish = load_unlock_required_finish;
    iface->create_sim = modem_create_sim;
    iface->create_sim_finish = modem_create_sim_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;

    /* Enabling steps */
    iface->modem_init = modem_init;
    iface->modem_init_finish = modem_init_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;
    iface->load_supported_charsets = load_supported_charsets;
    iface->load_supported_charsets_finish = load_supported_charsets_finish;
    iface->setup_charset = setup_charset;
    iface->setup_charset_finish = setup_charset_finish;

    /* Additional actions */
    iface->load_signal_quality = load_signal_quality;
    iface->load_signal_quality_finish = load_signal_quality_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Initialization steps */
    iface->load_imei = load_imei;
    iface->load_imei_finish = load_imei_finish;

    /* Enabling steps */
    iface->setup_indicators = setup_indicators;
    iface->setup_indicators_finish = setup_indicators_finish;
    iface->enable_unsolicited_events = enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = unsolicited_events_finish;
    iface->setup_unsolicited_registration = setup_unsolicited_registration;
    iface->setup_unsolicited_registration_finish = setup_unsolicited_registration_finish;
    iface->setup_cs_registration = setup_cs_registration;
    iface->setup_cs_registration_finish = setup_cs_registration_finish;
    iface->setup_ps_registration = setup_ps_registration;
    iface->setup_ps_registration_finish = setup_ps_registration_finish;

    /* Disabling steps */
    iface->disable_unsolicited_events = disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = unsolicited_events_finish;
    iface->cleanup_unsolicited_registration = cleanup_unsolicited_registration;
    iface->cleanup_unsolicited_registration_finish = cleanup_unsolicited_registration_finish;
    iface->cleanup_cs_registration = cleanup_cs_registration;
    iface->cleanup_cs_registration_finish = cleanup_cs_registration_finish;
    iface->cleanup_ps_registration = cleanup_ps_registration;
    iface->cleanup_ps_registration_finish = cleanup_ps_registration_finish;

    /* Additional actions */
    iface->load_operator_code = load_operator_code;
    iface->load_operator_code_finish = load_operator_code_finish;
    iface->load_operator_name = load_operator_name;
    iface->load_operator_name_finish = load_operator_name_finish;
    iface->run_cs_registration_check = run_cs_registration_check;
    iface->run_cs_registration_check_finish = run_cs_registration_check_finish;
    iface->run_ps_registration_check = run_ps_registration_check;
    iface->run_ps_registration_check_finish = run_ps_registration_check_finish;
    iface->register_in_network = register_in_network;
    iface->register_in_network_finish = register_in_network_finish;
    iface->scan_networks = scan_networks;
    iface->scan_networks_finish = scan_networks_finish;
    iface->create_3gpp_bearer = mm_bearer_3gpp_new;
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
                                      PROP_MODEM_SIMPLE_STATUS,
                                      MM_IFACE_MODEM_SIMPLE_STATUS);

}
