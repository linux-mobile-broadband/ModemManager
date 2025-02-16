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

#include "mm-port-scheduler-rr.h"
#include "mm-log-object.h"

/* Theory of operation:
 *
 * Sources (e.g. MMPort subclasses) register themselves with the scheduler.
 *
 * Each source notifies the scheduler whenever its command queue depth changes,
 * for example when new commands are submitted, when commands are completed,
 * or when commands are canceled.
 *
 * The scheduler will round-robin between all sources with pending commands,
 * sleeping when there are no pending commands from any source.
 *
 * For each source with a pending command the scheduler will emit the
 * 'send-command' signal with that source's ID. The given source should
 * send the next command in its queue to the modem.
 *
 * When that command is finished (either successfully or with an error/timeout)
 * the source must call mm_port_scheduler_notify_command_done() to notify the
 * scheduler that it may advance to the next source with a pending command, if
 * any. If the 'send-command' signal and the notify_command_done() call are not
 * balanced the scheduler may stall.
 */

static void mm_port_scheduler_iface_init (MMPortSchedulerInterface *iface);
static void log_object_iface_init (MMLogObjectInterface *iface);

struct _MMPortSchedulerRRPrivate {
    guint      instance_id;
    GPtrArray *sources;
    guint      cur_source;
    gboolean   in_command;
    guint      next_pending_id;

    /* Delay between allowing ports to send commands, in ms */
    guint inter_port_delay;
};

enum {
    PROP_0,
    PROP_INTER_PORT_DELAY,

    LAST_PROP
};

static guint send_command_signal = 0;
static guint instance_id_last = 0;

G_DEFINE_TYPE_WITH_CODE (MMPortSchedulerRR, mm_port_scheduler_rr, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MMPortSchedulerRR)
                         G_IMPLEMENT_INTERFACE (MM_TYPE_PORT_SCHEDULER,
                                                mm_port_scheduler_iface_init)
                         G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT,
                                                log_object_iface_init))

/*****************************************************************************/

typedef struct {
    gpointer  id;
    gchar    *tag; /* e.g. port name */
    guint     num_pending;
} Source;

static void
source_free (Source *s)
{
    g_free (s->tag);
    g_slice_free (Source, s);
}

static Source *
find_source (MMPortSchedulerRR *self,
             gpointer           source_id,
             guint             *out_idx)
{
    guint i;

    for (i = 0; i < self->priv->sources->len; i++) {
        Source *s;

        s = g_ptr_array_index (self->priv->sources, i);
        if (s->id == source_id) {
            if (out_idx)
                *out_idx = i;
            return s;
        }
    }

    return NULL;
}

static Source *
find_next_source (MMPortSchedulerRR *self,
                  guint             *out_idx)
{
    guint i, idx;

    /* Starting at the source *after* the current source, advance through
     * the entire array and back to the current source (in case only the
     * current source has pending commands) to find the next source with
     * a pending command.
     */
    for (i = 0, idx = self->priv->cur_source + 1;
         i < self->priv->sources->len;
         i++, idx++) {
        Source *s;

        /* Wrap around */
        if (idx >= self->priv->sources->len)
            idx = 0;

        s = g_ptr_array_index (self->priv->sources, idx);
        if (s->num_pending > 0) {
            if (out_idx)
                *out_idx = idx;
            return s;
        }
    }

    return NULL;
}

static void schedule_next_command (MMPortSchedulerRR *self);

static gboolean
run_next_command (MMPortSchedulerRR *self)
{
    self->priv->next_pending_id = 0;

    if (find_next_source (self, &self->priv->cur_source)) {
        Source *s;

        s = g_ptr_array_index (self->priv->sources, self->priv->cur_source);
        /* If this source has a pending command, run it. */
        self->priv->in_command = TRUE;
        g_signal_emit (MM_PORT_SCHEDULER (self),
                       send_command_signal,
                       0,
                       s->id);
    }

    return G_SOURCE_REMOVE;
}

static void
schedule_next_command (MMPortSchedulerRR *self)
{
    guint next_idx = 0;
    guint delay = 0;

    if (self->priv->next_pending_id || self->priv->in_command || !find_next_source (self, &next_idx))
        return;

    /* Only delay next command if we change sources and this isn't the
     * first time we're running a command.
     */
    if (next_idx != self->priv->cur_source && self->priv->cur_source < self->priv->sources->len)
        delay = self->priv->inter_port_delay;
    self->priv->next_pending_id = g_timeout_add (delay, (GSourceFunc) run_next_command, self);
}

static void
register_source (MMPortScheduler *scheduler,
                 gpointer         source_id,
                 const gchar     *tag)
{
    MMPortSchedulerRR *self = MM_PORT_SCHEDULER_RR (scheduler);
    Source *s;

    g_assert (source_id != NULL);

    s = find_source (self, source_id, NULL);
    if (!s) {
        s = g_slice_new0 (Source);
        s->id = source_id;
        s->tag = g_strdup (tag);
        g_ptr_array_add (self->priv->sources, s);

        g_assert_cmpint (self->priv->sources->len, <, UINT_MAX);
        mm_obj_dbg (self, "[%s] source id %p registered", tag, source_id);
        mm_log_object_reset_id (MM_LOG_OBJECT (self));
    }
}

