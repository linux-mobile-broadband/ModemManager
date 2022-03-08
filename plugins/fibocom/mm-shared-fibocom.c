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
 * Copyright (C) 2022 Fibocom Wireless Inc.
 */

#include <config.h>
#include <arpa/inet.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-broadband-modem.h"
#include "mm-broadband-modem-mbim.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-shared-fibocom.h"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-intel-private-tag"
static GQuark private_quark;

typedef struct {
    /* 3GPP interface support */
    MMIfaceModem3gpp *iface_modem_3gpp_parent;
} Private;

static void
private_free (Private *priv)
{
    g_slice_free (Private, priv);
}

static Private *
get_private (MMSharedFibocom *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);

        /* Setup parent class' MMIfaceModem3gpp */
        g_assert (MM_SHARED_FIBOCOM_GET_INTERFACE (self)->peek_parent_3gpp_interface);
        priv->iface_modem_3gpp_parent = MM_SHARED_FIBOCOM_GET_INTERFACE (self)->peek_parent_3gpp_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/

typedef struct {
    MMBearerProperties *config;
    gboolean            initial_eps_off_on;
} SetInitialEpsBearerSettingsContext;

static void
set_initial_eps_bearer_settings_context_free (SetInitialEpsBearerSettingsContext *ctx)
{
    g_clear_object (&ctx->config);
    g_slice_free (SetInitialEpsBearerSettingsContext, ctx);
}

gboolean
mm_shared_fibocom_set_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                          GAsyncResult      *res,
                                                          GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
after_attach_apn_modem_power_up_ready (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_power_state_finish (self, res, &error)) {
        mm_obj_warn (self, "failed to power up modem after attach APN settings update: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "success toggling modem power up after attach APN");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_set_initial_eps_bearer_settings_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult     *res,
                                              GTask            *task)
{
    SetInitialEpsBearerSettingsContext *ctx;
    Private                            *priv;
    GError                             *error = NULL;

    ctx = g_task_get_task_data (task);
    priv = get_private (MM_SHARED_FIBOCOM (self));

    if (!priv->iface_modem_3gpp_parent->set_initial_eps_bearer_settings_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (ctx->initial_eps_off_on) {
        mm_obj_dbg (self, "toggle modem power up after attach APN");
        mm_iface_modem_set_power_state (MM_IFACE_MODEM (self),
                                        MM_MODEM_POWER_STATE_ON,
                                        (GAsyncReadyCallback) after_attach_apn_modem_power_up_ready,
                                        task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_set_initial_eps_bearer_settings (GTask *task)
{
    MMSharedFibocom                    *self;
    SetInitialEpsBearerSettingsContext *ctx;
    Private                            *priv;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);
    priv = get_private (self);

    g_assert (priv->iface_modem_3gpp_parent);
    g_assert (priv->iface_modem_3gpp_parent->set_initial_eps_bearer_settings);
    g_assert (priv->iface_modem_3gpp_parent->set_initial_eps_bearer_settings_finish);

    priv->iface_modem_3gpp_parent->set_initial_eps_bearer_settings (MM_IFACE_MODEM_3GPP (self),
                                                                    ctx->config,
                                                                    (GAsyncReadyCallback)parent_set_initial_eps_bearer_settings_ready,
                                                                    task);
}

static void
before_attach_apn_modem_power_down_ready (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GTask        *task)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_power_state_finish (self, res, &error)) {
        mm_obj_warn (self, "failed to power down modem before attach APN settings update: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    mm_obj_dbg (self, "success toggling modem power down before attach APN");

    parent_set_initial_eps_bearer_settings (task);
}

void
mm_shared_fibocom_set_initial_eps_bearer_settings (MMIfaceModem3gpp    *self,
                                                   MMBearerProperties  *config,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data)
{
    SetInitialEpsBearerSettingsContext *ctx;
    GTask                              *task;
    MMPortMbim                         *port;

    task = g_task_new (self, NULL, callback, user_data);

    /* This shared logic is only expected in MBIM capable devices */
    g_assert (MM_IS_BROADBAND_MODEM_MBIM (self));
    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "No valid MBIM port found");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetInitialEpsBearerSettingsContext);
    ctx->config = g_object_ref (config);
    ctx->initial_eps_off_on = mm_kernel_device_get_property_as_boolean (mm_port_peek_kernel_device (MM_PORT (port)), "ID_MM_FIBOCOM_INITIAL_EPS_OFF_ON");
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_initial_eps_bearer_settings_context_free);

    if (ctx->initial_eps_off_on) {
        mm_obj_dbg (self, "toggle modem power down before attach APN");
        mm_iface_modem_set_power_state (MM_IFACE_MODEM (self),
                                        MM_MODEM_POWER_STATE_LOW,
                                        (GAsyncReadyCallback) before_attach_apn_modem_power_down_ready,
                                        task);
        return;
    }

    parent_set_initial_eps_bearer_settings (task);
}

/*****************************************************************************/

static void
shared_fibocom_init (gpointer g_iface)
{
}

GType
mm_shared_fibocom_get_type (void)
{
    static GType shared_fibocom_type = 0;

    if (!G_UNLIKELY (shared_fibocom_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedFibocom),  /* class_size */
            shared_fibocom_init,       /* base_init */
            NULL,                      /* base_finalize */
        };

        shared_fibocom_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedFibocom", &info, 0);
        g_type_interface_add_prerequisite (shared_fibocom_type, MM_TYPE_IFACE_MODEM);
        g_type_interface_add_prerequisite (shared_fibocom_type, MM_TYPE_IFACE_MODEM_3GPP);
    }

    return shared_fibocom_type;
}
