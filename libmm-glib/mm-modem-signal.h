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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MM_MODEM_SIGNAL_H_
#define _MM_MODEM_SIGNAL_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-signal.h"
#include "mm-gdbus-modem.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_SIGNAL            (mm_modem_signal_get_type ())
#define MM_MODEM_SIGNAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_SIGNAL, MMModemSignal))
#define MM_MODEM_SIGNAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_SIGNAL, MMModemSignalClass))
#define MM_IS_MODEM_SIGNAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_SIGNAL))
#define MM_IS_MODEM_SIGNAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_SIGNAL))
#define MM_MODEM_SIGNAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_SIGNAL, MMModemSignalClass))

typedef struct _MMModemSignal MMModemSignal;
typedef struct _MMModemSignalClass MMModemSignalClass;
typedef struct _MMModemSignalPrivate MMModemSignalPrivate;

/**
 * MMModemSignal:
 *
 * The #MMModemSignal structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemSignal {
    /*< private >*/
    MmGdbusModemSignalProxy parent;
    MMModemSignalPrivate *priv;
};

struct _MMModemSignalClass {
    /*< private >*/
    MmGdbusModemSignalProxyClass parent;
};

GType mm_modem_signal_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemSignal, g_object_unref)

const gchar *mm_modem_signal_get_path (MMModemSignal *self);
gchar       *mm_modem_signal_dup_path (MMModemSignal *self);
guint        mm_modem_signal_get_rate (MMModemSignal *self);

void     mm_modem_signal_setup        (MMModemSignal *self,
                                       guint rate,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
gboolean mm_modem_signal_setup_finish (MMModemSignal *self,
                                       GAsyncResult *res,
                                       GError **error);
gboolean mm_modem_signal_setup_sync   (MMModemSignal *self,
                                       guint rate,
                                       GCancellable *cancellable,
                                       GError **error);

MMSignal *mm_modem_signal_get_cdma (MMModemSignal *self);
MMSignal *mm_modem_signal_peek_cdma (MMModemSignal *self);

MMSignal *mm_modem_signal_get_evdo  (MMModemSignal *self);
MMSignal *mm_modem_signal_peek_evdo (MMModemSignal *self);

MMSignal *mm_modem_signal_get_gsm   (MMModemSignal *self);
MMSignal *mm_modem_signal_peek_gsm  (MMModemSignal *self);

MMSignal *mm_modem_signal_get_umts  (MMModemSignal *self);
MMSignal *mm_modem_signal_peek_umts (MMModemSignal *self);

MMSignal *mm_modem_signal_get_lte   (MMModemSignal *self);
MMSignal *mm_modem_signal_peek_lte  (MMModemSignal *self);

MMSignal *mm_modem_signal_get_nr5g  (MMModemSignal *self);
MMSignal *mm_modem_signal_peek_nr5g (MMModemSignal *self);

G_END_DECLS

#endif /* _MM_MODEM_SIGNAL_H_ */
