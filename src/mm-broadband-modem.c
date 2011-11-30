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
#include <mm-errors-types.h>
#include <mm-enums-types.h>

#include "mm-at.h"
#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-sim.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"

#define MM_MODEM_CAPABILITY_3GPP        \
    (MM_MODEM_CAPABILITY_GSM_UMTS |     \
     MM_MODEM_CAPABILITY_LTE |          \
     MM_MODEM_CAPABILITY_LTE_ADVANCED)

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModem, mm_broadband_modem, MM_TYPE_BASE_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

enum {
    PROP_0,
    PROP_MODEM_DBUS_SKELETON,
    PROP_MODEM_3GPP_DBUS_SKELETON,
    PROP_MODEM_SIM,
    PROP_MODEM_STATE,
    PROP_MODEM_CURRENT_CAPABILITIES,
    PROP_MODEM_3GPP_REGISTRATION_STATE,
    PROP_LAST
};

struct _MMBroadbandModemPrivate {
    GObject *modem_dbus_skeleton;
    GObject *modem_3gpp_dbus_skeleton;
    MMSim *modem_sim;
    MMModemState modem_state;
    MMModemCapability modem_current_capabilities;
    MMModem3gppRegistrationState modem_3gpp_registration_state;

    /* Modem helpers */
    MMModemCharset current_charset;

    /* 3GPP registration helpers */
    GPtrArray *reg_regex;
    MMModem3gppRegistrationState reg_cs;
    MMModem3gppRegistrationState reg_ps;
    gboolean manual_reg;
    guint pending_reg_id;
    GSimpleAsyncResult *pending_reg_request;
};

static gboolean
common_parse_string_reply (MMBroadbandModem *self,
                           gpointer none,
                           const gchar *command,
                           const gchar *response,
                           const GError *error,
                           GVariant **result,
                           GError **result_error)
{
    if (error) {
        *result_error = g_error_copy (error);
        return FALSE;
    }

    *result = g_variant_new_string (response);
    return TRUE;
}

