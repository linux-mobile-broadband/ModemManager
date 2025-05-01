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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#ifndef MM_CBM_LIST_H
#define MM_CBM_LIST_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-modem.h"
#include "mm-base-cbm.h"
#include "mm-cbm-part.h"

#define MM_TYPE_CBM_LIST            (mm_cbm_list_get_type ())
#define MM_CBM_LIST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CBM_LIST, MMCbmList))
#define MM_CBM_LIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CBM_LIST, MMCbmListClass))
#define MM_IS_CBM_LIST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CBM_LIST))
#define MM_IS_CBM_LIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CBM_LIST))
#define MM_CBM_LIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CBM_LIST, MMCbmListClass))

typedef struct _MMCbmList MMCbmList;
typedef struct _MMCbmListClass MMCbmListClass;
typedef struct _MMCbmListPrivate MMCbmListPrivate;

#define MM_CBM_LIST_MODEM "cbm-list-modem"

#define MM_CBM_ADDED   "cbm-added"
#define MM_CBM_DELETED "cbm-deleted"

struct _MMCbmList {
    GObject parent;
    MMCbmListPrivate *priv;
};

struct _MMCbmListClass {
    GObjectClass parent;

    /* Signals */
    void (* cbm_added)    (MMCbmList *self,
                           const gchar *cbm_path);
    void (* cbm_deleted)  (MMCbmList *self,
                           const gchar *sms_path);
};

GType mm_cbm_list_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCbmList, g_object_unref)

MMCbmList *mm_cbm_list_new (MMBaseModem *modem,
                            GObject     *bind_to);

GStrv mm_cbm_list_get_paths (MMCbmList *self);
guint mm_cbm_list_get_count (MMCbmList *self);

gboolean mm_cbm_list_has_part (MMCbmList *self,
                               guint16    serial,
                               guint16    message_id,
                               guint8     part_num);

gboolean mm_cbm_list_take_part (MMCbmList *self,
                                GObject *bind_to,
                                MMCbmPart *part,
                                MMCbmState state,
                                GError **error);

void mm_cbm_list_add_cbm (MMCbmList *self,
                          MMBaseCbm *cbm);

void     mm_cbm_list_delete_cbm        (MMCbmList *self,
                                        const gchar *cbm_path,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_cbm_list_delete_cbm_finish (MMCbmList *self,
                                        GAsyncResult *res,
                                        GError **error);

#endif /* MM_CBM_LIST_H */
