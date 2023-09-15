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
 * Copyright (C) 2011 Samsung Electronics, Inc.
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#include <string.h>
#include <gmodule.h>

#include "mm-plugin-common.h"
#include "mm-private-boxed-types.h"
#include "mm-broadband-modem-samsung.h"

#define MM_TYPE_PLUGIN_SAMSUNG mm_plugin_samsung_get_type ()
MM_DEFINE_PLUGIN (SAMSUNG, samsung, Samsung)

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
    return MM_BASE_MODEM (mm_broadband_modem_samsung_new (uid,
                                                          physdev,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
}

/*****************************************************************************/

MM_PLUGIN_NAMED_CREATOR_SCOPE MMPlugin *
mm_plugin_create_samsung (void)
{
    static const gchar *subsystems[] = { "tty", "net", NULL };
    static const mm_uint16_pair products[] = { { 0x04e8, 0x6872 },
                                               { 0x04e8, 0x6906 },
                                               { 0, 0 } };
    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_SAMSUNG,
                      MM_PLUGIN_NAME,                MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,  subsystems,
                      MM_PLUGIN_ALLOWED_PRODUCT_IDS, products,
                      MM_PLUGIN_ALLOWED_AT,          TRUE,
                      MM_PLUGIN_SEND_DELAY,          (guint64) 0,
                      NULL));
}

static void
mm_plugin_samsung_init (MMPluginSamsung *self)
{
}

static void
mm_plugin_samsung_class_init (MMPluginSamsungClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
