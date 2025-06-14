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

#ifndef MM_BROADBAND_MODEM_XMM7360_RPC_H
#define MM_BROADBAND_MODEM_XMM7360_RPC_H

#include <gio/gio.h>

#include "mm-broadband-modem-xmm7360.h"
#include "mm-port-serial-xmmrpc-xmm7360.h"

typedef enum {
    MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_CONTINUE,
    MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_SUCCESS,
    MM_BROADBAND_MODEM_XMM7360_RPC_RESPONSE_PROCESSOR_RESULT_FAILURE,
} MMBroadbandModemXmm7360RpcResponseProcessorResult;

/*
 * SUCCESS must be returned when the operation is to be considered successful,
 * and a result may be given.
 *
 * FAILURE must be returned when a GError is propagated into result_error,
 * which will be treated as a critical error and therefore the operation will be aborted.
 *
 * CONTINUE must be returned when no result_error is given and
 * the operation should go on with the next scheduled command.
 *
 * This setup, therefore allows:
 *  - Running a single command and processing its result.
 *  - Running a set of N commands out of M (N<M), where the global result is
 *    obtained without having executed all configured commands.
 */
typedef MMBroadbandModemXmm7360RpcResponseProcessorResult (* MMBroadbandModemXmm7360RpcResponseProcessor) (
    MMBroadbandModemXmm7360 *self,
    Xmm7360RpcResponse      *response,
    gboolean                 last_command,
    const GError            *error,
    GError                 **result_error);

/* Struct to configure XMMRPC command operations */
typedef struct {
    /* The RCP command's call ID */
    Xmm7360RpcCallId callid;
    /* Async or sync type RCP command */
    gboolean is_async;
    /* payload */
    const Xmm7360RpcMsgArg *body;
    /* Timeout of the command, in seconds */
    guint timeout;
    /* Flag to allow cached replies */
    gboolean allow_cached;
    gboolean allow_retry_once;
    /* The response processor */
    MMBroadbandModemXmm7360RpcResponseProcessor response_processor;
    /* Time to wait before sending this command (in seconds) */
    guint wait_seconds;
} MMBroadbandModemXmm7360RpcCommand;

/* Generic RPC sequence handling, using the first XMMRPC port available and without
 * explicit cancellations. */
void mm_broadband_modem_xmm7360_rpc_sequence (
    MMBroadbandModemXmm7360 *self,
    const MMBroadbandModemXmm7360RpcCommand *sequence,
    GAsyncReadyCallback callback,
    gpointer user_data);
Xmm7360RpcResponse *mm_broadband_modem_xmm7360_rpc_sequence_finish (
    MMBroadbandModemXmm7360 *self,
    GAsyncResult *res,
    GError **error);

/* Fully detailed RPC sequence handling, when specific XMMRPC port and/or explicit
 * cancellations need to be used. */
void mm_broadband_modem_xmm7360_rpc_sequence_full (
    MMBroadbandModemXmm7360 *self,
    MMPortSerialXmmrpcXmm7360 *port,
    const MMBroadbandModemXmm7360RpcCommand *sequence,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);
Xmm7360RpcResponse *mm_broadband_modem_xmm7360_rpc_sequence_full_finish (
    MMBroadbandModemXmm7360 *self,
    GAsyncResult *res,
    GError **error);

/* Common helper response processors */

/*
 * Response processor for commands that are treated as MANDATORY, where a
 * failure in the command triggers a failure in the sequence.
 */
MMBroadbandModemXmm7360RpcResponseProcessorResult mm_broadband_modem_xmm7360_rpc_response_processor_final (
    MMBroadbandModemXmm7360   *self,
    Xmm7360RpcResponse        *response,
    gboolean                   last_command,
    const GError              *error,
    GError                   **result_error);

/*
 * Response processor for commands that are treated as MANDATORY, where a
 * failure in the command triggers a failure in the sequence. If successful,
 * it will run the next command in the sequence.
 *
 * E.g. used when we provide a list of commands and we want to run all of
 * them successfully, and fail the sequence if one of them fails.
 */
MMBroadbandModemXmm7360RpcResponseProcessorResult mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success (
    MMBroadbandModemXmm7360   *self,
    Xmm7360RpcResponse        *response,
    gboolean                   last_command,
    const GError              *error,
    GError                   **result_error);

/*
 * Response processor for commands that are treated as OPTIONAL, where a
 * failure in the command doesn't trigger a failure in the sequence. If
 * successful, it finishes the sequence.
 *
 * E.g. used when we provide a list of commands and we want to stop
 * as soon as one of them doesn't fail.
 */
MMBroadbandModemXmm7360RpcResponseProcessorResult mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_error (
    MMBroadbandModemXmm7360   *self,
    Xmm7360RpcResponse        *response,
    gboolean                   last_command,
    const GError              *error,
    GError                   **result_error);

/* Generic RPC command handling, using the best XMMRPC port available and without
 * explicit cancellations. */
void mm_broadband_modem_xmm7360_rpc_command (
    MMBroadbandModemXmm7360 *self,
    Xmm7360RpcCallId callid,
    gboolean is_async,
    GByteArray *body,
    guint timeout,
    gboolean allow_cached,
    GAsyncReadyCallback callback,
    gpointer user_data);
Xmm7360RpcResponse *mm_broadband_modem_xmm7360_rpc_command_finish (
    MMBroadbandModemXmm7360 *self,
    GAsyncResult *res,
    GError **error);

/* Fully detailed RPC command handling, when specific XMMRPC port and/or explicit
 * cancellations need to be used. */
void mm_broadband_modem_xmm7360_rpc_command_full (
    MMBroadbandModemXmm7360 *self,
    MMPortSerialXmmrpcXmm7360 *port,
    Xmm7360RpcCallId callid,
    gboolean is_async,
    GByteArray *body,
    guint timeout,
    gboolean allow_cached,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);
Xmm7360RpcResponse *mm_broadband_modem_xmm7360_rpc_command_full_finish (
    MMBroadbandModemXmm7360 *self,
    GAsyncResult *res,
    GError **error);

#endif /* MM_BROADBAND_MODEM_XMM7360_RPC_H */
