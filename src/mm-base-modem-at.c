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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <glib.h>
#include <glib-object.h>

#include <ModemManager.h>

#include "mm-base-modem-at.h"
#include "mm-errors-types.h"

static gboolean
abort_task_if_port_unusable (MMBaseModem *self,
                             MMPortSerialAt *port,
                             GTask *task)
{
    GError *error = NULL;
    gboolean init_sequence_enabled = FALSE;

    /* If no port given, probably the port disappeared */
    if (!port) {
        g_task_return_new_error (task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_NOT_FOUND,
            "Cannot run sequence: port not given");
        g_object_unref (task);
        return FALSE;
    }

    /* Ensure we don't try to use a connected port */
    if (mm_port_get_connected (MM_PORT (port))) {
        g_task_return_new_error (task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Cannot run sequence: port is connected");
        g_object_unref (task);
        return FALSE;
    }

    /* Temporarily disable init sequence if we're just sending a
     * command to a just opened port */
    g_object_get (port, MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED, &init_sequence_enabled, NULL);
    g_object_set (port, MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED, FALSE, NULL);

    /* Ensure we have a port open during the sequence */
    if (!mm_port_serial_open (MM_PORT_SERIAL (port), &error)) {
        g_task_return_new_error (task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Cannot run sequence: '%s'",
            error->message);
        g_error_free (error);
        g_object_unref (task);
        return FALSE;
    }

    /* Reset previous init sequence state */
    g_object_set (port, MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED, init_sequence_enabled, NULL);

    return TRUE;
}

static void
parent_cancellable_cancelled (GCancellable *parent_cancellable,
                              GCancellable *cancellable)
{
    g_cancellable_cancel (cancellable);
}

/*****************************************************************************/
/* AT sequence handling */

typedef struct {
    MMPortSerialAt             *port;
    gulong                      cancelled_id;
    GCancellable               *parent_cancellable;
    const MMBaseModemAtCommand *current;
    const MMBaseModemAtCommand *sequence;
    gpointer                    response_processor_context;
    GDestroyNotify              response_processor_context_free;
    GVariant                   *result;
} AtSequenceContext;

static void
at_sequence_context_free (AtSequenceContext *ctx)
{
    mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
    g_object_unref (ctx->port);

    if (ctx->response_processor_context &&
        ctx->response_processor_context_free)
        ctx->response_processor_context_free (ctx->response_processor_context);

    if (ctx->parent_cancellable) {
        g_cancellable_disconnect (ctx->parent_cancellable,
                                  ctx->cancelled_id);
        g_object_unref (ctx->parent_cancellable);
    }

    if (ctx->result)
        g_variant_unref (ctx->result);
    g_free (ctx);
}

GVariant *
mm_base_modem_at_sequence_full_finish (MMBaseModem   *self,
                                       GAsyncResult  *res,
                                       gpointer      *response_processor_context,
                                       GError       **error)
{
    GTask *task;
    GVariant *result;

    task = G_TASK (res);
    result = g_task_propagate_pointer (task, error);

    if (response_processor_context && !g_task_had_error (task)) {
        AtSequenceContext *ctx;

        ctx = g_task_get_task_data (task);

        /* transfer none, no need to free the context ourselves, if
         * we gave a response_processor_context_free callback */
        *response_processor_context = ctx->response_processor_context;
    }

    /* transfer-none! (so that we can ignore it) */
    return result;
}

static void
at_sequence_parse_response (MMPortSerialAt    *port,
                            GAsyncResult      *res,
                            GTask             *task)
{
    MMBaseModemAtResponseProcessorResult  processor_result;
    GVariant                             *result = NULL;
    GError                               *result_error = NULL;
    AtSequenceContext                    *ctx;
    g_autofree gchar                     *response = NULL;
    GError                               *error = NULL;

    response = mm_port_serial_at_command_finish (port, res, &error);

    /* Cancelled? */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    if (!ctx->current->response_processor)
        processor_result = MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    else {
        const MMBaseModemAtCommand *next = ctx->current + 1;

        /* Response processor will tell us if we need to keep on the sequence */
        processor_result = ctx->current->response_processor (g_task_get_source_object (task),
                                                             ctx->response_processor_context,
                                                             ctx->current->command,
                                                             response,
                                                             next->command ? FALSE : TRUE,  /* Last command in sequence? */
                                                             error,
                                                             &result,
                                                             &result_error);
        switch (processor_result) {
            case MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE:
                g_assert (!result && !result_error);
                break;
            case MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS:
                g_assert (!result_error); /* result is optional */
                break;
            case MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE:
                /* On failure, complete with error right away */
                g_assert (!result && result_error); /* result is optional */
                g_task_return_error (task, result_error);
                g_object_unref (task);
                if (error)
                    g_error_free (error);
                return;
            default:
                g_assert_not_reached ();
        }
    }

    if (error)
        g_error_free (error);

    if (processor_result == MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE) {
        ctx->current++;
        if (ctx->current->command) {
            /* Schedule the next command in the probing group */
            mm_port_serial_at_command (
                ctx->port,
                ctx->current->command,
                ctx->current->timeout,
                FALSE,
                ctx->current->allow_cached,
                g_task_get_cancellable (task),
                (GAsyncReadyCallback)at_sequence_parse_response,
                task);
            return;
        }
        /* On last command, end. */
    }

    /* If we got a response, set it as result */
    if (result)
        /* transfer-full */
        ctx->result = result;

    /* transfer-none, the result remains owned by the GTask context */
    g_task_return_pointer (task, ctx->result, NULL);
    g_object_unref (task);
}

static void
at_sequence_common (MMBaseModem                *self,
                    MMPortSerialAt             *port,
                    const MMBaseModemAtCommand *sequence,
                    gpointer                    response_processor_context,
                    GDestroyNotify              response_processor_context_free,
                    GTask                      *task,
                    GCancellable               *parent_cancellable)
{
    AtSequenceContext *ctx;

    /* Ensure that we have an open port */
    if (!abort_task_if_port_unusable (self, port, task))
        return;

    /* Setup context */
    ctx = g_new0 (AtSequenceContext, 1);
    ctx->port = g_object_ref (port);
    ctx->current = ctx->sequence = sequence;
    ctx->response_processor_context = response_processor_context;
    ctx->response_processor_context_free = response_processor_context_free;

    /* Ensure the cancellable that's already associated with the modem
     * will also get cancelled if the modem wide-one gets cancelled */
    if (parent_cancellable) {
        GCancellable *cancellable;

        cancellable = g_task_get_cancellable (task);
        ctx->parent_cancellable = g_object_ref (parent_cancellable);
        ctx->cancelled_id = g_cancellable_connect (ctx->parent_cancellable,
                                                   G_CALLBACK (parent_cancellable_cancelled),
                                                   cancellable,
                                                   NULL);
    }

    g_task_set_task_data (task, ctx, (GDestroyNotify)at_sequence_context_free);

    /* Go on with the first one in the sequence */
    mm_port_serial_at_command (
        ctx->port,
        ctx->current->command,
        ctx->current->timeout,
        FALSE,
        ctx->current->allow_cached,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)at_sequence_parse_response,
        task);
}

