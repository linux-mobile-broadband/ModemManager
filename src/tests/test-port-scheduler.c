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

#include <glib.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>

#include "mm-port-scheduler-rr.h"
#include "mm-log-test.h"

static GMainLoop *loop;

/*****************************************************************************/

typedef struct {
    MMPortScheduler *sched;
    guint            sig_id;
    gpointer         source_id;
    gint             num_pending;
    gboolean         immediate;
    guint            idle_id;

    gpointer         data;
    gpointer         data2;
} TestSourceCtx;

static void
test_source_setup (TestSourceCtx   *ctx,
                   MMPortScheduler *sched,
                   GCallback        send_cmd_func)
{
    ctx->sched = g_object_ref (sched);
    mm_port_scheduler_register_source (sched, ctx->source_id, "test");
    ctx->sig_id = g_signal_connect (sched,
                                    MM_PORT_SCHEDULER_SIGNAL_SEND_COMMAND,
                                    send_cmd_func,
                                    ctx);
    mm_port_scheduler_notify_num_pending (ctx->sched, ctx->source_id, ctx->num_pending);
}

static void
test_source_cleanup (TestSourceCtx *ctx)
{
    g_assert_cmpint (ctx->num_pending, ==, 0);
    g_signal_handler_disconnect (ctx->sched, ctx->sig_id);
    mm_port_scheduler_unregister_source (ctx->sched, ctx->source_id);
    g_object_unref (ctx->sched);
}

/*****************************************************************************/

static gboolean
test_ss_command_done (TestSourceCtx *ctx)
{
    ctx->idle_id = 0;
    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);
    if (ctx->num_pending == 0)
        g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

static void
test_ss_send_command (MMPortScheduler *scheduler,
                      gpointer         source,
                      TestSourceCtx   *ctx)
{
    g_assert (scheduler == ctx->sched);
    g_assert (source == ctx->source_id);
    ctx->num_pending--;
    g_assert_cmpint (ctx->num_pending, >=, 0);

    if (ctx->immediate) {
        mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);
        if (ctx->num_pending == 0)
            g_main_loop_quit (loop);
    } else
        ctx->idle_id = g_idle_add ((GSourceFunc)test_ss_command_done, ctx);
}

static void
test_ss_done (gboolean immediate)
{
    MMPortScheduler *sched;
    TestSourceCtx  ctx = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .num_pending = 10,
        .immediate   = immediate,
    };

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx, sched, G_CALLBACK (test_ss_send_command));

    g_main_loop_run (loop);

    test_source_cleanup (&ctx);
    g_object_unref (sched);
}

/*****************************************************************************/

static void
test_ds_send_command (MMPortScheduler *scheduler,
                      gpointer         source,
                      TestSourceCtx   *ctx)
{
    guint *counter = ctx->data;

    g_assert (scheduler == ctx->sched);
    if (source != ctx->source_id)
        return;

    ctx->num_pending--;
    g_assert_cmpuint (ctx->num_pending, >=, 0);

    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);

    /* assert that the scheduler alternates between the two sources */
    g_assert_cmpuint ((*counter) % 2, ==, ctx->idle_id);

    (*counter)--;
    if (*counter == 0)
        g_main_loop_quit (loop);
}

static void
test_ds_ordering (void)
{
    MMPortScheduler *sched;
    guint            counter;

    TestSourceCtx  ctx1 = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .num_pending = 10,
        .data        = &counter,
        .idle_id     = 0,  /* expected value of (counter % 2) */
    };

    TestSourceCtx  ctx2 = {
        .source_id   = GUINT_TO_POINTER (0x2),
        .num_pending = 10,
        .data        = &counter,
        .idle_id     = 1,  /* expected value of (counter % 2) */
    };

    counter = ctx1.num_pending + ctx2.num_pending;

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx1, sched, G_CALLBACK (test_ds_send_command));
    test_source_setup (&ctx2, sched, G_CALLBACK (test_ds_send_command));

    g_main_loop_run (loop);

    test_source_cleanup (&ctx1);
    test_source_cleanup (&ctx2);
    g_object_unref (sched);
}

/*****************************************************************************/

static void
test_ds_uneven_send_command (MMPortScheduler *scheduler,
                             gpointer         source,
                             TestSourceCtx   *ctx)
{
    guint *counter = ctx->data;

    g_assert (scheduler == ctx->sched);
    if (source != ctx->source_id)
        return;

    ctx->num_pending--;
    /* Test that the scheduler only calls each source for the number of pending
     * commands it has notified the scheduler are in its queue.
     */
    g_assert_cmpint (ctx->num_pending, >=, 0);

    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);

    (*counter)--;
    if (*counter == 0)
        g_main_loop_quit (loop);
}

