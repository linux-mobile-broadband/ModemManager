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
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#include <string.h>
#include <gmodule.h>

#include "mm-plugin-novatel-lte.h"
#include "mm-private-boxed-types.h"
#include "mm-broadband-modem-novatel-lte.h"

G_DEFINE_TYPE (MMPluginNovatelLte, mm_plugin_novatel_lte, MM_TYPE_PLUGIN)

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
    return MM_BASE_MODEM (mm_broadband_modem_novatel_lte_new (uid,
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
    static const mm_uint16_pair products[] = { { 0x1410, 0x9010 }, /* Novatel E362 */
                                               {0, 0} };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_NOVATEL_LTE,
                      MM_PLUGIN_NAME,                MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,  subsystems,
                      MM_PLUGIN_ALLOWED_PRODUCT_IDS, products,
                      MM_PLUGIN_ALLOWED_SINGLE_AT,   TRUE,
                      NULL));
}

static void
mm_plugin_novatel_lte_init (MMPluginNovatelLte *self)
{
}

static void
mm_plugin_novatel_lte_class_init (MMPluginNovatelLteClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