void
mm_base_modem_at_sequence_full (MMBaseModem                *self,
                                MMPortSerialAt             *port,
                                const MMBaseModemAtCommand *sequence,
                                gpointer                    response_processor_context,
                                GDestroyNotify              response_processor_context_free,
                                GCancellable               *cancellable,
                                GAsyncReadyCallback         callback,
                                gpointer                    user_data)
{
    GCancellable *modem_cancellable;
    GTask *task;

    modem_cancellable = mm_base_modem_peek_cancellable (self);
    task = g_task_new (self,
                       cancellable ? cancellable : modem_cancellable,
                       callback,
                       user_data);

    at_sequence_common (self,
                        port,
                        sequence,
                        response_processor_context,
                        response_processor_context_free,
                        task,
                        cancellable ? modem_cancellable : NULL);
}

GVariant *
mm_base_modem_at_sequence_finish (MMBaseModem *self,
                                  GAsyncResult *res,
                                  gpointer *response_processor_context,
                                  GError **error)
{
    return (mm_base_modem_at_sequence_full_finish (
                self,
                res,
                response_processor_context,
                error));
}

void
mm_base_modem_at_sequence (MMBaseModem *self,
                           const MMBaseModemAtCommand *sequence,
                           gpointer response_processor_context,
                           GDestroyNotify response_processor_context_free,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    MMPortSerialAt *port;
    GError *error = NULL;
    GTask *task;

    task = g_task_new (self,
                       mm_base_modem_peek_cancellable (self),
                       callback,
                       user_data);

    /* No port given, so we'll try to guess which is best */
    port = mm_base_modem_peek_best_at_port (self, &error);
    if (!port) {
        g_assert (error != NULL);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    at_sequence_common (self,
                        port,
                        sequence,
                        response_processor_context,
                        response_processor_context_free,
                        task,
                        NULL);
}

/*****************************************************************************/
/* Response processor helpers */

MMBaseModemAtResponseProcessorResult
mm_base_modem_response_processor_string (MMBaseModem   *self,
                                         gpointer       none,
                                         const gchar   *command,
                                         const gchar   *response,
                                         gboolean       last_command,
                                         const GError  *error,
                                         GVariant     **result,
                                         GError       **result_error)
{
    if (error) {
        *result = NULL;
        *result_error = g_error_copy (error);
        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
    }

    *result = g_variant_new_string (response);
    *result_error = NULL;
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
}

MMBaseModemAtResponseProcessorResult
mm_base_modem_response_processor_no_result (MMBaseModem   *self,
                                            gpointer       none,
                                            const gchar   *command,
                                            const gchar   *response,
                                            gboolean       last_command,
                                            const GError  *error,
                                            GVariant     **result,
                                            GError       **result_error)
{
    if (error) {
        *result = NULL;
        *result_error = g_error_copy (error);
        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
    }

    *result = NULL;
    *result_error = NULL;
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
}

MMBaseModemAtResponseProcessorResult
mm_base_modem_response_processor_no_result_continue (MMBaseModem   *self,
                                                     gpointer       none,
                                                     const gchar   *command,
                                                     const gchar   *response,
                                                     gboolean       last_command,
                                                     const GError  *error,
                                                     GVariant     **result,
                                                     GError       **result_error)
{
    *result = NULL;

    if (error) {
        *result_error = g_error_copy (error);
        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
    }

    *result_error = NULL;
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
}

MMBaseModemAtResponseProcessorResult
mm_base_modem_response_processor_continue_on_error (MMBaseModem   *self,
                                                    gpointer       none,
                                                    const gchar   *command,
                                                    const gchar   *response,
                                                    gboolean       last_command,
                                                    const GError  *error,
                                                    GVariant     **result,
                                                    GError       **result_error)
{
    *result = NULL;
    *result_error = NULL;

    return (error ?
            MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE :
            MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS);
}

MMBaseModemAtResponseProcessorResult
mm_base_modem_response_processor_string_ignore_at_errors (MMBaseModem   *self,
                                                          gpointer       none,
                                                          const gchar   *command,
                                                          const gchar   *response,
                                                          gboolean       last_command,
                                                          const GError  *error,
                                                          GVariant     **result,
                                                          GError       **result_error)
{
    if (error) {
        *result = NULL;

        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command) {

            *result_error = g_error_copy (error);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
        }

        *result_error = NULL;
        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    }

    *result = g_variant_new_string (response);
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
}

