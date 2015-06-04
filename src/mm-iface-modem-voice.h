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
 * Copyright (C) 2015 - Marco Bascetta <marco.bascetta@sadel.it>
 */

#ifndef MM_IFACE_MODEM_VOICE_H
#define MM_IFACE_MODEM_VOICE_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-call.h"

#define MM_TYPE_IFACE_MODEM_VOICE               (mm_iface_modem_voice_get_type ())
#define MM_IFACE_MODEM_VOICE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_VOICE, MMIfaceModemVoice))
#define MM_IS_IFACE_MODEM_VOICE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_VOICE))
#define MM_IFACE_MODEM_VOICE_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_VOICE, MMIfaceModemVoice))

#define MM_IFACE_MODEM_VOICE_DBUS_SKELETON          "iface-modem-voice-dbus-skeleton"
#define MM_IFACE_MODEM_VOICE_CALL_LIST              "iface-modem-voice-call-list"

typedef struct _MMIfaceModemVoice MMIfaceModemVoice;

struct _MMIfaceModemVoice {
    GTypeInterface g_iface;

    /* Check for Voice support (async) */
    void (* check_support) (MMIfaceModemVoice *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
    gboolean (*check_support_finish) (MMIfaceModemVoice *self,
                                      GAsyncResult *res,
                                      GError **error);

    /* Asynchronous setting up unsolicited CALL reception events */
    void (*setup_unsolicited_events) (MMIfaceModemVoice *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
    gboolean (*setup_unsolicited_events_finish) (MMIfaceModemVoice *self,
                                                 GAsyncResult *res,
                                                 GError **error);

    /* Asynchronous cleaning up of unsolicited CALL reception events */
    void (*cleanup_unsolicited_events) (MMIfaceModemVoice *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (*cleanup_unsolicited_events_finish) (MMIfaceModemVoice *self,
                                                   GAsyncResult *res,
                                                   GError **error);

    /* Asynchronous enabling unsolicited CALL reception events */
    void (* enable_unsolicited_events) (MMIfaceModemVoice *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (* enable_unsolicited_events_finish) (MMIfaceModemVoice *self,
                                                   GAsyncResult *res,
                                                   GError **error);

    /* Asynchronous disabling unsolicited CALL reception events */
    void (* disable_unsolicited_events) (MMIfaceModemVoice *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (* disable_unsolicited_events_finish) (MMIfaceModemVoice *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Create CALL objects */
    MMBaseCall * (* create_call) (MMIfaceModemVoice *self);
};

GType mm_iface_modem_voice_get_type (void);

/* Initialize Voice interface (async) */
void     mm_iface_modem_voice_initialize        (MMIfaceModemVoice *self,
                                                 GCancellable *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mm_iface_modem_voice_initialize_finish (MMIfaceModemVoice *self,
                                                 GAsyncResult *res,
                                                 GError **error);

/* Enable Voice interface (async) */
void     mm_iface_modem_voice_enable        (MMIfaceModemVoice *self,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
gboolean mm_iface_modem_voice_enable_finish (MMIfaceModemVoice *self,
                                             GAsyncResult *res,
                                             GError **error);

/* Disable Voice interface (async) */
void     mm_iface_modem_voice_disable        (MMIfaceModemVoice *self,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
gboolean mm_iface_modem_voice_disable_finish (MMIfaceModemVoice *self,
                                              GAsyncResult *res,
                                              GError **error);

/* Shutdown Voice interface */
void mm_iface_modem_voice_shutdown (MMIfaceModemVoice *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_voice_bind_simple_status (MMIfaceModemVoice *self,
                                              MMSimpleStatus *status);

/* CALL creation */
MMBaseCall *mm_iface_modem_voice_create_call                    (MMIfaceModemVoice *self);
MMBaseCall *mm_iface_modem_voice_create_incoming_call           (MMIfaceModemVoice *self);
gboolean    mm_iface_modem_voice_update_incoming_call_number    (MMIfaceModemVoice *self,
                                                                 gchar *number,
                                                                 guint type,
                                                                 guint validity);
gboolean    mm_iface_modem_voice_call_dialing_to_ringing        (MMIfaceModemVoice *self);
gboolean    mm_iface_modem_voice_call_ringing_to_active         (MMIfaceModemVoice *self);
gboolean    mm_iface_modem_voice_network_hangup                 (MMIfaceModemVoice *self);
gboolean    mm_iface_modem_voice_received_dtmf                  (MMIfaceModemVoice *self,
                                                                 gchar *dtmf);

/* Look for a new valid multipart reference */
guint8 mm_iface_modem_voice_get_local_multipart_reference (MMIfaceModemVoice *self,
                                                           const gchar *number,
                                                           GError **error);

#endif /* MM_IFACE_MODEM_VOICE_H */
