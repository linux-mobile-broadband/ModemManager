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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 * Copyright (C) 2019 Purism SPC
 */

#ifndef MM_CALL_AT_H
#define MM_CALL_AT_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-call.h"
#include "mm-base-modem.h"

#define MM_TYPE_CALL_AT            (mm_call_at_get_type ())
#define MM_CALL_AT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL_AT, MMCallAt))
#define MM_CALL_AT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CALL_AT, MMCallAtClass))
#define MM_IS_CALL_AT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL_AT))
#define MM_IS_CALL_AT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CALL_AT))
#define MM_CALL_AT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CALL_AT, MMCallAtClass))

typedef struct _MMCallAt MMCallAt;
typedef struct _MMCallAtClass MMCallAtClass;
typedef struct _MMCallAtPrivate MMCallAtPrivate;

struct _MMCallAt {
    MMBaseCall parent;
    MMCallAtPrivate *priv;
};

struct _MMCallAtClass {
    MMBaseCallClass parent;
};

GType mm_call_at_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCallAt, g_object_unref)

MMBaseCall *mm_call_at_new (MMBaseModem     *modem,
                            GObject         *bind_to,
                            MMCallDirection  direction,
                            const gchar     *number,
                            const guint      dtmf_tone_duration,
                            gboolean         skip_incoming_timeout,
                            gboolean         supports_dialing_to_ringing,
                            gboolean         supports_ringing_to_active);

#endif /* MM_CALL_AT_H */