static void
unregister_source (MMPortScheduler *scheduler, gpointer source_id)
{
    MMPortSchedulerRR *self = MM_PORT_SCHEDULER_RR (scheduler);
    Source *s;
    guint   idx = 0;

    g_assert (source_id != NULL);

    s = find_source (self, source_id, &idx);
    if (s) {
        mm_obj_dbg (self, "[%s] source id %p unregistered", s->tag, s->id);
        g_ptr_array_remove_index (self->priv->sources, idx);
        mm_log_object_reset_id (MM_LOG_OBJECT (self));

        /* If we just removed the current source, advance to the next one */
        if (self->priv->cur_source == idx)
            schedule_next_command (self);
    }
}

static void
notify_num_pending (MMPortScheduler *scheduler,
                    gpointer         source_id,
                    guint            num_pending)
{
    MMPortSchedulerRR *self = MM_PORT_SCHEDULER_RR (scheduler);
    Source *s;

    g_assert (source_id != NULL);

    s = find_source (self, source_id, NULL);
    if (s && s->num_pending != num_pending) {
        s->num_pending = num_pending;
        schedule_next_command (self);
    }
}

static void
notify_command_done (MMPortScheduler *scheduler,
                     gpointer         source_id,
                     guint            num_pending)
{
    MMPortSchedulerRR *self = MM_PORT_SCHEDULER_RR (scheduler);
    Source            *s;
    guint              idx = 0;

    g_assert (source_id != NULL);

    s = find_source (self, source_id, &idx);
    if (!s) {
        mm_obj_warn (self, "unknown source %p notified command-done", source_id);
        return;
    }

    /* Only the current source gets to call this function */
    if (self->priv->cur_source != idx) {
        mm_obj_warn (self, "[%s] notified command-done but not active source", s->tag);
        return;
    }

    self->priv->in_command = FALSE;
    s->num_pending = num_pending;
    schedule_next_command (self);
}

/*****************************************************************************/

MMPortSchedulerRR *
mm_port_scheduler_rr_new (void)
{
    return MM_PORT_SCHEDULER_RR (g_object_new (MM_TYPE_PORT_SCHEDULER_RR, NULL));
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMPortSchedulerRR *self = MM_PORT_SCHEDULER_RR (object);

    switch (prop_id) {
    case PROP_INTER_PORT_DELAY:
        g_value_set_uint (value, self->priv->inter_port_delay);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMPortSchedulerRR *self = MM_PORT_SCHEDULER_RR (object);

    switch (prop_id) {
    case PROP_INTER_PORT_DELAY:
        self->priv->inter_port_delay = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_port_scheduler_iface_init (MMPortSchedulerInterface *scheduler_iface)
{
    scheduler_iface->register_source = register_source;
    scheduler_iface->unregister_source = unregister_source;
    scheduler_iface->notify_num_pending = notify_num_pending;
    scheduler_iface->notify_command_done = notify_command_done;

    send_command_signal = g_signal_lookup (MM_PORT_SCHEDULER_SIGNAL_SEND_COMMAND,
                                           MM_TYPE_PORT_SCHEDULER);
}

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMPortSchedulerRR  *self = MM_PORT_SCHEDULER_RR (_self);
    g_autoptr(GString)  str;
    guint               i;

    str = g_string_sized_new (16);
    for (i = 0; i < self->priv->sources->len; i++) {
        Source *s;

        s = g_ptr_array_index (self->priv->sources, i);
        if (str->len)
            g_string_append_c (str, ',');
        g_string_append (str, s->tag);
    }

    return g_strdup_printf ("scheduler-%u (%s)", self->priv->instance_id, str->str);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_port_scheduler_rr_init (MMPortSchedulerRR *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_PORT_SCHEDULER_RR,
                                              MMPortSchedulerRRPrivate);
    self->priv->sources = g_ptr_array_new_full (2, (GDestroyNotify) source_free);
    self->priv->cur_source = G_MAXUINT32;
    self->priv->instance_id = instance_id_last++;
}

static void
dispose (GObject *object)
{
    MMPortSchedulerRR *self = MM_PORT_SCHEDULER_RR (object);

    if (self->priv->next_pending_id) {
        g_source_remove (self->priv->next_pending_id);
        self->priv->next_pending_id = 0;
    }

    g_assert (self->priv->sources->len == 0);
    g_ptr_array_free (self->priv->sources, TRUE);

    G_OBJECT_CLASS (mm_port_scheduler_rr_parent_class)->dispose (object);
}

static void
mm_port_scheduler_rr_class_init (MMPortSchedulerRRClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;

    g_object_class_install_property
        (object_class, PROP_INTER_PORT_DELAY,
         g_param_spec_uint (MM_PORT_SCHEDULER_RR_INTER_PORT_DELAY,
                            "Inter-port Delay",
                            "Inter-port delay in ms",
                            0, G_MAXUINT, 0,
                            G_PARAM_READWRITE));
}
