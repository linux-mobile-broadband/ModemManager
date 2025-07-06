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
 * Copyright 2025 Dan Williams <dan@ioncontrol.co>
 */

#ifndef MM_SLEEP_CONTEXT_H
#define MM_SLEEP_CONTEXT_H

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

#define MM_TYPE_SLEEP_CONTEXT            (mm_sleep_context_get_type ())
#define MM_SLEEP_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SLEEP_CONTEXT, MMSleepContext))
#define MM_SLEEP_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SLEEP_CONTEXT, MMSleepContextClass))
#define MM_IS_SLEEP_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SLEEP_CONTEXT))
#define MM_IS_SLEEP_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SLEEP_CONTEXT))
#define MM_SLEEP_CONTEXT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SLEEP_CONTEXT, MMSleepContextClass))

#define MM_SLEEP_CONTEXT_DONE "done"

typedef struct _MMSleepContext        MMSleepContext;
typedef struct _MMSleepContextClass   MMSleepContextClass;
typedef struct _MMSleepContextPrivate MMSleepContextPrivate;

struct _MMSleepContext {
    GObject parent;

    MMSleepContextPrivate *priv;
};

struct _MMSleepContextClass {
    GObjectClass parent;

    void (*done) (MMSleepContext *ctx, GError *error);
};

GType mm_sleep_context_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSleepContext, g_object_unref)

MMSleepContext *mm_sleep_context_new      (guint           timeout_seconds);

void            mm_sleep_context_timeout_backoff (MMSleepContext *self,
                                                  guint           more_seconds);

/* The MMSleepContext assumes ownership of @error */
void            mm_sleep_context_complete (MMSleepContext *self,
                                           GError         *error);

void            mm_sleep_context_dispose  (MMSleepContext *self);

G_END_DECLS

#endif /* MM_SLEEP_CONTEXT_H */
