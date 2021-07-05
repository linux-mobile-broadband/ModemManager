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
 * Copyright (C) 2021 Joel Selvaraj <jo@jsfamily.in>
 */

#ifndef MM_CALL_QMI_H
#define MM_CALL_QMI_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-call.h"

#define MM_TYPE_CALL_QMI            (mm_call_qmi_get_type ())
#define MM_CALL_QMI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL_QMI, MMCallQmi))
#define MM_CALL_QMI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CALL_QMI, MMCallQmiClass))
#define MM_IS_CALL_QMI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL_QMI))
#define MM_IS_CALL_QMI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CALL_QMI))
#define MM_CALL_QMI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CALL_QMI, MMCallQmiClass))

typedef struct _MMCallQmi MMCallQmi;
typedef struct _MMCallQmiClass MMCallQmiClass;

struct _MMCallQmi {
    MMBaseCall parent;
};

struct _MMCallQmiClass {
    MMBaseCallClass parent;
};

GType mm_call_qmi_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCallQmi, g_object_unref)

MMBaseCall *mm_call_qmi_new (MMBaseModem     *modem,
                             MMCallDirection  direction,
                             const gchar     *number);

#endif /* MM_CALL_QMI_H */
