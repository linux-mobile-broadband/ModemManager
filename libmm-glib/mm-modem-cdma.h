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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_CDMA_H_
#define _MM_MODEM_CDMA_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-cdma-manual-activation-properties.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_CDMA            (mm_modem_cdma_get_type ())
#define MM_MODEM_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_CDMA, MMModemCdma))
#define MM_MODEM_CDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_CDMA, MMModemCdmaClass))
#define MM_IS_MODEM_CDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_CDMA))
#define MM_IS_MODEM_CDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_CDMA))
#define MM_MODEM_CDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_CDMA, MMModemCdmaClass))

typedef struct _MMModemCdma MMModemCdma;
typedef struct _MMModemCdmaClass MMModemCdmaClass;

/**
 * MMModemCdma:
 *
 * The #MMModemCdma structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemCdma {
    /*< private >*/
    MmGdbusModemCdmaProxy parent;
    gpointer unused;
};

struct _MMModemCdmaClass {
    /*< private >*/
    MmGdbusModemCdmaProxyClass parent;
};

GType mm_modem_cdma_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemCdma, g_object_unref)

const gchar *mm_modem_cdma_get_path (MMModemCdma *self);
gchar       *mm_modem_cdma_dup_path (MMModemCdma *self);

const gchar *mm_modem_cdma_get_meid (MMModemCdma *self);
gchar       *mm_modem_cdma_dup_meid (MMModemCdma *self);

const gchar *mm_modem_cdma_get_esn  (MMModemCdma *self);
gchar       *mm_modem_cdma_dup_esn  (MMModemCdma *self);

/**
 * MM_MODEM_CDMA_SID_UNKNOWN:
 *
 * Identifier for an unknown SID.
 */
#define MM_MODEM_CDMA_SID_UNKNOWN 99999
guint        mm_modem_cdma_get_sid  (MMModemCdma *self);

/**
 * MM_MODEM_CDMA_NID_UNKNOWN:
 *
 * Identifier for an unknown NID.
 */
#define MM_MODEM_CDMA_NID_UNKNOWN 99999
guint        mm_modem_cdma_get_nid  (MMModemCdma *self);

MMModemCdmaRegistrationState mm_modem_cdma_get_cdma1x_registration_state (MMModemCdma *self);
MMModemCdmaRegistrationState mm_modem_cdma_get_evdo_registration_state   (MMModemCdma *self);
MMModemCdmaActivationState   mm_modem_cdma_get_activation_state          (MMModemCdma *self);

void     mm_modem_cdma_activate        (MMModemCdma *self,
                                        const gchar *carrier,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_modem_cdma_activate_finish (MMModemCdma *self,
                                        GAsyncResult *res,
                                        GError **error);
gboolean mm_modem_cdma_activate_sync   (MMModemCdma *self,
                                        const gchar *carrier,
                                        GCancellable *cancellable,
                                        GError **error);

void     mm_modem_cdma_activate_manual        (MMModemCdma *self,
                                               MMCdmaManualActivationProperties *properties,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
gboolean mm_modem_cdma_activate_manual_finish (MMModemCdma *self,
                                               GAsyncResult *res,
                                               GError **error);
gboolean mm_modem_cdma_activate_manual_sync   (MMModemCdma *self,
                                               MMCdmaManualActivationProperties *properties,
                                               GCancellable *cancellable,
                                               GError **error);

G_END_DECLS

#endif /* _MM_MODEM_CDMA_H_ */
