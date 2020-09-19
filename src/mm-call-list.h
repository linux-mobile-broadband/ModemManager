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
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
 */

#ifndef MM_CALL_LIST_H
#define MM_CALL_LIST_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-modem.h"
#include "mm-base-call.h"

#define MM_TYPE_CALL_LIST            (mm_call_list_get_type ())
#define MM_CALL_LIST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL_LIST, MMCallList))
#define MM_CALL_LIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CALL_LIST, MMCallListClass))
#define MM_IS_CALL_LIST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL_LIST))
#define MM_IS_CALL_LIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CALL_LIST))
#define MM_CALL_LIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CALL_LIST, MMCallListClass))

typedef struct _MMCallList MMCallList;
typedef struct _MMCallListClass MMCallListClass;
typedef struct _MMCallListPrivate MMCallListPrivate;

#define MM_CALL_LIST_MODEM "call-list-modem"

#define MM_CALL_ADDED     "call-added"
#define MM_CALL_DELETED   "call-deleted"

struct _MMCallList {
    GObject parent;
    MMCallListPrivate *priv;
};

struct _MMCallListClass {
    GObjectClass parent;

    /* Signals */
    void (* call_added)   (MMCallList  *self,
                           const gchar *call_path,
                           gboolean     received);
    void (* call_deleted) (MMCallList  *self,
                           const gchar *call_path);
};

GType mm_call_list_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCallList, g_object_unref)

MMCallList *mm_call_list_new (MMBaseModem *modem);

GStrv mm_call_list_get_paths (MMCallList *self);
guint mm_call_list_get_count (MMCallList *self);

void mm_call_list_add_call  (MMCallList *self,
                             MMBaseCall *call);

MMBaseCall *mm_call_list_get_call (MMCallList   *self,
                                   const gchar  *call_path);

gboolean mm_call_list_delete_call (MMCallList   *self,
                                   const gchar  *call_path,
                                   GError      **error);

MMBaseCall *mm_call_list_get_first_incoming_call (MMCallList  *self,
                                                  MMCallState  incoming_state);

typedef void (* MMCallListForeachFunc) (MMBaseCall            *call,
                                        gpointer               user_data);
void            mm_call_list_foreach   (MMCallList            *self,
                                        MMCallListForeachFunc  callback,
                                        gpointer               user_data);

#endif /* MM_CALL_LIST_H */
