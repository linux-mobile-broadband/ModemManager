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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "mm-broadband-modem-mbim.h"
#include "mm-bearer-mbim.h"
#include "mm-sim-mbim.h"

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbim, mm_broadband_modem_mbim, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

struct _MMBroadbandModemMbimPrivate {
    /* Queried and cached capabilities */
    MbimCellularClass caps_cellular_class;
    MbimDataClass caps_data_class;
    MbimSmsCaps caps_sms;
    guint caps_max_sessions;
    gchar *caps_device_id;
    gchar *caps_firmware_info;
};

/*****************************************************************************/
/* Current Capabilities loading (Modem interface) */

typedef struct {
    MMBroadbandModemMbim *self;
    GSimpleAsyncResult *result;
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

    caps = ((MMModemCapability) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (
                    G_SIMPLE_ASYNC_RESULT (res))));
    caps_str = mm_modem_capability_build_string_from_mask (caps);
    mm_dbg ("loaded modem capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static void
device_caps_query_ready (MbimDevice *device,
                         GAsyncResult *res,
                         LoadCapabilitiesContext *ctx)
{
    MMModemCapability mask;
    MbimMessage *response;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (!response) {
        g_simple_async_result_take_error (ctx->result, error);
        load_capabilities_context_complete_and_free (ctx);
        return;
    }

    /* Gather and store the results we want */
    ctx->self->priv->caps_cellular_class = mbim_message_basic_connect_device_caps_query_response_get_cellular_class (response);
    ctx->self->priv->caps_data_class = mbim_message_basic_connect_device_caps_query_response_get_data_class (response);
    ctx->self->priv->caps_sms = mbim_message_basic_connect_device_caps_query_response_get_sms_caps (response);
    ctx->self->priv->caps_max_sessions = mbim_message_basic_connect_device_caps_query_response_get_max_sessions (response);
    ctx->self->priv->caps_device_id = mbim_message_basic_connect_device_caps_query_response_get_device_id (response);
    ctx->self->priv->caps_firmware_info = mbim_message_basic_connect_device_caps_query_response_get_firmware_info (response);
    mbim_message_unref (response);

    /* Build mask of modem capabilities */
    mask = 0;
    if (ctx->self->priv->caps_cellular_class & MBIM_CELLULAR_CLASS_GSM)
        mask |= MM_MODEM_CAPABILITY_GSM_UMTS;
    if (ctx->self->priv->caps_cellular_class & MBIM_CELLULAR_CLASS_CDMA)
        mask |= MM_MODEM_CAPABILITY_CDMA_EVDO;
    if (ctx->self->priv->caps_data_class & MBIM_DATA_CLASS_LTE)
        mask |= MM_MODEM_CAPABILITY_LTE;


    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (mask),
                                               NULL);
    load_capabilities_context_complete_and_free (ctx);
}

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    LoadCapabilitiesContext *ctx;
    MMMbimPort *port;
    MbimMessage *message;

    ctx = g_slice_new (LoadCapabilitiesContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_current_capabilities);

    mm_dbg ("loading current capabilities...");
    port = mm_base_modem_peek_port_mbim (MM_BASE_MODEM (self));
    message = (mbim_message_basic_connect_device_caps_query_request_new (
                   mm_mbim_port_get_next_transaction_id (port)));
    mbim_device_command (mm_mbim_port_peek_device (port),
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)device_caps_query_ready,
                         ctx);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Revision loading (Modem interface) */

static gchar *
modem_load_revision_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->caps_firmware_info)
        return g_strdup (MM_BROADBAND_MODEM_MBIM (self)->priv->caps_firmware_info);

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Firmware revision information not given in device capabilities");
    return NULL;
}

static void
modem_load_revision (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Just complete */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_revision);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Equipment Identifier loading (Modem interface) */

static gchar *
modem_load_equipment_identifier_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->caps_device_id)
        return g_strdup (MM_BROADBAND_MODEM_MBIM (self)->priv->caps_device_id);

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Device ID not given in device capabilities");
    return NULL;
}

