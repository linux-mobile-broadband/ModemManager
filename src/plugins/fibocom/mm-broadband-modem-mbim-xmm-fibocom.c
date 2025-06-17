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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-broadband-modem-mbim-xmm-fibocom.h"
#include "mm-shared-fibocom.h"

static void iface_modem_init          (MMIfaceModemInterface         *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface);
static void shared_fibocom_init       (MMSharedFibocomInterface      *iface);

static MMIfaceModemInterface         *iface_modem_parent;
static MMIfaceModemFirmwareInterface *iface_modem_firmware_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimXmmFibocom, mm_broadband_modem_mbim_xmm_fibocom, MM_TYPE_BROADBAND_MODEM_MBIM_XMM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_FIBOCOM, shared_fibocom_init))

struct _MMBroadbandModemMbimXmmFibocomPrivate {
    gboolean custom_att_310280_attach;
};

/******************************************************************************/

/* Custom AT&T attach operation in MBIM where the partner settings are fully
 * skipped, while the non-partner ones are given. */
#define CUSTOM_ATT_310280_ATTACH_UNNEEDED_VERSION 18500, 5001, 0, 7

static inline gboolean
compare_l850_version (guint A1, guint A2, guint A3,
                      guint A4, guint A5, guint A6,
                      guint B1, guint B2, guint B3, guint B4)
{
    return ((A1 == B1) && (A2 == B2) && (A4 >= B4));
}

static void
process_version_features (MMBroadbandModemMbimXmmFibocom *self,
                          const gchar                    *revision)
{
    g_auto(GStrv) split = NULL;
    guint         A1;
    guint         A2;
    guint         A3;
    guint         A4;
    guint         A5;
    guint         A6;

    /* Exit early if not L850 */
    if (!(mm_base_modem_get_vendor_id (MM_BASE_MODEM (self)) == 0x2cb7 &&
          mm_base_modem_get_product_id (MM_BASE_MODEM (self)) == 0x0007)) {
        return;
    }

    split = g_strsplit_set (revision, "._", -1);
    if (!split || g_strv_length (split) < 6) {
        mm_obj_warn (self, "failed to process firmware version string: splitting failed");
        return;
    }

    if (!mm_get_uint_from_str (split[0], &A1) ||
        !mm_get_uint_from_str (split[1], &A2) ||
        !mm_get_uint_from_str (split[2], &A3) ||
        !mm_get_uint_from_str (split[3], &A4) ||
        !mm_get_uint_from_str (split[4], &A5) ||
        !mm_get_uint_from_str (split[5], &A6)) {
        mm_obj_warn (self, "failed to process firmware version string: failed to convert to integer");
        return;
    }

    /* Check if fix for ATT attach APN for 310/280 is supported on L850 */
    self->priv->custom_att_310280_attach = !compare_l850_version (A1, A2, A3, A4, A5, A6, CUSTOM_ATT_310280_ATTACH_UNNEEDED_VERSION);
    mm_obj_info (self, "custom attach logic for AT&T 310280 %s needed",
                 self->priv->custom_att_310280_attach ? "is" : "not");
}

/******************************************************************************/

static gchar *
load_revision_finish (MMIfaceModem  *self,
                      GAsyncResult  *res,
                      GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_revision_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    GError *error = NULL;
    gchar  *revision;

    revision = iface_modem_parent->load_revision_finish (self, res, &error);
    if (!revision) {
        g_task_return_error (task, error);
    } else {
        process_version_features (MM_BROADBAND_MODEM_MBIM_XMM_FIBOCOM (self), revision);
        g_task_return_pointer (task, revision, g_free);
    }
    g_object_unref (task);
}

static void
load_revision (MMIfaceModem        *self,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    g_assert (iface_modem_parent->load_revision);
    g_assert (iface_modem_parent->load_revision_finish);
    iface_modem_parent->load_revision (self,
                                       (GAsyncReadyCallback)parent_load_revision_ready,
                                       g_task_new (self, NULL, callback, user_data));
}

