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
 * Copyright (C) 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-simple.h"
#include "mm-log.h"

/* typedef struct { */
/*     MmGdbusModemSimple *skeleton; */
/*     GDBusMethodInvocation *invocation; */
/*     MMIfaceModemSimple *self; */
/* } DbusCallContext; */

/* static void */
/* dbus_call_context_free (DbusCallContext *ctx) */
/* { */
/*     g_object_unref (ctx->skeleton); */
/*     g_object_unref (ctx->invocation); */
/*     g_object_unref (ctx->self); */
/*     g_free (ctx); */
/* } */

/* static DbusCallContext * */
/* dbus_call_context_new (MmGdbusModemSimple *skeleton, */
/*                        GDBusMethodInvocation *invocation, */
/*                        MMIfaceModemSimple *self) */
/* { */
/*     DbusCallContext *ctx; */

/*     ctx = g_new (DbusCallContext, 1); */
/*     ctx->skeleton = g_object_ref (skeleton); */
/*     ctx->invocation = g_object_ref (invocation); */
/*     ctx->self = g_object_ref (self); */
/*     return ctx; */
/* } */

/*****************************************************************************/

void
mm_iface_modem_simple_initialize (MMIfaceModemSimple *self)
{
    MmGdbusModemSimple *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_SIMPLE (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_simple_skeleton_new ();

        g_object_set (self,
                      MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON, skeleton,
                      NULL);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_simple (MM_GDBUS_OBJECT_SKELETON (self),
                                                   MM_GDBUS_MODEM_SIMPLE (skeleton));
    }
    g_object_unref (skeleton);
}

void
mm_iface_modem_simple_shutdown (MMIfaceModemSimple *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_SIMPLE (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_simple (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_simple_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON,
                              "Simple DBus skeleton",
                              "DBus skeleton for the Simple interface",
                              MM_GDBUS_TYPE_MODEM_SIMPLE_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_simple_get_type (void)
{
    static GType iface_modem_simple_type = 0;

    if (!G_UNLIKELY (iface_modem_simple_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemSimple), /* class_size */
            iface_modem_simple_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_simple_type = g_type_register_static (G_TYPE_INTERFACE,
                                                          "MMIfaceModemSimple",
                                                          &info,
                                                          0);

        g_type_interface_add_prerequisite (iface_modem_simple_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_simple_type;
}