static void
modem_load_equipment_identifier (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Just complete */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_equipment_identifier);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New bearer created at DBus path '%s'", mm_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBearer *bearer;
    GSimpleAsyncResult *result;

    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    /* We just create a MMBearerMbim */
    mm_dbg ("Creating MBIM bearer in MBIM modem");
    bearer = mm_bearer_mbim_new (MM_BROADBAND_MODEM_MBIM (self),
                                 properties);

    g_simple_async_result_set_op_res_gpointer (result, bearer, g_object_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_mbim_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New MBIM SIM */
    mm_sim_mbim_new (MM_BASE_MODEM (self),
                    NULL, /* cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* First enabling step */

static gboolean
enabling_started_finish (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_enabling_started_ready (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->enabling_started_finish (
            self,
            res,
            &error)) {
        /* Don't treat this as fatal. Parent enabling may fail if it cannot grab a primary
         * AT port, which isn't really an issue in MBIM-based modems */
        mm_dbg ("Couldn't start parent enabling: %s", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
enabling_started (MMBroadbandModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        enabling_started);
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->enabling_started (
        self,
        (GAsyncReadyCallback)parent_enabling_started_ready,
        result);
}

/*****************************************************************************/
/* First initialization step */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMMbimPort *mbim;
} InitializationStartedContext;

static void
initialization_started_context_complete_and_free (InitializationStartedContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->mbim)
        g_object_unref (ctx->mbim);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (InitializationStartedContext, ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* Just parent's pointer passed here */
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     InitializationStartedContext *ctx)
{
    gpointer parent_ctx;
    GError *error = NULL;

    parent_ctx = MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->initialization_started_finish (
        self,
        res,
        &error);
    if (error) {
        /* Don't treat this as fatal. Parent initialization may fail if it cannot grab a primary
         * AT port, which isn't really an issue in MBIM-based modems */
        mm_dbg ("Couldn't start parent initialization: %s", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gpointer (ctx->result, parent_ctx, NULL);
    initialization_started_context_complete_and_free (ctx);
}

static void
parent_initialization_started (InitializationStartedContext *ctx)
{
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->initialization_started (
        ctx->self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        ctx);
}

static void
mbim_port_open_ready (MMMbimPort *mbim,
                      GAsyncResult *res,
                      InitializationStartedContext *ctx)
{
    GError *error = NULL;

    if (!mm_mbim_port_open_finish (mbim, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        initialization_started_context_complete_and_free (ctx);
        return;
    }

    /* Done we are, launch parent's callback */
    parent_initialization_started (ctx);
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    InitializationStartedContext *ctx;

    ctx = g_slice_new0 (InitializationStartedContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_started);
    ctx->mbim = mm_base_modem_get_port_mbim (MM_BASE_MODEM (self));

    /* This may happen if we unplug the modem unexpectedly */
    if (!ctx->mbim) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Cannot initialize: MBIM port went missing");
        initialization_started_context_complete_and_free (ctx);
        return;
    }

    if (mm_mbim_port_is_open (ctx->mbim)) {
        /* Nothing to be done, just launch parent's callback */
        parent_initialization_started (ctx);
        return;
    }

    /* Now open our MBIM port */
    mm_mbim_port_open (ctx->mbim,
                       NULL,
                       (GAsyncReadyCallback)mbim_port_open_ready,
                       ctx);
}

/*****************************************************************************/

MMBroadbandModemMbim *
mm_broadband_modem_mbim_new (const gchar *device,
                             const gchar **drivers,
                             const gchar *plugin,
                             guint16 vendor_id,
                             guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_mbim_init (MMBroadbandModemMbim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_MBIM,
                                              MMBroadbandModemMbimPrivate);
}

static void
finalize (GObject *object)
{
    MMMbimPort *mbim;
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (object);

    g_free (self->priv->caps_device_id);
    g_free (self->priv->caps_firmware_info);

    mbim = mm_base_modem_peek_port_mbim (MM_BASE_MODEM (self));
    /* If we did open the MBIM port during initialization, close it now */
    if (mbim &&
        mm_mbim_port_is_open (mbim)) {
        mm_mbim_port_close (mbim);
    }

    G_OBJECT_CLASS (mm_broadband_modem_mbim_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Initialization steps */
    iface->load_current_capabilities = modem_load_current_capabilities;
    iface->load_current_capabilities_finish = modem_load_current_capabilities_finish;
    iface->load_modem_capabilities = NULL;
    iface->load_modem_capabilities_finish = NULL;
    iface->load_manufacturer = NULL; /* TODO */
    iface->load_manufacturer_finish = NULL;
    iface->load_model = NULL; /* TODO */
    iface->load_model_finish = NULL;
    iface->load_revision = modem_load_revision;
    iface->load_revision_finish = modem_load_revision_finish;
    iface->load_equipment_identifier = modem_load_equipment_identifier;
    iface->load_equipment_identifier_finish = modem_load_equipment_identifier_finish;

    /* Create MBIM-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* Create MBIM-specific bearer */
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

static void
mm_broadband_modem_mbim_class_init (MMBroadbandModemMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMbimPrivate));

    object_class->finalize = finalize;

    broadband_modem_class->initialization_started = initialization_started;
    broadband_modem_class->initialization_started_finish = initialization_started_finish;
    broadband_modem_class->enabling_started = enabling_started;
    broadband_modem_class->enabling_started_finish = enabling_started_finish;
    /* Do not initialize the MBIM modem through AT commands */
    broadband_modem_class->enabling_modem_init = NULL;
    broadband_modem_class->enabling_modem_init_finish = NULL;
}