static gboolean
common_parse_no_reply (MMBroadbandModem *self,
                       gpointer none,
                       const gchar *command,
                       const gchar *response,
                       const GError *error,
                       GVariant **result,
                       GError **result_error)
{
    if (error) {
        *result_error = g_error_copy (error);
        return FALSE;
    }
    return TRUE;
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
parse_caps_gcap (MMBroadbandModem *self,
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
parse_caps_cpin (MMBroadbandModem *self,
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
parse_caps_cgmm (MMBroadbandModem *self,
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

static gchar *
create_capabilities_string (MMModemCapability caps)
{
	GFlagsClass *flags_class;
    GString *str;
    MMModemCapability it;
    gboolean first = TRUE;

    str = g_string_new ("");
    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_MODEM_CAPABILITY));

    for (it = MM_MODEM_CAPABILITY_POTS; /* first */
         it <= MM_MODEM_CAPABILITY_LTE_ADVANCED; /* last */
         it = it << 1) {
        if (caps & it) {
            GFlagsValue *value;

            value = g_flags_get_first_value (flags_class, it);
            g_string_append_printf (str, "%s%s",
                                    first ? "" : ", ",
                                    value->value_nick);

            if (first)
                first = FALSE;
        }
    }
    g_type_class_unref (flags_class);

    return g_string_free (str, FALSE);
}

static MMModemCapability
load_current_capabilities_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    GVariant *result;
    MMModemCapability caps;
    gchar *caps_str;

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return MM_MODEM_CAPABILITY_NONE;

    caps = (MMModemCapability)g_variant_get_uint32 (result);
    caps_str = create_capabilities_string (caps);
    mm_dbg ("loaded current capabilities: %s", caps_str);
    g_free (caps_str);

    g_variant_unref (result);
    return caps;
}

static const MMAtCommand capabilities[] = {
    { "+GCAP",  2, (MMAtResponseProcessor)parse_caps_gcap },
    { "I",      1, (MMAtResponseProcessor)parse_caps_gcap },
    { "+CPIN?", 1, (MMAtResponseProcessor)parse_caps_cpin },
    { "+CGMM",  1, (MMAtResponseProcessor)parse_caps_cgmm },
    { NULL }
};

static void
load_current_capabilities (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    mm_dbg ("loading current capabilities...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)capabilities,
                    NULL, /* response_processor_context */
                    FALSE,
                    "u",
                    NULL, /* TODO: cancellable */
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

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return NULL;

    manufacturer = g_variant_dup_string (result, NULL);
    mm_dbg ("loaded manufacturer: %s", manufacturer);
    g_variant_unref (result);
    return manufacturer;
}

static const MMAtCommand manufacturers[] = {
    { "+CGMI",  3, (MMAtResponseProcessor)common_parse_string_reply },
    { "+GMI",   3, (MMAtResponseProcessor)common_parse_string_reply },
    { NULL }
};

static void
load_manufacturer (MMIfaceModem *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    mm_dbg ("loading manufacturer...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)manufacturers,
                    NULL, /* response_processor_context */
                    FALSE,
                    "s",
                    NULL, /* TODO: cancellable */
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

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return NULL;

    model = g_variant_dup_string (result, NULL);
    mm_dbg ("loaded model: %s", model);
    g_variant_unref (result);
    return model;
}

static const MMAtCommand models[] = {
    { "+CGMM",  3, (MMAtResponseProcessor)common_parse_string_reply },
    { "+GMM",   3, (MMAtResponseProcessor)common_parse_string_reply },
    { NULL }
};

static void
load_model (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    mm_dbg ("loading model...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)models,
                    NULL, /* response_processor_context */
                    FALSE,
                    "s",
                    NULL, /* TODO: cancellable */
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

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return NULL;

    revision = g_variant_dup_string (result, NULL);
    mm_dbg ("loaded revision: %s", revision);
    g_variant_unref (result);
    return revision;
}

static const MMAtCommand revisions[] = {
    { "+CGMR",  3, (MMAtResponseProcessor)common_parse_string_reply },
    { "+GMR",   3, (MMAtResponseProcessor)common_parse_string_reply },
    { NULL }
};

static void
load_revision (MMIfaceModem *self,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    mm_dbg ("loading revision...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)revisions,
                    NULL, /* response_processor_context */
                    FALSE,
                    "s",
                    NULL, /* TODO: cancellable */
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

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return NULL;

    equipment_identifier = g_variant_dup_string (result, NULL);
    mm_dbg ("loaded equipment identifier: %s", equipment_identifier);
    g_variant_unref (result);
    return equipment_identifier;
}

static const MMAtCommand equipment_identifiers[] = {
    { "+CGSN",  3, (MMAtResponseProcessor)common_parse_string_reply },
    { "+GSN",   3, (MMAtResponseProcessor)common_parse_string_reply },
    { NULL }
};

static void
load_equipment_identifier (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    mm_dbg ("loading equipment identifier...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)equipment_identifiers,
                    NULL, /* response_processor_context */
                    FALSE,
                    "s",
                    NULL, /* TODO: cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* DEVICE IDENTIFIER */

static gboolean
parse_optional_string_reply (MMBroadbandModem *self,
                             gpointer none,
                             const gchar *command,
                             const gchar *response,
                             const GError *error,
                             GVariant **result,
                             GError **result_error)
{
    *result = (error ?
               NULL :
               g_variant_new_string (response));
    return TRUE;
}

typedef struct {
    gchar *ati;
    gchar *ati1;
    GSimpleAsyncResult *result;
} DeviceIdentifierContext;

static void
device_identifier_context_free (DeviceIdentifierContext *ctx)
{
    g_object_unref (ctx->result);
    g_free (ctx->ati);
    g_free (ctx->ati1);
    g_free (ctx);
}

static gchar *
load_device_identifier_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    gchar *device_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    device_identifier = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("loaded device identifier: %s", device_identifier);
    return device_identifier;
}

static void
ati1_ready (MMBroadbandModem *self,
            GAsyncResult *res,
            DeviceIdentifierContext *ctx)
{
    gchar *device_identifier;
    GVariant *result;

    result = mm_at_command_finish (G_OBJECT (self), res, NULL);
    if (result) {
        ctx->ati1 = g_variant_dup_string (result, NULL);
        g_variant_unref (result);
    }

    device_identifier = mm_create_device_identifier (
        mm_base_modem_get_vendor_id (MM_BASE_MODEM (self)),
        mm_base_modem_get_product_id (MM_BASE_MODEM (self)),
        ctx->ati,
        ctx->ati1,
        mm_gdbus_modem_get_equipment_identifier (MM_GDBUS_MODEM (self->priv->modem_dbus_skeleton)),
        mm_gdbus_modem_get_revision (MM_GDBUS_MODEM (self->priv->modem_dbus_skeleton)),
        mm_gdbus_modem_get_model (MM_GDBUS_MODEM (self->priv->modem_dbus_skeleton)),
        mm_gdbus_modem_get_manufacturer (MM_GDBUS_MODEM (self->priv->modem_dbus_skeleton)));

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               device_identifier,
                                               NULL);
    g_simple_async_result_complete (ctx->result);
    device_identifier_context_free (ctx);
}

static void
ati_ready (MMBroadbandModem *self,
           GAsyncResult *res,
           DeviceIdentifierContext *ctx)
{
    GVariant *result;

    result = mm_at_command_finish (G_OBJECT (self), res, NULL);
    if (result) {
        ctx->ati = g_variant_dup_string (result, NULL);
        g_variant_unref (result);
    }

    /* Go on with ATI1 */
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "ATI1",
                   3,
                   (MMAtResponseProcessor)parse_optional_string_reply,
                   NULL, /* response_processor_context */
                   "s",
                   NULL, /* TODO: cancellable */
                   (GAsyncReadyCallback)ati1_ready,
                   ctx);
}

static void
load_device_identifier (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    DeviceIdentifierContext *ctx;

    mm_dbg ("loading device identifier...");

    /* To build the device identifier, we still need to get the replies for:
     *  - ATI
     *  - ATI1
     */

    ctx = g_new0 (DeviceIdentifierContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             load_device_identifier);

    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "ATI",
                   3,
                   (MMAtResponseProcessor)parse_optional_string_reply,
                   NULL, /* response_processor_context */
                   "s",
                   NULL, /* TODO: cancellable */
                   (GAsyncReadyCallback)ati_ready,
                   ctx);
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

static gboolean
parse_unlock_required_reply (MMBroadbandModem *self,
                             gpointer none,
                             const gchar *command,
                             const gchar *response,
                             const GError *error,
                             GVariant **result,
                             GError **result_error)
{
    if (error) {
        /* Let errors here be fatal */
        *result_error = g_error_copy (error);
        return FALSE;
    }

    if (response &&
        strstr (response, "+CPIN: ")) {
        CPinResult *iter = &unlock_results[0];
        const gchar *str;

        str = strstr (response, "+CPIN: ") + 7;

        /* Some phones (Motorola EZX models) seem to quote the response */
        if (str[0] == '"')
            str++;

        /* Translate the reply */
        while (iter->result) {
            if (g_str_has_prefix (str, iter->result)) {
                *result = g_variant_new_uint32 (iter->code);
                return TRUE;
            }
            iter++;
        }
    }

    /* Assume unlocked if we don't recognize the pin request result */
    *result = g_variant_new_uint32 (MM_MODEM_LOCK_NONE);
    return TRUE;
}

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
load_unlock_required_ready (MMBroadbandModem *self,
                            GAsyncResult *res,
                            GSimpleAsyncResult *unlock_required_result)
{
    GError *error = NULL;
    GVariant *command_result;

    command_result = mm_at_command_finish (G_OBJECT (self), res, &error);
    if (!command_result) {
        g_assert (error);
        g_simple_async_result_take_error (unlock_required_result, error);
    }
    else {
        g_simple_async_result_set_op_res_gpointer (unlock_required_result,
                                                   GUINT_TO_POINTER (g_variant_get_uint32 (command_result)),
                                                   NULL);
        g_variant_unref (command_result);
    }

    g_simple_async_result_complete (unlock_required_result);
    g_object_unref (unlock_required_result);
}

static void
load_unlock_required (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    mm_dbg ("checking if unlock required...");

    result  = g_simple_async_result_new (G_OBJECT (self),
                                         callback,
                                         user_data,
                                         load_unlock_required);
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "+CPIN?",
                   3,
                   (MMAtResponseProcessor)parse_unlock_required_reply,
                   NULL, /* response_processor_context */
                   "u",
                   NULL, /* TODO: cancellable */
                   (GAsyncReadyCallback)load_unlock_required_ready,
                   result);
}

/*****************************************************************************/

typedef struct {
    GSimpleAsyncResult *result;
    MMModemCharset charset;
    gboolean tried_without_quotes;
} ModemCharsetContext;

static void
modem_charset_context_free (ModemCharsetContext *ctx)
{
    g_object_unref (ctx->result);
    g_free (ctx);
}

static gboolean
modem_charset_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    return TRUE;
}

static gboolean
parse_modem_charset_reply (MMBroadbandModem *self,
                           gpointer none,
                           const gchar *command,
                           const gchar *response,
                           const GError *error,
                           GVariant **result,
                           GError **result_error)
{
    if (error) {
        *result_error = g_error_copy (error);
        return FALSE;
    }
    return TRUE;
}

static gboolean
parse_current_charset_reply (MMBroadbandModem *self,
                             gpointer none,
                             const gchar *command,
                             const gchar *response,
                             const GError *error,
                             GVariant **result,
                             GError **result_error)
{
    const gchar *p;
    MMModemCharset current;

    if (error) {
        *result_error = g_error_copy (error);
        return FALSE;
    }

    p = response;
    if (g_str_has_prefix (p, "+CSCS:"))
        p += 6;
    while (*p == ' ')
        p++;

    current = mm_modem_charset_from_string (p);
    *result = g_variant_new_uint32 (current);
    return TRUE;
}

static void
current_charset_ready (MMBroadbandModem *self,
                       GAsyncResult *result,
                       ModemCharsetContext *ctx)
{
    GError *error = NULL;
    GVariant *reply;
    MMModemCharset current;

    reply = mm_at_command_finish (G_OBJECT (self), result, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        g_assert (reply != NULL);
        current = g_variant_get_uint32 (reply);
        if (ctx->charset != current)
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Modem failed to change character set to %s",
                                             mm_modem_charset_to_string (ctx->charset));
        else {
            /* We'll keep track ourselves of the current charset */
            self->priv->current_charset = current;
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        }
    }

    g_simple_async_result_complete (ctx->result);
    modem_charset_context_free (ctx);
}

static void
modem_charset_ready (MMBroadbandModem *self,
                     GAsyncResult *result,
                     ModemCharsetContext *ctx)
{
    GError *error = NULL;

    mm_at_command_finish (G_OBJECT (self), result, &error);
    if (error) {
        if (!ctx->tried_without_quotes) {
            gchar *command;

            g_error_free (error);
            ctx->tried_without_quotes = TRUE;

            /* Some modems puke if you include the quotes around the character
             * set name, so lets try it again without them.
             */
            command = g_strdup_printf ("+CSCS=%s",
                                       mm_modem_charset_to_string (ctx->charset));
            mm_at_command (G_OBJECT (self),
                           mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                           command,
                           3,
                           (MMAtResponseProcessor)parse_modem_charset_reply,
                           NULL,  /* response processor context */
                           NULL,  /* reply signature */
                           NULL,  /* cancellable */
                           (GAsyncReadyCallback)modem_charset_ready,
                           ctx);
            g_free (command);
            return;
        }

        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete (ctx->result);
        modem_charset_context_free (ctx);
        return;
    }

    /* Check whether we did properly set the charset */
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "+CSCS?",
                   3,
                   (MMAtResponseProcessor)parse_current_charset_reply,
                   NULL,  /* response processor context */
                   "u",
                   NULL,  /* cancellable */
                   (GAsyncReadyCallback)current_charset_ready,
                   ctx);
}

