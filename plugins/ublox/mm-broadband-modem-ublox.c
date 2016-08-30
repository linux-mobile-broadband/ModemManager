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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-ublox.h"
#include "mm-modem-helpers-ublox.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemUblox, mm_broadband_modem_ublox, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

struct _MMBroadbandModemUbloxPrivate {
    /* USB profile in use */
    MMUbloxUsbProfile profile;
    gboolean          profile_checked;
    /* Networking mode in use */
    MMUbloxNetworkingMode mode;
    gboolean              mode_checked;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

typedef enum {
    CREATE_BEARER_STEP_FIRST,
    CREATE_BEARER_STEP_CHECK_PROFILE,
    CREATE_BEARER_STEP_CHECK_MODE,
    CREATE_BEARER_STEP_CREATE_BEARER,
    CREATE_BEARER_STEP_LAST,
} CreateBearerStep;

typedef struct {
    MMBroadbandModemUblox *self;
    CreateBearerStep       step;
    MMBearerProperties    *properties;
    MMBaseBearer          *bearer;
} CreateBearerContext;

static void
create_bearer_context_free (CreateBearerContext *ctx)
{
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    g_object_unref (ctx->properties);
    g_object_unref (ctx->self);
    g_slice_free (CreateBearerContext, ctx);
}

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem  *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return MM_BASE_BEARER (g_task_propagate_pointer (G_TASK (res), error));
}

static void create_bearer_step (GTask *task);

static void
broadband_bearer_new_ready (GObject      *source,
                            GAsyncResult *res,
                            GTask        *task)
{
    CreateBearerContext *ctx;
    GError *error = NULL;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);

    g_assert (!ctx->bearer);
    ctx->bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!ctx->bearer) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_dbg ("u-blox: new generic broadband bearer created at DBus path '%s'", mm_base_bearer_get_path (ctx->bearer));
    ctx->step++;
    create_bearer_step (task);
}

static void
mode_check_ready (MMBaseModem  *self,
                  GAsyncResult *res,
                  GTask        *task)
{
    const gchar *response;
    GError *error = NULL;
    CreateBearerContext *ctx;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        mm_dbg ("u-blox: couldn't load current networking mode: %s", error->message);
        g_error_free (error);
    } else if (!mm_ublox_parse_ubmconf_response (response, &ctx->self->priv->mode, &error)) {
        mm_dbg ("u-blox: couldn't parse current networking mode response '%s': %s", response, error->message);
        g_error_free (error);
    } else {
        switch (ctx->self->priv->mode) {
        case MM_UBLOX_NETWORKING_MODE_ROUTER:
            mm_dbg ("u-blox: networking mode loaded: router");
            break;
        case MM_UBLOX_NETWORKING_MODE_BRIDGE:
            mm_dbg ("u-blox: networking mode loaded: bridge");
            break;
        default:
            g_assert_not_reached ();
        }
    }

    /* Assume the operation has been performed, even if it may have failed */
    ctx->self->priv->mode_checked = TRUE;

    ctx->step++;
    create_bearer_step (task);
}

static void
profile_check_ready (MMBaseModem  *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    const gchar *response;
    GError *error = NULL;
    CreateBearerContext *ctx;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        mm_dbg ("u-blox: couldn't load current usb profile: %s", error->message);
        g_error_free (error);
    } else if (!mm_ublox_parse_uusbconf_response (response, &ctx->self->priv->profile, &error)) {
        mm_dbg ("u-blox: couldn't parse current usb profile response '%s': %s", response, error->message);
        g_error_free (error);
    } else {
        switch (ctx->self->priv->profile) {
        case MM_UBLOX_USB_PROFILE_RNDIS:
            mm_dbg ("u-blox: usb profile loaded: high throughput");
            break;
        case MM_UBLOX_USB_PROFILE_ECM:
            mm_dbg ("u-blox: usb profile loaded: medium/low throughput");
            break;
        case MM_UBLOX_USB_PROFILE_BACK_COMPATIBLE:
            mm_dbg ("u-blox: usb profile loaded: back-compatible");
            break;
        default:
            g_assert_not_reached ();
        }
    }

    /* Assume the operation has been performed, even if it may have failed */
    ctx->self->priv->profile_checked = TRUE;

    ctx->step++;
    create_bearer_step (task);
}

static void
create_bearer_step (GTask *task)
{
    CreateBearerContext *ctx;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);
    switch (ctx->step) {
    case CREATE_BEARER_STEP_FIRST:
        ctx->step++;
        /* fall down */

    case CREATE_BEARER_STEP_CHECK_PROFILE:
        if (!ctx->self->priv->profile_checked) {
            mm_dbg ("u-blox: checking current USB profile...");
            mm_base_modem_at_command (
                MM_BASE_MODEM (ctx->self),
                "+UUSBCONF?",
                3,
                FALSE,
                (GAsyncReadyCallback) profile_check_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall down */

    case CREATE_BEARER_STEP_CHECK_MODE:
        if (!ctx->self->priv->mode_checked) {
            mm_dbg ("u-blox: checking current networking mode...");
            mm_base_modem_at_command (
                MM_BASE_MODEM (ctx->self),
                "+UBMCONF?",
                3,
                FALSE,
                (GAsyncReadyCallback) mode_check_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall down */

    case CREATE_BEARER_STEP_CREATE_BEARER:
        /* For now, we just create a MMBroadbandBearer */
        mm_dbg ("u-blox: creating generic broadband bearer in u-blox modem...");
        mm_broadband_bearer_new (MM_BROADBAND_MODEM (ctx->self),
                                 ctx->properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback) broadband_bearer_new_ready,
                                 task);
        return;

    case CREATE_BEARER_STEP_LAST:
        g_assert (ctx->bearer);
        g_task_return_pointer (task, g_object_ref (ctx->bearer), (GDestroyNotify) g_object_unref);
        g_object_unref (task);
        return;
    }

    g_assert_not_reached ();
}

static void
modem_create_bearer (MMIfaceModem        *self,
                     MMBearerProperties  *properties,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    CreateBearerContext *ctx;
    GTask               *task;

    ctx = g_slice_new0 (CreateBearerContext);
    ctx->step = CREATE_BEARER_STEP_FIRST;
    ctx->self = g_object_ref (self);
    ctx->properties = g_object_ref (properties);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) create_bearer_context_free);
    create_bearer_step (task);
}

/*****************************************************************************/

MMBroadbandModemUblox *
mm_broadband_modem_ublox_new (const gchar  *device,
                              const gchar **drivers,
                              const gchar  *plugin,
                              guint16       vendor_id,
                              guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_UBLOX,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_ublox_init (MMBroadbandModemUblox *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_UBLOX,
                                              MMBroadbandModemUbloxPrivate);
    self->priv->profile = MM_UBLOX_USB_PROFILE_UNKNOWN;
    self->priv->mode = MM_UBLOX_NETWORKING_MODE_UNKNOWN;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->create_bearer        = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

static void
mm_broadband_modem_ublox_class_init (MMBroadbandModemUbloxClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemUbloxPrivate));
}
