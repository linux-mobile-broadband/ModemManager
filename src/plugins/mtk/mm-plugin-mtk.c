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
 * Copyright (C) 2023 Mediatek, Inc
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-common.h"
#include "mm-broadband-modem.h"
#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#include "mm-broadband-modem-mbim-mtk.h"
#include "mm-broadband-modem-mbim-mtk-fibocom.h"
#endif
#include "mm-log.h"

#define MM_TYPE_PLUGIN_MTK mm_plugin_mtk_get_type ()
MM_DEFINE_PLUGIN (MTK, mtk, Mtk)

/*****************************************************************************/
static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar  *physdev,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              guint16 subsystem_vendor,
              guint16 subsystem_device,
              GList *probes,
              GError **error)
{
#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        /* Support with MTK-based modem changes */
        if (vendor == 0x14c3 && product == 0x4d75) {
            mm_obj_dbg (self, "MBIM-powered MTK-based modem found...");
            return MM_BASE_MODEM (mm_broadband_modem_mbim_mtk_fibocom_new (uid,
                                                                           physdev,
                                                                           drivers,
                                                                           mm_plugin_get_name (self),
                                                                           vendor,
                                                                           product));
        }
        mm_obj_dbg (self, "MBIM-powered MTK modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_mtk_new (uid,
                                                               physdev,
                                                               drivers,
                                                               mm_plugin_get_name (self),
                                                               vendor,
                                                               product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_new (uid,
                                                  physdev,
                                                  drivers,
                                                  mm_plugin_get_name (self),
                                                  vendor,
                                                  product));
}

/*****************************************************************************/

MM_PLUGIN_NAMED_CREATOR_SCOPE MMPlugin *
mm_plugin_create_mtk (void)
{
    static const gchar *subsystems[] = { "wwan", "net", NULL };
    static const gchar *drivers[] = { "mtk_t7xx", NULL };

    MMPlugin *self = MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_MTK,
                      MM_PLUGIN_NAME,               MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,    drivers,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                      NULL));

    return self;
}

static void
mm_plugin_mtk_init (MMPluginMtk *self)
{
}

static void
mm_plugin_mtk_class_init (MMPluginMtkClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
