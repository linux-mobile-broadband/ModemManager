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
 * Copyright (C) 2021 Fibocom Wireless Inc.
 */

#ifndef MM_IFACE_MODEM_SAR_H
#define MM_IFACE_MODEM_SAR_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#define MM_TYPE_IFACE_MODEM_SAR               (mm_iface_modem_sar_get_type ())
#define MM_IFACE_MODEM_SAR(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_SAR, MMIfaceModemSar))
#define MM_IS_IFACE_MODEM_SAR(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_SAR))
#define MM_IFACE_MODEM_SAR_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_SAR, MMIfaceModemSar))

#define MM_IFACE_MODEM_SAR_DBUS_SKELETON  "iface-modem-sar-dbus-skeleton"

typedef struct _MMIfaceModemSar MMIfaceModemSar;

struct _MMIfaceModemSar {
    GTypeInterface g_iface;

    /* Check for SAR support (async) */
    void (* check_support) (MMIfaceModemSar    *self,
                            GAsyncReadyCallback callback,
                            gpointer            user_data);
    gboolean (* check_support_finish) (MMIfaceModemSar *self,
                                       GAsyncResult    *res,
                                       GError         **error);
    /* Enable SAR  (async) */
    void (* enable) (MMIfaceModemSar    *self,
                     gboolean            enable,
                     GAsyncReadyCallback callback,
                     gpointer            user_data);
    gboolean (* enable_finish) (MMIfaceModemSar *self,
                                GAsyncResult    *res,
                                guint           *out_sar_power_level,
                                GError         **error);
    /* Get SAR state (async) */
    void (* load_state) (MMIfaceModemSar    *self,
                         GAsyncReadyCallback callback,
                         gpointer            user_data);
    gboolean (* load_state_finish) (MMIfaceModemSar *self,
                                    GAsyncResult    *res,
                                    gboolean        *out_state,
                                    GError         **error);

    /* Get power level (async) */
    void (* load_power_level) (MMIfaceModemSar    *self,
                               GAsyncReadyCallback callback,
                               gpointer            user_data);
    gboolean (* load_power_level_finish) (MMIfaceModemSar *self,
                                          GAsyncResult    *res,
                                          guint           *out_power_level,
                                          GError         **error);

    /* Set power level (async) */
    void (* set_power_level) (MMIfaceModemSar     *self,
                              guint               power_level,
                              GAsyncReadyCallback callback,
                              gpointer            user_data);
    gboolean (* set_power_level_finish) (MMIfaceModemSar *self,
                                         GAsyncResult    *res,
                                         GError         **error);
};

GType mm_iface_modem_sar_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModemSar, g_object_unref)

/* Initialize Sar interface (async) */
void     mm_iface_modem_sar_initialize        (MMIfaceModemSar    *self,
                                               GCancellable       *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer            user_data);
gboolean mm_iface_modem_sar_initialize_finish (MMIfaceModemSar *self,
                                               GAsyncResult    *res,
                                               GError         **error);

/* Shutdown Sar interface */
void     mm_iface_modem_sar_shutdown           (MMIfaceModemSar *self);

/* Bind properties for simple GetStatus() */
void     mm_iface_modem_sar_bind_simple_status (MMIfaceModemSar*self,
                                                MMSimpleStatus *status);

guint    mm_iface_modem_sar_get_power_level    (MMIfaceModemSar *self);

#endif /* MM_IFACE_MODEM_SAR_H */