static void
modem_charset (MMIfaceModem *self,
               MMModemCharset charset,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    ModemCharsetContext *ctx;
    const gchar *charset_str;
    gchar *command;

    ctx = g_new0 (ModemCharsetContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_charset);
    ctx->charset = charset;

    charset_str = mm_modem_charset_to_string (charset);
    if (!charset_str) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Unhandled character set 0x%X",
                                         charset);
        g_simple_async_result_complete_in_idle (ctx->result);
        modem_charset_context_free (ctx);
        return;
    }

    command = g_strdup_printf ("+CSCS=\"%s\"", charset_str);
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   command,
                   3,
                   (MMAtResponseProcessor)parse_modem_charset_reply,
                   NULL,  /* response processor context */
                   NULL,  /* reply signature */
                   NULL,  /* cancellable */
                   (GAsyncReadyCallback)modem_charset_ready,
                   ctx);
    g_free (command);
}

/*****************************************************************************/

static MMModemCharset
load_supported_charsets_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CHARSET_UNKNOWN;

    return (MMModemCharset) g_variant_get_uint32 (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static gboolean
parse_load_supported_charsets_reply (MMBroadbandModem *self,
                                     gpointer none,
                                     const gchar *command,
                                     const gchar *response,
                                     const GError *error,
                                     GVariant **result,
                                     GError **result_error)
{
    MMModemCharset charsets;

    if (error) {
        *result_error = g_error_copy (error);
        return FALSE;
    }

    charsets = MM_MODEM_CHARSET_UNKNOWN;
    if (!mm_gsm_parse_cscs_support_response (response, &charsets)) {
        *result_error = g_error_new_literal (MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Failed to parse the supported character "
                                             "sets response");
        return FALSE;
    }

    *result = g_variant_new_uint32 (charsets);
    return TRUE;
}

static void
load_supported_charsets (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "+CSCS=?",
                   3,
                   (MMAtResponseProcessor)parse_load_supported_charsets_reply,
                   NULL,  /* response processor context */
                   "u",
                   NULL,  /* cancellable */
                   callback,
                   user_data);
}

