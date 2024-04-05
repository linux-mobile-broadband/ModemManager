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
#include "mm-base-modem-at.h"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-intel-private-tag"
static GQuark private_quark;

typedef struct {
    /* Parent class */
    MMBaseModemClass *class_parent;
    /* 3GPP interface support */
    MMIfaceModem3gpp *iface_modem_3gpp_parent;
    /* URCs to ignore */
    GRegex *sim_ready_regex;
} Private;

static void
private_free (Private *priv)
{
    g_regex_unref (priv->sim_ready_regex);
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

        priv->sim_ready_regex = g_regex_new ("\\r\\n\\+SIM READY\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

        /* Setup parent class */
        g_assert (MM_SHARED_FIBOCOM_GET_INTERFACE (self)->peek_parent_class);
        priv->class_parent = MM_SHARED_FIBOCOM_GET_INTERFACE (self)->peek_parent_class (self);

        /* Setup parent class' MMIfaceModem3gpp */
        g_assert (MM_SHARED_FIBOCOM_GET_INTERFACE (self)->peek_parent_3gpp_interface);
        priv->iface_modem_3gpp_parent = MM_SHARED_FIBOCOM_GET_INTERFACE (self)->peek_parent_3gpp_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/

void
mm_shared_fibocom_setup_ports (MMBroadbandModem *self)
{
    MMPortSerialAt *ports[2];
    guint           i;
    Private        *priv;

    mm_obj_dbg (self, "setting up ports in fibocom modem...");

    priv = get_private (MM_SHARED_FIBOCOM (self));
    g_assert (priv->class_parent);
    g_assert (MM_BROADBAND_MODEM_CLASS (priv->class_parent)->setup_ports);

    /* Parent setup first always */
    MM_BROADBAND_MODEM_CLASS (priv->class_parent)->setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            priv->sim_ready_regex,
            NULL, NULL, NULL);
    }
}

/*****************************************************************************/

typedef struct {
    MMBearerProperties *config;
    gboolean            initial_eps_off_on;
    GError             *saved_error;
} SetInitialEpsBearerSettingsContext;

static void
set_initial_eps_bearer_settings_context_free (SetInitialEpsBearerSettingsContext *ctx)
{
    g_clear_error (&ctx->saved_error);
    g_clear_object (&ctx->config);
    g_slice_free (SetInitialEpsBearerSettingsContext, ctx);
}

static void
set_initial_eps_bearer_settings_complete (GTask *task)
{
    SetInitialEpsBearerSettingsContext *ctx;

    ctx = g_task_get_task_data (task);
    if (ctx->saved_error)
        g_task_return_error (task, g_steal_pointer (&ctx->saved_error));
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
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
    SetInitialEpsBearerSettingsContext *ctx;
    g_autoptr(GError)                   error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_iface_modem_set_power_state_finish (self, res, &error)) {
        mm_obj_warn (self, "failed to power up modem after attach APN settings update: %s", error->message);
        if (!ctx->saved_error)
            ctx->saved_error = g_steal_pointer (&error);
    } else
        mm_obj_dbg (self, "success toggling modem power up after attach APN");

    set_initial_eps_bearer_settings_complete (task);
}

