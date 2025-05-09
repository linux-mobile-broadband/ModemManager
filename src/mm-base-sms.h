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

#ifndef MM_BASE_SMS_H
#define MM_BASE_SMS_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-sms-part.h"

/*****************************************************************************/

/* Default timeout value to be used when sending a SMS, long enough so that the
 * operation succeeds or fails under low signal conditions. */
#define MM_BASE_SMS_DEFAULT_SEND_TIMEOUT (5 * 60)

/*****************************************************************************/

#define MM_TYPE_BASE_SMS            (mm_base_sms_get_type ())
#define MM_BASE_SMS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BASE_SMS, MMBaseSms))
#define MM_BASE_SMS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BASE_SMS, MMBaseSmsClass))
#define MM_IS_BASE_SMS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BASE_SMS))
#define MM_IS_BASE_SMS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BASE_SMS))
#define MM_BASE_SMS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BASE_SMS, MMBaseSmsClass))

typedef struct _MMBaseSms MMBaseSms;
typedef struct _MMBaseSmsClass MMBaseSmsClass;
typedef struct _MMBaseSmsPrivate MMBaseSmsPrivate;

/* Properties */
#define MM_BASE_SMS_PATH                "sms-path"
#define MM_BASE_SMS_CONNECTION          "sms-connection"
#define MM_BASE_SMS_IS_MULTIPART        "sms-is-multipart"
#define MM_BASE_SMS_MAX_PARTS           "sms-max-parts"
#define MM_BASE_SMS_MULTIPART_REFERENCE "sms-multipart-reference"
#define MM_BASE_SMS_IS_3GPP             "sms-is-3gpp"
#define MM_BASE_SMS_DEFAULT_STORAGE     "sms-default-storage"
#define MM_BASE_SMS_SUPPORTED_STORAGES  "sms-supported-storages"
#define MM_BASE_SMS_CONNECTION_PARENT_PROPERTY_NAME "sms-connection-parent-property-name"

/* Signals */
#define MM_BASE_SMS_SET_LOCAL_MULTIPART_REFERENCE "sms-set-local-multipart-reference"

struct _MMBaseSms {
    MmGdbusSmsSkeleton parent;
    MMBaseSmsPrivate *priv;
};

struct _MMBaseSmsClass {
    MmGdbusSmsSkeletonClass parent;

    /* Store the SMS */
    void (* store) (MMBaseSms *self,
                    MMSmsStorage storage,
                    GAsyncReadyCallback callback,
                    gpointer user_data);
    gboolean (* store_finish) (MMBaseSms *self,
                               GAsyncResult *res,
                               GError **error);

    /* Send the SMS */
    void (* send) (MMBaseSms *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data);
    gboolean (* send_finish) (MMBaseSms *self,
                              GAsyncResult *res,
                              GError **error);

    /* Delete the SMS */
    void (* delete) (MMBaseSms *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data);
    gboolean (* delete_finish) (MMBaseSms *self,
                                GAsyncResult *res,
                                GError **error);

    /* Signal */
    void (*set_local_multipart_reference) (MMBaseSms   *self,
                                           const gchar *number);
};

GType mm_base_sms_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBaseSms, g_object_unref)

gboolean   mm_base_sms_init_from_properties (MMBaseSms        *self,
                                             MMSmsProperties  *properties,
                                             GError          **error);
gboolean   mm_base_sms_singlepart_init      (MMBaseSms     *self,
                                             MMSmsState     state,
                                             MMSmsStorage   storage,
                                             MMSmsPart     *part,
                                             GError       **error);
gboolean   mm_base_sms_multipart_init       (MMBaseSms     *self,
                                             MMSmsState     state,
                                             MMSmsStorage   storage,
                                             guint          reference,
                                             guint          max_parts,
                                             MMSmsPart     *first_part,
                                             GError       **error);

gboolean   mm_base_sms_multipart_take_part (MMBaseSms *self,
                                            MMSmsPart *part,
                                            GError **error);

void          mm_base_sms_export      (MMBaseSms *self);
void          mm_base_sms_unexport    (MMBaseSms *self);
const gchar  *mm_base_sms_get_path    (MMBaseSms *self);
MMSmsStorage  mm_base_sms_get_storage (MMBaseSms *self);

gboolean     mm_base_sms_has_part_index (MMBaseSms *self,
                                         guint index);
GList       *mm_base_sms_get_parts      (MMBaseSms *self);

gboolean     mm_base_sms_is_multipart            (MMBaseSms *self);
guint        mm_base_sms_get_max_parts           (MMBaseSms *self);
guint        mm_base_sms_get_multipart_reference (MMBaseSms *self);
void         mm_base_sms_set_multipart_reference (MMBaseSms *self,
                                                  guint      reference);
gboolean     mm_base_sms_multipart_is_complete   (MMBaseSms *self);
gboolean     mm_base_sms_multipart_is_assembled  (MMBaseSms *self);

void     mm_base_sms_delete        (MMBaseSms *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean mm_base_sms_delete_finish (MMBaseSms *self,
                                    GAsyncResult *res,
                                    GError **error);

#endif /* MM_BASE_SMS_H */