/*****************************************************************************/

static gboolean
modem_flow_control_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !mm_at_sequence_finish (G_OBJECT (self), res, error);
}

static void
modem_flow_control (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    /* By default, try to set XOFF/XON flow control, and ignore errors */
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "+IFC=1,1",
                   3,
                   NULL,  /* response processor */
                   NULL,  /* response processor context */
                   NULL,  /* result signature */
                   NULL,  /* cancellable */
                   callback,
                   user_data);
}

/*****************************************************************************/

static gboolean
modem_power_up_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    return !mm_at_command_finish (G_OBJECT (self), res, error);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    /* By default, errors in the power up command are ignored.
     * Plugins wanting to treat power up errors should subclass the power up
     * handling. */
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "+CFUN=1",
                   5,
                   NULL,  /* response processor */
                   NULL,  /* response processor context */
                   NULL,  /* result signature */
                   NULL,  /* cancellable */
                   callback,
                   user_data);
}

/*****************************************************************************/

static gboolean
modem_init_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !mm_at_sequence_finish (G_OBJECT (self), res, error);
}

static gboolean
parse_init_response (MMBroadbandModem *self,
                     gpointer none,
                     const gchar *command,
                     const gchar *response,
                     const GError *error,
                     GVariant **variant,
                     GError **result_error)
{
    /* Errors in the mandatory init commands will abort the whole modem
     * initialization process */
    if (error)
        *result_error = g_error_copy (error);

    /* Return FALSE so that we keep on with the next steps in the sequence */
    return FALSE;
}

