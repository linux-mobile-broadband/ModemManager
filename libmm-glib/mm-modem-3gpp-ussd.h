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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_3GPP_USSD_H_
#define _MM_MODEM_3GPP_USSD_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_3GPP_USSD            (mm_modem_3gpp_ussd_get_type ())
#define MM_MODEM_3GPP_USSD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_3GPP_USSD, MMModem3gppUssd))
#define MM_MODEM_3GPP_USSD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_3GPP_USSD, MMModem3gppUssdClass))
#define MM_IS_MODEM_3GPP_USSD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_3GPP_USSD))
#define MM_IS_MODEM_3GPP_USSD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_3GPP_USSD))
#define MM_MODEM_3GPP_USSD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_3GPP_USSD, MMModem3gppUssdClass))

typedef struct _MMModem3gppUssd MMModem3gppUssd;
typedef struct _MMModem3gppUssdClass MMModem3gppUssdClass;

/**
 * MMModem3gppUssd:
 *
 * The #MMModem3gppUssd structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModem3gppUssd {
    /*< private >*/
    MmGdbusModem3gppUssdProxy parent;
    gpointer unused;
};

struct _MMModem3gppUssdClass {
    /*< private >*/
    MmGdbusModem3gppUssdProxyClass parent;
};

GType mm_modem_3gpp_ussd_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModem3gppUssd, g_object_unref)

const gchar *mm_modem_3gpp_ussd_get_path (MMModem3gppUssd *self);
gchar       *mm_modem_3gpp_ussd_dup_path (MMModem3gppUssd *self);

MMModem3gppUssdSessionState mm_modem_3gpp_ussd_get_state (MMModem3gppUssd *self);

const gchar *mm_modem_3gpp_ussd_get_network_notification (MMModem3gppUssd *self);
gchar       *mm_modem_3gpp_ussd_dup_network_notification (MMModem3gppUssd *self);

const gchar *mm_modem_3gpp_ussd_get_network_request      (MMModem3gppUssd *self);
gchar       *mm_modem_3gpp_ussd_dup_network_request      (MMModem3gppUssd *self);

void   mm_modem_3gpp_ussd_initiate        (MMModem3gppUssd *self,
                                           const gchar *command,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gchar *mm_modem_3gpp_ussd_initiate_finish (MMModem3gppUssd *self,
                                           GAsyncResult *res,
                                           GError **error);
gchar *mm_modem_3gpp_ussd_initiate_sync   (MMModem3gppUssd *self,
                                           const gchar *command,
                                           GCancellable *cancellable,
                                           GError **error);

void   mm_modem_3gpp_ussd_respond        (MMModem3gppUssd *self,
                                          const gchar *response,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);
gchar *mm_modem_3gpp_ussd_respond_finish (MMModem3gppUssd *self,
                                          GAsyncResult *res,
                                          GError **error);
gchar *mm_modem_3gpp_ussd_respond_sync   (MMModem3gppUssd *self,
                                          const gchar *response,
                                          GCancellable *cancellable,
                                          GError **error);

void     mm_modem_3gpp_ussd_cancel        (MMModem3gppUssd *self,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_modem_3gpp_ussd_cancel_finish (MMModem3gppUssd *self,
                                           GAsyncResult *res,
                                           GError **error);
gboolean mm_modem_3gpp_ussd_cancel_sync   (MMModem3gppUssd *self,
                                           GCancellable *cancellable,
                                           GError **error);

G_END_DECLS

#endif /* _MM_MODEM_3GPP_USSD_H_ */
