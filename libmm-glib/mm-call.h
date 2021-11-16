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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
 */

#ifndef _MM_CALL_H_
#define _MM_CALL_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-call.h"
#include "mm-call-audio-format.h"

G_BEGIN_DECLS

#define MM_TYPE_CALL            (mm_call_get_type ())
#define MM_CALL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL, MMCall))
#define MM_CALL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_CALL, MMCallClass))
#define MM_IS_CALL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL))
#define MM_IS_CALL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_CALL))
#define MM_CALL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_CALL, MMCallClass))

typedef struct _MMCall MMCall;
typedef struct _MMCallClass MMCallClass;
typedef struct _MMCallPrivate MMCallPrivate;

/**
 * MMCall:
 *
 * The #MMCall structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMCall {
    /*< private >*/
    MmGdbusCallProxy parent;
    MMCallPrivate *priv;
};

struct _MMCallClass {
    /*< private >*/
    MmGdbusCallProxyClass parent;
};

GType mm_call_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCall, g_object_unref)

const gchar       *mm_call_get_path         (MMCall *self);
gchar             *mm_call_dup_path         (MMCall *self);

const gchar       *mm_call_get_number       (MMCall *self);
gchar             *mm_call_dup_number       (MMCall *self);

MMCallState        mm_call_get_state        (MMCall *self);

MMCallStateReason  mm_call_get_state_reason (MMCall *self);

MMCallDirection    mm_call_get_direction    (MMCall *self);

gboolean           mm_call_get_multiparty   (MMCall *self);

const gchar       *mm_call_get_audio_port   (MMCall *self);
gchar             *mm_call_dup_audio_port   (MMCall *self);

MMCallAudioFormat *mm_call_get_audio_format (MMCall *self);
MMCallAudioFormat *mm_call_peek_audio_format(MMCall *self);


void               mm_call_start            (MMCall *self,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);

gboolean           mm_call_start_finish     (MMCall *self,
                                             GAsyncResult *res,
                                             GError **error);

gboolean           mm_call_start_sync       (MMCall *self,
                                             GCancellable *cancellable,
                                             GError **error);


void               mm_call_accept           (MMCall *self,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);

gboolean           mm_call_accept_finish    (MMCall *self,
                                             GAsyncResult *res,
                                             GError **error);

gboolean           mm_call_accept_sync      (MMCall *self,
                                             GCancellable *cancellable,
                                             GError **error);

void               mm_call_deflect          (MMCall *self,
                                             const gchar *number,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);

gboolean           mm_call_deflect_finish   (MMCall *self,
                                             GAsyncResult *res,
                                             GError **error);

gboolean           mm_call_deflect_sync     (MMCall *self,
                                             const gchar *number,
                                             GCancellable *cancellable,
                                             GError **error);

void               mm_call_join_multiparty        (MMCall              *self,
                                                   GCancellable        *cancellable,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data);

gboolean           mm_call_join_multiparty_finish (MMCall        *self,
                                                   GAsyncResult  *res,
                                                   GError       **error);

gboolean           mm_call_join_multiparty_sync   (MMCall        *self,
                                                   GCancellable  *cancellable,
                                                   GError       **error);

void               mm_call_leave_multiparty        (MMCall              *self,
                                                    GCancellable        *cancellable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data);

gboolean           mm_call_leave_multiparty_finish (MMCall        *self,
                                                    GAsyncResult  *res,
                                                    GError       **error);

gboolean           mm_call_leave_multiparty_sync   (MMCall        *self,
                                                    GCancellable  *cancellable,
                                                    GError       **error);

void               mm_call_hangup           (MMCall *self,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);

gboolean           mm_call_hangup_finish    (MMCall *self,
                                             GAsyncResult *res,
                                             GError **error);

gboolean           mm_call_hangup_sync      (MMCall *self,
                                             GCancellable *cancellable,
                                             GError **error);


void               mm_call_send_dtmf        (MMCall *self,
                                             const gchar *dtmf,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);

gboolean           mm_call_send_dtmf_finish (MMCall *self,
                                             GAsyncResult *res,
                                             GError **error);

gboolean           mm_call_send_dtmf_sync   (MMCall *self,
                                             const gchar *dtmf,
                                             GCancellable *cancellable,
                                             GError **error);

G_END_DECLS

#endif /* _MM_CALL_H_ */
