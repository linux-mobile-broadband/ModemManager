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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_SMS_LIST_H
#define MM_SMS_LIST_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-modem.h"
#include "mm-sms-part.h"

#define MM_TYPE_SMS_LIST            (mm_sms_list_get_type ())
#define MM_SMS_LIST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SMS_LIST, MMSmsList))
#define MM_SMS_LIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SMS_LIST, MMSmsListClass))
#define MM_IS_SMS_LIST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SMS_LIST))
#define MM_IS_SMS_LIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SMS_LIST))
#define MM_SMS_LIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SMS_LIST, MMSmsListClass))

typedef struct _MMSmsList MMSmsList;
typedef struct _MMSmsListClass MMSmsListClass;
typedef struct _MMSmsListPrivate MMSmsListPrivate;

#define MM_SMS_LIST_MODEM "sms-list-modem"

#define MM_SMS_ADDED     "sms-added"
#define MM_SMS_DELETED   "sms-deleted"

struct _MMSmsList {
    GObject parent;
    MMSmsListPrivate *priv;
};

struct _MMSmsListClass {
    GObjectClass parent;

    /* Signals */
    void (*sms_added)     (MMSmsList *self,
                           const gchar *sms_path,
                           gboolean received);
    void (*sms_deleted)   (MMSmsList *self,
                           const gchar *sms_path);
};

GType mm_sms_list_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSmsList, g_object_unref)

MMSmsList *mm_sms_list_new (MMBaseModem *modem);

GStrv mm_sms_list_get_paths (MMSmsList *self);
guint mm_sms_list_get_count (MMSmsList *self);

gboolean mm_sms_list_has_part (MMSmsList *self,
                               MMSmsStorage storage,
                               guint index);

gboolean mm_sms_list_take_part (MMSmsList *self,
                                MMSmsPart *part,
                                MMSmsState state,
                                MMSmsStorage storage,
                                GError **error);

void mm_sms_list_add_sms (MMSmsList *self,
                          MMBaseSms *sms);

void     mm_sms_list_delete_sms        (MMSmsList *self,
                                        const gchar *sms_path,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_sms_list_delete_sms_finish (MMSmsList *self,
                                        GAsyncResult *res,
                                        GError **error);

gboolean mm_sms_list_has_local_multipart_reference (MMSmsList *self,
                                                    const gchar *number,
                                                    guint8 reference);

#endif /* MM_SMS_LIST_H */