static const MMAtCommand modem_init_sequence[] = {
    /* Send the init command twice; some devices (Nokia N900) appear to take a
     * few commands before responding correctly.  Instead of penalizing them for
     * being stupid the first time by failing to enable the device, just
     * try again.

     * TODO: only send init command 2nd time if 1st time failed?
     */
    { "Z E0 V1", 3, NULL },
    { "Z E0 V1", 3, (MMAtResponseProcessor)parse_init_response },

    /* Ensure echo is off after the init command; some modems ignore the
     * E0 when it's in the same line as ATZ (Option GIO322).
     */
    { "E0",      3, NULL },

    /* Some phones (like Blackberries) don't support +CMEE=1, so make it
     * optional.  It completely violates 3GPP TS 27.007 (9.1) but what can we do...
     */
    { "+CMEE=1", 3, NULL },

    /* Additional OPTIONAL initialization */
    { "X4 &C1",  3, NULL },

    { NULL }
};

static void
modem_init (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)modem_init_sequence,
                    NULL,  /* response processor context */
                    FALSE, /* free sequence */
                    NULL,  /* result signature */
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
    GVariant *result;
    gchar *imei;

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return NULL;

    imei = g_variant_dup_string (result, NULL);
    mm_dbg ("loaded IMEI: %s", imei);
    g_variant_unref (result);
    return imei;
}

static const MMAtCommand imei_commands[] = {
    { "+CGSN",  3, (MMAtResponseProcessor)common_parse_string_reply },
    { NULL }
};

