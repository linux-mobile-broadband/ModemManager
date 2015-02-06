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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 */

#include <stdlib.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-plugin-sierra.h"
#include "mm-common-sierra.h"
#include "mm-broadband-modem-sierra.h"
#include "mm-broadband-modem-sierra-icera.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#endif

G_DEFINE_TYPE (MMPluginSierra, mm_plugin_sierra, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_dbg ("QMI-powered Sierra modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_dbg ("MBIM-powered Sierra modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (sysfs_path,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    if (mm_common_sierra_port_probe_list_is_icera (probes))
        return MM_BASE_MODEM (mm_broadband_modem_sierra_icera_new (sysfs_path,
                                                                   drivers,
                                                                   mm_plugin_get_name (self),
                                                                   vendor,
                                                                   product));

    return MM_BASE_MODEM (mm_broadband_modem_sierra_new (sysfs_path,
                                                         drivers,
                                                         mm_plugin_get_name (self),
                                                         vendor,
                                                         product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const gchar *drivers[] = { "sierra", "sierra_net", NULL };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (mm_common_sierra_custom_init),
        .finish = G_CALLBACK (mm_common_sierra_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_SIERRA,
                      MM_PLUGIN_NAME,               "Sierra",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,    drivers,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      MM_PLUGIN_ICERA_PROBE,        TRUE,
                      MM_PLUGIN_REMOVE_ECHO,        FALSE,
                      NULL));
}

static void
mm_plugin_sierra_init (MMPluginSierra *self)
{
}

static void
mm_plugin_sierra_class_init (MMPluginSierraClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = mm_common_sierra_grab_port;
}