static void
test_ds_uneven_num_pending (void)
{
    MMPortScheduler *sched;
    guint            counter;

    TestSourceCtx  ctx1 = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .num_pending = 10,
        .data        = &counter,
    };

    TestSourceCtx  ctx2 = {
        .source_id   = GUINT_TO_POINTER (0x2),
        .num_pending = 5,
        .data        = &counter,
    };

    counter = ctx1.num_pending + ctx2.num_pending;

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx1, sched, G_CALLBACK (test_ds_uneven_send_command));
    test_source_setup (&ctx2, sched, G_CALLBACK (test_ds_uneven_send_command));

    g_main_loop_run (loop);

    test_source_cleanup (&ctx1);
    test_source_cleanup (&ctx2);
    g_object_unref (sched);
}

/*****************************************************************************/

static gboolean
ds_later_notify_pending (TestSourceCtx *ctx)
{
    mm_port_scheduler_notify_num_pending (ctx->sched, ctx->source_id, ctx->num_pending);
    return G_SOURCE_REMOVE;
}

static void
test_ds_later_send_command (MMPortScheduler *scheduler,
                            gpointer         source,
                            TestSourceCtx   *ctx)
{
    guint *counter = ctx->data;

    g_assert (scheduler == ctx->sched);
    if (source != ctx->source_id)
        return;

    ctx->num_pending--;
    g_assert_cmpint (ctx->num_pending, >=, 0);

    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);

    /* After we've reached zero pending commands wait a short time and then
     * add more to make sure the scheduler wakes up and processes the new pending
     * requests */
    if (ctx->num_pending == 0 && ctx->idle_id > 0) {
        ctx->num_pending = ctx->idle_id;
        ctx->idle_id = 0;
        g_timeout_add_seconds (GPOINTER_TO_UINT (ctx->source_id),
                               (GSourceFunc)ds_later_notify_pending,
                               ctx);
    }

    (*counter)--;
    if (*counter == 0)
        g_main_loop_quit (loop);
}

static void
test_ds_num_pending_later (void)
{
    MMPortScheduler *sched;
    guint            counter;

    TestSourceCtx  ctx1 = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .num_pending = 5,
        .data        = &counter,
        .idle_id     = 2, /* num pending to add after original num_pending reaches 0 */
    };

    TestSourceCtx  ctx2 = {
        .source_id   = GUINT_TO_POINTER (0x2),
        .num_pending = 4,
        .data        = &counter,
        .idle_id     = 1, /* num pending to add after original num_pending reaches 0 */
    };

    counter = ctx1.num_pending + ctx2.num_pending + ctx1.idle_id + ctx2.idle_id;

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx1, sched, G_CALLBACK (test_ds_later_send_command));
    test_source_setup (&ctx2, sched, G_CALLBACK (test_ds_later_send_command));

    g_main_loop_run (loop);

    test_source_cleanup (&ctx1);
    test_source_cleanup (&ctx2);
    g_object_unref (sched);
}

/*****************************************************************************/

static gboolean
quit_loop (void)
{
    g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

static void
test_ds_bad_notify_send_command (MMPortScheduler *scheduler,
                                 gpointer         source,
                                 TestSourceCtx   *ctx)
{
    g_assert (scheduler == ctx->sched);

    if (source != ctx->source_id)
        return;

    if (GPOINTER_TO_UINT (source) == 0x2) {
        /* Second source without any pending commands tries to call
         * notify_command_done but this should have no effect; the scheduler
         * should ignore the num_pending given here.
         */
        mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, 15);
        return;
    }

    g_assert_cmpuint (GPOINTER_TO_UINT (source), ==, 0x1);

    ctx->num_pending--;
    g_assert_cmpint (ctx->num_pending, >=, 0);

    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);

    if (ctx->num_pending == 0) {
        /* Schedule a timeout to quit the mainloop to give enough time for
         * the scheduler to mess up (which we don't expect).
         */
        g_timeout_add_seconds (1, (GSourceFunc)quit_loop, NULL);
    }
}

static gboolean
assert_not_reached (void)
{
    g_assert_not_reached ();
    return G_SOURCE_REMOVE;
}