static void
load_imei (MMIfaceModem3gpp *self,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    mm_dbg ("loading IMEI...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)imei_commands,
                    NULL, /* response_processor_context */
                    FALSE,
                    "s",
                    NULL, /* TODO: cancellable */
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
    GVariant *result;
    gchar *operator_code;

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return NULL;

    operator_code = mm_3gpp_parse_operator (g_variant_get_string (result, NULL),
                                            MM_MODEM_CHARSET_UNKNOWN);
    if (operator_code)
        mm_dbg ("loaded Operator Code: %s", operator_code);

    g_variant_unref (result);
    return operator_code;
}

static const MMAtCommand operator_code_commands[] = {
    { "+COPS=3,2;+COPS?",  3, (MMAtResponseProcessor)common_parse_string_reply },
    { NULL }
};

static void
load_operator_code (MMIfaceModem3gpp *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_dbg ("loading Operator Code...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)operator_code_commands,
                    NULL, /* response_processor_context */
                    FALSE,
                    "s",
                    NULL, /* TODO: cancellable */
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
    GVariant *result;
    gchar *operator_name;

    result = mm_at_sequence_finish (G_OBJECT (self), res, error);
    if (!result)
        return NULL;

    operator_name = mm_3gpp_parse_operator (g_variant_get_string (result, NULL),
                                            MM_MODEM_CHARSET_UNKNOWN);
    if (operator_name)
        mm_dbg ("loaded Operator Name: %s", operator_name);

    g_variant_unref (result);
    return operator_name;
}

static const MMAtCommand operator_name_commands[] = {
    { "+COPS=3,0;+COPS?",  3, (MMAtResponseProcessor)common_parse_string_reply },
    { NULL }
};

static void
load_operator_name (MMIfaceModem3gpp *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_dbg ("loading Operator Name...");
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)operator_name_commands,
                    NULL, /* response_processor_context */
                    FALSE,
                    "s",
                    NULL, /* TODO: cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* Unsolicited registration messages handling (3GPP) */

static void clear_previous_registration_request (MMBroadbandModem *self,
                                                 gboolean complete_with_cancel);

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
    MMModemAccessTech act = MM_MODEM_ACCESS_TECH_UNKNOWN;
    gboolean cgreg = FALSE;
    GError *error = NULL;

    if (!mm_gsm_parse_creg_response (match_info,
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
    mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                   get_consolidated_reg_state (self));

    /* If registration is finished (either registered or failed) but the
     * registration query hasn't completed yet, just remove the timeout and
     * let the registration query complete by itself.
     */
    clear_previous_registration_request (self, FALSE);

    /* TODO: report LAC/CI location */
    /* update_lac_ci (self, lac, cell_id, cgreg ? 1 : 0); */

    /* Report access technology, if available */
    /* Only update access technology if it appeared in the CREG/CGREG response */
    /* if (act != -1) */
    /*     mm_generic_gsm_update_access_technology (self, etsi_act_to_mm_act (act)); */
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
    array = mm_gsm_creg_regex_get (FALSE);
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
    mm_gsm_creg_regex_destroy (array);

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Register in network (3GPP) */

static void run_all_registration_checks_ready (MMBroadbandModem *self,
                                               GAsyncResult *res,
                                               GSimpleAsyncResult *operation_result);

static gboolean
register_in_network_finish (MMIfaceModem3gpp *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#define REG_IS_IDLE(state)                                  \
    (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||      \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING || \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)

#define REG_IS_DONE(state)                                  \
    (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||      \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING ||   \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED)

static void
clear_previous_registration_request (MMBroadbandModem *self,
                                     gboolean complete_with_cancel)
{

    if (self->priv->pending_reg_id) {
        /* Clear the registration timeout handler */
        g_source_remove (self->priv->pending_reg_id);
        self->priv->pending_reg_id = 0;
    }

    if (self->priv->pending_reg_request) {
        if (complete_with_cancel) {
            g_simple_async_result_set_error (self->priv->pending_reg_request,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_CANCELLED,
                                             "New registration request to be processed");
            g_simple_async_result_complete_in_idle (self->priv->pending_reg_request);
        }
        g_object_unref (self->priv->pending_reg_request);
        self->priv->pending_reg_request = NULL;
    }
}

static gboolean
register_in_network_timed_out (MMBroadbandModem *self)
{
    g_assert (self->priv->pending_reg_request != NULL);

    /* Report IDLE registration state */
    mm_iface_modem_3gpp_update_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                   MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);

    g_simple_async_result_take_error (
        self->priv->pending_reg_request,
        mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT));
    g_simple_async_result_complete (self->priv->pending_reg_request);
    g_object_unref (self->priv->pending_reg_request);
    self->priv->pending_reg_request = NULL;
    self->priv->pending_reg_id = 0;
    return FALSE;
}

static gboolean
run_all_registration_checks_again (GSimpleAsyncResult *operation_result)
{
    MMBroadbandModem *self;

    self = MM_BROADBAND_MODEM (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));

    /* If the registration timed out (and thus pending_reg_info will be NULL)
     * and the modem eventually got around to sending the response for the
     * registration request then just ignore the response since the callback is
     * already called.
     */
    if (!self->priv->pending_reg_request)
        g_object_unref (operation_result);
    else
        /* Get fresh registration state */
        mm_iface_modem_3gpp_run_all_registration_checks (
            MM_IFACE_MODEM_3GPP (self),
            (GAsyncReadyCallback)run_all_registration_checks_ready,
            operation_result);
    g_object_unref (self);
    return FALSE;
}

static void
run_all_registration_checks_ready (MMBroadbandModem *self,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (MM_IFACE_MODEM_3GPP (self),
                                                            res,
                                                            &error);

    /* If the registration timed out (and thus pending_reg_info will be NULL)
     * and the modem eventually got around to sending the response for the
     * registration request then just ignore the response since the callback is
     * already called.
     */
    if (!self->priv->pending_reg_request) {
        g_object_unref (operation_result);
        g_clear_error (&error);
        return;
    }

    if (error)
        g_simple_async_result_take_error (operation_result, error);
    else if (REG_IS_DONE (self->priv->modem_3gpp_registration_state))
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    else {
        /* If we're still waiting for automatic registration to complete or
         * fail, check again in a few seconds.
         */
        g_timeout_add_seconds (1,
                               (GSourceFunc)run_all_registration_checks_again,
                               operation_result);
        return;
    }

    g_simple_async_result_complete (operation_result);
    clear_previous_registration_request (self, FALSE);
    g_object_unref (operation_result);
}

static void
register_in_network_ready (MMBroadbandModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_at_command_finish (G_OBJECT (self), res, &error);

    /* If the registration timed out (and thus pending_reg_info will be NULL)
     * and the modem eventually got around to sending the response for the
     * registration request then just ignore the response since the callback is
     * already called.
     */
    if (!self->priv->pending_reg_request) {
        g_object_unref (operation_result);
        g_clear_error (&error);
        return;
    }

    if (error) {
        /* Propagate error in COPS, if any */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        clear_previous_registration_request (self, FALSE);
        g_object_unref (operation_result);
        return;
    }

    /* Get fresh registration state */
    mm_iface_modem_3gpp_run_all_registration_checks (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)run_all_registration_checks_ready,
        operation_result);
}

