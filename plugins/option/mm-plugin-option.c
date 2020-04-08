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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-private-boxed-types.h"
#include "mm-plugin-option.h"
#include "mm-broadband-modem-option.h"

G_DEFINE_TYPE (MMPluginOption, mm_plugin_option, MM_TYPE_PLUGIN)

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
    return MM_BASE_MODEM (mm_broadband_modem_option_new (uid,
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
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;
    MMKernelDevice *port;
    guint usbif;

    /* The Option plugin cannot do anything with non-AT ports */
    if (!mm_port_probe_is_at (probe)) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "Ignoring non-AT port");
        return FALSE;
    }

    port = mm_port_probe_peek_port (probe);

    /* Genuine Option NV devices are always supposed to use USB interface 0 as
     * the modem/data port, per mail with Option engineers.  Only this port
     * will emit responses to dialing commands.
     */
    usbif = mm_kernel_device_get_property_as_int_hex (port, "ID_USB_INTERFACE_NUM");
    if (usbif == 0)
        pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY | MM_PORT_SERIAL_AT_FLAG_PPP;

    return mm_base_modem_grab_port (modem,
                                    port,
                                    MM_PORT_TYPE_AT, /* we only allow AT ports here */
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", NULL };
    static const guint16 vendor_ids[] = { 0x0af0, /* Option USB devices */
                                          0x1931, /* Nozomi CardBus devices */
                                          0 };
    static const gchar *drivers[] = { "option1", "option", "nozomi", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_OPTION,
                      MM_PLUGIN_NAME,                MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,  subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,     drivers,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,  vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,          TRUE,
                      NULL));
}

static void
mm_plugin_option_init (MMPluginOption *self)
{
}

static void
mm_plugin_option_class_init (MMPluginOptionClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
