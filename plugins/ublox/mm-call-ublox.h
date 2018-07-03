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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_CALL_UBLOX_H
#define MM_CALL_UBLOX_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-call.h"

#define MM_TYPE_CALL_UBLOX            (mm_call_ublox_get_type ())
#define MM_CALL_UBLOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL_UBLOX, MMCallUblox))
#define MM_CALL_UBLOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CALL_UBLOX, MMCallUbloxClass))
#define MM_IS_CALL_UBLOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL_UBLOX))
#define MM_IS_CALL_UBLOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CALL_UBLOX))
#define MM_CALL_UBLOX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CALL_UBLOX, MMCallUbloxClass))

typedef struct _MMCallUblox MMCallUblox;
typedef struct _MMCallUbloxClass MMCallUbloxClass;
typedef struct _MMCallUbloxPrivate MMCallUbloxPrivate;

struct _MMCallUblox {
    MMBaseCall parent;
    MMCallUbloxPrivate *priv;
};

struct _MMCallUbloxClass {
    MMBaseCallClass parent;
};

GType mm_call_ublox_get_type (void);

MMBaseCall *mm_call_ublox_new (MMBaseModem     *modem,
                               MMCallDirection  direction,
                               const gchar     *number);

#endif /* MM_CALL_UBLOX_H */
