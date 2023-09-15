/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Copyright (C) 2011 - 2012 Google Inc.
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <ModemManager-tags.h>

#include "mm-plugin-common.h"
#include "mm-broadband-modem-cinterion.h"
#include "mm-log-object.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi-cinterion.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim-cinterion.h"
#endif

#define MM_TYPE_PLUGIN_CINTERION mm_plugin_cinterion_get_type ()
MM_DEFINE_PLUGIN (CINTERION, cinterion, Cinterion)

/*****************************************************************************/
/* Custom init */

/* helps shorten a long if */
#define CHECK_PORT_HAS_TAG(p, t) ( \
        mm_kernel_device_get_property_as_boolean (mm_port_probe_peek_port (p), t) )

static gboolean
cinterion_custom_init_finish (MMPortProbe   *probe,
                              GAsyncResult  *result,
                              GError       **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

/* is_port_already_tagged checks whether a port provided by probe has
 * already tags assigned. */
static gboolean
is_port_already_tagged (MMPortProbe *probe)
{
    if (CHECK_PORT_HAS_TAG (probe, ID_MM_PORT_TYPE_AT_PRIMARY) ||
        CHECK_PORT_HAS_TAG (probe, ID_MM_PORT_TYPE_AT_SECONDARY) ||
        CHECK_PORT_HAS_TAG (probe, ID_MM_PORT_TYPE_AT_PPP) ||
        CHECK_PORT_HAS_TAG (probe, ID_MM_PORT_TYPE_GPS) ) {
        return TRUE;
    }

    return FALSE;
}

static void
sqport_ready (MMPortSerialAt *port,
              GAsyncResult   *res,
              GTask          *task)
{
    MMPortProbe *probe;
    g_autofree gchar *response = NULL;

    probe = g_task_get_source_object (task);

    /* Ignore errors, just avoid tagging */
    response = mm_port_serial_at_command_finish (port, res, NULL);
    if (response) {
        /* A valid reply to AT^SQPORT tells us this is an AT port already */
        mm_port_probe_set_result_at (probe, TRUE);

        if (strstr (response, "Application"))
            g_object_set_data (G_OBJECT (probe), ID_MM_PORT_TYPE_AT_PRIMARY, GUINT_TO_POINTER (TRUE));
        else if (strstr (response, "Modem"))
            g_object_set_data (G_OBJECT (probe), ID_MM_PORT_TYPE_AT_PPP, GUINT_TO_POINTER (TRUE));
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
cinterion_custom_init (MMPortProbe         *probe,
                       MMPortSerialAt      *port,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    GTask *task;

    task = g_task_new (probe, cancellable, callback, user_data);

    /* if the port is already tagged then it means that, most likely, udev
     * has sorted things out. in that case it is not needed to run SQPORT?
     * to try to guess whether this is AT-capable port. lets skip it. */
    if (is_port_already_tagged (probe)) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* alright, no tags on this port whatsoever so lets figure things out
     * using SQPORT? */
    mm_port_serial_at_command (
        port,
        "AT^SQPORT?",
        3,
        FALSE, /* raw */
        FALSE, /* allow cached */
        cancellable,
        (GAsyncReadyCallback) sqport_ready,
        task);
}

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar *physdev,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              guint16 subsystem_vendor,
              GList *probes,
              GError **error)
{
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_obj_dbg (self, "QMI-powered Cinterion modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_cinterion_new (uid,
                                                                    physdev,
                                                                    drivers,
                                                                    mm_plugin_get_name (self),
                                                                    vendor,
                                                                    product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_obj_dbg (self, "MBIM-powered Cinterion modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_cinterion_new (uid,
                                                                     physdev,
                                                                     drivers,
                                                                     mm_plugin_get_name (self),
                                                                     vendor,
                                                                     product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_cinterion_new (uid,
                                                            physdev,
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
    MMPortType ptype;

    ptype = mm_port_probe_get_port_type (probe);

    if (g_object_get_data (G_OBJECT (probe), ID_MM_PORT_TYPE_AT_PRIMARY)) {
        mm_obj_dbg (self, "(%s/%s)' Port flagged as primary",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
        pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
    } else if (g_object_get_data (G_OBJECT (probe), ID_MM_PORT_TYPE_AT_PPP)) {
        mm_obj_dbg (self, "(%s/%s)' Port flagged as PPP",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
        pflags = MM_PORT_SERIAL_AT_FLAG_PPP;
    } /* else unknown or set via generic udev tags */

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_peek_port (probe),
                                    ptype,
                                    pflags,
                                    error);
}

/*****************************************************************************/

MM_PLUGIN_NAMED_CREATOR_SCOPE MMPlugin *
mm_plugin_create_cinterion (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usbmisc", "wwan", NULL };
    static const gchar *vendor_strings[] = { "cinterion", "siemens", NULL };
    static const guint16 vendor_ids[] = { 0x1e2d, 0x0681, 0x1269, 0 };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (cinterion_custom_init),
        .finish = G_CALLBACK (cinterion_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_CINTERION,
                      MM_PLUGIN_NAME,                   MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,     subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_STRINGS, vendor_strings,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,     vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,             TRUE,
                      MM_PLUGIN_ALLOWED_QMI,            TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,           TRUE,
                      MM_PLUGIN_CUSTOM_INIT,            &custom_init,
                      NULL));
}

static void
mm_plugin_cinterion_init (MMPluginCinterion *self)
{
}

static void
mm_plugin_cinterion_class_init (MMPluginCinterionClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
