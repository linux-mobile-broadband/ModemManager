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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MM_MODEM_CDMA_H_
#define _MM_MODEM_CDMA_H_

#include <ModemManager.h>
#include <mm-gdbus-modem.h>

G_BEGIN_DECLS

#define MM_MODEM_CDMA_SID_UNKNOWN 99999
#define MM_MODEM_CDMA_NID_UNKNOWN 99999

typedef MmGdbusModemCdma      MMModemCdma;
#define MM_TYPE_MODEM_CDMA(o) MM_GDBUS_TYPE_MODEM_CDMA (o)
#define MM_MODEM_CDMA(o)      MM_GDBUS_MODEM_CDMA(o)
#define MM_IS_MODEM_CDMA(o)   MM_GDBUS_IS_MODEM_CDMA(o)

const gchar *mm_modem_cdma_get_path (MMModemCdma *self);
gchar       *mm_modem_cdma_dup_path (MMModemCdma *self);

const gchar *mm_modem_cdma_get_meid (MMModemCdma *self);
gchar       *mm_modem_cdma_dup_meid (MMModemCdma *self);
const gchar *mm_modem_cdma_get_esn  (MMModemCdma *self);
gchar       *mm_modem_cdma_dup_esn  (MMModemCdma *self);
guint        mm_modem_cdma_get_sid  (MMModemCdma *self);
guint        mm_modem_cdma_get_nid  (MMModemCdma *self);
MMModemCdmaRegistrationState mm_modem_cdma_get_cdma1x_registration_state (MMModemCdma *self);
MMModemCdmaRegistrationState mm_modem_cdma_get_evdo_registration_state   (MMModemCdma *self);

void     mm_modem_cdma_activate        (MMModemCdma *self,
                                        const gchar *carrier,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_modem_cdma_activate_finish (MMModemCdma *self,
                                        GAsyncResult *res,
                                        GError **error);
gboolean mm_modem_cdma_activate_sync   (MMModemCdma *self,
                                        const gchar *carrier,
                                        GCancellable *cancellable,
                                        GError **error);

G_END_DECLS

#endif /* _MM_MODEM_CDMA_H_ */
