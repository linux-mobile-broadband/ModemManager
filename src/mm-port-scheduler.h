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

#ifndef MM_PORT_SCHEDULER_H
#define MM_PORT_SCHEDULER_H

#include <glib.h>
#include <glib-object.h>

#define MM_TYPE_PORT_SCHEDULER mm_port_scheduler_get_type ()
#define MM_PORT_SCHEDULER_GET_INTERFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), MM_TYPE_PORT_SCHEDULER, MMPortSchedulerInterface))

G_DECLARE_INTERFACE (MMPortScheduler, mm_port_scheduler, MM, PORT_SCHEDULER, GObject)

#define MM_PORT_SCHEDULER_SIGNAL_SEND_COMMAND "send-command"

typedef struct _MMPortSchedulerInterface MMPortSchedulerInterface;

struct _MMPortSchedulerInterface
{
    GTypeInterface g_iface;

    /* Signals */
    void (*send_command) (MMPortScheduler *self,
                          gpointer         source);

    /* Methods */
    void (*register_source)     (MMPortScheduler *self,
                                 gpointer         source,
                                 const gchar     *tag);

    void (*unregister_source)   (MMPortScheduler *self,
                                 gpointer         source);

    void (*notify_num_pending)  (MMPortScheduler *self,
                                 gpointer         source,
                                 guint            num_pending);

    void (*notify_command_done) (MMPortScheduler *self,
                                 gpointer         source,
                                 guint            num_pending);
};

void mm_port_scheduler_register_source      (MMPortScheduler *self,
                                             gpointer         source,
                                             const gchar     *tag);

void  mm_port_scheduler_unregister_source   (MMPortScheduler *self,
                                             gpointer         source);

void  mm_port_scheduler_notify_num_pending  (MMPortScheduler *self,
                                             gpointer         source,
                                             guint            num_pending);

void  mm_port_scheduler_notify_command_done (MMPortScheduler *self,
                                             gpointer         source,
                                             guint            num_pending);

#endif /* MM_PORT_SCHEDULER_H */
