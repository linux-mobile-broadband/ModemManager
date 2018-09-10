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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
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
#include "mm-broadband-modem-xmm.h"
#include "mm-shared-xmm.h"

static void iface_modem_init (MMIfaceModem *iface);
static void shared_xmm_init  (MMSharedXmm  *iface);
static void iface_modem_signal_init (MMIfaceModemSignal *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemXmm, mm_broadband_modem_xmm, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_XMM,  shared_xmm_init))

/*****************************************************************************/

MMBroadbandModemXmm *
mm_broadband_modem_xmm_new (const gchar  *device,
                            const gchar **drivers,
                            const gchar  *plugin,
                            guint16       vendor_id,
                            guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_XMM,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_xmm_init (MMBroadbandModemXmm *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_supported_modes        = mm_shared_xmm_load_supported_modes;
    iface->load_supported_modes_finish = mm_shared_xmm_load_supported_modes_finish;
    iface->load_current_modes          = mm_shared_xmm_load_current_modes;
    iface->load_current_modes_finish   = mm_shared_xmm_load_current_modes_finish;
    iface->set_current_modes           = mm_shared_xmm_set_current_modes;
    iface->set_current_modes_finish    = mm_shared_xmm_set_current_modes_finish;

    iface->load_supported_bands        = mm_shared_xmm_load_supported_bands;
    iface->load_supported_bands_finish = mm_shared_xmm_load_supported_bands_finish;
    iface->load_current_bands          = mm_shared_xmm_load_current_bands;
    iface->load_current_bands_finish   = mm_shared_xmm_load_current_bands_finish;
    iface->set_current_bands           = mm_shared_xmm_set_current_bands;
    iface->set_current_bands_finish    = mm_shared_xmm_set_current_bands_finish;

    iface->load_power_state        = mm_shared_xmm_load_power_state;
    iface->load_power_state_finish = mm_shared_xmm_load_power_state_finish;
    iface->modem_power_up          = mm_shared_xmm_power_up;
    iface->modem_power_up_finish   = mm_shared_xmm_power_up_finish;
    iface->modem_power_down        = mm_shared_xmm_power_down;
    iface->modem_power_down_finish = mm_shared_xmm_power_down_finish;
    iface->modem_power_off         = mm_shared_xmm_power_off;
    iface->modem_power_off_finish  = mm_shared_xmm_power_off_finish;
    iface->reset                   = mm_shared_xmm_reset;
    iface->reset_finish            = mm_shared_xmm_reset_finish;
}

static void
iface_modem_signal_init (MMIfaceModemSignal *iface)
{
    iface->check_support        = mm_shared_xmm_signal_check_support;
    iface->check_support_finish = mm_shared_xmm_signal_check_support_finish;
    iface->load_values          = mm_shared_xmm_signal_load_values;
    iface->load_values_finish   = mm_shared_xmm_signal_load_values_finish;
}

static void
shared_xmm_init (MMSharedXmm *iface)
{
}

static void
mm_broadband_modem_xmm_class_init (MMBroadbandModemXmmClass *klass)
{
}
