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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#ifndef MM_FAKE_CALL_H
#define MM_FAKE_CALL_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-call.h"
#include "mm-iface-modem-voice.h"

#define MM_TYPE_FAKE_CALL            (mm_fake_call_get_type ())
#define MM_FAKE_CALL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_FAKE_CALL, MMFakeCall))
#define MM_FAKE_CALL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_FAKE_CALL, MMFakeCallClass))
#define MM_IS_FAKE_CALL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_FAKE_CALL))
#define MM_IS_FAKE_CALL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_FAKE_CALL))
#define MM_FAKE_CALL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_FAKE_CALL, MMFakeCallClass))

typedef struct _MMFakeCall MMFakeCall;
typedef struct _MMFakeCallClass MMFakeCallClass;
typedef struct _MMFakeCallPrivate MMFakeCallPrivate;

struct _MMFakeCallPrivate {
    const gchar *start_error_msg;
    const gchar *accept_error_msg;
    const gchar *deflect_error_msg;
    const gchar *hangup_error_msg;

    /* DTMF */
    const gchar *dtmf_error_msg;
    const gchar *dtmf_stop_error_msg;
    /* How many DTMF characters we can accept at a time */
    guint  dtmf_accept_len;
    /* How many DTMF characters were actually accepted */
    guint        dtmf_num_accepted;
    GString     *dtmf_sent;
    gboolean     dtmf_in_send;
    gboolean     dtmf_stop_called;

    guint idle_id;
};

struct _MMFakeCall {
    MMBaseCall parent;
    MMFakeCallPrivate *priv;
};

struct _MMFakeCallClass {
    MMBaseCallClass parent;
};

GType mm_fake_call_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMFakeCall, g_object_unref)

MMFakeCall *mm_fake_call_new (GDBusConnection   *connection,
                              MMIfaceModemVoice *voice,
                              MMCallDirection    direction,
                              const gchar       *number,
                              const guint        dtmf_tone_duration);

void mm_fake_call_enable_dtmf_stop (MMFakeCall *self,
                                    gboolean    enable);

#endif /* MM_FAKE_CALL_H */
