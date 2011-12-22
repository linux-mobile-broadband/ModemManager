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

#ifndef _MM_MODEM_SIMPLE_H_
#define _MM_MODEM_SIMPLE_H_

#include <ModemManager.h>
#include <mm-gdbus-modem.h>

#include "mm-bearer.h"

G_BEGIN_DECLS

typedef MmGdbusModemSimple      MMModemSimple;
#define MM_TYPE_MODEM_SIMPLE(o) MM_GDBUS_TYPE_MODEM_SIMPLE (o)
#define MM_MODEM_SIMPLE(o)      MM_GDBUS_MODEM_SIMPLE(o)
#define MM_IS_MODEM_SIMPLE(o)   MM_GDBUS_IS_MODEM_SIMPLE(o)

const gchar *mm_modem_simple_get_path (MMModemSimple *self);
gchar       *mm_modem_simple_dup_path (MMModemSimple *self);

#define MM_SIMPLE_PROPERTY_PIN            "pin"            /* string  */
#define MM_SIMPLE_PROPERTY_OPERATOR_ID    "operator-id"    /* string  */
#define MM_SIMPLE_PROPERTY_ALLOWED_BANDS  "allowed-bands"  /* MMModemBand */
#define MM_SIMPLE_PROPERTY_ALLOWED_MODES  "allowed-modes"  /* MMModemMode */
#define MM_SIMPLE_PROPERTY_PREFERRED_MODE "preferred-mode" /* MMModemMode */
#define MM_SIMPLE_PROPERTY_APN            "apn"            /* string */
#define MM_SIMPLE_PROPERTY_IP_TYPE        "ip-type"        /* string  */
#define MM_SIMPLE_PROPERTY_NUMBER         "number"         /* string  */
#define MM_SIMPLE_PROPERTY_ALLOW_ROAMING  "allow-roaming"  /* boolean */

void      mm_modem_simple_connect        (MMModemSimple *self,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data,
                                          const gchar *first_property_name,
                                          ...);
MMBearer *mm_modem_simple_connect_finish (MMModemSimple *self,
                                          GAsyncResult *res,
                                          GError **error);
MMBearer *mm_modem_simple_connect_sync   (MMModemSimple *self,
                                          GCancellable *cancellable,
                                          GError **error,
                                          const gchar *first_property_name,
                                          ...);

/* void   mm_modem_simple_disconnect        (MMModemSimple *self, */
/*                                           GCancellable *cancellable, */
/*                                           GAsyncReadyCallback callback, */
/*                                           gpointer user_data); */
/* GList *mm_modem_simple_disconnect_finish (MMModemSimple *self, */
/*                                           GAsyncResult *res, */
/*                                           GError **error); */
/* GList *mm_modem_simple_disconnect_sync   (MMModemSimple *self, */
/*                                           GCancellable *cancellable, */
/*                                           GError **error); */

G_END_DECLS

#endif /* _MM_MODEM_SIMPLE_H_ */
