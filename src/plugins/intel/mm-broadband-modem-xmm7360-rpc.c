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
 * Copyright (C) 2024 Thomas Vogt
 */

#include <glib.h>
#include <glib-object.h>

#include <ModemManager.h>

#include "mm-log-object.h"
#include "mm-errors-types.h"

#include "mm-broadband-modem-xmm7360-rpc.h"

static gboolean
abort_task_if_port_unusable (MMBroadbandModemXmm7360 *self,
                             MMPortSerialXmmrpcXmm7360 *port,
                             GTask *task)
{
    GError *error = NULL;

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

    return TRUE;
}

static void
parent_cancellable_cancelled (GCancellable *parent_cancellable,
                              GCancellable *cancellable)
{
    g_cancellable_cancel (cancellable);
}

/*****************************************************************************/
/* RPC sequence handling */

static void rpc_sequence_parse_response (MMPortSerialXmmrpcXmm7360 *port,
                                         GAsyncResult              *res,
                                         GTask                     *task);

typedef struct {
    MMPortSerialXmmrpcXmm7360               *port;
    gulong                                   cancelled_id;
    GCancellable                            *parent_cancellable;
    const MMBroadbandModemXmm7360RpcCommand *current;
    gboolean                                 current_retry;
    const MMBroadbandModemXmm7360RpcCommand *sequence;
    guint                                    next_command_wait_id;
} RpcSequenceContext;

static void
rpc_sequence_context_free (RpcSequenceContext *ctx)
{
    mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
    g_object_unref (ctx->port);

    if (ctx->parent_cancellable) {
        g_cancellable_disconnect (ctx->parent_cancellable,
                                  ctx->cancelled_id);
        g_object_unref (ctx->parent_cancellable);
    }

    if (ctx->next_command_wait_id > 0) {
        g_source_remove (ctx->next_command_wait_id);
        ctx->next_command_wait_id = 0;
    }

    g_free (ctx);
}

Xmm7360RpcResponse *
mm_broadband_modem_xmm7360_rpc_sequence_full_finish (MMBroadbandModemXmm7360   *self,
                                                     GAsyncResult              *res,
                                                     GError                   **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static gboolean
rpc_sequence_next_command (GTask *task)
{
    RpcSequenceContext *ctx;
    g_autoptr(GByteArray) body = NULL;

    ctx = g_task_get_task_data (task);
    ctx->next_command_wait_id = 0;

    body = xmm7360_rpc_args_to_byte_array ((const Xmm7360RpcMsgArg *)ctx->current->body);

    /* Schedule the next command in the probing group */
    mm_port_serial_xmmrpc_xmm7360_command (
        ctx->port,
        ctx->current->callid,
        ctx->current->is_async,
        body,
        ctx->current->timeout,
        ctx->current->allow_cached,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)rpc_sequence_parse_response,
        task);

    return G_SOURCE_REMOVE;
}

static void
rpc_sequence_parse_response (MMPortSerialXmmrpcXmm7360 *port,
                             GAsyncResult              *res,
                             GTask                     *task)
{
    MMBroadbandModemXmm7360RpcResponseProcessorResult  processor_result;
    GError                                            *result_error = NULL;
    RpcSequenceContext                                *ctx;
    Xmm7360RpcResponse                                *response = NULL;
    g_autoptr(GError)                                  command_error = NULL;
    gboolean                                           command_retry = FALSE;

    response = mm_port_serial_xmmrpc_xmm7360_command_finish (port, res, &command_error);

    /* Cancelled? */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    if (!ctx->current->response_processor)
        processor_result = MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    else {
        const MMBroadbandModemXmm7360RpcCommand *next = ctx->current + 1;

        /* Response processor will tell us if we need to keep on the sequence */
        processor_result = ctx->current->response_processor (g_task_get_source_object (task),
                                                             response,
                                                             next->callid ? FALSE : TRUE,  /* Last command in sequence? */
                                                             command_error,
                                                             &result_error);

        if (processor_result == MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_FAILURE
            && ctx->current->allow_retry_once && !ctx->current_retry) {
            mm_obj_warn (port, "Command failed (%s), retrying once",
                         result_error ? result_error->message : "UNKNOWN");

            if (response)
                xmm7360_rpc_response_free (response);
            g_clear_error (&result_error);

            command_retry = TRUE;
            goto skip_result;
        }

        switch (processor_result) {
            case MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_CONTINUE:
                g_assert (!result_error);
                if (response)
                    xmm7360_rpc_response_free (response);
                break;
            case MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_SUCCESS:
                g_assert (!result_error);
                break;
            case MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_FAILURE:
                /* On failure, complete with error right away */
                g_assert (result_error);
                if (response)
                    xmm7360_rpc_response_free (response);
                g_task_return_error (task, result_error);
                g_object_unref (task);
                return;
            default:
                g_assert_not_reached ();
        }
    }

skip_result:
    if (processor_result == MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_CONTINUE ||
        command_retry) {
        if (!command_retry) {
            ctx->current++;
        }
        ctx->current_retry = command_retry;

        if (ctx->current->callid) {
            g_assert (!ctx->next_command_wait_id);
            ctx->next_command_wait_id = g_timeout_add_seconds (ctx->current->wait_seconds,
                                                               (GSourceFunc) rpc_sequence_next_command,
                                                               task);
            return;
        }
        g_assert (!command_retry);
        /* On last command, end. */
    }

    g_task_return_pointer (task, response, (GDestroyNotify) xmm7360_rpc_response_free);
    g_object_unref (task);
}

