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
 * Copyright (C) 2016 Trimble Navigation Limited
 * Author: Matthew Stanger <matthew_stanger@trimble.com>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <ModemManager.h>
#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-cinterion.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-cinterion.h"
#include "mm-daemon-enums-types.h"

G_DEFINE_TYPE (MMBroadbandBearerCinterion, mm_broadband_bearer_cinterion, MM_TYPE_BROADBAND_BEARER)

/*****************************************************************************/
/* WWAN interface mapping */

typedef struct {
    guint swwan_index;
    guint usb_iface_num;
} UsbInterfaceConfig;

/* Map SWWAN index, USB interface number and preferred PDP context.
 *
 * The expected USB interface mapping is:
 *   INTERFACE=usb0 -> ID_USB_INTERFACE_NUM=0a
 *   INTERFACE=usb1 -> ID_USB_INTERFACE_NUM=0c
 */
static const UsbInterfaceConfig usb_interface_configs[] = {
    {
        .swwan_index   = 1,
        .usb_iface_num = 0x0a,
    },
    {
        .swwan_index   = 2,
        .usb_iface_num = 0x0c,
    },
};

static gint
get_usb_interface_config_index (MMPort  *data,
                                GError **error)
{
    guint usb_iface_num;
    guint i;

    usb_iface_num = mm_kernel_device_get_property_as_int_hex (mm_port_peek_kernel_device (data), "ID_USB_INTERFACE_NUM");

    for (i = 0; i < G_N_ELEMENTS (usb_interface_configs); i++) {
        if (usb_interface_configs[i].usb_iface_num == usb_iface_num)
            return (gint) i;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Unsupported WWAN interface: unexpected interface number: 0x%02x", usb_iface_num);
    return -1;
}

/*****************************************************************************/
/* Connection status loading
 * NOTE: only CONNECTED or DISCONNECTED should be reported here.
 */

static MMBearerConnectionStatus
load_connection_status_finish (MMBaseBearer  *bearer,
                               GAsyncResult  *res,
                               GError       **error)
{
    GError *inner_error = NULL;
    gssize aux;

    aux = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_BEARER_CONNECTION_STATUS_UNKNOWN;
    }
    return (MMBearerConnectionStatus) aux;
}

static void
swwan_check_status_ready (MMBaseModem  *modem,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandBearerCinterion *self;
    const gchar                *response;
    GError                     *error = NULL;
    MMBearerConnectionStatus    status;
    guint                       cid;

    self = g_task_get_source_object (task);
    cid = GPOINTER_TO_UINT (g_task_get_task_data (task));

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        goto out;
    }

    status = mm_cinterion_parse_swwan_response (response, cid, self, &error);
    if (status == MM_BEARER_CONNECTION_STATUS_UNKNOWN) {
        g_task_return_error (task, error);
        goto out;
    }

    g_assert (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED ||
              status == MM_BEARER_CONNECTION_STATUS_CONNECTED);
    g_task_return_int (task, (gssize) status);

out:
    g_object_unref (task);
}

static void
load_connection_status_by_cid (MMBroadbandBearerCinterion *bearer,
                               guint                       cid,
                               GAsyncReadyCallback         callback,
                               gpointer                    user_data)
{
    GTask       *task;
    MMBaseModem *modem;

    task = g_task_new (bearer, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (cid), NULL);

    g_object_get (bearer,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);

    mm_base_modem_at_command (modem,
                              "^SWWAN?",
                              5,
                              FALSE,
                              (GAsyncReadyCallback) swwan_check_status_ready,
                              task);
    g_object_unref (modem);
}

static void
load_connection_status (MMBaseBearer        *bearer,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    load_connection_status_by_cid (MM_BROADBAND_BEARER_CINTERION (bearer),
                                   mm_broadband_bearer_get_3gpp_cid (MM_BROADBAND_BEARER (bearer)),
                                   callback,
                                   user_data);
}

/******************************************************************************/
/* Dial 3GPP */

typedef enum {
    DIAL_3GPP_CONTEXT_STEP_FIRST = 0,
    DIAL_3GPP_CONTEXT_STEP_AUTH,
    DIAL_3GPP_CONTEXT_STEP_START_SWWAN,
    DIAL_3GPP_CONTEXT_STEP_VALIDATE_CONNECTION,
    DIAL_3GPP_CONTEXT_STEP_LAST,
} Dial3gppContextStep;