static void
register_in_network (MMIfaceModem3gpp *self,
                     const gchar *network_id,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModem *broadband = MM_BROADBAND_MODEM (self);
    GSimpleAsyncResult *result;
    gchar *command = NULL;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        register_in_network);

    /* Cleanup any previous registration, completing it with a cancelled error */
    clear_previous_registration_request (broadband, TRUE);

    /* Setup timeout */
    broadband->priv->pending_reg_id =
        g_timeout_add_seconds (60,
                               (GSourceFunc)register_in_network_timed_out,
                               self);
    broadband->priv->pending_reg_request = g_object_ref (result);

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
    } else
        mm_dbg ("Not launching any new network selection request");

    if (command) {
        mm_at_command (G_OBJECT (self),
                       mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                       command,
                       120,
                       (MMAtResponseProcessor)common_parse_no_reply,
                       NULL, /* response processor context */
                       NULL, /* reply signature */
                       NULL, /* cancellable */
                       (GAsyncReadyCallback)register_in_network_ready,
                       result);
    } else
        /* Just rely on the unsolicited registration, periodic registration
         * checks or the timeout. */
        g_object_unref (result);
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
    GVariant *response_variant;
    const gchar *response = NULL;
    GError *error = NULL;

    response_variant = mm_at_command_finish (G_OBJECT (self), res, &error);
    if (response_variant)
        response = g_variant_get_string (response_variant, NULL);

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
            MMModemAccessTech act = MM_MODEM_ACCESS_TECH_UNKNOWN;
            gulong lac = 0;
            gulong cid = 0;

            parsed = mm_gsm_parse_creg_response (match_info,
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
                                                               get_consolidated_reg_state (self));

                /* TODO: report LAC/CI location */
                /* TODO: report access technology, if available */

                g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
            }
        }
    }

    if (response_variant)
        g_variant_unref (response_variant);
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
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "+CREG?",
                   10,
                   (MMAtResponseProcessor)common_parse_string_reply,
                   NULL, /* response processor context */
                   "s",  /* raw reply */
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
    mm_at_command (G_OBJECT (self),
                   mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                   "+CGREG?",
                   10,
                   (MMAtResponseProcessor)common_parse_string_reply,
                   NULL, /* response processor context */
                   "s",  /* raw reply */
                   NULL, /* cancellable */
                   (GAsyncReadyCallback)registration_status_check_ready,
                   result);
}

/*****************************************************************************/
/* CS and PS Registrations (3GPP) */

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
parse_reg_setup_reply (MMBroadbandModem *self,
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

    *result = g_variant_new_string (command);
    return TRUE;
}

static const MMAtCommand cs_registration_sequence[] = {
    /* Enable unsolicited registration notifications in CS network, with location */
    { "+CREG=2", 3, (MMAtResponseProcessor)parse_reg_setup_reply },
    /* Enable unsolicited registration notifications in CS network, without location */
    { "+CREG=1", 3, (MMAtResponseProcessor)parse_reg_setup_reply },
    { NULL }
};

static const MMAtCommand ps_registration_sequence[] = {
    /* Enable unsolicited registration notifications in PS network, with location */
    { "+CGREG=2", 3, (MMAtResponseProcessor)parse_reg_setup_reply },
    /* Enable unsolicited registration notifications in PS network, without location */
    { "+CGREG=1", 3, (MMAtResponseProcessor)parse_reg_setup_reply },
    { NULL }
};

static void
setup_registration_sequence_ready (MMBroadbandModem *self,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *operation_result)
{
    MMAtSerialPort *secondary;
    GError *error = NULL;
    GVariant *reply;

    reply = mm_at_sequence_finish (G_OBJECT (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    secondary = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));
    if (secondary) {
        /* Now use the same registration setup in secondary port, if any */
        mm_at_command (G_OBJECT (self),
                       secondary,
                       g_variant_get_string (reply, NULL),
                       3,
                       NULL, /* response processor */
                       NULL, /* response processor context */
                       NULL, /* result signature */
                       NULL, /* cancellable */
                       NULL, /* NO callback, just queue the command and forget */
                       NULL); /* user data */
    }

    /* We're done */
    g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
    g_variant_unref (reply);
}

static void
setup_cs_registration (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        setup_cs_registration);
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)cs_registration_sequence,
                    NULL,  /* response processor context */
                    FALSE, /* free sequence */
                    "s",   /* The command which worked */
                    NULL,  /* cancellable */
                    (GAsyncReadyCallback)setup_registration_sequence_ready,
                    result);
}