/*****************************************************************************/
/* Single AT command handling */

typedef struct {
    MMPortSerialAt *port;
    gulong cancelled_id;
    GCancellable *parent_cancellable;
    gchar *response;
} AtCommandContext;

static void
at_command_context_free (AtCommandContext *ctx)
{
    mm_port_serial_close (MM_PORT_SERIAL (ctx->port));

    if (ctx->parent_cancellable) {
        g_cancellable_disconnect (ctx->parent_cancellable,
                                  ctx->cancelled_id);
        g_object_unref (ctx->parent_cancellable);
    }

    g_object_unref (ctx->port);
    g_free (ctx->response);
    g_free (ctx);
}

const gchar *
mm_base_modem_at_command_full_finish (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
at_command_ready (MMPortSerialAt *port,
                  GAsyncResult *res,
                 GTask *task)
{
    AtCommandContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    g_assert (!ctx->response);
    ctx->response = mm_port_serial_at_command_finish (port, res, &error);

    /* Cancelled? */
    if (g_task_return_error_if_cancelled (task)) {
        if (error)
            g_error_free (error);
    }
    /* Error coming from the serial port? */
    else if (error)
        g_task_return_error (task, error);
    /* Valid string response */
    else if (ctx->response)
        /* transfer-none, the response remains owned by the GTask context */
        g_task_return_pointer (task, ctx->response, NULL);
    else
        g_assert_not_reached ();

    g_object_unref (task);
}

static void
at_command_common (MMBaseModem *self,
                   MMPortSerialAt *port,
                   const gchar *command,
                   guint timeout,
                   gboolean allow_cached,
                   gboolean is_raw,
                   GTask *task,
                   GCancellable *parent_cancellable)
{
    AtCommandContext *ctx;

    /* Ensure that we have an open port */
    if (!abort_task_if_port_unusable (self, port, task))
        return;

    ctx = g_new0 (AtCommandContext, 1);
    ctx->port = g_object_ref (port);

    /* Ensure the cancellable that's already associated with the modem
     * will also get cancelled if the modem wide-one gets cancelled */
    if (parent_cancellable) {
        GCancellable *cancellable;

        cancellable = g_task_get_cancellable (task);
        ctx->parent_cancellable = g_object_ref (parent_cancellable);
        ctx->cancelled_id = g_cancellable_connect (ctx->parent_cancellable,
                                                   G_CALLBACK (parent_cancellable_cancelled),
                                                   cancellable,
                                                   NULL);
    }

    g_task_set_task_data (task, ctx, (GDestroyNotify)at_command_context_free);

    /* Go on with the command */
    mm_port_serial_at_command (
        port,
        command,
        timeout,
        is_raw,
        allow_cached,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)at_command_ready,
        task);
}