typedef struct {
    MMBroadbandBearerCinterion *self;
    MMBaseModem                *modem;
    MMPortSerialAt             *primary;
    guint                       cid;
    MMPort                     *data;
    gint                        usb_interface_config_index;
    Dial3gppContextStep         step;
} Dial3gppContext;

static void
dial_3gpp_context_free (Dial3gppContext *ctx)
{
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_object_unref (ctx->primary);
    g_clear_object (&ctx->data);
    g_slice_free (Dial3gppContext, ctx);
}

static MMPort *
dial_3gpp_finish (MMBroadbandBearer  *self,
                  GAsyncResult       *res,
                  GError            **error)
{
    return MM_PORT (g_task_propagate_pointer (G_TASK (res), error));
}

static void dial_3gpp_context_step (GTask *task);

static void
dial_connection_status_ready (MMBroadbandBearerCinterion *self,
                              GAsyncResult               *res,
                              GTask                      *task)
{
    MMBearerConnectionStatus  status;
    Dial3gppContext          *ctx;
    GError                   *error = NULL;

    ctx = (Dial3gppContext *) g_task_get_task_data (task);

    status = load_connection_status_finish (MM_BASE_BEARER (self), res, &error);
    if (status == MM_BEARER_CONNECTION_STATUS_UNKNOWN) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "CID %u is reported disconnected", ctx->cid);
        g_object_unref (task);
        return;
    }

    g_assert (status == MM_BEARER_CONNECTION_STATUS_CONNECTED);

    /* Go to next step */
    ctx->step++;
    dial_3gpp_context_step (task);
}

static void
common_dial_operation_ready (MMBaseModem  *modem,
                             GAsyncResult *res,
                             GTask        *task)
{
    Dial3gppContext *ctx;
    GError          *error = NULL;

    ctx = (Dial3gppContext *) g_task_get_task_data (task);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go to next step */
    ctx->step++;
    dial_3gpp_context_step (task);
}

static void
handle_cancel_dial (GTask *task)
{
    Dial3gppContext *ctx;
    gchar           *command;

    ctx = (Dial3gppContext *) g_task_get_task_data (task);

    /* Disconnect, may not succeed. Will not check response on cancel */
    command = g_strdup_printf ("^SWWAN=0,%u,%u",
                               ctx->cid, usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   NULL,
                                   NULL);
    g_free (command);
}

