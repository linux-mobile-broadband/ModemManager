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
 * Copyright (C) 2013 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_OMA_H
#define MM_IFACE_MODEM_OMA_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#define MM_TYPE_IFACE_MODEM_OMA               (mm_iface_modem_oma_get_type ())
#define MM_IFACE_MODEM_OMA(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_OMA, MMIfaceModemOma))
#define MM_IS_IFACE_MODEM_OMA(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_OMA))
#define MM_IFACE_MODEM_OMA_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_OMA, MMIfaceModemOma))

#define MM_IFACE_MODEM_OMA_DBUS_SKELETON "iface-modem-oma-dbus-skeleton"

typedef struct _MMIfaceModemOma MMIfaceModemOma;

struct _MMIfaceModemOma {
    GTypeInterface g_iface;

    /* Check for Oma support (async) */
    void     (* check_support)        (MMIfaceModemOma *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    gboolean (* check_support_finish) (MMIfaceModemOma *self,
                                       GAsyncResult *res,
                                       GError **error);

    /* Asynchronous setting up unsolicited events */
    void     (* setup_unsolicited_events)        (MMIfaceModemOma *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
    gboolean (* setup_unsolicited_events_finish) (MMIfaceModemOma *self,
                                                  GAsyncResult *res,
                                                  GError **error);

    /* Asynchronous cleaning up of unsolicited events */
    void     (* cleanup_unsolicited_events)        (MMIfaceModemOma *self,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data);
    gboolean (* cleanup_unsolicited_events_finish) (MMIfaceModemOma *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Asynchronous enabling unsolicited events */
    void     (* enable_unsolicited_events)        (MMIfaceModemOma *self,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
    gboolean (* enable_unsolicited_events_finish) (MMIfaceModemOma *self,
                                                   GAsyncResult *res,
                                                   GError **error);

    /* Asynchronous disabling unsolicited events */
    void     (* disable_unsolicited_events)        (MMIfaceModemOma *self,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data);
    gboolean (* disable_unsolicited_events_finish) (MMIfaceModemOma *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Get current features */
    void         (* load_features)        (MMIfaceModemOma *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
    MMOmaFeature (* load_features_finish) (MMIfaceModemOma *self,
                                           GAsyncResult *res,
                                           GError **error);

    /* Setup */
    void     (* setup)        (MMIfaceModemOma *self,
                               MMOmaFeature features,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gboolean (* setup_finish) (MMIfaceModemOma *self,
                               GAsyncResult *res,
                               GError **error);

    /* Start client-initiated session */
    void     (* start_client_initiated_session)        (MMIfaceModemOma *self,
                                                        MMOmaSessionType session_type,
                                                        GAsyncReadyCallback callback,
                                                        gpointer user_data);
    gboolean (* start_client_initiated_session_finish) (MMIfaceModemOma *self,
                                                        GAsyncResult *res,
                                                        GError **error);

    /* Accept network-initiated session */
    void     (* accept_network_initiated_session)        (MMIfaceModemOma *self,
                                                          guint session_id,
                                                          gboolean accept,
                                                          GAsyncReadyCallback callback,
                                                          gpointer user_data);
    gboolean (* accept_network_initiated_session_finish) (MMIfaceModemOma *self,
                                                          GAsyncResult *res,
                                                          GError **error);

    /* Cancel session */
    void     (* cancel_session)        (MMIfaceModemOma *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (* cancel_session_finish) (MMIfaceModemOma *self,
                                        GAsyncResult *res,
                                        GError **error);
};

GType mm_iface_modem_oma_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModemOma, g_object_unref)

/* Initialize Oma interface (async) */
void     mm_iface_modem_oma_initialize        (MMIfaceModemOma *self,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
gboolean mm_iface_modem_oma_initialize_finish (MMIfaceModemOma *self,
                                               GAsyncResult *res,
                                               GError **error);

/* Enable Oma interface (async) */
void     mm_iface_modem_oma_enable        (MMIfaceModemOma *self,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_iface_modem_oma_enable_finish (MMIfaceModemOma *self,
                                           GAsyncResult *res,
                                           GError **error);

/* Disable Oma interface (async) */
void     mm_iface_modem_oma_disable        (MMIfaceModemOma *self,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_iface_modem_oma_disable_finish (MMIfaceModemOma *self,
                                            GAsyncResult *res,
                                            GError **error);

/* Shutdown Oma interface */
void mm_iface_modem_oma_shutdown (MMIfaceModemOma *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_oma_bind_simple_status (MMIfaceModemOma *self,
                                            MMSimpleStatus *status);

/* Report new pending network-initiated session */
void mm_iface_modem_oma_add_pending_network_initiated_session (MMIfaceModemOma *self,
                                                               MMOmaSessionType session_type,
                                                               guint session_id);

/* Report new session state */
void mm_iface_modem_oma_update_session_state (MMIfaceModemOma *self,
                                              MMOmaSessionState session_state,
                                              MMOmaSessionStateFailedReason session_state_failed_reason);

#endif /* MM_IFACE_MODEM_OMA_H */
