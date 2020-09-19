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
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
 * Copyright (C) 2019 Purism SPC
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

#define MM_IFACE_MODEM_VOICE_DBUS_SKELETON                     "iface-modem-voice-dbus-skeleton"
#define MM_IFACE_MODEM_VOICE_CALL_LIST                         "iface-modem-voice-call-list"
#define MM_IFACE_MODEM_VOICE_PERIODIC_CALL_LIST_CHECK_DISABLED "iface-modem-voice-periodic-call-list-check-disabled"

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

    /* Asynchronous setup of in-call unsolicited events */
    void     (* setup_in_call_unsolicited_events)        (MMIfaceModemVoice   *self,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
    gboolean (* setup_in_call_unsolicited_events_finish) (MMIfaceModemVoice   *self,
                                                          GAsyncResult        *res,
                                                          GError             **error);

    /* Asynchronous cleanup of in-call unsolicited events */
    void     (* cleanup_in_call_unsolicited_events)        (MMIfaceModemVoice   *self,
                                                            GAsyncReadyCallback  callback,
                                                            gpointer             user_data);
    gboolean (* cleanup_in_call_unsolicited_events_finish) (MMIfaceModemVoice   *self,
                                                            GAsyncResult        *res,
                                                            GError             **error);

    /* Asynchronous setup of in-call audio channel */
    void     (* setup_in_call_audio_channel)        (MMIfaceModemVoice   *self,
                                                     GAsyncReadyCallback  callback,
                                                     gpointer             user_data);
    gboolean (* setup_in_call_audio_channel_finish) (MMIfaceModemVoice   *self,
                                                     GAsyncResult        *res,
                                                     MMPort             **audio_port,   /* optional */
                                                     MMCallAudioFormat  **audio_format, /* optional */
                                                     GError             **error);

    /* Asynchronous cleanup of in-call audio channel */
    void     (* cleanup_in_call_audio_channel)        (MMIfaceModemVoice   *self,
                                                       GAsyncReadyCallback  callback,
                                                       gpointer             user_data);
    gboolean (* cleanup_in_call_audio_channel_finish) (MMIfaceModemVoice   *self,
                                                       GAsyncResult        *res,
                                                       GError             **error);

    /* Load full list of calls (MMCallInfo list) */
    void     (* load_call_list)        (MMIfaceModemVoice    *self,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data);
    gboolean (* load_call_list_finish) (MMIfaceModemVoice    *self,
                                        GAsyncResult         *res,
                                        GList               **call_info_list,
                                        GError              **error);

    /* Create call objects */
    MMBaseCall * (* create_call) (MMIfaceModemVoice *self,
                                  MMCallDirection    direction,
                                  const gchar       *number);

    /* Hold and accept */
    void     (* hold_and_accept)        (MMIfaceModemVoice    *self,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data);
    gboolean (* hold_and_accept_finish) (MMIfaceModemVoice    *self,
                                         GAsyncResult         *res,
                                         GError              **error);

    /* Hangup and accept */
    void     (* hangup_and_accept)        (MMIfaceModemVoice    *self,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
    gboolean (* hangup_and_accept_finish) (MMIfaceModemVoice    *self,
                                           GAsyncResult         *res,
                                           GError              **error);

    /* Hangup all */
    void     (* hangup_all)        (MMIfaceModemVoice    *self,
                                    GAsyncReadyCallback   callback,
                                    gpointer              user_data);
    gboolean (* hangup_all_finish) (MMIfaceModemVoice    *self,
                                    GAsyncResult         *res,
                                    GError              **error);

    /* Join multiparty */
    void     (* join_multiparty)        (MMIfaceModemVoice    *self,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data);
    gboolean (* join_multiparty_finish) (MMIfaceModemVoice    *self,
                                         GAsyncResult         *res,
                                         GError              **error);

    /* Leave multiparty */
    void     (* leave_multiparty)        (MMIfaceModemVoice    *self,
                                          MMBaseCall           *call,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
    gboolean (* leave_multiparty_finish) (MMIfaceModemVoice    *self,
                                          GAsyncResult         *res,
                                          GError              **error);

    /* Transfer */
    void     (* transfer)        (MMIfaceModemVoice    *self,
                                  GAsyncReadyCallback   callback,
                                  gpointer              user_data);
    gboolean (* transfer_finish) (MMIfaceModemVoice    *self,
                                  GAsyncResult         *res,
                                  GError              **error);

    /* Call waiting setup */
    void     (* call_waiting_setup)        (MMIfaceModemVoice    *self,
                                            gboolean              enable,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data);
    gboolean (* call_waiting_setup_finish) (MMIfaceModemVoice    *self,
                                            GAsyncResult         *res,
                                            GError              **error);

    /* Call waiting query */
    void     (* call_waiting_query)        (MMIfaceModemVoice    *self,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data);
    gboolean (* call_waiting_query_finish) (MMIfaceModemVoice    *self,
                                            GAsyncResult         *res,
                                            gboolean             *status,
                                            GError              **error);
};

GType mm_iface_modem_voice_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModemVoice, g_object_unref)

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
                                              MMSimpleStatus    *status);

/* Single call info reporting */
void mm_iface_modem_voice_report_call (MMIfaceModemVoice *self,
                                       const MMCallInfo  *call_info);

/* Full current call list reporting (MMCallInfo list) */
void mm_iface_modem_voice_report_all_calls (MMIfaceModemVoice *self,
                                            GList             *call_info_list);

/* Report an incoming DTMF received */
void mm_iface_modem_voice_received_dtmf (MMIfaceModemVoice *self,
                                         guint              index,
                                         const gchar       *dtmf);

/* Authorize outgoing call based on modem status and ECC list */
gboolean mm_iface_modem_voice_authorize_outgoing_call (MMIfaceModemVoice  *self,
                                                       MMBaseCall         *call,
                                                       GError            **error);

/* Join/Leave multiparty calls
 *
 * These actions are provided in the Call API, but implemented in the
 * modem Voice interface because they really affect multiple calls at
 * the same time.
 */
void     mm_iface_modem_voice_join_multiparty         (MMIfaceModemVoice    *self,
                                                       MMBaseCall           *call,
                                                       GAsyncReadyCallback   callback,
                                                       gpointer              user_data);
gboolean mm_iface_modem_voice_join_multiparty_finish  (MMIfaceModemVoice    *self,
                                                       GAsyncResult         *res,
                                                       GError              **error);
void     mm_iface_modem_voice_leave_multiparty        (MMIfaceModemVoice    *self,
                                                       MMBaseCall           *call,
                                                       GAsyncReadyCallback   callback,
                                                       gpointer              user_data);
gboolean mm_iface_modem_voice_leave_multiparty_finish (MMIfaceModemVoice    *self,
                                                       GAsyncResult         *res,
                                                       GError              **error);

#endif /* MM_IFACE_MODEM_VOICE_H */
