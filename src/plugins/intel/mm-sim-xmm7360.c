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

#include "mm-broadband-modem-xmm7360.h"
#include "mm-broadband-modem-xmm7360-rpc.h"
#include "mm-log-object.h"
#include "mm-sim-xmm7360.h"
#include "mm-bind.h"

G_DEFINE_TYPE (MMSimXmm7360, mm_sim_xmm7360, MM_TYPE_BASE_SIM)

#define PIN_PADDED { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
#define PIN_PADDED_SIZE 8

static void
copy_pin (const gchar *pin, gchar *pin_padded)
{
    gsize i;

    for (i = 0; pin[i] != 0 && i < PIN_PADDED_SIZE; i++)
        pin_padded[i] = pin[i];
}

static void
update_modem_unlock_retries (MMSimXmm7360 *self,
                             MMModemLock lock_type,
                             guint32 remaining_attempts)
{
    g_autoptr(MMBaseModem) modem = NULL;

    g_object_get (G_OBJECT (self),
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    mm_broadband_modem_xmm7360_set_unlock_retries (MM_BROADBAND_MODEM_XMM7360 (modem),
                                                   lock_type,
                                                   remaining_attempts);
    g_object_unref (modem);
}

/*****************************************************************************/
/* Enable PIN */

static gboolean
enable_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gen_pin_enable_ready (MMBroadbandModem *modem,
                      GAsyncResult *res,
                      GTask *task)
{
    MMSimXmm7360 *self;
    GError *error = NULL;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;
    guint8 err_code_1;
    guint8 err_code_2;
    guint32 remaining_attempts;
    Xmm7360GenPinOp op;
    MMModemLock lock_type;

    self = g_task_get_source_object (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (MM_BROADBAND_MODEM_XMM7360 (modem),
                                                              res,
                                                              &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 2);
    remaining_attempts = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 3);
    err_code_1 = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 4);
    err_code_2 = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 7);
    op = (Xmm7360GenPinOp) XMM7360_RPC_MSG_ARG_GET_INT (arg);

    lock_type = remaining_attempts > 0 ? MM_MODEM_LOCK_SIM_PIN : MM_MODEM_LOCK_SIM_PUK;
    update_modem_unlock_retries (self, lock_type, remaining_attempts);

    if (err_code_1 == 0x83 && err_code_2 == 0x69) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "%s PIN failed (need to unlock PUK first)",
                                 op == XMM7360_GEN_PIN_OP_ENABLE ? "Enabling" : "Disabling");
        g_object_unref (task);
        return;
    } else if (err_code_1 == 0x84 && err_code_2 == 0x69) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "%s PIN failed (already %s)",
                                 op == XMM7360_GEN_PIN_OP_ENABLE ? "Enabling" : "Disabling",
                                 op == XMM7360_GEN_PIN_OP_ENABLE ? "enabled" : "disabled");
        g_object_unref (task);
        return;
    } else if (err_code_2 == 0x63 && (err_code_1 == 0xc2 || err_code_1 == 0xc1 || err_code_1 == 0xc0)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "%s PIN failed (wrong PIN, %d retries left)",
                                 op == XMM7360_GEN_PIN_OP_ENABLE ? "Enabling" : "Disabling",
                                 remaining_attempts);
        g_object_unref (task);
        return;
    } else if (err_code_1 != 0x00 || err_code_2 != 0x90) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "%s PIN failed (unknown error: 0x%02x, 0x%02x)",
                                 op == XMM7360_GEN_PIN_OP_ENABLE ? "Enabling" : "Disabling",
                                 err_code_1, err_code_2);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static GByteArray *
padded_pack_gen_pin_enable (const gchar *pin_padded, const Xmm7360GenPinOp op)
{
    const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = op } },
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = pin_padded }, 8, 2 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static GByteArray *
pack_gen_pin_enable (const gchar *pin, gboolean enabled)
{
    gchar pin_padded[] = PIN_PADDED;
    copy_pin (pin, pin_padded);
    return padded_pack_gen_pin_enable (
        pin_padded,
        enabled ? XMM7360_GEN_PIN_OP_ENABLE : XMM7360_GEN_PIN_OP_DISABLE
    );
}

static void
enable_pin (MMBaseSim *self,
            const gchar *pin,
            gboolean enabled,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GTask                     *task;
    g_autoptr(MMBaseModem)     modem = NULL;
    MMPortSerialXmmrpcXmm7360 *port;
    g_autoptr(GByteArray)      body = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    g_object_get (G_OBJECT (self),
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    port = mm_broadband_modem_xmm7360_peek_port_xmmrpc (MM_BROADBAND_MODEM_XMM7360 (modem));
    g_object_unref (modem);

    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't peek XMMRPC port");
        g_object_unref (task);
        return;
    }

    body = pack_gen_pin_enable (pin, enabled);

    mm_broadband_modem_xmm7360_rpc_command_full (MM_BROADBAND_MODEM_XMM7360 (modem),
                                                 port,
                                                 XMM7360_RPC_CALL_UTA_MS_SIM_GEN_PIN_REQ,
                                                 TRUE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)gen_pin_enable_ready,
                                                 task);
}

/*****************************************************************************/
/* Change PIN */

