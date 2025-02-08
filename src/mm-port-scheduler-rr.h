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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#ifndef _MM_PORT_SCHEDULER_RR_H_
#define _MM_PORT_SCHEDULER_RR_H_

#include <glib-object.h>
#include <gio/gio.h>

#include "mm-port-scheduler.h"

#define MM_TYPE_PORT_SCHEDULER_RR            (mm_port_scheduler_rr_get_type ())
#define MM_PORT_SCHEDULER_RR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_SCHEDULER_RR, MMPortSchedulerRR))
#define MM_PORT_SCHEDULER_RR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_SCHEDULER_RR, MMPortSchedulerRRClass))
#define MM_IS_PORT_SCHEDULER_RR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_SCHEDULER_RR))
#define MM_IS_PORT_SCHEDULER_RR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_SCHEDULER_RR))
#define MM_PORT_SCHEDULER_RR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_SCHEDULER_RR, MMPortSchedulerRRClass))

#define MM_PORT_SCHEDULER_RR_INTER_PORT_DELAY "inter-port-delay"

typedef struct _MMPortSchedulerRR MMPortSchedulerRR;
typedef struct _MMPortSchedulerRRClass MMPortSchedulerRRClass;
typedef struct _MMPortSchedulerRRPrivate MMPortSchedulerRRPrivate;

struct _MMPortSchedulerRR {
    /*< private >*/
    GObject parent;
    MMPortSchedulerRRPrivate *priv;
};

struct _MMPortSchedulerRRClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_port_scheduler_rr_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortSchedulerRR, g_object_unref)

MMPortSchedulerRR *mm_port_scheduler_rr_new (void);

#endif /* _MM_PORT_SCHEDULER_RR_H_ */
