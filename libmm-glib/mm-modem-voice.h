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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Marco Bascetta <marco.bascetta@sadel.it>
 */

#ifndef _MM_MODEM_VOICE_H_
#define _MM_MODEM_VOICE_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-call.h"
#include "mm-call-properties.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_VOICE            (mm_modem_voice_get_type ())
#define MM_MODEM_VOICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_VOICE, MMModemVoice))
#define MM_MODEM_VOICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_VOICE, MMModemVoiceClass))
#define MM_IS_MODEM_VOICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_VOICE))
#define MM_IS_MODEM_VOICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_VOICE))
#define MM_MODEM_VOICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_VOICE, MMModemVoiceClass))

typedef struct _MMModemVoice MMModemVoice;
typedef struct _MMModemVoiceClass MMModemVoiceClass;
typedef struct _MMModemVoicePrivate MMModemVoicePrivate;

/**
 * MMModemVoice:
 *
 * The #MMModemVoice structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemVoice {
    /*< private >*/
    MmGdbusModemVoiceProxy parent;
    MMModemVoicePrivate *priv;
};

struct _MMModemVoiceClass {
    /*< private >*/
    MmGdbusModemVoiceProxyClass parent;
};

GType mm_modem_voice_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemVoice, g_object_unref)

const gchar *mm_modem_voice_get_path           (MMModemVoice *self);
gchar       *mm_modem_voice_dup_path           (MMModemVoice *self);
gboolean     mm_modem_voice_get_emergency_only (MMModemVoice *self);

void    mm_modem_voice_create_call          (MMModemVoice *self,
                                             MMCallProperties *properties,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
MMCall *mm_modem_voice_create_call_finish   (MMModemVoice *self,
                                             GAsyncResult *res,
                                             GError **error);
MMCall *mm_modem_voice_create_call_sync     (MMModemVoice *self,
                                             MMCallProperties *properties,
                                             GCancellable *cancellable,
                                             GError **error);

void   mm_modem_voice_list_calls            (MMModemVoice *self,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
GList *mm_modem_voice_list_calls_finish     (MMModemVoice *self,
                                             GAsyncResult *res,
                                             GError **error);
GList *mm_modem_voice_list_calls_sync       (MMModemVoice *self,
                                             GCancellable *cancellable,
                                             GError **error);

void     mm_modem_voice_delete_call         (MMModemVoice *self,
                                             const gchar *call,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
gboolean mm_modem_voice_delete_call_finish  (MMModemVoice *self,
                                             GAsyncResult *res,
                                             GError **error);
gboolean mm_modem_voice_delete_call_sync    (MMModemVoice *self,
                                             const gchar *call,
                                             GCancellable *cancellable,
                                             GError **error);

void     mm_modem_voice_hold_and_accept         (MMModemVoice        *self,
                                                 GCancellable        *cancellable,
                                                 GAsyncReadyCallback  callback,
                                                 gpointer             user_data);
gboolean mm_modem_voice_hold_and_accept_finish  (MMModemVoice        *self,
                                                 GAsyncResult        *res,
                                                 GError             **error);
gboolean mm_modem_voice_hold_and_accept_sync    (MMModemVoice        *self,
                                                 GCancellable        *cancellable,
                                                 GError             **error);

void     mm_modem_voice_hangup_and_accept         (MMModemVoice        *self,
                                                   GCancellable        *cancellable,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data);
gboolean mm_modem_voice_hangup_and_accept_finish  (MMModemVoice        *self,
                                                   GAsyncResult        *res,
                                                   GError             **error);
gboolean mm_modem_voice_hangup_and_accept_sync    (MMModemVoice        *self,
                                                   GCancellable        *cancellable,
                                                   GError             **error);

void     mm_modem_voice_hangup_all         (MMModemVoice        *self,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);
gboolean mm_modem_voice_hangup_all_finish  (MMModemVoice        *self,
                                            GAsyncResult        *res,
                                            GError             **error);
gboolean mm_modem_voice_hangup_all_sync    (MMModemVoice        *self,
                                            GCancellable        *cancellable,
                                            GError             **error);

void     mm_modem_voice_transfer         (MMModemVoice        *self,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data);
gboolean mm_modem_voice_transfer_finish  (MMModemVoice        *self,
                                          GAsyncResult        *res,
                                          GError             **error);
gboolean mm_modem_voice_transfer_sync    (MMModemVoice        *self,
                                          GCancellable        *cancellable,
                                          GError             **error);

void     mm_modem_voice_call_waiting_setup         (MMModemVoice        *self,
                                                    gboolean             enable,
                                                    GCancellable        *cancellable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data);
gboolean mm_modem_voice_call_waiting_setup_finish  (MMModemVoice        *self,
                                                    GAsyncResult        *res,
                                                    GError             **error);
gboolean mm_modem_voice_call_waiting_setup_sync    (MMModemVoice        *self,
                                                    gboolean             enable,
                                                    GCancellable        *cancellable,
                                                    GError             **error);

void     mm_modem_voice_call_waiting_query         (MMModemVoice        *self,
                                                    GCancellable        *cancellable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data);
gboolean mm_modem_voice_call_waiting_query_finish  (MMModemVoice        *self,
                                                    GAsyncResult        *res,
                                                    gboolean            *status,
                                                    GError             **error);
gboolean mm_modem_voice_call_waiting_query_sync    (MMModemVoice        *self,
                                                    GCancellable        *cancellable,
                                                    gboolean            *status,
                                                    GError             **error);

G_END_DECLS

#endif /* _MM_MODEM_VOICE_H_ */
