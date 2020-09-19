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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 * Copyright (C) 2019 Purism SPC
 */

#ifndef MM_BASE_CALL_H
#define MM_BASE_CALL_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem.h"
#include "mm-call-audio-format.h"

#define MM_TYPE_BASE_CALL            (mm_base_call_get_type ())
#define MM_BASE_CALL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BASE_CALL, MMBaseCall))
#define MM_BASE_CALL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BASE_CALL, MMBaseCallClass))
#define MM_IS_BASE_CALL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BASE_CALL))
#define MM_IS_BASE_CALL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BASE_CALL))
#define MM_BASE_CALL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BASE_CALL, MMBaseCallClass))

typedef struct _MMBaseCall MMBaseCall;
typedef struct _MMBaseCallClass MMBaseCallClass;
typedef struct _MMBaseCallPrivate MMBaseCallPrivate;

#define MM_BASE_CALL_PATH                        "call-path"
#define MM_BASE_CALL_CONNECTION                  "call-connection"
#define MM_BASE_CALL_MODEM                       "call-modem"
#define MM_BASE_CALL_SKIP_INCOMING_TIMEOUT       "call-skip-incoming-timeout"
#define MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING "call-supports-dialing-to-ringing"
#define MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE  "call-supports-ringing-to-active"

struct _MMBaseCall {
    MmGdbusCallSkeleton parent;
    MMBaseCallPrivate *priv;
};

struct _MMBaseCallClass {
    MmGdbusCallSkeletonClass parent;

    /* Start the call */
    void     (* start)        (MMBaseCall *self,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gboolean (* start_finish) (MMBaseCall *self,
                               GAsyncResult *res,
                               GError **error);

    /* Accept the call */
    void     (* accept)        (MMBaseCall *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
    gboolean (* accept_finish) (MMBaseCall *self,
                                GAsyncResult *res,
                                GError **error);

    /* Deflect the call */
    void     (* deflect)        (MMBaseCall           *self,
                                 const gchar          *number,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data);
    gboolean (* deflect_finish) (MMBaseCall           *self,
                                 GAsyncResult         *res,
                                 GError              **error);

    /* Hangup the call */
    void     (* hangup)        (MMBaseCall *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
    gboolean (* hangup_finish) (MMBaseCall *self,
                                GAsyncResult *res,
                                GError **error);

    /* Send a DTMF tone */
    void     (* send_dtmf)        (MMBaseCall *self,
                                   const gchar *dtmf,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
    gboolean (* send_dtmf_finish) (MMBaseCall *self,
                                   GAsyncResult *res,
                                   GError **error);
};

GType mm_base_call_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBaseCall, g_object_unref)

/* This one can be overriden by plugins */
MMBaseCall *mm_base_call_new (MMBaseModem     *modem,
                              MMCallDirection  direction,
                              const gchar     *number,
                              gboolean         skip_incoming_timeout,
                              gboolean         supports_dialing_to_ringing,
                              gboolean         supports_ringing_to_active);

void             mm_base_call_export         (MMBaseCall *self);
void             mm_base_call_unexport       (MMBaseCall *self);

const gchar     *mm_base_call_get_path       (MMBaseCall *self);
const gchar     *mm_base_call_get_number     (MMBaseCall *self);
MMCallDirection  mm_base_call_get_direction  (MMBaseCall *self);
MMCallState      mm_base_call_get_state      (MMBaseCall *self);
guint            mm_base_call_get_index      (MMBaseCall *self);
gboolean         mm_base_call_get_multiparty (MMBaseCall *self);

void             mm_base_call_set_number     (MMBaseCall  *self,
                                              const gchar *number);
void             mm_base_call_set_index      (MMBaseCall  *self,
                                              guint        index);
void             mm_base_call_set_multiparty (MMBaseCall  *self,
                                              gboolean     multiparty);

void         mm_base_call_change_state (MMBaseCall *self,
                                        MMCallState new_state,
                                        MMCallStateReason reason);

void         mm_base_call_change_audio_settings (MMBaseCall        *self,
                                                 MMPort            *audio_port,
                                                 MMCallAudioFormat *audio_format);

void         mm_base_call_received_dtmf (MMBaseCall *self,
                                         const gchar *dtmf);

void         mm_base_call_incoming_refresh (MMBaseCall *self);

#endif /* MM_BASE_CALL_H */