static void
test_ds_bad_notify_done (void)
{
    MMPortScheduler *sched;
    guint            timeout_id;

    TestSourceCtx  ctx1 = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .num_pending = 5,
    };

    TestSourceCtx  ctx2 = {
        .source_id   = GUINT_TO_POINTER (0x2),
        .num_pending = 0,
        /* This source just hammers notify_done even though it never has any
         * pending commands.
         */
    };

    timeout_id = g_timeout_add_seconds (3, (GSourceFunc)assert_not_reached, NULL);

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx1, sched, G_CALLBACK (test_ds_bad_notify_send_command));
    test_source_setup (&ctx2, sched, G_CALLBACK (test_ds_bad_notify_send_command));

    g_main_loop_run (loop);

    g_source_remove (timeout_id);
    test_source_cleanup (&ctx1);
    test_source_cleanup (&ctx2);
    g_object_unref (sched);
}

/*****************************************************************************/

static void
test_ds_delay_notify_send_command (MMPortScheduler *scheduler,
                                   gpointer         source,
                                   TestSourceCtx   *ctx)
{
    guint  *counter = ctx->data;
    gint64 *last_call = ctx->data2;
    gint64  now;

    g_assert (scheduler == ctx->sched);
    if (source != ctx->source_id)
        return;

    /* Ensure there was at least the inter-port delay time since the last call */
    now = g_get_monotonic_time ();
    g_assert_cmpint (now - *last_call, >=, 500);
    *last_call = now;

    ctx->num_pending--;
    g_assert_cmpuint (ctx->num_pending, >=, 0);

    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);

    (*counter)--;
    if (*counter == 0)
        g_main_loop_quit (loop);
}

static void
test_ds_inter_port_delay (void)
{
    MMPortScheduler *sched;
    guint            counter;
    gint64           last_call = 0;

    TestSourceCtx  ctx1 = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .data        = &counter,
        .data2       = &last_call,
        .num_pending = 5,
    };

    TestSourceCtx  ctx2 = {
        .source_id   = GUINT_TO_POINTER (0x2),
        .data        = &counter,
        .data2       = &last_call,
        .num_pending = 5,
    };

    counter = ctx1.num_pending + ctx2.num_pending;

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    g_object_set (sched, MM_PORT_SCHEDULER_RR_INTER_PORT_DELAY, 500, NULL);
    test_source_setup (&ctx1, sched, G_CALLBACK (test_ds_delay_notify_send_command));
    test_source_setup (&ctx2, sched, G_CALLBACK (test_ds_delay_notify_send_command));

    g_main_loop_run (loop);

    test_source_cleanup (&ctx1);
    test_source_cleanup (&ctx2);
    g_object_unref (sched);
}

/*****************************************************************************/

static void
test_ds_no_delay_notify_send_command (MMPortScheduler *scheduler,
                                      gpointer         source,
                                      TestSourceCtx   *ctx)
{
    guint  *counter = ctx->data;
    gint64 *last_call = ctx->data2;
    gint64  now;

    g_assert (scheduler == ctx->sched);
    if (source != ctx->source_id)
        return;

    /* Since the second source has no pending commands, there should be
     * no delay between calls since only one source is executing.
     */
    now = g_get_monotonic_time ();
    g_assert_cmpint (now - *last_call, <, 1000);
    *last_call = now;

    ctx->num_pending--;
    g_assert_cmpuint (ctx->num_pending, >=, 0);

    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);

    (*counter)--;
    if (*counter == 0)
        g_main_loop_quit (loop);
}

static void
test_ds_inter_port_no_delay (void)
{
    MMPortScheduler *sched;
    guint            counter;
    gint64           last_call;

    TestSourceCtx  ctx1 = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .data        = &counter,
        .data2       = &last_call,
        .num_pending = 5,
    };

    TestSourceCtx  ctx2 = {
        .source_id   = GUINT_TO_POINTER (0x2),
        .data        = &counter,
        .data2       = &last_call,
        .num_pending = 0,
    };

    counter = ctx1.num_pending + ctx2.num_pending;
    last_call = g_get_monotonic_time ();

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx1, sched, G_CALLBACK (test_ds_no_delay_notify_send_command));
    test_source_setup (&ctx2, sched, G_CALLBACK (test_ds_no_delay_notify_send_command));

    g_main_loop_run (loop);

    test_source_cleanup (&ctx1);
    test_source_cleanup (&ctx2);
    g_object_unref (sched);
}

/*****************************************************************************/

