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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>

#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-qcom-soc.h"
#include "mm-broadband-modem-qmi-qcom-soc.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPluginQcomSoc, mm_plugin_qcom_soc, MM_TYPE_PLUGIN)

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
    if (!mm_port_probe_list_has_qmi_port (probes)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unsupported device: at least a QMI port is required");
        return NULL;
    }

    mm_obj_dbg (self, "Qualcomm SoC modem found...");
    return MM_BASE_MODEM (mm_broadband_modem_qmi_qcom_soc_new (uid,
                                                               drivers,
                                                               mm_plugin_get_name (self),
                                                               vendor,
                                                               product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "rpmsg", "net", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_QCOM_SOC,
                      MM_PLUGIN_NAME,               MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      NULL));
}

static void
mm_plugin_qcom_soc_init (MMPluginQcomSoc *self)
{
}

static void
mm_plugin_qcom_soc_class_init (MMPluginQcomSocClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
