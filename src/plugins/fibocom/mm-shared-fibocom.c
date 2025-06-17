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
#include "mm-iface-modem.h"
#include "mm-shared-fibocom.h"
#include "mm-base-modem-at.h"
#if defined WITH_MBIM
# include "mm-port-mbim-fibocom.h"
#endif

G_DEFINE_INTERFACE (MMSharedFibocom, mm_shared_fibocom, MM_TYPE_IFACE_MODEM)

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-intel-private-tag"
static GQuark private_quark;

typedef struct {
    /* Parent class */
    MMBaseModemClass *class_parent;
    /* Modem interface of parent class */
    MMIfaceModemInterface *iface_modem_parent;
    /* Firmware interface of parent class */
    MMIfaceModemFirmwareInterface *iface_modem_firmware_parent;
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
        g_assert (MM_SHARED_FIBOCOM_GET_IFACE (self)->peek_parent_class);
        priv->class_parent = MM_SHARED_FIBOCOM_GET_IFACE (self)->peek_parent_class (self);

        /* Setup modem interface of parent class */
        if (MM_SHARED_FIBOCOM_GET_IFACE (self)->peek_parent_modem_interface)
            priv->iface_modem_parent = MM_SHARED_FIBOCOM_GET_IFACE (self)->peek_parent_modem_interface (self);

        /* Setup firmware interface of parent class */
        if (MM_SHARED_FIBOCOM_GET_IFACE (self)->peek_parent_firmware_interface)
            priv->iface_modem_firmware_parent = MM_SHARED_FIBOCOM_GET_IFACE (self)->peek_parent_firmware_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/

static void
sim_hotswap_unsolicited_handler (MMPortSerialAt *port,
                                 GMatchInfo     *match_info,
                                 MMIfaceModem   *self)
{
    mm_obj_dbg (self, "processing +SIM URC reporting a SIM hotswap event");
    mm_iface_modem_process_sim_event (MM_IFACE_MODEM (self));
}

/*****************************************************************************/
/* Setup SIM hot swap context (Modem interface) */

gboolean
mm_shared_fibocom_setup_sim_hot_swap_finish (MMIfaceModem  *self,
                                             GAsyncResult  *res,
                                             GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_setup_sim_hot_swap_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    Private           *priv;
    g_autoptr(GError)  error = NULL;

    priv = get_private (MM_SHARED_FIBOCOM (self));

    if (!priv->iface_modem_parent->setup_sim_hot_swap_finish (self, res, &error))
        mm_obj_dbg (self, "additional SIM hot swap detection setup failed: %s", error->message);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_fibocom_setup_sim_hot_swap (MMIfaceModem        *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    Private           *priv;
    MMPortSerialAt    *ports[2];
    GTask             *task;
    guint              i;
    g_autoptr(GRegex)  pattern = NULL;
    g_autoptr(GError)  error = NULL;

    priv = get_private (MM_SHARED_FIBOCOM (self));

    task = g_task_new (self, NULL, callback, user_data);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    pattern = g_regex_new ("(\\+SIM: Inserted)|(\\+SIM: Removed)|(\\+SIM DROP)\\r\\n", G_REGEX_RAW, 0, NULL);
    g_assert (pattern);

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (ports[i])
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                pattern,
                (MMPortSerialAtUnsolicitedMsgFn)sim_hotswap_unsolicited_handler,
                self,
                NULL);
    }

    mm_obj_dbg (self, "+SIM based hotswap detection set up");

    if (!mm_broadband_modem_sim_hot_swap_ports_context_init (MM_BROADBAND_MODEM (self), &error))
        mm_obj_warn (self, "failed to initialize SIM hot swap ports context: %s", error->message);

    /* Now, if available, setup parent logic */
    if (priv->iface_modem_parent->setup_sim_hot_swap &&
        priv->iface_modem_parent->setup_sim_hot_swap_finish) {
        priv->iface_modem_parent->setup_sim_hot_swap (self,
                                                      (GAsyncReadyCallback) parent_setup_sim_hot_swap_ready,
                                                      task);
        return;
    }

    /* Otherwise, we're done */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/

#if defined WITH_MBIM

MMPort *
mm_shared_fibocom_create_usbmisc_port (MMBaseModem *self,
                                       const gchar *name,
                                       MMPortType   ptype)
{
    Private *priv;

    priv = get_private (MM_SHARED_FIBOCOM (self));
    if (ptype == MM_PORT_TYPE_MBIM) {
        mm_obj_dbg (self, "creating fibocom-specific MBIM port...");
        return MM_PORT (mm_port_mbim_fibocom_new (name, MM_PORT_SUBSYS_USBMISC));
    }

    return priv->class_parent->create_usbmisc_port (self, name, ptype);
}

MMPort *
mm_shared_fibocom_create_wwan_port (MMBaseModem *self,
                                    const gchar *name,
                                    MMPortType   ptype)
{
    Private *priv;

    priv = get_private (MM_SHARED_FIBOCOM (self));
    if (ptype == MM_PORT_TYPE_MBIM) {
        mm_obj_dbg (self, "creating fibocom-specific MBIM port...");
        return MM_PORT (mm_port_mbim_fibocom_new (name, MM_PORT_SUBSYS_WWAN));
    }

    return priv->class_parent->create_wwan_port (self, name, ptype);
}

#endif /* WITH_MBIM */

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

static void
parent_load_update_settings_ready (MMIfaceModemFirmware *self,
                                   GAsyncResult         *res,
                                   GTask                *task)
{
    Private                             *priv;
    MMPortSerialAt                      *at_port;
    MMModemFirmwareUpdateMethod          update_methods;
    g_autoptr(GError)                    error = NULL;
    g_autoptr(MMFirmwareUpdateSettings)  update_settings;

    priv = get_private (MM_SHARED_FIBOCOM (self));
    update_settings = priv->iface_modem_firmware_parent->load_update_settings_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_set_task_data (task, g_object_ref (update_settings), g_object_unref);

    /* We always report the primary port as the one to be used for FW upgrade */
    at_port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (at_port) {
        update_methods = mm_firmware_update_settings_get_method (update_settings);

        /* Prefer any parent's update method */
        if (update_methods == MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
            update_methods = fibocom_get_firmware_update_methods (MM_BASE_MODEM (self), MM_PORT (at_port));
            mm_firmware_update_settings_set_method (update_settings, update_methods);
        }

        /* Get modem version by AT */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+GTPKGVER?",
                                  3,
                                  TRUE,
                                  (GAsyncReadyCallback) fibocom_at_port_get_firmware_version_ready,
                                  task);

        return;
    }

    g_task_return_pointer (task, g_object_ref (update_settings), g_object_unref);
    g_object_unref (task);
}

void
mm_shared_fibocom_firmware_load_update_settings (MMIfaceModemFirmware *self,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data)
{
    GTask   *task;
    Private *priv;

    priv = get_private (MM_SHARED_FIBOCOM (self));
    g_assert (priv->iface_modem_firmware_parent);
    g_assert (priv->iface_modem_firmware_parent->load_update_settings);
    g_assert (priv->iface_modem_firmware_parent->load_update_settings_finish);

    task = g_task_new (self, NULL, callback, user_data);

    priv->iface_modem_firmware_parent->load_update_settings (
        self,
        (GAsyncReadyCallback)parent_load_update_settings_ready,
        task);
}

/*****************************************************************************/

static void
mm_shared_fibocom_default_init (MMSharedFibocomInterface *iface)
{
}
