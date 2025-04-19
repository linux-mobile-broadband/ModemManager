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
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 *
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#ifndef MM_SMS_AT_H
#define MM_SMS_AT_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-sms.h"
#include "mm-base-modem.h"

/*****************************************************************************/

#define MM_TYPE_SMS_AT            (mm_sms_at_get_type ())
#define MM_SMS_AT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SMS_AT, MMSmsAt))
#define MM_SMS_AT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SMS_AT, MMSmsAtClass))
#define MM_IS_SMS_AT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SMS_AT))
#define MM_IS_SMS_AT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SMS_AT))
#define MM_SMS_AT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SMS_AT, MMSmsAtClass))

typedef struct _MMSmsAt MMSmsAt;
typedef struct _MMSmsAtClass MMSmsAtClass;
typedef struct _MMSmsAtPrivate MMSmsAtPrivate;

struct _MMSmsAt {
    MMBaseSms parent;
    MMSmsAtPrivate *priv;
};

struct _MMSmsAtClass {
    MMBaseSmsClass parent;
};

GType mm_sms_at_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSmsAt, g_object_unref)

MMBaseSms *mm_sms_at_new (MMBaseModem  *modem,
                          gboolean      is_3gpp,
                          MMSmsStorage  default_storage);

#endif /* MM_SMS_AT_H */
