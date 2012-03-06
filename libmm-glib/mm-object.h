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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_OBJECT_H_
#define _MM_OBJECT_H_

#include <ModemManager.h>
#include <mm-gdbus-modem.h>

#include "mm-modem.h"
#include "mm-modem-3gpp.h"
#include "mm-modem-3gpp-ussd.h"
#include "mm-modem-cdma.h"
#include "mm-modem-simple.h"
#include "mm-modem-location.h"
#include "mm-modem-messaging.h"
#include "mm-modem-time.h"

G_BEGIN_DECLS

typedef MmGdbusObject     MMObject;
#define MM_TYPE_OBJECT(o) MM_GDBUS_TYPE_OBJECT (o)
#define MM_OBJECT(o)      MM_GDBUS_OBJECT(o)
#define MM_IS_OBJECT(o)   MM_GDBUS_IS_OBJECT(o)

const gchar *mm_object_get_path (MMObject *self);
gchar       *mm_object_dup_path (MMObject *self);

MMModem          *mm_object_get_modem            (MMObject *object);
MMModem3gpp      *mm_object_get_modem_3gpp       (MMObject *object);
MMModem3gppUssd  *mm_object_get_modem_3gpp_ussd  (MMObject *object);
MMModemCdma      *mm_object_get_modem_cdma       (MMObject *object);
MMModemSimple    *mm_object_get_modem_simple     (MMObject *object);
MMModemLocation  *mm_object_get_modem_location   (MMObject *object);
MMModemMessaging *mm_object_get_modem_messaging  (MMObject *object);
MMModemTime      *mm_object_get_modem_time       (MMObject *object);

MMModem          *mm_object_peek_modem           (MMObject *object);
MMModem3gpp      *mm_object_peek_modem_3gpp      (MMObject *object);
MMModem3gppUssd  *mm_object_peek_modem_3gpp_ussd (MMObject *object);
MMModemCdma      *mm_object_peek_modem_cdma      (MMObject *object);
MMModemSimple    *mm_object_peek_modem_simple    (MMObject *object);
MMModemLocation  *mm_object_peek_modem_location  (MMObject *object);
MMModemMessaging *mm_object_peek_modem_messaging (MMObject *object);
MMModemTime      *mm_object_peek_modem_time      (MMObject *object);

G_END_DECLS

#endif /* _MM_OBJECT_H_ */