void
mm_base_modem_at_command_full (MMBaseModem *self,
                               MMPortSerialAt *port,
                               const gchar *command,
                               guint timeout,
                               gboolean allow_cached,
                               gboolean is_raw,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GCancellable *modem_cancellable;
    GTask *task;

    modem_cancellable = mm_base_modem_peek_cancellable (self);
    task = g_task_new (self,
                       cancellable ? cancellable : modem_cancellable,
                       callback,
                       user_data);

    at_command_common (self,
                       port,
                       command,
                       timeout,
                       allow_cached,
                       is_raw,
                       task,
                       cancellable ? modem_cancellable : NULL);
}

const gchar *
mm_base_modem_at_command_finish (MMBaseModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return mm_base_modem_at_command_full_finish (self, res, error);
}

static void
_at_command (MMBaseModem *self,
             const gchar *command,
             guint timeout,
             gboolean allow_cached,
             gboolean is_raw,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    MMPortSerialAt *port;
    GError *error = NULL;
    GTask *task;

    task = g_task_new (self,
                       mm_base_modem_peek_cancellable (self),
                       callback,
                       user_data);

    /* No port given, so we'll try to guess which is best */
    port = mm_base_modem_peek_best_at_port (self, &error);
    if (!port) {
        g_assert (error != NULL);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    at_command_common (self,
                       port,
                       command,
                       timeout,
                       allow_cached,
                       is_raw,
                       task,
                       NULL);
}

void
mm_base_modem_at_command (MMBaseModem *self,
                          const gchar *command,
                          guint timeout,
                          gboolean allow_cached,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    _at_command (self, command, timeout, allow_cached, FALSE, callback, user_data);
}

void
mm_base_modem_at_command_raw (MMBaseModem *self,
                              const gchar *command,
                              guint timeout,
                              gboolean allow_cached,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    _at_command (self, command, timeout, allow_cached, TRUE, callback, user_data);
}

void
mm_base_modem_at_command_alloc_clear (MMBaseModemAtCommandAlloc *command)
{
    g_free (command->command);
}
