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
#include "mm-errors.h"
#include "mm-log.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModem, mm_broadband_modem, MM_TYPE_BASE_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

enum {
    PROP_0,
    PROP_MODEM_DBUS_SKELETON,
    PROP_MODEM_SIM,
    PROP_MODEM_STATE,
    PROP_LAST
};

struct _MMBroadbandModemPrivate {
    GObject *modem_dbus_skeleton;
    MMSim *modem_sim;
    MMModemState modem_state;
};

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
load_modem_capabilities_finish (MMIfaceModem *self,
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
    mm_dbg ("loaded modem capabilities: %s", caps_str);
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
load_modem_capabilities (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    mm_dbg ("loading modem capabilities...");
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

    g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    g_simple_async_result_complete_in_idle (G_SIMPLE_ASYNC_RESULT (res));
    g_object_unref (res);
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

/*****************************************************************************/

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

    g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    g_simple_async_result_complete_in_idle (G_SIMPLE_ASYNC_RESULT (res));
    g_object_unref (res);
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

/*****************************************************************************/

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
iface_modem_initialize_ready (MMBroadbandModem *self,
                              GAsyncResult *result,
                              GAsyncResult *initialize_result)
{
    GError *error = NULL;

    if (!mm_iface_modem_initialize_finish (MM_IFACE_MODEM (self),
                                           result,
                                           &error))
        g_simple_async_result_take_error (G_SIMPLE_ASYNC_RESULT (initialize_result), error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (initialize_result), TRUE);

    g_simple_async_result_complete (G_SIMPLE_ASYNC_RESULT (initialize_result));
    g_object_unref (initialize_result);
}

static void
initialize (MMBaseModem *self,
            GCancellable *cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GSimpleAsyncResult *res;

    res = g_simple_async_result_new (G_OBJECT (self),
                                     callback,
                                     user_data,
                                     initialize);

    /* Initialize the Modem interface */
    mm_iface_modem_initialize (MM_IFACE_MODEM (self),
                               (GAsyncReadyCallback)iface_modem_initialize_ready,
                               res);
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
}

static void
dispose (GObject *object)
{
    MMBroadbandModem *self = MM_BROADBAND_MODEM (object);
    GError *error = NULL;

    if (self->priv->modem_dbus_skeleton) {
        if (!mm_iface_modem_shutdown (MM_IFACE_MODEM (object), &error)) {
            /* TODO: Cancel initialization */
            mm_warn ("couldn't shutdown interface: '%s'",
                     error ? error->message : "unknown error");
            g_clear_error (&error);
        }

        g_clear_object (&self->priv->modem_dbus_skeleton);
    }

    if (self->priv->modem_sim)
        g_clear_object (&self->priv->modem_sim);

    G_OBJECT_CLASS (mm_broadband_modem_parent_class)->dispose (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_modem_capabilities = load_modem_capabilities;
    iface->load_modem_capabilities_finish = load_modem_capabilities_finish;
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
}
