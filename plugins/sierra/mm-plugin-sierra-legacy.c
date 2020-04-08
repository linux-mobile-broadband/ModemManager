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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <stdlib.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-sierra-legacy.h"
#include "mm-common-sierra.h"
#include "mm-broadband-modem-sierra.h"
#include "mm-broadband-modem-sierra-icera.h"

G_DEFINE_TYPE (MMPluginSierraLegacy, mm_plugin_sierra_legacy, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    if (mm_common_sierra_port_probe_list_is_icera (probes))
        return MM_BASE_MODEM (mm_broadband_modem_sierra_icera_new (uid,
                                                                   drivers,
                                                                   mm_plugin_get_name (self),
                                                                   vendor,
                                                                   product));

    return MM_BASE_MODEM (mm_broadband_modem_sierra_new (uid,
                                                         drivers,
                                                         mm_plugin_get_name (self),
                                                         vendor,
                                                         product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", NULL };
    static const gchar *drivers[] = { "sierra", "sierra_net", NULL };
    static const gchar *forbidden_drivers[] = { "qmi_wwan", "cdc_mbim", NULL };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (mm_common_sierra_custom_init),
        .finish = G_CALLBACK (mm_common_sierra_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_SIERRA_LEGACY,
                      MM_PLUGIN_NAME,                MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,  subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,     drivers,
                      MM_PLUGIN_FORBIDDEN_DRIVERS,   forbidden_drivers,
                      MM_PLUGIN_ALLOWED_AT,          TRUE,
                      MM_PLUGIN_CUSTOM_INIT,         &custom_init,
                      MM_PLUGIN_ICERA_PROBE,         TRUE,
                      MM_PLUGIN_REMOVE_ECHO,         FALSE,
                      NULL));
}

static void
mm_plugin_sierra_legacy_init (MMPluginSierraLegacy *self)
{
}

static void
mm_plugin_sierra_legacy_class_init (MMPluginSierraLegacyClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = mm_common_sierra_grab_port;
}
