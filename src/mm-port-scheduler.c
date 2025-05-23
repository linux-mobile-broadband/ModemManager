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

#include "mm-port-scheduler.h"

G_DEFINE_INTERFACE (MMPortScheduler, mm_port_scheduler, G_TYPE_OBJECT)

enum {
    SIGNAL_SEND_COMMAND,
    SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

/*****************************************************************************/

void
mm_port_scheduler_register_source (MMPortScheduler *self,
                                   gpointer         source,
                                   const gchar     *tag)
{
    MM_PORT_SCHEDULER_GET_INTERFACE (self)->register_source (self, source, tag);
}

void
mm_port_scheduler_unregister_source (MMPortScheduler *self,
                                     gpointer         source)
{
    MM_PORT_SCHEDULER_GET_INTERFACE (self)->unregister_source (self, source);
}

void
mm_port_scheduler_notify_num_pending (MMPortScheduler *self,
                                      gpointer         source,
                                      guint            num_pending)
{
    MM_PORT_SCHEDULER_GET_INTERFACE (self)->notify_num_pending (self, source, num_pending);
}

void
mm_port_scheduler_notify_command_done (MMPortScheduler *self,
                                       gpointer         source,
                                       guint            num_pending)
{
    MM_PORT_SCHEDULER_GET_INTERFACE (self)->notify_command_done (self, source, num_pending);
}

/*****************************************************************************/

static void
mm_port_scheduler_default_init (MMPortSchedulerInterface *iface)
{
    signals[SIGNAL_SEND_COMMAND] =
        g_signal_new (MM_PORT_SCHEDULER_SIGNAL_SEND_COMMAND,
                  MM_TYPE_PORT_SCHEDULER,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MMPortSchedulerInterface, send_command),
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_POINTER);
}
