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
#include "mm-sim.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

#define MM_MODEM_CAPABILITY_3GPP        \
    (MM_MODEM_CAPABILITY_GSM_UMTS |     \
     MM_MODEM_CAPABILITY_LTE |          \
     MM_MODEM_CAPABILITY_LTE_ADVANCED)

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModem, mm_broadband_modem, MM_TYPE_BASE_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

enum {
    PROP_0,
    PROP_MODEM_DBUS_SKELETON,
    PROP_MODEM_SIM,
    PROP_MODEM_STATE,
    PROP_MODEM_CURRENT_CAPABILITIES,
    PROP_LAST
};

struct _MMBroadbandModemPrivate {
    GObject *modem_dbus_skeleton;
    MMSim *modem_sim;
    MMModemState modem_state;
    MMModemCapability modem_current_capabilities;
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
        else
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
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
iface_modem_enable_ready (MMBroadbandModem *self,
                          GAsyncResult *result,
                          GAsyncResult *enable_result)
{
    GError *error = NULL;

    if (!mm_iface_modem_enable_finish (MM_IFACE_MODEM (self),
                                       result,
                                       &error))
        g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (enable_result), error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (enable_result), TRUE);

    g_simple_async_result_complete (G_SIMPLE_ASYNC_RESULT (enable_result));
    g_object_unref (enable_result);
}

static void
enable (MMBaseModem *self,
        GCancellable *cancellable,
        GAsyncReadyCallback callback,
        gpointer user_data)
{
    GSimpleAsyncResult *res;

    res = g_simple_async_result_new (G_OBJECT (self),
                                     callback,
                                     user_data,
                                     enable);

    /* Enable the Modem interface */
    mm_iface_modem_enable (MM_IFACE_MODEM (self),
                           (GAsyncReadyCallback)iface_modem_enable_ready,
                           res);
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
            /* TODO: expose the 3GPP interfaces */
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
    case PROP_MODEM_SIM:
        self->priv->modem_sim = g_value_dup_object (value);
        break;
    case PROP_MODEM_STATE:
        self->priv->modem_state = g_value_get_enum (value);
        break;
    case PROP_MODEM_CURRENT_CAPABILITIES:
        self->priv->modem_current_capabilities = g_value_get_flags (value);
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
    case PROP_MODEM_SIM:
        g_value_set_object (value, self->priv->modem_sim);
        break;
    case PROP_MODEM_STATE:
        g_value_set_enum (value, self->priv->modem_state);
        break;
    case PROP_MODEM_CURRENT_CAPABILITIES:
        g_value_set_flags (value, self->priv->modem_current_capabilities);
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
mm_broadband_modem_class_init (MMBroadbandModemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseModemClass *base_modem_class = MM_BASE_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;

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
                                      PROP_MODEM_SIM,
                                      MM_IFACE_MODEM_SIM);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_STATE,
                                      MM_IFACE_MODEM_STATE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CURRENT_CAPABILITIES,
                                      MM_IFACE_MODEM_CURRENT_CAPABILITIES);
}
