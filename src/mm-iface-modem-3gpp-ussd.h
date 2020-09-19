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
 * Copyright (C) 2010 Guido Guenther <agx@sigxcpu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_3GPP_USSD_H
#define MM_IFACE_MODEM_3GPP_USSD_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#define MM_TYPE_IFACE_MODEM_3GPP_USSD               (mm_iface_modem_3gpp_ussd_get_type ())
#define MM_IFACE_MODEM_3GPP_USSD(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_3GPP_USSD, MMIfaceModem3gppUssd))
#define MM_IS_IFACE_MODEM_3GPP_USSD(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_3GPP_USSD))
#define MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_3GPP_USSD, MMIfaceModem3gppUssd))

/* CBS data coding scheme - 3GPP TS 23.038 */
#define MM_MODEM_GSM_USSD_SCHEME_7BIT 0b00001111
#define MM_MODEM_GSM_USSD_SCHEME_UCS2 0b01001000

#define MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON "iface-modem-3gpp-ussd-dbus-skeleton"

typedef struct _MMIfaceModem3gppUssd MMIfaceModem3gppUssd;

struct _MMIfaceModem3gppUssd {
    GTypeInterface g_iface;

    /* Check for USSD support (async) */
    void     (* check_support)        (MMIfaceModem3gppUssd  *self,
                                       GAsyncReadyCallback    callback,
                                       gpointer               user_data);
    gboolean (* check_support_finish) (MMIfaceModem3gppUssd  *self,
                                       GAsyncResult          *res,
                                       GError               **error);

    /* Asynchronous setup of unsolicited events */
    void     (* setup_unsolicited_events)        (MMIfaceModem3gppUssd  *self,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer               user_data);
    gboolean (* setup_unsolicited_events_finish) (MMIfaceModem3gppUssd  *self,
                                                  GAsyncResult          *res,
                                                  GError               **error);

    /* Asynchronous enabling of unsolicited events */
    void     (* enable_unsolicited_events)        (MMIfaceModem3gppUssd  *self,
                                                   GAsyncReadyCallback    callback,
                                                   gpointer               user_data);
    gboolean (* enable_unsolicited_events_finish) (MMIfaceModem3gppUssd  *self,
                                                   GAsyncResult          *res,
                                                   GError               **error);

    /* Asynchronous disabling of unsolicited events */
    void     (* disable_unsolicited_events)        (MMIfaceModem3gppUssd  *self,
                                                    GAsyncReadyCallback    callback,
                                                    gpointer               user_data);
    gboolean (* disable_unsolicited_events_finish) (MMIfaceModem3gppUssd  *self,
                                                    GAsyncResult          *res,
                                                    GError               **error);

    /* Asynchronous cleaning up of unsolicited events */
    void     (* cleanup_unsolicited_events)        (MMIfaceModem3gppUssd  *self,
                                                    GAsyncReadyCallback    callback,
                                                    gpointer               user_data);
    gboolean (* cleanup_unsolicited_events_finish) (MMIfaceModem3gppUssd  *self,
                                                    GAsyncResult          *res,
                                                    GError               **error);

    /* Encode/Decode */
    gchar * (*encode) (MMIfaceModem3gppUssd *self,
                       const gchar *command,
                       guint *scheme,
                       GError **error);
    gchar * (*decode) (MMIfaceModem3gppUssd *self,
                       const gchar *reply,
                       GError **error);

    /* Send command */
    void (* send) (MMIfaceModem3gppUssd *self,
                   const gchar *command,
                   GAsyncReadyCallback callback,
                   gpointer user_data);
    gchar * (* send_finish) (MMIfaceModem3gppUssd *self,
                             GAsyncResult *res,
                             GError **error);

    /* Cancel */
    void (* cancel) (MMIfaceModem3gppUssd *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data);
    gboolean (* cancel_finish) (MMIfaceModem3gppUssd *self,
                                GAsyncResult *res,
                                GError **error);
};

GType mm_iface_modem_3gpp_ussd_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModem3gppUssd, g_object_unref)

/* Initialize USSD interface (async) */
void     mm_iface_modem_3gpp_ussd_initialize        (MMIfaceModem3gppUssd *self,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gboolean mm_iface_modem_3gpp_ussd_initialize_finish (MMIfaceModem3gppUssd *self,
                                                     GAsyncResult *res,
                                                     GError **error);

/* Enable USSD interface (async) */
void     mm_iface_modem_3gpp_ussd_enable        (MMIfaceModem3gppUssd *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mm_iface_modem_3gpp_ussd_enable_finish (MMIfaceModem3gppUssd *self,
                                                 GAsyncResult *res,
                                                 GError **error);

/* Disable USSD interface (async) */
void     mm_iface_modem_3gpp_ussd_disable        (MMIfaceModem3gppUssd *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean mm_iface_modem_3gpp_ussd_disable_finish (MMIfaceModem3gppUssd *self,
                                                  GAsyncResult *res,
                                                  GError **error);

MMModem3gppUssdSessionState mm_iface_modem_3gpp_ussd_get_state (MMIfaceModem3gppUssd *self);

/* Property updaters */
void mm_iface_modem_3gpp_ussd_update_state                (MMIfaceModem3gppUssd *self,
                                                           MMModem3gppUssdSessionState new_state);
void mm_iface_modem_3gpp_ussd_update_network_notification (MMIfaceModem3gppUssd *self,
                                                           const gchar *network_notification);
void mm_iface_modem_3gpp_ussd_update_network_request      (MMIfaceModem3gppUssd *self,
                                                           const gchar *network_request);

/* Encode/Decode USSD */
gchar *mm_iface_modem_3gpp_ussd_encode (MMIfaceModem3gppUssd *self,
                                        const gchar *command,
                                        guint *scheme,
                                        GError **error);
gchar *mm_iface_modem_3gpp_ussd_decode (MMIfaceModem3gppUssd *self,
                                        const gchar *reply,
                                        GError **error);

/* Shutdown USSD interface */
void mm_iface_modem_3gpp_ussd_shutdown (MMIfaceModem3gppUssd *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_3gpp_ussd_bind_simple_status (MMIfaceModem3gppUssd *self,
                                                  MMSimpleStatus *status);

#endif /* MM_IFACE_MODEM_3GPP_USSD_H */
