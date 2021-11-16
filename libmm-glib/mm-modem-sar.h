/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2021 Fibocom Wireless Inc.
 */

#ifndef _MM_MODEM_SAR_H_
#define _MM_MODEM_SAR_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_SAR            (mm_modem_sar_get_type ())
#define MM_MODEM_SAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_SAR, MMModemSar))
#define MM_MODEM_SAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_SAR, MMModemSarClass))
#define MM_IS_MODEM_SAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_SAR))
#define MM_IS_MODEM_SAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_SAR))
#define MM_MODEM_SAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_SAR, MMModemSarClass))

typedef struct _MMModemSar MMModemSar;
typedef struct _MMModemSarClass MMModemSarClass;

/**
 * MMModemSar:
 *
 * The #MMModemSar structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemSar {
    /*< private >*/
    MmGdbusModemSarProxy parent;
    gpointer unused;
};

struct _MMModemSarClass {
    /*< private >*/
    MmGdbusModemSarProxyClass parent;
};

GType mm_modem_sar_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemSar, g_object_unref)

const gchar *mm_modem_sar_get_path (MMModemSar *self);
gchar       *mm_modem_sar_dup_path (MMModemSar *self);

void     mm_modem_sar_enable        (MMModemSar         *self,
                                     gboolean            enable,
                                     GCancellable       *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data);

gboolean mm_modem_sar_enable_finish   (MMModemSar   *self,
                                       GAsyncResult *res,
                                       GError      **error);

gboolean mm_modem_sar_enable_sync     (MMModemSar   *self,
                                       gboolean      enable,
                                       GCancellable *cancellable,
                                       GError      **error);

void     mm_modem_sar_set_power_level        (MMModemSar         *self,
                                              guint               level,
                                              GCancellable       *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer            user_data);
gboolean mm_modem_sar_set_power_level_finish (MMModemSar   *self,
                                              GAsyncResult *res,
                                              GError      **error);

gboolean mm_modem_sar_set_power_level_sync   (MMModemSar   *self,
                                              guint         level,
                                              GCancellable *cancellable,
                                              GError      **error);

gboolean mm_modem_sar_get_state              (MMModemSar *self);
guint    mm_modem_sar_get_power_level        (MMModemSar *self);


G_END_DECLS

#endif /* _MM_MODEM_SAR_H_ */