/******************************************************************************/

static MMBroadbandModemMbimSetInitialEpsBearerSettingsFlag
load_set_initial_eps_bearer_settings_mask (MMBroadbandModemMbim *_self)
{
    MMBroadbandModemMbimXmmFibocom                      *self = MM_BROADBAND_MODEM_MBIM_XMM_FIBOCOM (_self);
    MMBroadbandModemMbimSetInitialEpsBearerSettingsFlag  mask = MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_DEFAULT;
    g_autoptr(MMBaseSim)                                 modem_sim = NULL;
    const gchar                                         *operator_identifier = NULL;

    if (!self->priv->custom_att_310280_attach)
        return mask;

    g_object_get (self,
                  MM_IFACE_MODEM_SIM, &modem_sim,
                  NULL);

    if (modem_sim)
        operator_identifier = mm_gdbus_sim_get_operator_identifier (MM_GDBUS_SIM (modem_sim));

    /* Fix for attach APN issue with ATT SIM cards MCC/MNC 310/280
     * is available on L850 MR8. Execute custom attach logic only
     * for old versions */
    if (mm_base_modem_get_vendor_id (MM_BASE_MODEM (self)) == 0x2cb7 &&
        mm_base_modem_get_product_id (MM_BASE_MODEM (self)) == 0x0007 &&
        !g_strcmp0 (operator_identifier, "310280")) {
        mm_obj_dbg (self, "requesting LTE attach configuration update without partner section");
        mask &= ~MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_UPDATE_PARTNER;
        mask |= MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_SKIP_PARTNER;
    }

    return mask;
}

/******************************************************************************/

MMBroadbandModemMbimXmmFibocom *
mm_broadband_modem_mbim_xmm_fibocom_new (const gchar  *device,
                                         const gchar  *physdev,
                                         const gchar **drivers,
                                         const gchar  *plugin,
                                         guint16       vendor_id,
                                         guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_XMM_FIBOCOM,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_PHYSDEV,    physdev,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, TRUE,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                         MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED, TRUE,
#endif
                         NULL);
}

static void
mm_broadband_modem_mbim_xmm_fibocom_init (MMBroadbandModemMbimXmmFibocom *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_MBIM_XMM_FIBOCOM,
                                              MMBroadbandModemMbimXmmFibocomPrivate);
    self->priv->custom_att_310280_attach = FALSE;
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_revision = load_revision;
    iface->load_revision_finish = load_revision_finish;
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
    return MM_BASE_MODEM_CLASS (mm_broadband_modem_mbim_xmm_fibocom_parent_class);
}

static void
shared_fibocom_init (MMSharedFibocomInterface *iface)
{
    iface->peek_parent_modem_interface    = peek_parent_modem_interface;
    iface->peek_parent_firmware_interface = peek_parent_firmware_interface;
    iface->peek_parent_class              = peek_parent_class;
}

static void
mm_broadband_modem_mbim_xmm_fibocom_class_init (MMBroadbandModemMbimXmmFibocomClass *klass)
{
    MMBaseModemClass          *base_modem_class = MM_BASE_MODEM_CLASS (klass);
    MMBroadbandModemClass     *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);
    MMBroadbandModemMbimClass *broadband_modem_mbim_class = MM_BROADBAND_MODEM_MBIM_CLASS (klass);

    g_type_class_add_private (G_OBJECT_CLASS (klass), sizeof (MMBroadbandModemMbimXmmFibocomPrivate));

    base_modem_class->create_usbmisc_port = mm_shared_fibocom_create_usbmisc_port;
    base_modem_class->create_wwan_port = mm_shared_fibocom_create_wwan_port;
    broadband_modem_class->setup_ports = mm_shared_fibocom_setup_ports;
    broadband_modem_mbim_class->load_set_initial_eps_bearer_settings_mask = load_set_initial_eps_bearer_settings_mask;
}