static void
parent_set_initial_eps_bearer_settings_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult     *res,
                                              GTask            *task)
{
    SetInitialEpsBearerSettingsContext *ctx;
    Private                            *priv;

    ctx = g_task_get_task_data (task);
    priv = get_private (MM_SHARED_FIBOCOM (self));

    if (!priv->iface_modem_3gpp_parent->set_initial_eps_bearer_settings_finish (self, res, &ctx->saved_error))
        mm_obj_warn (self, "failed to update APN settings: %s", ctx->saved_error->message);

    if (ctx->initial_eps_off_on) {
        mm_obj_dbg (self, "toggle modem power up after attach APN");
        mm_iface_modem_set_power_state (MM_IFACE_MODEM (self),
                                        MM_MODEM_POWER_STATE_ON,
                                        (GAsyncReadyCallback) after_attach_apn_modem_power_up_ready,
                                        task);
        return;
    }

    set_initial_eps_bearer_settings_complete (task);
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

MMFirmwareUpdateSettings *
mm_shared_fibocom_firmware_load_update_settings_finish (MMIfaceModemFirmware  *self,
                                                        GAsyncResult          *res,
                                                        GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static gboolean
fibocom_is_fastboot_supported (MMBaseModem *modem,
                               MMPort      *port)
{
    return mm_kernel_device_get_global_property_as_boolean (mm_port_peek_kernel_device (port), "ID_MM_FIBOCOM_FASTBOOT");
}

static MMModemFirmwareUpdateMethod
fibocom_get_firmware_update_methods (MMBaseModem *modem,
                                     MMPort      *port)
{
    MMModemFirmwareUpdateMethod update_methods;

    update_methods = MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE;

    if (fibocom_is_fastboot_supported (modem, port))
        update_methods |= MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT;

    return update_methods;
}

static void
fibocom_at_port_get_firmware_version_ready (MMBaseModem  *self,
                                            GAsyncResult *res,
                                            GTask        *task)
{
    MMFirmwareUpdateSettings    *update_settings;
    MMModemFirmwareUpdateMethod  update_methods;
    const gchar                 *version_from_at;
    g_auto(GStrv)                version = NULL;
    g_autoptr(GPtrArray)         ids = NULL;
    GError                      *error = NULL;

    update_settings = g_task_get_task_data (task);
    update_methods = mm_firmware_update_settings_get_method (update_settings);

    /* Set device ids */
    ids = mm_iface_firmware_build_generic_device_ids (MM_IFACE_MODEM_FIRMWARE (self), &error);
    if (error) {
        mm_obj_warn (self, "failed to build generic device ids: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* The version get from the AT needs to be formatted
     *
     * version_from_at : AT+GTPKGVER: "19500.0000.00.01.02.80_6001.0000.007.000.075_A89"
     * version[1] : 19500.0000.00.01.02.80_6001.0000.007.000.075_A89
     */
    version_from_at = mm_base_modem_at_command_finish (self, res, NULL);
    if (version_from_at) {
        version = g_strsplit (version_from_at, "\"", -1);
        if (version && version[0] && version[1] && g_utf8_validate (version[1], -1, NULL)) {
            mm_firmware_update_settings_set_version (update_settings, version[1]);
        }
    }

    mm_firmware_update_settings_set_device_ids (update_settings, (const gchar **)ids->pdata);

    /* Set update methods */
    if (update_methods & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
        /* Fastboot AT */
        mm_firmware_update_settings_set_fastboot_at (update_settings, "AT+SYSCMD=\"sys_reboot bootloader\"");
    }

    g_task_return_pointer (task, g_object_ref (update_settings), g_object_unref);
    g_object_unref (task);
}

void
mm_shared_fibocom_firmware_load_update_settings (MMIfaceModemFirmware *self,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data)
{
    GTask *task;
    MMPortSerialAt *at_port;
    MMModemFirmwareUpdateMethod update_methods;
    MMFirmwareUpdateSettings *update_settings;

    task = g_task_new (self, NULL, callback, user_data);

    /* We always report the primary port as the one to be used for FW upgrade */
    at_port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (at_port) {
        update_methods = fibocom_get_firmware_update_methods (MM_BASE_MODEM (self), MM_PORT (at_port));
        update_settings = mm_firmware_update_settings_new (update_methods);
        g_task_set_task_data (task, update_settings, g_object_unref);

        /* Get modem version by AT */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+GTPKGVER?",
                                  3,
                                  TRUE,
                                  (GAsyncReadyCallback) fibocom_at_port_get_firmware_version_ready,
                                  task);

        return;
    }

    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Couldn't find a port to fetch firmware info");
    g_object_unref (task);
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
