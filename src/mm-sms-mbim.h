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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_SMS_MBIM_H
#define MM_SMS_MBIM_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-sms.h"

#define MM_TYPE_SMS_MBIM            (mm_sms_mbim_get_type ())
#define MM_SMS_MBIM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SMS_MBIM, MMSmsMbim))
#define MM_SMS_MBIM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SMS_MBIM, MMSmsMbimClass))
#define MM_IS_SMS_MBIM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SMS_MBIM))
#define MM_IS_SMS_MBIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SMS_MBIM))
#define MM_SMS_MBIM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SMS_MBIM, MMSmsMbimClass))

typedef struct _MMSmsMbim MMSmsMbim;
typedef struct _MMSmsMbimClass MMSmsMbimClass;

struct _MMSmsMbim {
    MMBaseSms parent;
};

struct _MMSmsMbimClass {
    MMBaseSmsClass parent;
};

GType mm_sms_mbim_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSmsMbim, g_object_unref)

MMBaseSms *mm_sms_mbim_new (MMBaseModem *modem);

#endif /* MM_SMS_MBIM_H */
