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
 * Copyright (C) 2017-2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <stdlib.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-plugin-quectel.h"
#include "mm-broadband-modem-quectel.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi-quectel.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#endif

G_DEFINE_TYPE (MMPluginQuectel, mm_plugin_quectel, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin     *self,
              const gchar  *uid,
              const gchar **drivers,
              guint16       vendor,
              guint16       product,
              GList        *probes,
              GError      **error)
{
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_obj_dbg (self, "QMI-powered Quectel modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_quectel_new (uid,
                                                                  drivers,
                                                                  mm_plugin_get_name (self),
                                                                  vendor,
                                                                  product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_obj_dbg (self, "MBIM-powered Quectel modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (uid,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_quectel_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usbmisc", NULL };
    static const gchar *vendor_strings[] = { "quectel", NULL };
    static const guint16 vendor_ids[] = { 0x2c7c, 0 };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_QUECTEL,
                      MM_PLUGIN_NAME,                   MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,     subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,     vendor_ids,
                      MM_PLUGIN_ALLOWED_VENDOR_STRINGS, vendor_strings,
                      MM_PLUGIN_ALLOWED_AT,             TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,           TRUE,
                      MM_PLUGIN_ALLOWED_QMI,            TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,           TRUE,
                      NULL));
}

static void
mm_plugin_quectel_init (MMPluginQuectel *self)
{
}

static void
mm_plugin_quectel_class_init (MMPluginQuectelClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