static void
dial_3gpp_context_step (GTask *task)
{
    MMBroadbandBearerCinterion *self;
    Dial3gppContext            *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* Check for cancellation */
    if (g_task_return_error_if_cancelled (task)) {
        handle_cancel_dial (task);
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case DIAL_3GPP_CONTEXT_STEP_FIRST: {
        MMBearerIpFamily ip_family;

        ip_family = mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
        if (ip_family == MM_BEARER_IP_FAMILY_NONE || ip_family == MM_BEARER_IP_FAMILY_ANY) {
            gchar *ip_family_str;

            ip_family = mm_base_bearer_get_default_ip_family (MM_BASE_BEARER (ctx->self));
            ip_family_str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            mm_obj_dbg (self, "no specific IP family requested, defaulting to %s", ip_family_str);
            g_free (ip_family_str);
        }

        ctx->step++;
    } /* fall through */

    case DIAL_3GPP_CONTEXT_STEP_AUTH: {
        gchar *command;

        command = mm_cinterion_build_auth_string (self,
                                                  mm_broadband_modem_cinterion_get_family (MM_BROADBAND_MODEM_CINTERION (ctx->modem)),
                                                  mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)),
                                                  ctx->cid);

        if (command) {
            mm_obj_dbg (self, "dial step %u/%u: authenticating...", ctx->step, DIAL_3GPP_CONTEXT_STEP_LAST);
            /* Send SGAUTH write, if User & Pass are provided.
             * advance to next state by callback */
            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->primary,
                                           command,
                                           10,
                                           FALSE,
                                           FALSE,
                                           NULL,
                                           (GAsyncReadyCallback) common_dial_operation_ready,
                                           task);
            g_free (command);
            return;
        }

        mm_obj_dbg (self, "dial step %u/%u: authentication not required", ctx->step, DIAL_3GPP_CONTEXT_STEP_LAST);
        ctx->step++;
    } /* fall through */

    case DIAL_3GPP_CONTEXT_STEP_START_SWWAN: {
        gchar *command;

        mm_obj_dbg (self, "dial step %u/%u: starting SWWAN interface %u connection...",
                    ctx->step, DIAL_3GPP_CONTEXT_STEP_LAST, usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        command = g_strdup_printf ("^SWWAN=1,%u,%u",
                                   ctx->cid,
                                   usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback) common_dial_operation_ready,
                                       task);
        g_free (command);
        return;
    }

    case DIAL_3GPP_CONTEXT_STEP_VALIDATE_CONNECTION:
        mm_obj_dbg (self, "dial step %u/%u: checking SWWAN interface %u status...",
                    ctx->step, DIAL_3GPP_CONTEXT_STEP_LAST, usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        load_connection_status_by_cid (ctx->self,
                                       ctx->cid,
                                       (GAsyncReadyCallback) dial_connection_status_ready,
                                       task);
        return;

    case DIAL_3GPP_CONTEXT_STEP_LAST:
        mm_obj_dbg (self, "dial step %u/%u: finished", ctx->step, DIAL_3GPP_CONTEXT_STEP_LAST);
        g_task_return_pointer (task, g_object_ref (ctx->data), g_object_unref);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
dial_3gpp (MMBroadbandBearer   *self,
           MMBaseModem         *modem,
           MMPortSerialAt      *primary,
           guint                cid,
           GCancellable        *cancellable,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    GTask           *task;
    Dial3gppContext *ctx;
    GError          *error = NULL;

    g_assert (primary != NULL);

    /* Setup task and create connection context */
    task = g_task_new (self, cancellable, callback, user_data);
    ctx = g_slice_new0 (Dial3gppContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) dial_3gpp_context_free);

    /* Setup context */
    ctx->self    = g_object_ref (self);
    ctx->modem   = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid     = cid;
    ctx->step    = DIAL_3GPP_CONTEXT_STEP_FIRST;

    /* Get a net port to setup the connection on */
    ctx->data = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!ctx->data) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "No valid data port found to launch connection");
        g_object_unref (task);
        return;
    }
    g_object_ref (ctx->data);

    /* Validate configuration */
    ctx->usb_interface_config_index = get_usb_interface_config_index (ctx->data, &error);
    if (ctx->usb_interface_config_index < 0) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Run! */
    dial_3gpp_context_step (task);
}

/*****************************************************************************/
/* Disconnect 3GPP */

typedef enum {
    DISCONNECT_3GPP_CONTEXT_STEP_FIRST,
    DISCONNECT_3GPP_CONTEXT_STEP_STOP_SWWAN,
    DISCONNECT_3GPP_CONTEXT_STEP_CONNECTION_STATUS,
    DISCONNECT_3GPP_CONTEXT_STEP_LAST,
} Disconnect3gppContextStep;

typedef struct {
    MMBroadbandBearerCinterion *self;
    MMBaseModem                *modem;
    MMPortSerialAt             *primary;
    MMPort                     *data;
    guint                       cid;
    gint                        usb_interface_config_index;
    Disconnect3gppContextStep   step;
} Disconnect3gppContext;

