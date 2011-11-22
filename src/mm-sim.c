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
#include <ctype.h>

#include <ModemManager.h>

#include <mm-enums-types.h>
#include <mm-errors-types.h>
#include <mm-gdbus-sim.h>
#include <mm-marshal.h>

#include "mm-iface-modem.h"
#include "mm-at.h"
#include "mm-sim.h"
#include "mm-base-modem.h"
#include "mm-utils.h"
#include "mm-errors.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (InitAsyncContext *ctx);
static void async_initable_iface_init     (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMSim, mm_sim, MM_GDBUS_TYPE_SIM_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               async_initable_iface_init));

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMSimPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* The modem which owns this SIM */
    MMBaseModem *modem;
    /* The path where the SIM object is exported */
    gchar *path;
};

static void
mm_sim_export (MMSim *self)
{
    GError *error = NULL;

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_warn ("couldn't export SIM at '%s': '%s'",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
}

static void
mm_sim_unexport (MMSim *self)
{
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
}

/*****************************************************************************/

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMSim *self;
    InitializationStep step;
    guint sim_identifier_tries;
    MMAtSerialPort *port;
};

static void
init_async_context_free (InitAsyncContext *ctx,
                         gboolean close_port)
{
    if (close_port)
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

MMSim *
mm_sim_new_finish (GAsyncInitable *initable,
                   GAsyncResult  *res,
                   GError       **error)
{
    return MM_SIM (g_async_initable_new_finish (initable, res, error));
}

static gboolean
initable_init_finish (GAsyncInitable  *initable,
                      GAsyncResult    *result,
                      GError         **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
        return FALSE;

    return TRUE;
}

static void
interface_initialization_step (InitAsyncContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        break;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, TRUE);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}


static void
initable_init_async (GAsyncInitable *initable,
                     int io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    InitAsyncContext *ctx;
    GError *error = NULL;

    mm_gdbus_sim_set_sim_identifier (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_imsi (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_operator_identifier (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_operator_name (MM_GDBUS_SIM (initable), NULL);

    ctx = g_new (InitAsyncContext, 1);
    ctx->self = g_object_ref (initable);
    ctx->result = g_simple_async_result_new (G_OBJECT (initable),
                                             callback,
                                             user_data,
                                             initable_init_async);
    ctx->cancellable = (cancellable ?
                        g_object_ref (cancellable) :
                        NULL);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->sim_identifier_tries = 0;

    ctx->port = mm_base_modem_get_port_primary (ctx->self->priv->modem);
    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    interface_initialization_step (ctx);
}

void
mm_sim_new (MMBaseModem *modem,
            GCancellable *cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    gchar *path;
    static guint32 id = 0;

    /* Build the unique path for the SIM, and create the object */
    path = g_strdup_printf (MM_DBUS_PATH"/SIMs/%d", id++);
    g_async_initable_new_async (MM_TYPE_SIM,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_SIM_PATH,  path,
                                MM_SIM_MODEM, modem,
                                NULL);
    g_free (path);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMSim *self = MM_SIM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);
        break;
    case PROP_CONNECTION:
        if (self->priv->connection)
            g_object_unref (self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection */
        if (self->priv->connection)
            mm_sim_export (self);
        else
            mm_sim_unexport (self);
        break;
    case PROP_MODEM:
        if (self->priv->modem)
            g_object_unref (self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem) {
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the SIM's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_SIM_CONNECTION,
                                    G_BINDING_DEFAULT);
        }
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
    MMSim *self = MM_SIM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_sim_init (MMSim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SIM,
                                              MMSimPrivate);
}

static void
dispose (GObject *object)
{
    MMSim *self = MM_SIM (object);

    if (self->priv->connection)
        g_clear_object (&self->priv->connection);

    if (self->priv->modem)
        g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_sim_parent_class)->dispose (object);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    iface->init_async = initable_init_async;
    iface->init_finish = initable_init_finish;
}

static void
mm_sim_class_init (MMSimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSimPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose = dispose;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_SIM_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_SIM_PATH,
                             "Path",
                             "DBus path of the SIM",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_SIM_MODEM,
                             "Modem",
                             "The Modem which owns this SIM",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);
}