static gboolean
change_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gen_pin_change_ready (MMBroadbandModem *modem,
                      GAsyncResult *res,
                      GTask *task)
{
    MMSimXmm7360 *self;
    GError *error = NULL;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;
    guint8 err_code_1;
    guint8 err_code_2;
    guint32 remaining_attempts;
    MMModemLock lock_type;

    self = g_task_get_source_object (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (MM_BROADBAND_MODEM_XMM7360 (modem),
                                                              res,
                                                              &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (response->content->len < 10) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Changing PIN failed (response too short)");
        g_object_unref (task);
        return;
    }

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 2);
    remaining_attempts = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 3);
    err_code_1 = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 4);
    err_code_2 = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    lock_type = remaining_attempts > 0 ? MM_MODEM_LOCK_SIM_PIN : MM_MODEM_LOCK_SIM_PUK;
    update_modem_unlock_retries (self, lock_type, remaining_attempts);

    if (err_code_1 == 0x83 && err_code_2 == 0x69) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Changing PIN failed (need to unlock PUK first)");
        g_object_unref (task);
        return;
    } else if (err_code_1 == 0x84 && err_code_2 == 0x69) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Changing PIN failed (PIN lock is disabled)");
        g_object_unref (task);
        return;
    } else if (err_code_2 == 0x63 && (err_code_1 == 0xc2 || err_code_1 == 0xc1 || err_code_1 == 0xc0)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Changing PIN failed (wrong PIN, %d retries left)",
                                 remaining_attempts);
        g_object_unref (task);
        return;
    } else if (err_code_1 != 0x00 || err_code_2 != 0x90) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Changing PIN failed (unknown error: 0x%02x, 0x%02x)",
                                 err_code_1, err_code_2);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static GByteArray *
padded_pack_gen_pin_change (const gchar *old_pin_padded, const gchar *new_pin_padded)
{
    const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = XMM7360_GEN_PIN_OP_CHANGE } },
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = new_pin_padded }, 8, 2},
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = old_pin_padded }, 8, 2},
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static GByteArray *
pack_gen_pin_change (const gchar *old_pin, const gchar *new_pin)
{
    gchar old_pin_padded[] = PIN_PADDED;
    gchar new_pin_padded[] = PIN_PADDED;
    copy_pin (old_pin, old_pin_padded);
    copy_pin (new_pin, new_pin_padded);
    return padded_pack_gen_pin_change (old_pin_padded, new_pin_padded);
}

static void
change_pin (MMBaseSim *self,
            const gchar *old_pin,
            const gchar *new_pin,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GTask                     *task;
    g_autoptr(MMBaseModem)     modem = NULL;
    MMPortSerialXmmrpcXmm7360 *port;
    g_autoptr(GByteArray)      body = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    g_object_get (G_OBJECT (self),
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    port = mm_broadband_modem_xmm7360_peek_port_xmmrpc (MM_BROADBAND_MODEM_XMM7360 (modem));
    g_object_unref (modem);

    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't peek XMMRPC port");
        g_object_unref (task);
        return;
    }

    body = pack_gen_pin_change (old_pin, new_pin);

    mm_broadband_modem_xmm7360_rpc_command_full (MM_BROADBAND_MODEM_XMM7360 (modem),
                                                 port,
                                                 XMM7360_RPC_CALL_UTA_MS_SIM_GEN_PIN_REQ,
                                                 TRUE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)gen_pin_change_ready,
                                                 task);
}

/*****************************************************************************/

MMBaseSim *
mm_sim_xmm7360_new_finish (GAsyncResult  *res,
                        GError       **error)
{
    GObject *source;
    GObject *sim;

    source = g_async_result_get_source_object (res);
    sim = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!sim)
        return NULL;

    /* Only export valid SIMs */
    mm_base_sim_export (MM_BASE_SIM (sim));

    return MM_BASE_SIM (sim);
}

void
mm_sim_xmm7360_new (MMBaseModem *modem,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_XMM7360,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                MM_BIND_TO, modem,
                                "active", TRUE, /* by default always active */
                                NULL);
}

MMBaseSim *
mm_sim_xmm7360_new_initialized (MMBaseModem *modem,
                             guint            slot_number,
                             gboolean         active,
                             MMSimType        sim_type,
                             MMSimEsimStatus  esim_status,
                             const gchar     *sim_identifier,
                             const gchar     *imsi,
                             const gchar     *eid,
                             const gchar     *operator_identifier,
                             const gchar     *operator_name,
                             const GStrv      emergency_numbers)
{
    MMBaseSim *sim;

    sim = MM_BASE_SIM (g_object_new (MM_TYPE_SIM_XMM7360,
                                     MM_BASE_SIM_MODEM,       modem,
                                     MM_BIND_TO,              modem,
                                     MM_BASE_SIM_SLOT_NUMBER, slot_number,
                                     "active",                active,
                                     "sim-type",              sim_type,
                                     "esim-status",           esim_status,
                                     "sim-identifier",        sim_identifier,
                                     "eid",                   eid,
                                     "operator-identifier",   operator_identifier,
                                     "operator-name",         operator_name,
                                     "emergency-numbers",     emergency_numbers,
                                     NULL));

    mm_base_sim_export (sim);
    return sim;
}

static void
mm_sim_xmm7360_init (MMSimXmm7360 *self)
{
}

static void
mm_sim_xmm7360_class_init (MMSimXmm7360Class *klass)
{
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    base_sim_class->enable_pin = enable_pin;
    base_sim_class->enable_pin_finish = enable_pin_finish;
    base_sim_class->change_pin = change_pin;
    base_sim_class->change_pin_finish = change_pin_finish;
}