static void
disconnect_3gpp_context_free (Disconnect3gppContext *ctx)
{
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_slice_free (Disconnect3gppContext, ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer  *self,
                        GAsyncResult       *res,
                        GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void disconnect_3gpp_context_step (GTask *task);

static void
disconnect_connection_status_ready (MMBroadbandBearerCinterion *self,
                                    GAsyncResult               *res,
                                    GTask                      *task)
{
    MMBearerConnectionStatus  status;
    Disconnect3gppContext    *ctx;
    GError                   *error = NULL;

    ctx = (Disconnect3gppContext *) g_task_get_task_data (task);

    status = load_connection_status_finish (MM_BASE_BEARER (self), res, &error);
    switch (status) {
    case MM_BEARER_CONNECTION_STATUS_UNKNOWN:
        /* Assume disconnected */
        mm_obj_dbg (self, "couldn't get CID %u status, assume disconnected: %s", ctx->cid, error->message);
        g_clear_error (&error);
        break;
    case MM_BEARER_CONNECTION_STATUS_DISCONNECTED:
        break;
    case MM_BEARER_CONNECTION_STATUS_CONNECTED:
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "CID %u is reported connected", ctx->cid);
        g_object_unref (task);
        return;
    case MM_BEARER_CONNECTION_STATUS_DISCONNECTING:
    case MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED:
    default:
        g_assert_not_reached ();
    }

    /* Go on to next step */
    ctx->step++;
    disconnect_3gpp_context_step (task);
}

static void
swwan_disconnect_ready (MMBaseModem  *modem,
                        GAsyncResult *res,
                        GTask        *task)
{
    Disconnect3gppContext *ctx;

    ctx = (Disconnect3gppContext *) g_task_get_task_data (task);

    /* We don't bother to check error or response here since, ctx flow's
     * next step checks it */
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    /* Go on to next step */
    ctx->step++;
    disconnect_3gpp_context_step (task);
}

static void
disconnect_3gpp_context_step (GTask *task)
{
    MMBroadbandBearerCinterion *self;
    Disconnect3gppContext      *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISCONNECT_3GPP_CONTEXT_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISCONNECT_3GPP_CONTEXT_STEP_STOP_SWWAN: {
        gchar *command;

        command = g_strdup_printf ("^SWWAN=0,%u,%u",
                                   ctx->cid, usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        mm_obj_dbg (self, "disconnect step %u/%u: disconnecting PDP CID %u...",
                    ctx->step, DISCONNECT_3GPP_CONTEXT_STEP_LAST, ctx->cid);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback) swwan_disconnect_ready,
                                       task);
        g_free (command);
        return;
    }

    case DISCONNECT_3GPP_CONTEXT_STEP_CONNECTION_STATUS:
        mm_obj_dbg (self, "disconnect step %u/%u: checking SWWAN interface %u status...",
                    ctx->step, DISCONNECT_3GPP_CONTEXT_STEP_LAST,
                    usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        load_connection_status_by_cid (MM_BROADBAND_BEARER_CINTERION (ctx->self),
                                       ctx->cid,
                                       (GAsyncReadyCallback) disconnect_connection_status_ready,
                                       task);
         return;

    case DISCONNECT_3GPP_CONTEXT_STEP_LAST:
        mm_obj_dbg (self, "disconnect step %u/%u: finished",
                    ctx->step, DISCONNECT_3GPP_CONTEXT_STEP_LAST);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
disconnect_3gpp (MMBroadbandBearer  *self,
                 MMBroadbandModem   *modem,
                 MMPortSerialAt     *primary,
                 MMPortSerialAt     *secondary,
                 MMPort             *data,
                 guint               cid,
                 GAsyncReadyCallback callback,
                 gpointer            user_data)
{
    GTask                 *task;
    Disconnect3gppContext *ctx;
    GError                *error = NULL;

    g_assert (primary != NULL);
    g_assert (data != NULL);

    /* Setup task and create connection context */
    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (Disconnect3gppContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) disconnect_3gpp_context_free);

    /* Setup context */
    ctx->self    = g_object_ref (self);
    ctx->modem   = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->data    = g_object_ref (data);
    ctx->cid     = cid;
    ctx->step    = DISCONNECT_3GPP_CONTEXT_STEP_FIRST;

    /* Validate configuration */
    ctx->usb_interface_config_index = get_usb_interface_config_index (data, &error);
    if (ctx->usb_interface_config_index < 0) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Start */
    disconnect_3gpp_context_step (task);
}

/*****************************************************************************/
/* Setup and Init Bearers */

MMBaseBearer *
mm_broadband_bearer_cinterion_new_finish (GAsyncResult  *res,
                                          GError       **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

void
mm_broadband_bearer_cinterion_new (MMBroadbandModemCinterion *modem,
                                   MMBearerProperties        *config,
                                   GCancellable              *cancellable,
                                   GAsyncReadyCallback        callback,
                                   gpointer                   user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_CINTERION,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_cinterion_init (MMBroadbandBearerCinterion *self)
{
}

static void
mm_broadband_bearer_cinterion_class_init (MMBroadbandBearerCinterionClass *klass)
{
    MMBaseBearerClass      *base_bearer_class      = MM_BASE_BEARER_CLASS      (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    base_bearer_class->load_connection_status        = load_connection_status;
    base_bearer_class->load_connection_status_finish = load_connection_status_finish;

    broadband_bearer_class->dial_3gpp              = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish       = dial_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp        = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
