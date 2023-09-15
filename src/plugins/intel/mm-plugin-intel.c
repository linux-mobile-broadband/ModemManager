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
 * Copyright (C) 2021-2022 Intel Corporation
 */

#include <stdio.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-common.h"
#include "mm-log-object.h"
#include "mm-broadband-modem.h"

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim-intel.h"
#endif

#define MM_TYPE_PLUGIN_INTEL mm_plugin_intel_get_type ()
MM_DEFINE_PLUGIN (INTEL, intel, Intel)

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin      *self,
              const gchar   *uid,
              const gchar   *physdev,
              const gchar  **drivers,
              guint16        vendor,
              guint16        product,
              guint16        subsystem_vendor,
              GList         *probes,
              GError       **error)
{
#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_obj_dbg (self, "MBIM-powered Intel modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_intel_new (uid,
                              physdev,
                              drivers,
                              mm_plugin_get_name (self),
                              vendor,
                              product));
    }
#endif

    mm_obj_dbg (self, "Generic Intel modem found...");
    return MM_BASE_MODEM (mm_broadband_modem_new (uid,
                                                  physdev,
                                                  drivers,
                                                  mm_plugin_get_name (self),
                                                  vendor,
                                                  product));
}

/*****************************************************************************/

MM_PLUGIN_NAMED_CREATOR_SCOPE MMPlugin *
mm_plugin_create_intel (void)
{
    static const gchar   *subsystems[] = { "net", "wwan", NULL };
    static const guint16  vendor_ids[]  = { 0x8086, 0 };

    return MM_PLUGIN (
               g_object_new (MM_TYPE_PLUGIN_INTEL,
                             MM_PLUGIN_NAME,               "Intel",
                             MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                             MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                             MM_PLUGIN_ALLOWED_AT,         TRUE,
                             MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                             NULL));
}

static void
mm_plugin_intel_init (MMPluginIntel *self)
{
    /*nothing to be done here, but required for creating intel plugin instance*/
}

static void
mm_plugin_intel_class_init (MMPluginIntelClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);
    plugin_class->create_modem  = create_modem;
}