static void
setup_ps_registration (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        setup_ps_registration);
    mm_at_sequence (G_OBJECT (self),
                    mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                    (MMAtCommand *)ps_registration_sequence,
                    NULL,  /* response processor context */
                    FALSE, /* free sequence */
                    "s",   /* The command which worked */
                    NULL,  /* cancellable */
                    (GAsyncReadyCallback)setup_registration_sequence_ready,
                    result);
}

/*****************************************************************************/

static gboolean
disable_finish (MMBaseModem *self,
                GAsyncResult *res,
                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    return TRUE;
}

static void
iface_modem_disable_ready (MMBroadbandModem *self,
                           GAsyncResult *result,
                           GAsyncResult *disable_result)
{
    GError *error = NULL;

    if (!mm_iface_modem_disable_finish (MM_IFACE_MODEM (self),
                                        result,
                                        &error))
        g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (disable_result), error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (disable_result), TRUE);

    g_simple_async_result_complete (G_SIMPLE_ASYNC_RESULT (disable_result));
    g_object_unref (disable_result);
}

static void
disable (MMBaseModem *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    GSimpleAsyncResult *res;

    res = g_simple_async_result_new (G_OBJECT (self),
                                     callback,
                                     user_data,
                                     disable);

    /* Disable the Modem interface */
    mm_iface_modem_disable (MM_IFACE_MODEM (self),
                            (GAsyncReadyCallback)iface_modem_disable_ready,
                            res);
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
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enable);
    ctx->step = ENABLING_STEP_FIRST;

    enabling_step (ctx);
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
        }                                                               \
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

        /* Open and send first commands to the serial port */
        if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
            g_simple_async_result_take_error (ctx->result, error);
            initialize_context_complete_and_free (ctx);
            return;
        }
        ctx->close_port = TRUE;
        /* Try to disable echo */
        mm_at_serial_port_queue_command (ctx->port, "E0", 3, NULL, NULL);
        /* Try to get extended errors */
        mm_at_serial_port_queue_command (ctx->port, "+CMEE=1", 2, NULL, NULL);
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
        if (ctx->self->priv->modem_current_capabilities & MM_MODEM_CAPABILITY_3GPP) {
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
    case PROP_MODEM_SIM:
        self->priv->modem_sim = g_value_dup_object (value);
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
    case PROP_MODEM_SIM:
        g_value_set_object (value, self->priv->modem_sim);
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
    self->priv->reg_regex = mm_gsm_creg_regex_get (TRUE);
    self->priv->reg_cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->reg_ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->current_charset = MM_MODEM_CHARSET_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);

    if (self->priv->reg_regex)
        mm_gsm_creg_regex_destroy (self->priv->reg_regex);

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

    if (self->priv->modem_sim)
        g_clear_object (&self->priv->modem_sim);

    G_OBJECT_CLASS (mm_broadband_modem_parent_class)->dispose (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
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

    iface->modem_init = modem_init;
    iface->modem_init_finish = modem_init_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->modem_flow_control = modem_flow_control;
    iface->modem_flow_control_finish = modem_flow_control_finish;
    iface->load_supported_charsets = load_supported_charsets;
    iface->load_supported_charsets_finish = load_supported_charsets_finish;
    iface->modem_charset = modem_charset;
    iface->modem_charset_finish = modem_charset_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface->load_imei = load_imei;
    iface->load_imei_finish = load_imei_finish;
    iface->load_operator_code = load_operator_code;
    iface->load_operator_code_finish = load_operator_code_finish;
    iface->load_operator_name = load_operator_name;
    iface->load_operator_name_finish = load_operator_name_finish;

    iface->setup_unsolicited_registration = setup_unsolicited_registration;
    iface->setup_unsolicited_registration_finish = setup_unsolicited_registration_finish;
    iface->setup_cs_registration = setup_cs_registration;
    iface->setup_cs_registration_finish = setup_cs_registration_finish;
    iface->run_cs_registration_check = run_cs_registration_check;
    iface->run_cs_registration_check_finish = run_cs_registration_check_finish;
    iface->setup_ps_registration = setup_ps_registration;
    iface->setup_ps_registration_finish = setup_ps_registration_finish;
    iface->run_ps_registration_check = run_ps_registration_check;
    iface->run_ps_registration_check_finish = run_ps_registration_check_finish;
    iface->register_in_network = register_in_network;
    iface->register_in_network_finish = register_in_network_finish;
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
                                      PROP_MODEM_SIM,
                                      MM_IFACE_MODEM_SIM);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_STATE,
                                      MM_IFACE_MODEM_STATE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CURRENT_CAPABILITIES,
                                      MM_IFACE_MODEM_CURRENT_CAPABILITIES);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_3GPP_REGISTRATION_STATE,
                                      MM_IFACE_MODEM_3GPP_REGISTRATION_STATE);
}
