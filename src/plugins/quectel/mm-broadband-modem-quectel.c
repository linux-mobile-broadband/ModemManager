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
 * Copyright (C) 2018-2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2024 JUCR GmbH
 */

#include <config.h>

#include "mm-broadband-modem-quectel.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-time.h"
#include "mm-shared-quectel.h"
#include "mm-base-modem-at.h"

static void iface_modem_init          (MMIfaceModemInterface         *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface);
static void iface_modem_location_init (MMIfaceModemLocationInterface *iface);
static void iface_modem_time_init     (MMIfaceModemTimeInterface     *iface);
static void shared_quectel_init       (MMSharedQuectelInterface      *iface);

static MMIfaceModemInterface         *iface_modem_parent;
static MMIfaceModemLocationInterface *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQuectel, mm_broadband_modem_quectel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init))

/*****************************************************************************/
/* Power state loading (Modem interface) */

static MMModemPowerState
load_power_state_finish (MMIfaceModem  *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    MMModemPowerState  state = MM_MODEM_POWER_STATE_UNKNOWN;
    guint              cfun_state = 0;
    const gchar       *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return MM_MODEM_POWER_STATE_UNKNOWN;

    if (!mm_3gpp_parse_cfun_query_response (response, &cfun_state, error))
        return MM_MODEM_POWER_STATE_UNKNOWN;

    switch (cfun_state) {
    case 0:
        state = MM_MODEM_POWER_STATE_OFF;
        break;
    case 1:
        state = MM_MODEM_POWER_STATE_ON;
        break;
    case 4:
        state = MM_MODEM_POWER_STATE_LOW;
        break;
    /* Some modems support the following (ASR chipsets at least) */
    case 3: /* Disable RX */
    case 5: /* Disable (U)SIM */
        /* We'll just call these low-power... */
        state = MM_MODEM_POWER_STATE_LOW;
        break;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown +CFUN pòwer state: '%u'", state);
        return MM_MODEM_POWER_STATE_UNKNOWN;
    }

    return state;
}

static void
load_power_state (MMIfaceModem        *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Modem power up/down/off (Modem interface) */

static gboolean
common_modem_power_operation_finish (MMIfaceModem  *self,
                                     GAsyncResult  *res,
                                     GError       **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
common_modem_power_operation (MMBroadbandModemQuectel  *self,
                              const gchar              *command,
                              GAsyncReadyCallback       callback,
                              gpointer                  user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              30,
                              FALSE,
                              callback,
                              user_data);
}

static void
modem_reset (MMIfaceModem        *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_QUECTEL (self), "+CFUN=1,1", callback, user_data);
}

static void
modem_power_off (MMIfaceModem        *self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_QUECTEL (self), "+QPOWD=1", callback, user_data);
}

static void
modem_power_down (MMIfaceModem        *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_QUECTEL (self), "+CFUN=4", callback, user_data);
}

static void
modem_power_up (MMIfaceModem        *self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_QUECTEL (self), "+CFUN=1", callback, user_data);
}

/*****************************************************************************/

MMBroadbandModemQuectel *
mm_broadband_modem_quectel_new (const gchar  *device,
                                const gchar  *physdev,
                                const gchar **drivers,
                                const gchar  *plugin,
                                guint16       vendor_id,
                                guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QUECTEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Generic bearer supports TTY only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, FALSE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_quectel_init (MMBroadbandModemQuectel *self)
{
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->setup_sim_hot_swap = mm_shared_quectel_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = mm_shared_quectel_setup_sim_hot_swap_finish;
    iface->cleanup_sim_hot_swap = mm_shared_quectel_cleanup_sim_hot_swap;
    iface->load_power_state        = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;
    iface->modem_power_up        = modem_power_up;
    iface->modem_power_up_finish = common_modem_power_operation_finish;
    iface->modem_power_down        = modem_power_down;
    iface->modem_power_down_finish = common_modem_power_operation_finish;
    iface->modem_power_off        = modem_power_off;
    iface->modem_power_off_finish = common_modem_power_operation_finish;
    iface->reset        = modem_reset;
    iface->reset_finish = common_modem_power_operation_finish;
}

static void
iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface)
{
    iface->load_update_settings = mm_shared_quectel_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_quectel_firmware_load_update_settings_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocationInterface *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_quectel_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_quectel_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_quectel_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_quectel_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_quectel_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_quectel_disable_location_gathering_finish;
}

static void
iface_modem_time_init (MMIfaceModemTimeInterface *iface)
{
    iface->check_support        = mm_shared_quectel_time_check_support;
    iface->check_support_finish = mm_shared_quectel_time_check_support_finish;
}

static MMBaseModemClass *
peek_parent_class (MMSharedQuectel *self)
{
    return MM_BASE_MODEM_CLASS (mm_broadband_modem_quectel_parent_class);
}

static MMIfaceModemInterface *
peek_parent_modem_interface (MMSharedQuectel *self)
{
    return iface_modem_parent;
}

static MMIfaceModemLocationInterface *
peek_parent_modem_location_interface (MMSharedQuectel *self)
{
    return iface_modem_location_parent;
}

static void
shared_quectel_init (MMSharedQuectelInterface *iface)
{
    iface->peek_parent_modem_interface          = peek_parent_modem_interface;
    iface->peek_parent_modem_location_interface = peek_parent_modem_location_interface;
    iface->peek_parent_class                    = peek_parent_class;
}

static void
mm_broadband_modem_quectel_class_init (MMBroadbandModemQuectelClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = mm_shared_quectel_setup_ports;
}
