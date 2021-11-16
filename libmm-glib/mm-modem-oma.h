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
 * Copyright (C) 2013 Google, Inc.
 */

#ifndef _MM_MODEM_OMA_H_
#define _MM_MODEM_OMA_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-helper-types.h"
#include "mm-gdbus-modem.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_OMA            (mm_modem_oma_get_type ())
#define MM_MODEM_OMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_OMA, MMModemOma))
#define MM_MODEM_OMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_OMA, MMModemOmaClass))
#define MM_IS_MODEM_OMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_OMA))
#define MM_IS_MODEM_OMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_OMA))
#define MM_MODEM_OMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_OMA, MMModemOmaClass))

typedef struct _MMModemOma MMModemOma;
typedef struct _MMModemOmaClass MMModemOmaClass;
typedef struct _MMModemOmaPrivate MMModemOmaPrivate;

/**
 * MMModemOma:
 *
 * The #MMModemOma structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemOma {
    /*< private >*/
    MmGdbusModemOmaProxy parent;
    MMModemOmaPrivate *priv;
};

struct _MMModemOmaClass {
    /*< private >*/
    MmGdbusModemOmaProxyClass parent;
};

GType mm_modem_oma_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemOma, g_object_unref)

const gchar *mm_modem_oma_get_path (MMModemOma *self);
gchar       *mm_modem_oma_dup_path (MMModemOma *self);

void     mm_modem_oma_setup        (MMModemOma *self,
                                    MMOmaFeature features,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean mm_modem_oma_setup_finish (MMModemOma *self,
                                    GAsyncResult *res,
                                    GError **error);
gboolean mm_modem_oma_setup_sync   (MMModemOma *self,
                                    MMOmaFeature features,
                                    GCancellable *cancellable,
                                    GError **error);

void     mm_modem_oma_start_client_initiated_session        (MMModemOma *self,
                                                             MMOmaSessionType session_type,
                                                             GCancellable *cancellable,
                                                             GAsyncReadyCallback callback,
                                                             gpointer user_data);
gboolean mm_modem_oma_start_client_initiated_session_finish (MMModemOma *self,
                                                             GAsyncResult *res,
                                                             GError **error);
gboolean mm_modem_oma_start_client_initiated_session_sync   (MMModemOma *self,
                                                             MMOmaSessionType session_type,
                                                             GCancellable *cancellable,
                                                             GError **error);

void     mm_modem_oma_accept_network_initiated_session        (MMModemOma *self,
                                                               guint session_id,
                                                               gboolean accept,
                                                               GCancellable *cancellable,
                                                               GAsyncReadyCallback callback,
                                                               gpointer user_data);
gboolean mm_modem_oma_accept_network_initiated_session_finish (MMModemOma *self,
                                                               GAsyncResult *res,
                                                               GError **error);
gboolean mm_modem_oma_accept_network_initiated_session_sync   (MMModemOma *self,
                                                               guint session_id,
                                                               gboolean accept,
                                                               GCancellable *cancellable,
                                                               GError **error);

void     mm_modem_oma_cancel_session        (MMModemOma *self,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
gboolean mm_modem_oma_cancel_session_finish (MMModemOma *self,
                                             GAsyncResult *res,
                                             GError **error);
gboolean mm_modem_oma_cancel_session_sync   (MMModemOma *self,
                                             GCancellable *cancellable,
                                             GError **error);

MMOmaFeature      mm_modem_oma_get_features      (MMModemOma *self);
MMOmaSessionType  mm_modem_oma_get_session_type  (MMModemOma *self);
MMOmaSessionState mm_modem_oma_get_session_state (MMModemOma *self);

gboolean mm_modem_oma_peek_pending_network_initiated_sessions (MMModemOma                                 *self,
                                                               const MMOmaPendingNetworkInitiatedSession **sessions,
                                                               guint                                      *n_sessions);
gboolean mm_modem_oma_get_pending_network_initiated_sessions  (MMModemOma                                 *self,
                                                               MMOmaPendingNetworkInitiatedSession       **sessions,
                                                               guint                                      *n_sessions);

G_END_DECLS

#endif /* _MM_MODEM_OMA_H_ */
