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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-plugin-pantech.h"
#include "mm-broadband-modem-pantech.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

G_DEFINE_TYPE (MMPluginPantech, mm_plugin_pantech, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/
/* Custom commands for AT probing
 * There's currently no WMC probing plugged in the logic, so We need to detect
 * WMC ports ourselves somehow. Just assume that the WMC port will reply "ERROR"
 * to the "ATE0" command.
 */
static gboolean
port_probe_response_processor_is_pantech_at (const gchar *command,
                                             const gchar *response,
                                             gboolean last_command,
                                             const GError *error,
                                             GVariant **result,
                                             GError **result_error)
{
    if (error) {
        /* Timeout errors are the only ones not fatal;
         * they will just go on to the next command. */
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            return FALSE;
        }

        /* All other errors indicate NOT an AT port */
        *result = g_variant_new_boolean (FALSE);
        return TRUE;
    }

    /* No error reported, valid AT port! */
    *result = g_variant_new_boolean (TRUE);
    return TRUE;
}

static const MMPortProbeAtCommand custom_at_probe[] = {
    { "ATE0", 3, port_probe_response_processor_is_pantech_at },
    { "ATE0", 3, port_probe_response_processor_is_pantech_at },
    { "ATE0", 3, port_probe_response_processor_is_pantech_at },
    { NULL }
};

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
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_obj_dbg (self, "QMI-powered Pantech modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_pantech_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
}

static gboolean
grab_port (MMPlugin *self,
           MMBaseModem *modem,
           MMPortProbe *probe,
           GError **error)
{
    MMPortType ptype;
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;

    ptype = mm_port_probe_get_port_type (probe);

    /* Always prefer the ttyACM port as PRIMARY AT port */
    if (ptype == MM_PORT_TYPE_AT &&
        g_str_has_prefix (mm_port_probe_get_port_name (probe), "ttyACM")) {
        pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_peek_port (probe),
                                    ptype,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usbmisc", NULL };
    static const guint16 vendor_ids[] = { 0x106c, 0 };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_PANTECH,
                      MM_PLUGIN_NAME,               MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      MM_PLUGIN_CUSTOM_AT_PROBE,    custom_at_probe,
                      NULL));
}

static void
mm_plugin_pantech_init (MMPluginPantech *self)
{
}

static void
mm_plugin_pantech_class_init (MMPluginPantechClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
