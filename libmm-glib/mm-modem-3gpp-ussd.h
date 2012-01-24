/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_3GPP_USSD_H_
#define _MM_MODEM_3GPP_USSD_H_

#include <ModemManager.h>
#include <mm-gdbus-modem.h>

G_BEGIN_DECLS

typedef MmGdbusModem3gppUssd      MMModem3gppUssd;
#define MM_TYPE_MODEM_3GPP_USSD(o) MM_GDBUS_TYPE_MODEM3GPP_USSD (o)
#define MM_MODEM_3GPP_USSD(o)      MM_GDBUS_MODEM3GPP_USSD(o)
#define MM_IS_MODEM_3GPP_USSD(o)   MM_GDBUS_IS_MODEM3GPP_USSD(o)

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
