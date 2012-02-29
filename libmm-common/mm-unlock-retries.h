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

#ifndef MM_UNLOCK_RETRIES_H
#define MM_UNLOCK_RETRIES_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_UNLOCK_RETRIES            (mm_unlock_retries_get_type ())
#define MM_UNLOCK_RETRIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_UNLOCK_RETRIES, MMUnlockRetries))
#define MM_UNLOCK_RETRIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_UNLOCK_RETRIES, MMUnlockRetriesClass))
#define MM_IS_UNLOCK_RETRIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_UNLOCK_RETRIES))
#define MM_IS_UNLOCK_RETRIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_UNLOCK_RETRIES))
#define MM_UNLOCK_RETRIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_UNLOCK_RETRIES, MMUnlockRetriesClass))

#define MM_UNLOCK_RETRIES_UNKNOWN 999

typedef struct _MMUnlockRetries MMUnlockRetries;
typedef struct _MMUnlockRetriesClass MMUnlockRetriesClass;
typedef struct _MMUnlockRetriesPrivate MMUnlockRetriesPrivate;

struct _MMUnlockRetries {
    GObject parent;
    MMUnlockRetriesPrivate *priv;
};

struct _MMUnlockRetriesClass {
    GObjectClass parent;
};

GType mm_unlock_retries_get_type (void);

MMUnlockRetries *mm_unlock_retries_new (void);
MMUnlockRetries *mm_unlock_retries_new_from_dictionary (GVariant *dictionary);

void  mm_unlock_retries_set (MMUnlockRetries *self,
                             MMModemLock lock,
                             guint retries);

void  mm_unlock_retries_unset (MMUnlockRetries *self,
                               MMModemLock lock);

guint mm_unlock_retries_get (MMUnlockRetries *self,
                             MMModemLock lock);

typedef void (* MMUnlockRetriesForeachCb) (MMModemLock lock,
                                           guint count,
                                           gpointer user_data);

void mm_unlock_retries_foreach (MMUnlockRetries *self,
                                MMUnlockRetriesForeachCb callback,
                                gpointer user_data);

gboolean mm_unlock_retries_cmp (MMUnlockRetries *a,
                                MMUnlockRetries *b);

GVariant *mm_unlock_retries_get_dictionary (MMUnlockRetries *self);

gchar *mm_unlock_retries_build_string (MMUnlockRetries *self);

G_END_DECLS

#endif /* MM_UNLOCK_RETRIES_H */