static void
rpc_sequence_common (MMBroadbandModemXmm7360                 *self,
                     MMPortSerialXmmrpcXmm7360               *port,
                     const MMBroadbandModemXmm7360RpcCommand *sequence,
                     GTask                                   *task,
                     GCancellable                            *parent_cancellable)
{
    RpcSequenceContext *ctx;
    g_autoptr(GByteArray) body = NULL;

    /* Ensure that we have an open port */
    if (!abort_task_if_port_unusable (self, port, task))
        return;

    /* Setup context */
    ctx = g_new0 (RpcSequenceContext, 1);
    ctx->port = g_object_ref (port);
    ctx->current = ctx->sequence = sequence;

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

    g_task_set_task_data (task, ctx, (GDestroyNotify)rpc_sequence_context_free);

    body = xmm7360_rpc_args_to_byte_array ((const Xmm7360RpcMsgArg *)ctx->current->body);

    /* Go on with the first one in the sequence */
    mm_port_serial_xmmrpc_xmm7360_command (
        ctx->port,
        ctx->current->callid,
        ctx->current->is_async,
        body,
        ctx->current->timeout,
        ctx->current->allow_cached,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)rpc_sequence_parse_response,
        task);
}

void
mm_broadband_modem_xmm7360_rpc_sequence_full (MMBroadbandModemXmm7360                 *self,
                                              MMPortSerialXmmrpcXmm7360               *port,
                                              const MMBroadbandModemXmm7360RpcCommand *sequence,
                                              GCancellable                            *cancellable,
                                              GAsyncReadyCallback                      callback,
                                              gpointer                                 user_data)
{
    GCancellable *modem_cancellable;
    GTask *task;

    modem_cancellable = mm_base_modem_peek_cancellable (MM_BASE_MODEM (self));
    task = g_task_new (self,
                       cancellable ? cancellable : modem_cancellable,
                       callback,
                       user_data);

    rpc_sequence_common (self,
                         port,
                         sequence,
                         task,
                         cancellable ? modem_cancellable : NULL);
}

Xmm7360RpcResponse *
mm_broadband_modem_xmm7360_rpc_sequence_finish (MMBroadbandModemXmm7360 *self,
                                                GAsyncResult *res,
                                                GError **error)
{
    return mm_broadband_modem_xmm7360_rpc_sequence_full_finish (self, res, error);
}

