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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-common.h"
#include "mm-log-object.h"
#include "mm-broadband-modem.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim-foxconn.h"
#endif

#define MM_TYPE_PLUGIN_FOXCONN mm_plugin_foxconn_get_type ()
MM_DEFINE_PLUGIN (FOXCONN, foxconn, Foxconn)

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin     *self,
              const gchar  *uid,
              const gchar  *physdev,
              const gchar **drivers,
              guint16       vendor,
              guint16       product,
              guint16       subsystem_vendor,
              guint16       subsystem_device,
              GList        *probes,
              GError      **error)
{
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_obj_dbg (self, "QMI-powered Foxconn-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (uid,
                                                          physdev,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_obj_dbg (self, "MBIM-powered Foxconn-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_foxconn_new (uid,
                                                                   physdev,
                                                                   drivers,
                                                                   mm_plugin_get_name (self),
                                                                   vendor,
                                                                   product,
                                                                   subsystem_vendor,
                                                                   subsystem_device));
    }
#endif

    mm_obj_dbg (self, "Foxconn-branded generic modem found...");
    return MM_BASE_MODEM (mm_broadband_modem_new (uid,
                                                  physdev,
                                                  drivers,
                                                  mm_plugin_get_name (self),
                                                  vendor,
                                                  product));
}

/*****************************************************************************/

MM_PLUGIN_NAMED_CREATOR_SCOPE MMPlugin *
mm_plugin_create_foxconn (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usbmisc", "wwan", NULL };
    static const guint16 vendor_ids[] = {
        0x0489, /* usb vid */
        0x105b, /* pci vid */
        0 };
    static const mm_uint16_pair subsystem_vendor_ids[] = {
        {0x17cb, 0x105b }, /* QC VID, Foxconn Sub-VID*/
        {0x105b, 0x105b }, /* Foxconn VID, Foxconn Sub-VID*/
        {0, 0 }
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_FOXCONN,
                      MM_PLUGIN_NAME,                         MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,           subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,           vendor_ids,
                      MM_PLUGIN_ALLOWED_SUBSYSTEM_VENDOR_IDS, subsystem_vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,                   TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,                 TRUE,
                      MM_PLUGIN_ALLOWED_QMI,                  TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,                 TRUE,
                      NULL));
}

static void
mm_plugin_foxconn_init (MMPluginFoxconn *self)
{
}

static void
mm_plugin_foxconn_class_init (MMPluginFoxconnClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
