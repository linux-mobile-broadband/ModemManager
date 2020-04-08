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
#include "mm-plugin-motorola.h"
#include "mm-broadband-modem-motorola.h"

G_DEFINE_TYPE (MMPluginMotorola, mm_plugin_motorola, MM_TYPE_PLUGIN)

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
    return MM_BASE_MODEM (mm_broadband_modem_motorola_new (uid,
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
    static const mm_uint16_pair product_ids[] = {
        { 0x22b8, 0x3802 }, /* C330/C350L/C450/EZX GSM Phone */
        { 0x22b8, 0x4902 }, /* Triplet GSM Phone */
        { 0, 0 }
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_MOTOROLA,
                      MM_PLUGIN_NAME,                MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,  subsystems,
                      MM_PLUGIN_ALLOWED_PRODUCT_IDS, product_ids,
                      MM_PLUGIN_ALLOWED_AT,          TRUE,
                      NULL));
}

static void
mm_plugin_motorola_init (MMPluginMotorola *self)
{
}

static void
mm_plugin_motorola_class_init (MMPluginMotorolaClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
