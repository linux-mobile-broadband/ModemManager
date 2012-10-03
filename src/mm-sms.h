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
 */

#ifndef MM_SMS_H
#define MM_SMS_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-sms-part.h"
#include "mm-base-modem.h"

#define MM_TYPE_SMS            (mm_sms_get_type ())
#define MM_SMS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SMS, MMSms))
#define MM_SMS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SMS, MMSmsClass))
#define MM_IS_SMS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SMS))
#define MM_IS_SMS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SMS))
#define MM_SMS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SMS, MMSmsClass))

typedef struct _MMSms MMSms;
typedef struct _MMSmsClass MMSmsClass;
typedef struct _MMSmsPrivate MMSmsPrivate;

#define MM_SMS_PATH                "sms-path"
#define MM_SMS_CONNECTION          "sms-connection"
#define MM_SMS_MODEM               "sms-modem"
#define MM_SMS_IS_MULTIPART        "sms-is-multipart"
#define MM_SMS_MAX_PARTS           "sms-max-parts"
#define MM_SMS_MULTIPART_REFERENCE "sms-multipart-reference"

struct _MMSms {
    MmGdbusSmsSkeleton parent;
    MMSmsPrivate *priv;
};

struct _MMSmsClass {
    MmGdbusSmsSkeletonClass parent;

    /* Store the SMS */
    void (* store) (MMSms *self,
                    MMSmsStorage storage,
                    GAsyncReadyCallback callback,
                    gpointer user_data);
    gboolean (* store_finish) (MMSms *self,
                               GAsyncResult *res,
                               GError **error);

    /* Send the SMS */
    void (* send) (MMSms *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data);
    gboolean (* send_finish) (MMSms *self,
                              GAsyncResult *res,
                              GError **error);

    /* Delete the SMS */
    void (* delete) (MMSms *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data);
    gboolean (* delete_finish) (MMSms *self,
                                GAsyncResult *res,
                                GError **error);
};

GType mm_sms_get_type (void);

/* This one can be overriden by plugins */
MMSms *mm_sms_new (MMBaseModem *modem);

MMSms *mm_sms_new_from_properties (MMBaseModem *modem,
                                   MMSmsProperties *properties,
                                   GError **error);

MMSms *mm_sms_singlepart_new (MMBaseModem *modem,
                              MMSmsState state,
                              MMSmsStorage storage,
                              MMSmsPart *part,
                              GError **error);

MMSms    *mm_sms_multipart_new       (MMBaseModem *modem,
                                      MMSmsState state,
                                      MMSmsStorage storage,
                                      guint reference,
                                      guint max_parts,
                                      MMSmsPart *first_part,
                                      GError **error);
gboolean  mm_sms_multipart_take_part (MMSms *self,
                                      MMSmsPart *part,
                                      GError **error);

void          mm_sms_export      (MMSms *self);
void          mm_sms_unexport    (MMSms *self);
const gchar  *mm_sms_get_path    (MMSms *self);
MMSmsStorage  mm_sms_get_storage (MMSms *self);

gboolean     mm_sms_has_part_index (MMSms *self,
                                    guint index);
GList       *mm_sms_get_parts      (MMSms *self);

gboolean     mm_sms_is_multipart            (MMSms *self);
guint        mm_sms_get_multipart_reference (MMSms *self);
gboolean     mm_sms_multipart_is_complete   (MMSms *self);
gboolean     mm_sms_multipart_is_assembled  (MMSms *self);

void     mm_sms_delete        (MMSms *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
gboolean mm_sms_delete_finish (MMSms *self,
                               GAsyncResult *res,
                               GError **error);

#endif /* MM_SMS_H */
