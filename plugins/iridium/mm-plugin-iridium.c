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
 * Copyright (C) 2011 - 2012 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-iridium.h"
#include "mm-broadband-modem-iridium.h"
#include "mm-private-boxed-types.h"

G_DEFINE_TYPE (MMPluginIridium, mm_plugin_iridium, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_iridium_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", NULL };
    static const guint16 vendor_ids[] = { 0x1edd, 0 };
    static const gchar *vendor_strings[] = { "iridium", NULL };
    /* Also support motorola-branded Iridium modems */
    static const mm_str_pair product_strings[] = {{(gchar *)"motorola", (gchar *)"satellite" },
                                                  { NULL, NULL }};

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_IRIDIUM,
                      MM_PLUGIN_NAME,                    MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,      subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_STRINGS,  vendor_strings,
                      MM_PLUGIN_ALLOWED_PRODUCT_STRINGS, product_strings,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,      vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,              TRUE,
                      NULL));
}

static void
mm_plugin_iridium_init (MMPluginIridium *self)
{
}

static void
mm_plugin_iridium_class_init (MMPluginIridiumClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
