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

#include "mm-at.h"
#include "mm-errors-types.h"

typedef struct {
    GObject *owner;
    MMAtSerialPort *port;
    GCancellable *cancellable;
    MMAtCommand *current;
    MMAtCommand *sequence;
    gboolean sequence_free;
    GSimpleAsyncResult *result;
    gchar *result_signature;
    gpointer response_processor_context;
} AtSequenceContext;

static void
at_sequence_free (MMAtCommand *sequence)
{
    MMAtCommand *it;

    for (it = sequence; it->command; it++)
        g_free (it->command);

    g_free (sequence);
}

static void
at_sequence_context_free (AtSequenceContext *ctx)
{
    if (ctx->sequence_free)
        at_sequence_free (ctx->sequence);
    if (ctx->owner)
        g_object_unref (ctx->owner);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx->result_signature);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_free (ctx);
}

static void
at_sequence_parse_response (MMAtSerialPort *port,
                            GString *response,
                            GError *error,
                            AtSequenceContext *ctx)
{
    GVariant *result = NULL;
    GError *result_error = NULL;
    gboolean continue_sequence;

    /* Cancelled? */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "AT sequence was cancelled");
        g_simple_async_result_complete (ctx->result);
        at_sequence_context_free (ctx);
        return;
    }

    /* Translate the AT response into a GVariant with the expected signature */
    if (!ctx->current->response_processor)
        /* No need to process response, go on to next command */
        continue_sequence = TRUE;
    else {
        /* Response processor will tell us if we need to keep on the sequence */
        continue_sequence = !ctx->current->response_processor (
            ctx->owner,
            ctx->response_processor_context,
            ctx->current->command,
            response->str,
            error,
            &result,
            &result_error);
        /* Were we told to abort the sequence? */
        if (result_error) {
            g_assert (result == NULL);
            g_simple_async_result_take_error (ctx->result, result_error);
            g_simple_async_result_complete (ctx->result);
            at_sequence_context_free (ctx);
            return;
        }
    }

    if (continue_sequence) {
        g_assert (result == NULL);
        ctx->current++;
        if (ctx->current->command) {
            /* Schedule the next command in the probing group */
            mm_at_serial_port_queue_command (
                ctx->port,
                ctx->current->command,
                ctx->current->timeout,
                (MMAtSerialResponseFn)at_sequence_parse_response,
                ctx);
            return;
        }

        /* On last command, end. */
    }

    /* Maybe we do not expect/need any result */
    if (!ctx->result_signature) {
        g_assert (result == NULL);
        g_simple_async_result_set_op_res_gpointer (ctx->result, NULL, NULL);
    }
    /* If we do expect one but got none, set error */
    else if (!result) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Expecting result type '%s', got none",
                                         ctx->result_signature);
    }
    /* Check if the result we got is of the type we expected */
    else if (!g_str_equal (g_variant_get_type_string (result),
                           ctx->result_signature)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Expecting result type '%s', got '%s'",
                                         ctx->result_signature,
                                         g_variant_get_type_string (result));
    }
    /* We got a valid expected reply */
    else {
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_variant_ref (result),
                                                   (GDestroyNotify)g_variant_unref);
    }

    if (result)
        g_variant_unref (result);
    g_simple_async_result_complete (ctx->result);
    at_sequence_context_free (ctx);
}

GVariant *
mm_at_sequence_finish (GObject *owner,
                       GAsyncResult *res,
                       GError **error)
{
    GVariant *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res),
                                               error))
        return NULL;

    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    return (result ? g_variant_ref (result) : NULL);
}

void
mm_at_sequence (GObject *owner,
                MMAtSerialPort *port,
                MMAtCommand *sequence,
                gpointer response_processor_context,
                gboolean sequence_free,
                const gchar *result_signature,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    AtSequenceContext *ctx;

    g_return_if_fail (MM_IS_AT_SERIAL_PORT (port));
    g_return_if_fail (sequence != NULL);
    g_return_if_fail (callback != NULL);

    ctx = g_new (AtSequenceContext, 1);
    ctx->owner = owner ? g_object_ref (owner) : NULL;
    ctx->port = g_object_ref (port);
    ctx->result_signature = g_strdup (result_signature);
    ctx->cancellable = (cancellable ?
                        g_object_ref (cancellable) :
                        NULL);
    ctx->result = g_simple_async_result_new (owner,
                                             callback,
                                             user_data,
                                             mm_at_sequence);
    ctx->current = ctx->sequence = sequence;
    ctx->sequence_free = sequence_free;
    ctx->response_processor_context = response_processor_context;

    mm_at_serial_port_queue_command (
        ctx->port,
        ctx->current->command,
        ctx->current->timeout,
        (MMAtSerialResponseFn)at_sequence_parse_response,
        ctx);
}

GVariant *
mm_at_command_finish (GObject *owner,
                      GAsyncResult *res,
                      GError **error)
{
    return mm_at_sequence_finish (owner, res, error);
}

void
mm_at_command (GObject *owner,
               MMAtSerialPort *port,
               const gchar *command,
               guint timeout,
               const MMAtResponseProcessor response_processor,
               gpointer response_processor_context,
               const gchar *result_signature,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    MMAtCommand *sequence;

    g_return_if_fail (MM_IS_AT_SERIAL_PORT (port));
    g_return_if_fail (command != NULL);
    g_return_if_fail (response_processor != NULL ||
                      result_signature == NULL);
    g_return_if_fail (callback != NULL);

    sequence = g_new0 (MMAtCommand, 2);
    sequence->command = g_strdup (command);
    sequence->timeout = timeout;
    sequence->response_processor = response_processor;

    mm_at_sequence (owner,
                    port,
                    sequence,
                    response_processor_context,
                    TRUE,
                    result_signature,
                    cancellable,
                    callback,
                    user_data);
}
