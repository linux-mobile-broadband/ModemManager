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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_BEARER_LIST_H
#define MM_BEARER_LIST_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-bearer.h"

#define MM_TYPE_BEARER_LIST            (mm_bearer_list_get_type ())
#define MM_BEARER_LIST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_LIST, MMBearerList))
#define MM_BEARER_LIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_LIST, MMBearerListClass))
#define MM_IS_BEARER_LIST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_LIST))
#define MM_IS_BEARER_LIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_LIST))
#define MM_BEARER_LIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_LIST, MMBearerListClass))

#define MM_BEARER_LIST_NUM_BEARERS        "num-bearers"
#define MM_BEARER_LIST_MAX_BEARERS        "max-bearers"
#define MM_BEARER_LIST_MAX_ACTIVE_BEARERS "max-active-bearers"

typedef struct _MMBearerList MMBearerList;
typedef struct _MMBearerListClass MMBearerListClass;
typedef struct _MMBearerListPrivate MMBearerListPrivate;

struct _MMBearerList {
    GObject parent;
    MMBearerListPrivate *priv;
};

struct _MMBearerListClass {
    GObjectClass parent;
};

GType mm_bearer_list_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBearerList, g_object_unref)

MMBearerList *mm_bearer_list_new (guint max_bearers,
                                  guint max_active_bearers);

GStrv mm_bearer_list_get_paths (MMBearerList *self);

guint mm_bearer_list_get_count (MMBearerList *self);
guint mm_bearer_list_get_count_active (MMBearerList *self);
guint mm_bearer_list_get_max (MMBearerList *self);
guint mm_bearer_list_get_max_active (MMBearerList *self);

gboolean mm_bearer_list_add_bearer (MMBearerList *self,
                                    MMBaseBearer *bearer,
                                    GError **error);
gboolean mm_bearer_list_delete_bearer (MMBearerList *self,
                                       const gchar *path,
                                       GError **error);

typedef void (*MMBearerListForeachFunc) (MMBaseBearer *bearer,
                                         gpointer user_data);
void mm_bearer_list_foreach (MMBearerList *self,
                             MMBearerListForeachFunc func,
                             gpointer user_data);

MMBaseBearer *mm_bearer_list_find_by_properties (MMBearerList *self,
                                                 MMBearerProperties *properties);
MMBaseBearer *mm_bearer_list_find_by_path (MMBearerList *self,
                                           const gchar *path);

void     mm_bearer_list_disconnect_all_bearers        (MMBearerList *self,
                                                       GAsyncReadyCallback callback,
                                                       gpointer user_data);
gboolean mm_bearer_list_disconnect_all_bearers_finish (MMBearerList *self,
                                                       GAsyncResult *res,
                                                       GError **error);

#endif /* MM_BEARER_LIST_H */