void
mm_broadband_modem_xmm7360_rpc_sequence (MMBroadbandModemXmm7360 *self,
                                         const MMBroadbandModemXmm7360RpcCommand *sequence,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    MMPortSerialXmmrpcXmm7360 *port;
    GError *error = NULL;
    GTask *task;

    task = g_task_new (self,
                       mm_base_modem_peek_cancellable (MM_BASE_MODEM (self)),
                       callback,
                       user_data);

    /* No port given, so we'll try to guess which is best */
    port = mm_broadband_modem_xmm7360_peek_port_xmmrpc (self);
    if (!port) {
        g_set_error (&error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_CONNECTED,
                     "No XMMRPC port available to run command");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    rpc_sequence_common (self,
                         port,
                         sequence,
                         task,
                         NULL);
}

/*****************************************************************************/
/* Response processor helpers */

MMBroadbandModemXmm7360RpcResponseProcessorResult
mm_broadband_modem_xmm7360_rpc_response_processor_final (MMBroadbandModemXmm7360   *self,
                                                         Xmm7360RpcResponse        *response,
                                                         gboolean                   last_command,
                                                         const GError              *error,
                                                         GError                   **result_error)
{
    if (error) {
        *result_error = g_error_copy (error);
        return MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_FAILURE;
    }

    *result_error = NULL;
    return MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_SUCCESS;
}

MMBroadbandModemXmm7360RpcResponseProcessorResult
mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success (MMBroadbandModemXmm7360   *self,
                                                                       Xmm7360RpcResponse        *response,
                                                                       gboolean                   last_command,
                                                                       const GError              *error,
                                                                       GError                   **result_error)
{
    if (error) {
        *result_error = g_error_copy (error);
        return MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_FAILURE;
    }

    *result_error = NULL;
    return MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_CONTINUE;
}

MMBroadbandModemXmm7360RpcResponseProcessorResult
mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_error (MMBroadbandModemXmm7360   *self,
                                                                     Xmm7360RpcResponse        *response,
                                                                     gboolean                   last_command,
                                                                     const GError              *error,
                                                                     GError                   **result_error)
{
    *result_error = NULL;
    return (error ?
        MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_CONTINUE :
        MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_SUCCESS);
}

/*****************************************************************************/
/* Single RPC command handling */

typedef struct {
    MMPortSerialXmmrpcXmm7360 *port;
    gulong cancelled_id;
    GCancellable *parent_cancellable;
} RpcCommandContext;

static void
rpc_command_context_free (RpcCommandContext *ctx)
{
    mm_port_serial_close (MM_PORT_SERIAL (ctx->port));

    if (ctx->parent_cancellable) {
        g_cancellable_disconnect (ctx->parent_cancellable,
                                  ctx->cancelled_id);
        g_object_unref (ctx->parent_cancellable);
    }

    g_object_unref (ctx->port);
    g_free (ctx);
}

Xmm7360RpcResponse *
mm_broadband_modem_xmm7360_rpc_command_full_finish (MMBroadbandModemXmm7360 *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
rpc_command_ready (MMPortSerialXmmrpcXmm7360 *port,
                   GAsyncResult *res,
                   GTask *task)
{
    g_autoptr(GError)   command_error = NULL;
    Xmm7360RpcResponse *response;

    response = mm_port_serial_xmmrpc_xmm7360_command_finish (port, res, &command_error);

    if (g_task_return_error_if_cancelled (task)) {
        /* task cancelled */
        g_object_unref (task);
        return;
    }

    if (command_error)
        /* error coming from the serial port */
        g_task_return_error (task, g_steal_pointer (&command_error));
    else if (response)
        /* valid response */
        g_task_return_pointer (task, response, (GDestroyNotify)xmm7360_rpc_response_free);
    else
        g_assert_not_reached ();
    g_object_unref (task);
}

static void
rpc_command_common (MMBroadbandModemXmm7360 *self,
                    MMPortSerialXmmrpcXmm7360 *port,
                    Xmm7360RpcCallId callid,
                    gboolean is_async,
                    GByteArray *body,
                    guint timeout,
                    gboolean allow_cached,
                    GTask *task,
                    GCancellable *parent_cancellable)
{
    RpcCommandContext *ctx;

    /* Ensure that we have an open port */
    if (!abort_task_if_port_unusable (self, port, task))
        return;

    ctx = g_new0 (RpcCommandContext, 1);
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

    g_task_set_task_data (task, ctx, (GDestroyNotify)rpc_command_context_free);

    /* Go on with the command */
    mm_port_serial_xmmrpc_xmm7360_command (
        port,
        callid,
        is_async,
        body,
        timeout,
        allow_cached,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)rpc_command_ready,
        task);
}

void
mm_broadband_modem_xmm7360_rpc_command_full (MMBroadbandModemXmm7360 *self,
                                             MMPortSerialXmmrpcXmm7360 *port,
                                             Xmm7360RpcCallId callid,
                                             gboolean is_async,
                                             GByteArray *body,
                                             guint timeout,
                                             gboolean allow_cached,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    GCancellable *modem_cancellable;
    GTask *task;

    modem_cancellable = mm_base_modem_peek_cancellable (MM_BASE_MODEM (self));
    task = g_task_new (self,
                       cancellable ? cancellable : modem_cancellable,
                       callback,
                       user_data);

    rpc_command_common (self,
                        port,
                        callid,
                        is_async,
                        body,
                        timeout,
                        allow_cached,
                        task,
                        cancellable ? modem_cancellable : NULL);
}

Xmm7360RpcResponse *
mm_broadband_modem_xmm7360_rpc_command_finish (MMBroadbandModemXmm7360 *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    return mm_broadband_modem_xmm7360_rpc_command_full_finish (self, res, error);
}

static void
_rpc_command (MMBroadbandModemXmm7360 *self,
              Xmm7360RpcCallId callid,
              gboolean is_async,
              GByteArray *body,
              guint timeout,
              gboolean allow_cached,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    MMPortSerialXmmrpcXmm7360 *port;
    GError *error = NULL;
    GTask *task;

    task = g_task_new (self,
                       mm_base_modem_peek_cancellable (MM_BASE_MODEM (self)),
                       callback,
                       user_data);

    /* No port given, so we'll try to guess which is best */
    port = mm_broadband_modem_xmm7360_peek_port_xmmrpc (self);
    if (!port) {
        g_set_error (&error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_CONNECTED,
                     "No XMMRPC port available to run command");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    rpc_command_common (self,
                        port,
                        callid,
                        is_async,
                        body,
                        timeout,
                        allow_cached,
                        task,
                        NULL);
}

void
mm_broadband_modem_xmm7360_rpc_command (MMBroadbandModemXmm7360 *self,
                                        Xmm7360RpcCallId callid,
                                        gboolean is_async,
                                        GByteArray *body,
                                        guint timeout,
                                        gboolean allow_cached,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    _rpc_command (self, callid, is_async, body, timeout, allow_cached, callback, user_data);
}
