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

#ifndef _MM_OBJECT_H_
#define _MM_OBJECT_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-modem.h"
#include "mm-modem-3gpp.h"
#include "mm-modem-3gpp-profile-manager.h"
#include "mm-modem-3gpp-ussd.h"
#include "mm-modem-cdma.h"
#include "mm-modem-simple.h"
#include "mm-modem-location.h"
#include "mm-modem-messaging.h"
#include "mm-modem-voice.h"
#include "mm-modem-time.h"
#include "mm-modem-firmware.h"
#include "mm-modem-signal.h"
#include "mm-modem-oma.h"

G_BEGIN_DECLS

#define MM_TYPE_OBJECT            (mm_object_get_type ())
#define MM_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_OBJECT, MMObject))
#define MM_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_OBJECT, MMObjectClass))
#define MM_IS_OBJECT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_OBJECT))
#define MM_IS_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_OBJECT))
#define MM_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_OBJECT, MMObjectClass))

typedef struct _MMObject MMObject;
typedef struct _MMObjectClass MMObjectClass;

/**
 * MMObject:
 *
 * The #MMObject structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMObject {
    /*< private >*/
    MmGdbusObjectProxy parent;
    gpointer unused;
};

struct _MMObjectClass {
    /*< private >*/
    MmGdbusObjectProxyClass parent;
};

GType mm_object_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMObject, g_object_unref)

const gchar *mm_object_get_path (MMObject *self);
gchar       *mm_object_dup_path (MMObject *self);

MMModem                    *mm_object_get_modem                       (MMObject *self);
MMModem3gpp                *mm_object_get_modem_3gpp                  (MMObject *self);
MMModem3gppProfileManager  *mm_object_get_modem_3gpp_profile_manager  (MMObject *self);
MMModem3gppUssd            *mm_object_get_modem_3gpp_ussd             (MMObject *self);
MMModemCdma                *mm_object_get_modem_cdma                  (MMObject *self);
MMModemSimple              *mm_object_get_modem_simple                (MMObject *self);
MMModemLocation            *mm_object_get_modem_location              (MMObject *self);
MMModemMessaging           *mm_object_get_modem_messaging             (MMObject *self);
MMModemVoice               *mm_object_get_modem_voice                 (MMObject *self);
MMModemTime                *mm_object_get_modem_time                  (MMObject *self);
MMModemFirmware            *mm_object_get_modem_firmware              (MMObject *self);
MMModemSignal              *mm_object_get_modem_signal                (MMObject *self);
MMModemOma                 *mm_object_get_modem_oma                   (MMObject *self);

MMModem                    *mm_object_peek_modem                      (MMObject *self);
MMModem3gpp                *mm_object_peek_modem_3gpp                 (MMObject *self);
MMModem3gppProfileManager  *mm_object_peek_modem_3gpp_profile_manager (MMObject *self);
MMModem3gppUssd            *mm_object_peek_modem_3gpp_ussd            (MMObject *self);
MMModemCdma                *mm_object_peek_modem_cdma                 (MMObject *self);
MMModemSimple              *mm_object_peek_modem_simple               (MMObject *self);
MMModemLocation            *mm_object_peek_modem_location             (MMObject *self);
MMModemMessaging           *mm_object_peek_modem_messaging            (MMObject *self);
MMModemVoice               *mm_object_peek_modem_voice                (MMObject *self);
MMModemTime                *mm_object_peek_modem_time                 (MMObject *self);
MMModemFirmware            *mm_object_peek_modem_firmware             (MMObject *self);
MMModemSignal              *mm_object_peek_modem_signal               (MMObject *self);
MMModemOma                 *mm_object_peek_modem_oma                  (MMObject *self);

G_END_DECLS

#endif /* _MM_OBJECT_H_ */
