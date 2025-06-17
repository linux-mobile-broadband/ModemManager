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
 * Copyright (C) 2022 Disruptive Technologies Research AS
 */

#include <config.h>

#include "mm-broadband-modem-fibocom.h"
#include "mm-broadband-bearer-fibocom-ecm.h"
#include "mm-broadband-modem.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-log.h"
#include "mm-shared-fibocom.h"

static void iface_modem_init          (MMIfaceModemInterface         *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface);
static void shared_fibocom_init       (MMSharedFibocomInterface      *iface);

static MMIfaceModemInterface         *iface_modem_parent;
static MMIfaceModemFirmwareInterface *iface_modem_firmware_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemFibocom, mm_broadband_modem_fibocom, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_FIBOCOM,  shared_fibocom_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init))

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED,
} FeatureSupport;

struct _MMBroadbandModemFibocomPrivate {
    FeatureSupport gtrndis_support;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_fibocom_ecm_new_ready (GObject *source,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError       *error = NULL;

    bearer = mm_broadband_bearer_fibocom_ecm_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError       *error = NULL;

    bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

static void
common_create_bearer (GTask *task)
{
    MMBroadbandModemFibocom *self;

    self = g_task_get_source_object (task);

    switch (self->priv->gtrndis_support) {
    case FEATURE_SUPPORTED:
        mm_obj_dbg (self, "+GTRNDIS supported, creating Fibocom ECM bearer");
        mm_broadband_bearer_fibocom_ecm_new (self,
                                             g_task_get_task_data (task),
                                             NULL, /* cancellable */
                                             (GAsyncReadyCallback) broadband_bearer_fibocom_ecm_new_ready,
                                             task);
        return;
    case FEATURE_NOT_SUPPORTED:
        mm_obj_dbg (self, "+GTRNDIS not supported, creating generic PPP bearer");
        mm_broadband_bearer_new (MM_BROADBAND_MODEM (self),
                                 g_task_get_task_data (task),
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback) broadband_bearer_new_ready,
                                 task);
        return;
    case FEATURE_SUPPORT_UNKNOWN:
    default:
        g_assert_not_reached ();
    }
}

static void
gtrndis_test_ready (MMBaseModem  *_self,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);

    if (!mm_base_modem_at_command_finish (_self, res, NULL)) {
        mm_obj_dbg (self, "+GTRNDIS unsupported");
        self->priv->gtrndis_support = FEATURE_NOT_SUPPORTED;
    } else {
        mm_obj_dbg (self, "+GTRNDIS supported");
        self->priv->gtrndis_support = FEATURE_SUPPORTED;
    }

    /* Go on and create the bearer */
    common_create_bearer (task);
}

static void
modem_create_bearer (MMIfaceModem       *_self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer            user_data)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);
    GTask                   *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, g_object_ref (properties), g_object_unref);

    if (self->priv->gtrndis_support != FEATURE_SUPPORT_UNKNOWN) {
        common_create_bearer (task);
        return;
    }

    if (!mm_base_modem_peek_best_data_port (MM_BASE_MODEM (self), MM_PORT_TYPE_NET)) {
        mm_obj_dbg (self, "skipping +GTRNDIS check as no data port is available");
        self->priv->gtrndis_support = FEATURE_NOT_SUPPORTED;
        common_create_bearer (task);
        return;
    }

    mm_obj_dbg (self, "checking +GTRNDIS support...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+GTRNDIS=?",
                              6, /* timeout [s] */
                              TRUE, /* allow_cached */
                              (GAsyncReadyCallback) gtrndis_test_ready,
                              task);
}

/*****************************************************************************/
/* Reset / Power (Modem interface)                                           */

static gboolean
modem_common_power_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_reset (MMIfaceModem *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=15",
                              15,
                              FALSE,
                              callback,
                              user_data);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              15,
                              FALSE,
                              callback,
                              user_data);
}

static void
modem_power_off (MMIfaceModem *self,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPWROFF",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/

MMBroadbandModemFibocom *
mm_broadband_modem_fibocom_new (const gchar  *device,
                                const gchar  *physdev,
                                const gchar **drivers,
                                const gchar  *plugin,
                                guint16       vendor_id,
                                guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_FIBOCOM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_fibocom_init (MMBroadbandModemFibocom *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_FIBOCOM,
                                              MMBroadbandModemFibocomPrivate);

    self->priv->gtrndis_support = FEATURE_SUPPORT_UNKNOWN;
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->setup_sim_hot_swap = mm_shared_fibocom_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = mm_shared_fibocom_setup_sim_hot_swap_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->reset = modem_reset;
    iface->reset_finish = modem_common_power_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_common_power_finish;
    iface->modem_power_off = modem_power_off;
    iface->modem_power_off_finish = modem_common_power_finish;
}

static void
iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface)
{
    iface_modem_firmware_parent = g_type_interface_peek_parent (iface);

    iface->load_update_settings = mm_shared_fibocom_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_fibocom_firmware_load_update_settings_finish;
}

static MMIfaceModemInterface *
peek_parent_modem_interface (MMSharedFibocom *self)
{
    return iface_modem_parent;
}

static MMIfaceModemFirmwareInterface *
peek_parent_firmware_interface (MMSharedFibocom *self)
{
    return iface_modem_firmware_parent;
}

static MMBaseModemClass *
peek_parent_class (MMSharedFibocom *self)
{
    return MM_BASE_MODEM_CLASS (mm_broadband_modem_fibocom_parent_class);
}

static void
shared_fibocom_init (MMSharedFibocomInterface *iface)
{
    iface->peek_parent_modem_interface    = peek_parent_modem_interface;
    iface->peek_parent_firmware_interface = peek_parent_firmware_interface;
    iface->peek_parent_class              = peek_parent_class;
}

static void
mm_broadband_modem_fibocom_class_init (MMBroadbandModemFibocomClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (G_OBJECT_CLASS (klass),
                              sizeof (MMBroadbandModemFibocomPrivate));

    broadband_modem_class->setup_ports = mm_shared_fibocom_setup_ports;
}