static void
test_ds_pending_during_done_notify_send_command (MMPortScheduler *scheduler,
                                                 gpointer         source,
                                                 TestSourceCtx   *ctx)
{
    guint  *counter = ctx->data;

    g_assert (scheduler == ctx->sched);
    if (source != ctx->source_id)
        return;

    ctx->num_pending--;
    g_assert_cmpuint (ctx->num_pending, >=, 0);

    /* Simulate command completion adding more pending commands before calling command-done */
    if (ctx->idle_id > 0) {
        ctx->idle_id--;
        ctx->num_pending++; /* increase length of fake source's command queue */
        mm_port_scheduler_notify_num_pending (ctx->sched, ctx->source_id, ctx->num_pending);
    }

    mm_port_scheduler_notify_command_done (ctx->sched, ctx->source_id, ctx->num_pending);

    (*counter)--;
    if (*counter == 0)
        g_main_loop_quit (loop);
}

static void
test_ds_pending_during_done (void)
{
    MMPortScheduler *sched;
    guint            counter;

    TestSourceCtx  ctx1 = {
        .source_id   = GUINT_TO_POINTER (0x1),
        .data        = &counter,
        .idle_id     = 5, /* additional to add during notify-command-done */
        .num_pending = 5,
    };

    TestSourceCtx  ctx2 = {
        .source_id   = GUINT_TO_POINTER (0x2),
        .data        = &counter,
        .idle_id     = 5, /* additional to add during notify-command-done */
        .num_pending = 5,
    };

    counter = ctx1.num_pending + ctx2.num_pending + ctx1.idle_id + ctx2.idle_id;

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx1, sched, G_CALLBACK (test_ds_pending_during_done_notify_send_command));
    test_source_setup (&ctx2, sched, G_CALLBACK (test_ds_pending_during_done_notify_send_command));

    g_main_loop_run (loop);

    test_source_cleanup (&ctx1);
    test_source_cleanup (&ctx2);
    g_object_unref (sched);
}

/*****************************************************************************/

static void
test_errors_bad_source_done (void)
{
    MMPortScheduler *sched;

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    mm_port_scheduler_notify_command_done (sched, GUINT_TO_POINTER (0x1), 5);
    g_object_unref (sched);
}

/*****************************************************************************/

static void
test_errors_source_done_before_loop (void)
{
    MMPortScheduler *sched;

    TestSourceCtx  ctx = {
        .source_id   = GUINT_TO_POINTER (0x1),
    };

    sched = MM_PORT_SCHEDULER (mm_port_scheduler_rr_new ());
    test_source_setup (&ctx, sched, G_CALLBACK (assert_not_reached));
    mm_port_scheduler_notify_command_done (sched, ctx.source_id, 5);

    test_source_cleanup (&ctx);
    g_object_unref (sched);
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    int ret;

    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    loop = g_main_loop_new (NULL, FALSE);

    g_test_add_data_func ("/MM/port-scheduler/single-source/done-immediate",    GUINT_TO_POINTER (TRUE),  (GTestDataFunc)test_ss_done);
    g_test_add_data_func ("/MM/port-scheduler/single-source/done-idle",         GUINT_TO_POINTER (FALSE), (GTestDataFunc)test_ss_done);
    g_test_add_data_func ("/MM/port-scheduler/dual-source/ordering",            NULL,                     (GTestDataFunc)test_ds_ordering);
    g_test_add_data_func ("/MM/port-scheduler/dual-source/uneven-num-pending",  NULL,                     (GTestDataFunc)test_ds_uneven_num_pending);
    g_test_add_data_func ("/MM/port-scheduler/dual-source/num-pending-later",   NULL,                     (GTestDataFunc)test_ds_num_pending_later);
    g_test_add_data_func ("/MM/port-scheduler/dual-source/bad-notify-done",     NULL,                     (GTestDataFunc)test_ds_bad_notify_done);
    g_test_add_data_func ("/MM/port-scheduler/dual-source/inter-port-delay",    NULL,                     (GTestDataFunc)test_ds_inter_port_delay);
    g_test_add_data_func ("/MM/port-scheduler/dual-source/inter-port-no-delay", NULL,                     (GTestDataFunc)test_ds_inter_port_no_delay);
    g_test_add_data_func ("/MM/port-scheduler/dual-source/pending-during-done", NULL,                     (GTestDataFunc)test_ds_pending_during_done);
    g_test_add_data_func ("/MM/port-scheduler/errors/bad-source-done",          NULL,                     (GTestDataFunc)test_errors_bad_source_done);
    g_test_add_data_func ("/MM/port-scheduler/errors/source-done-before-loop",  NULL,                     (GTestDataFunc)test_errors_source_done_before_loop);

    ret = g_test_run();

    g_main_loop_unref (loop);

    return ret;
}
