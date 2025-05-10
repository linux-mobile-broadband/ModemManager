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
 * Copyright (C) 2019 James Wah
 * Copyright (C) 2020 Marinus Enzinger <marinus@enzingerm.de>
 * Copyright (C) 2023 Shane Parslow
 * Copyright (C) 2024 Thomas Vogt
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-broadband-modem-xmm.h"

#include "mm-broadband-modem-xmm7360.h"
#include "mm-broadband-modem-xmm7360-rpc.h"
#include "mm-port-scheduler-rr.h"
#include "mm-port-serial-xmmrpc-xmm7360.h"
#include "mm-bearer-xmm7360.h"
#include "mm-shared-xmm.h"
#include "mm-sim-xmm7360.h"

static void iface_modem_init (MMIfaceModemInterface *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gppInterface *iface);
static void iface_shared_xmm_init (MMSharedXmmInterface *iface);

static MMIfaceModemInterface *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemXmm7360, mm_broadband_modem_xmm7360, MM_TYPE_BROADBAND_MODEM_XMM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_XMM, iface_shared_xmm_init)
)

struct _MMBroadbandModemXmm7360Private {
    MMUnlockRetries *unlock_retries;
    GRegex *nmea_regex_full, *nmea_regex_trace;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static void
create_bearer (MMIfaceModem *self,
               MMBearerProperties *properties,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    MMBaseBearer *bearer;
    GTask *task;

    bearer = mm_bearer_xmm7360_new (MM_BROADBAND_MODEM_XMM7360 (self), properties);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, bearer, (GDestroyNotify)g_object_unref);
    g_object_unref (task);
}

static MMBaseBearer *
create_bearer_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_xmm7360_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New XMM7360 SIM */
    mm_sim_xmm7360_new (MM_BASE_MODEM (self),
                        NULL, /* cancellable */
                        callback,
                        user_data);
}

/*****************************************************************************/
/* Set initial EPS bearer settings (3GPP interface) */

typedef enum {
    APN_AUTH_TYPE_NONE = 0,
    APN_AUTH_TYPE_PAP = 1,
    APN_AUTH_TYPE_CHAP = 2,
} ApnAuthType;

static gboolean
modem_3gpp_set_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                   GAsyncResult      *res,
                                                   GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
attach_apn_config_ready (MMBroadbandModemXmm7360 *modem,
                         GAsyncResult *res,
                         GTask *task)
{
    GError                       *error = NULL;
    g_autoptr(Xmm7360RpcResponse) response = NULL;

    response = mm_broadband_modem_xmm7360_rpc_command_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
    } else {
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);


}

static GByteArray *
padded_pack_uta_ms_call_ps_attach_apn_config_req (const gchar *apn_padded,
                                                  ApnAuthType auth_type,
                                                  const gchar *user_padded,
                                                  const gchar *password_padded)
{
    static const gchar zeroes[270] = { 0 };
    const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 257, 3 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 65, 1 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 65, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 2 },
        { XMM7360_RPC_MSG_ARG_TYPE_SHORT, { .s = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 20, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 101, 3 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 257, 3 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 65, 1 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 65, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 2 },
        { XMM7360_RPC_MSG_ARG_TYPE_SHORT, { .s = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 20, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 101, 3 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 257, 3 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = auth_type } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = password_padded }, 65, 1 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = user_padded }, 65, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 2 },
        { XMM7360_RPC_MSG_ARG_TYPE_SHORT, { .s = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0x404 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 20, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 3 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = apn_padded }, 101, 3 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 257, 3 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = auth_type } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = password_padded }, 65, 1 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = user_padded }, 65, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 250, 2 },
        { XMM7360_RPC_MSG_ARG_TYPE_SHORT, { .s = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0x404 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = zeroes }, 20, 0 },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 3 } },
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = apn_padded }, 101, 2 },
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 3 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static GByteArray *
pack_uta_ms_call_ps_attach_apn_config_req (const gchar *apn,
                                           MMBearerAllowedAuth allowed_auth,
                                           const gchar *user,
                                           const gchar *password)
{
    gchar apn_padded[102] = { 0 };
    gchar user_padded[66] = { 0 };
    gchar password_padded[66] = { 0 };
    ApnAuthType auth_type;

    if (apn != NULL)
        g_strlcpy (apn_padded, apn, sizeof (apn_padded));
    if (user != NULL)
        g_strlcpy (user_padded, user, sizeof (user_padded));
    if (password != NULL)
        g_strlcpy (password_padded, password, sizeof (password_padded));

    if (allowed_auth & MM_BEARER_ALLOWED_AUTH_NONE) {
        auth_type = APN_AUTH_TYPE_NONE;
    } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_PAP) {
        auth_type = APN_AUTH_TYPE_PAP;
    } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_CHAP) {
        auth_type = APN_AUTH_TYPE_CHAP;
    } else {
        gchar *str;

        str = mm_bearer_allowed_auth_build_string_from_mask (allowed_auth);
        mm_obj_dbg (NULL,
                    "Specified APN authentication methods unknown (%s)."
                    " Falling back to default method (none).",
                    str);
        auth_type = APN_AUTH_TYPE_NONE;
        g_free (str);
    }

    return padded_pack_uta_ms_call_ps_attach_apn_config_req (apn_padded,
                                                             auth_type,
                                                             user_padded,
                                                             password_padded);
}

static void
modem_3gpp_set_initial_eps_bearer_settings (MMIfaceModem3gpp    *_self,
                                            MMBearerProperties  *config,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    MMBroadbandModemXmm7360             *self = MM_BROADBAND_MODEM_XMM7360 (_self);
    g_autoptr(MMPortSerialXmmrpcXmm7360) port = NULL;
    GTask                               *task;
    g_autoptr(GByteArray)                body = NULL;

    port = mm_broadband_modem_xmm7360_get_port_xmmrpc (self);

    task = g_task_new (self, NULL, callback, user_data);

    body = pack_uta_ms_call_ps_attach_apn_config_req (mm_bearer_properties_get_apn (config),
                                                      mm_bearer_properties_get_allowed_auth (config),
                                                      mm_bearer_properties_get_user (config),
                                                      mm_bearer_properties_get_password (config));

    mm_broadband_modem_xmm7360_rpc_command_full (self,
                                                 port,
                                                 XMM7360_RPC_CALL_UTA_MS_CALL_PS_ATTACH_APN_CONFIG_REQ,
                                                 TRUE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)attach_apn_config_ready,
                                                 task);
}

/*****************************************************************************/

MMPortSerialXmmrpcXmm7360 *
mm_broadband_modem_xmm7360_peek_port_xmmrpc (MMBroadbandModemXmm7360 *self)
{
    MMPortSerialXmmrpcXmm7360 *primary_xmmrpc_port = NULL;
    GList                     *xmmrpc_ports;

    g_assert (MM_IS_BROADBAND_MODEM_XMM7360 (self));

    xmmrpc_ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                             MM_PORT_SUBSYS_UNKNOWN,
                                             MM_PORT_TYPE_XMMRPC);

    /* First XMMRPC port in the list is the primary one always */
    if (xmmrpc_ports) {
        primary_xmmrpc_port = mm_port_serial_xmmrpc_xmm7360_new (
            mm_port_get_device (MM_PORT (xmmrpc_ports->data))
        );
    }

    g_list_free_full (xmmrpc_ports, g_object_unref);

    return primary_xmmrpc_port;
}

MMPortSerialXmmrpcXmm7360 *
mm_broadband_modem_xmm7360_get_port_xmmrpc (MMBroadbandModemXmm7360 *self)
{
    MMPortSerialXmmrpcXmm7360 *primary_xmmrpc_port;

    g_assert (MM_IS_BROADBAND_MODEM_XMM7360 (self));

    primary_xmmrpc_port = mm_broadband_modem_xmm7360_peek_port_xmmrpc (self);
    return (primary_xmmrpc_port ?
            MM_PORT_SERIAL_XMMRPC_XMM7360 (g_object_ref (primary_xmmrpc_port)) :
            NULL);
}

/*****************************************************************************/

void
mm_broadband_modem_xmm7360_set_unlock_retries (MMBroadbandModemXmm7360 *self,
                                               MMModemLock              lock_type,
                                               guint32                  remaining_attempts)
{
    g_assert (MM_IS_BROADBAND_MODEM_XMM7360 (self));

    if (!self->priv->unlock_retries)
        self->priv->unlock_retries = mm_unlock_retries_new ();

    /* Interpret 0xffffffff as device not supporting this information. */
    if (remaining_attempts != G_MAXUINT32)
        mm_unlock_retries_set (self->priv->unlock_retries,
                               lock_type,
                               remaining_attempts);
}

/*****************************************************************************/

typedef struct {
    MMPortSerialXmmrpcXmm7360 *port;
    gboolean port_dispose;
    gboolean unlock;
} CheckFccLockContext;

static void
check_fcc_lock_context_free (CheckFccLockContext *ctx)
{
    if (ctx->port_dispose) {
        mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
        g_clear_object (&ctx->port);
    }
    g_slice_free (CheckFccLockContext, ctx);
}

gboolean
mm_broadband_modem_xmm7360_check_fcc_lock_finish (MMBroadbandModemXmm7360  *self,
                                                  GAsyncResult             *res,
                                                  GError                  **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
fcc_unlock_ready (MMBroadbandModem *self,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;

    response = mm_broadband_modem_xmm7360_rpc_command_finish (MM_BROADBAND_MODEM_XMM7360 (self),
                                                              res,
                                                              &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (response->content->len < 1) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Invalid response after answering FCC unlock challenge (too short)");
        g_object_unref (task);
        return;
    }
    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 0);
    g_assert (arg->type == XMM7360_RPC_MSG_ARG_TYPE_LONG);
    if (XMM7360_RPC_MSG_ARG_GET_INT (arg) != 1) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Our answer to the FCC unlock challenge was not accepted");
        g_object_unref (task);
        return;
    }

    /* successfully unlocked, return FALSE (unlock not required) */
    g_task_return_boolean (task, FALSE);
    g_object_unref (task);
}

static void
fcc_unlock_challenge_ready (MMBroadbandModem *self,
                            GAsyncResult *res,
                            GTask *task)
{
    GError *error = NULL;
    CheckFccLockContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;
    gint32 fcc_challenge;
    GChecksum *checksum;
    guchar salt[] = { 0x3d, 0xf8, 0xc7, 0x19 };
    guint8 digest[32] = { 0 };
    gsize digest_len = 32;
    g_autoptr(GByteArray) digest_response = NULL;

    response = mm_broadband_modem_xmm7360_rpc_command_finish (MM_BROADBAND_MODEM_XMM7360 (self),
                                                              res,
                                                              &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 1);
    g_assert (arg->type == XMM7360_RPC_MSG_ARG_TYPE_LONG);
    fcc_challenge = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    checksum = g_checksum_new (G_CHECKSUM_SHA256);
    g_checksum_update (checksum, (guchar *) &fcc_challenge, 4);
    g_checksum_update (checksum, salt, 4);
    g_checksum_get_digest (checksum, digest, &digest_len);
    g_checksum_free (checksum);

    digest_response = g_byte_array_new ();
    xmm7360_byte_array_append_asn_int4 (digest_response, GINT32_FROM_LE (*(gint32 *) digest));

    ctx = g_task_get_task_data (task);
    mm_broadband_modem_xmm7360_rpc_command_full (MM_BROADBAND_MODEM_XMM7360 (self),
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_CSI_FCC_LOCK_VER_CHALLENGE_REQ,
                                                 TRUE,
                                                 digest_response,
                                                 3,
                                                 FALSE,
                                                 NULL, /* cancellable */
                                                 (GAsyncReadyCallback)fcc_unlock_ready,
                                                 task);
}

static void
fcc_lock_query_ready (MMBroadbandModem *self,
                      GAsyncResult *res,
                      GTask *task)
{
    GError *error = NULL;
    CheckFccLockContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;

    response = mm_broadband_modem_xmm7360_rpc_command_finish (MM_BROADBAND_MODEM_XMM7360 (self),
                                                              res,
                                                              &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (response->content->len < 2) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "The response to the FCC check is invalid (too short)");
        g_object_unref (task);
        return;
    }

    /* second argument is fcc_state */
    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 1);
    g_assert (arg->type == XMM7360_RPC_MSG_ARG_TYPE_LONG);
    if (XMM7360_RPC_MSG_ARG_GET_INT (arg)) {
        /* no FCC unlock required: FCC state is != 0 */
        g_task_return_boolean (task, FALSE);
        g_object_unref (task);
        return;
    }

    /* third argument is fcc_mode */
    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 2);
    g_assert (arg->type == XMM7360_RPC_MSG_ARG_TYPE_LONG);
    if (!XMM7360_RPC_MSG_ARG_GET_INT (arg)) {
        /* no FCC unlock required: FCC mode is == 0 */
        g_task_return_boolean (task, FALSE);
        g_object_unref (task);
        return;
    }

    /* FCC unlock required: FCC mode is != 0 */
    ctx = g_task_get_task_data (task);

    if (!ctx->unlock) {
        /* we are told not to unlock, return TRUE (unlock required) */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* request unlock challenge */
    mm_broadband_modem_xmm7360_rpc_command_full (MM_BROADBAND_MODEM_XMM7360 (self),
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_CSI_FCC_LOCK_GEN_CHALLENGE_REQ,
                                                 TRUE,
                                                 NULL, /* body */
                                                 3,
                                                 FALSE,
                                                 NULL, /* cancellable */
                                                 (GAsyncReadyCallback)fcc_unlock_challenge_ready,
                                                 task);
}

void
mm_broadband_modem_xmm7360_check_fcc_lock (MMBroadbandModemXmm7360   *self,
                                           GAsyncReadyCallback        callback,
                                           gpointer                   user_data,
                                           MMPortSerialXmmrpcXmm7360 *port,
                                           gboolean                   unlock)
{
    GError *error = NULL;
    CheckFccLockContext *ctx;
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (CheckFccLockContext);
    ctx->port = port;
    ctx->port_dispose = FALSE;
    ctx->unlock = unlock;
    g_task_set_task_data (task, ctx, (GDestroyNotify)check_fcc_lock_context_free);

    if (!port) {
        /* if no port is given, open a new XMMRPC port for the FCC lock query */
        ctx->port = mm_broadband_modem_xmm7360_get_port_xmmrpc (self);
        ctx->port_dispose = TRUE;

        if (!mm_port_serial_open (MM_PORT_SERIAL (ctx->port), &error)) {
            g_prefix_error (&error, "Couldn't open XMMRPC port during FCC lock query: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    }

    mm_broadband_modem_xmm7360_rpc_command_full (MM_BROADBAND_MODEM_XMM7360 (self),
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_CSI_FCC_LOCK_QUERY_REQ,
                                                 TRUE,
                                                 NULL, /* body */
                                                 3,
                                                 FALSE,
                                                 NULL, /* cancellable */
                                                 (GAsyncReadyCallback)fcc_lock_query_ready,
                                                 task);
}

/*****************************************************************************/

typedef struct {
    MMPortSerialXmmrpcXmm7360 *port;
    guint unsol_handler_id;
    guint timeout_id;
    gboolean is_fcc_unlocked;
    gboolean is_uta_mode_set;
    gboolean is_sim_initialized;
    gboolean awaiting_uta_mode_set_rsp_cb;
} PowerUpContext;

static void
power_up_context_free (PowerUpContext *ctx)
{
    if (ctx->timeout_id)
        g_source_remove (ctx->timeout_id);
    if (ctx->unsol_handler_id)
        mm_port_serial_xmmrpc_xmm7360_enable_unsolicited_msg_handler (
            ctx->port,
            ctx->unsol_handler_id,
            FALSE);
    mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
    g_clear_object (&ctx->port);
    g_slice_free (PowerUpContext, ctx);
}

static gboolean
modem_power_up_finish (MMIfaceModem  *self,
                       GAsyncResult  *res,
                       GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_modem_power_up_ready (MMIfaceModem *self,
                             GAsyncResult *res,
                             GTask        *task)
{
    gboolean  parent_res;
    GError   *error = NULL;

    parent_res = iface_modem_parent->modem_power_up_finish (self, res, &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* parent's result passed here */
    g_task_return_boolean (task, parent_res);
    g_object_unref (task);
}

static void
modem_power_up_ready (MMIfaceModem *self,
                      GAsyncResult *res,
                      GTask *task)
{
    GError *error = NULL;
    gboolean success;

    success = g_task_propagate_boolean (G_TASK (res), &error);
    if (error || !success) {
        if (error) {
            g_task_return_error (task, error);
        } else {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Powering up XMM7360 failed (unknown reason)");
        }
        g_object_unref (task);
        return;
    }

    iface_modem_parent->modem_power_up (
        self,
        (GAsyncReadyCallback)parent_modem_power_up_ready,
        task);
}

static gboolean
power_up_timeout_cb (GTask *task)
{
    MMBroadbandModemXmm7360 *self;
    PowerUpContext *ctx;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    if (ctx->is_uta_mode_set) {
        if (!ctx->is_sim_initialized) {
            /* this can happen if the device was initialized before */
            mm_obj_warn (self, "Waiting for SIM init timed out (trying to continue anyway...)");
            g_task_return_boolean (task, TRUE);
        }
    } else {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Initialization timed out (waiting for UTA mode)");
    }
    g_object_unref (task);

    return G_SOURCE_REMOVE;
}

static void
uta_mode_set_ready (MMBroadbandModem *self,
                    GAsyncResult *res,
                    GTask *task)
{
    GError *error = NULL;
    PowerUpContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;

    response = mm_broadband_modem_xmm7360_rpc_command_finish (MM_BROADBAND_MODEM_XMM7360 (self),
                                                              res,
                                                              &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (response->content->len < 1) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Invalid response setting UTA mode (too short)");
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    ctx->awaiting_uta_mode_set_rsp_cb = TRUE;
    ctx->timeout_id = g_timeout_add_seconds (5, (GSourceFunc)power_up_timeout_cb, task);
}

static GByteArray *
pack_uta_mode_set (gint32 mode)
{
    const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 15 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = mode } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static void
check_fcc_lock_ready (MMBroadbandModem *self,
                      GAsyncResult *res,
                      GTask *task)
{
    GError *error = NULL;
    gboolean locked;
    PowerUpContext *ctx;
    g_autoptr(GByteArray) body = NULL;

    locked = mm_broadband_modem_xmm7360_check_fcc_lock_finish (MM_BROADBAND_MODEM_XMM7360 (self),
                                                               res,
                                                               &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (locked) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_RETRY,
                                 "Modem is FCC locked.");
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    ctx->is_fcc_unlocked = TRUE;

    body = pack_uta_mode_set (1);

    mm_broadband_modem_xmm7360_rpc_command_full (MM_BROADBAND_MODEM_XMM7360 (self),
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_UTA_MODE_SET_REQ,
                                                 FALSE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)uta_mode_set_ready,
                                                 task);
}

static gboolean
power_up_unsol_handler (MMPortSerialXmmrpcXmm7360 *port,
                        Xmm7360RpcResponse *response,
                        GTask *task)
{
    PowerUpContext *ctx;
    Xmm7360RpcMsgArg *arg;
    gint32 value;

    ctx = g_task_get_task_data (task);

    if (response->unsol_id == XMM7360_RPC_UNSOL_UTA_MS_SIM_INIT_IND_CB) {
        ctx->is_sim_initialized = TRUE;
    } else if (response->unsol_id == XMM7360_RPC_UNSOL_UTA_MODE_SET_RSP_CB) {
        if (!ctx->awaiting_uta_mode_set_rsp_cb) {
            mm_obj_dbg (port, "Ignoring premature MODE_SET_RSP_CB ...");
            return TRUE;
        }

        arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 0);

        if (arg->type != XMM7360_RPC_MSG_ARG_TYPE_LONG) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "The response to the UTA mode-set is invalid (wrong type)");
            g_object_unref (task);
            return TRUE;
        }

        value = XMM7360_RPC_MSG_ARG_GET_INT (arg);
        if (value != 1) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Setting UTA mode failed (wrong value: %d)", value);
            g_object_unref (task);
            return TRUE;
        }

        ctx->is_uta_mode_set = TRUE;
    } else {
        return FALSE;
    }

    /* we check each time since the two messages might come in any order */
    if (ctx->awaiting_uta_mode_set_rsp_cb && ctx->is_uta_mode_set && ctx->is_sim_initialized) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
    }

    return TRUE;
}

static void
modem_power_up (MMIfaceModem        *iface,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    GError *error = NULL;
    MMBroadbandModemXmm7360 *self = MM_BROADBAND_MODEM_XMM7360 (iface);
    PowerUpContext *ctx;
    GTask *task;

    task = g_task_new (self,
                       NULL,
                       (GAsyncReadyCallback)modem_power_up_ready,
                       g_task_new (self, NULL, callback, user_data));

    ctx = g_slice_new0 (PowerUpContext);
    ctx->port = mm_broadband_modem_xmm7360_get_port_xmmrpc (self);
    ctx->is_fcc_unlocked = FALSE;
    ctx->is_uta_mode_set = FALSE;
    ctx->is_sim_initialized = FALSE;
    ctx->awaiting_uta_mode_set_rsp_cb = FALSE;
    ctx->timeout_id = 0;
    g_task_set_task_data (task, ctx, (GDestroyNotify)power_up_context_free);

    /* Open XMMRPC port for power-up */
    if (!mm_port_serial_open (MM_PORT_SERIAL (ctx->port), &error)) {
        g_prefix_error (&error, "Couldn't open XMMRPC port during power-up: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->unsol_handler_id = mm_port_serial_xmmrpc_xmm7360_add_unsolicited_msg_handler (
        ctx->port,
        (MMPortSerialXmmrpcXmm7360UnsolicitedMsgFn)power_up_unsol_handler,
        task,
        NULL);

    mm_broadband_modem_xmm7360_check_fcc_lock (MM_BROADBAND_MODEM_XMM7360 (self),
                                               (GAsyncReadyCallback)check_fcc_lock_ready,
                                               task,
                                               ctx->port,
                                               FALSE);
}

/*****************************************************************************/

typedef struct {
    MMPortSerialXmmrpcXmm7360 *port;
    guint unsol_handler_id;
    guint timeout_id;
    gboolean is_sim_initialized;
} InitializationStartedContext;

static void
initialization_started_context_free (InitializationStartedContext *ctx)
{
    if (ctx->timeout_id)
        g_source_remove (ctx->timeout_id);
    if (ctx->unsol_handler_id)
        mm_port_serial_xmmrpc_xmm7360_enable_unsolicited_msg_handler (
            ctx->port,
            ctx->unsol_handler_id,
            FALSE);
    mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
    g_clear_object (&ctx->port);
    g_slice_free (InitializationStartedContext, ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem  *self,
                               GAsyncResult      *res,
                               GError           **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult     *res,
                                     GTask            *task)
{
    gpointer  parent_ctx;
    GError   *error = NULL;

    parent_ctx = MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_xmm7360_parent_class)->initialization_started_finish (
        self,
        res,
        &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Just parent's pointer passed here */
    g_task_return_pointer (task, parent_ctx, NULL);
    g_object_unref (task);
}

static void
initialization_started_ready (MMBroadbandModem *self,
                              GAsyncResult *res,
                              GTask *task)
{
    GError *error = NULL;
    gboolean success;

    success = g_task_propagate_boolean (G_TASK (res), &error);
    if (error || !success) {
        if (error) {
            g_task_return_error (task, error);
        } else {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Initializing XMM7360 failed (unknown reason)");
        }
        g_object_unref (task);
        return;
    }

    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_xmm7360_parent_class)->initialization_started (
        self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        task);
}

static gboolean
init_timeout_cb (GTask *task)
{
    MMBroadbandModemXmm7360 *self;
    InitializationStartedContext *ctx;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    if (!ctx->is_sim_initialized) {
        /* this can happen if the device was initialized before */
        mm_obj_warn (self, "Waiting for SIM init timed out (trying to continue anyway...)");
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);

    return G_SOURCE_REMOVE;
}

static void
init_sequence_ready (MMBroadbandModem *self,
                     GAsyncResult *res,
                     GTask *task)
{
    GError *error = NULL;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    InitializationStartedContext *ctx;

    response = mm_broadband_modem_xmm7360_rpc_sequence_finish (MM_BROADBAND_MODEM_XMM7360 (self),
                                                               res,
                                                               &error);

    if (error) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Failed to complete init sequence: %s", error->message);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = g_timeout_add_seconds (5, (GSourceFunc)init_timeout_cb, task);
}

static gboolean
init_unsol_handler (MMPortSerialXmmrpcXmm7360 *port,
                    Xmm7360RpcResponse *response,
                    GTask *task)
{
    InitializationStartedContext *ctx;

    ctx = g_task_get_task_data (task);

    if (response->unsol_id != XMM7360_RPC_UNSOL_UTA_MS_SIM_INIT_IND_CB) {
        return FALSE;
    }

    ctx->is_sim_initialized = TRUE;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    return TRUE;
}

static const Xmm7360RpcMsgArg set_radio_signal_reporting_args[] = {
    { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 1 } },
    { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
    { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
};

static const MMBroadbandModemXmm7360RpcCommand init_sequence[] = {
    { XMM7360_RPC_CALL_UTA_MS_SMS_INIT, FALSE, NULL, 3, FALSE, TRUE, mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success },
    { XMM7360_RPC_CALL_UTA_MS_CBS_INIT, FALSE, NULL, 3, FALSE, FALSE, mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success },
    { XMM7360_RPC_CALL_UTA_MS_NET_OPEN, FALSE, NULL, 3, FALSE, FALSE, mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success },
    { XMM7360_RPC_CALL_UTA_MS_NET_SET_RADIO_SIGNAL_REPORTING, FALSE, set_radio_signal_reporting_args, 3, FALSE, FALSE, mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success},
    { XMM7360_RPC_CALL_UTA_MS_CALL_CS_INIT, FALSE, NULL, 3, FALSE, FALSE, mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success },
    { XMM7360_RPC_CALL_UTA_MS_CALL_PS_INITIALIZE, FALSE, NULL, 3, FALSE, FALSE, mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success },
    { XMM7360_RPC_CALL_UTA_MS_SS_INIT, FALSE, NULL, 3, FALSE, FALSE, mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success },
    { XMM7360_RPC_CALL_UTA_MS_SIM_OPEN_REQ, FALSE, NULL, 3, FALSE, FALSE, mm_broadband_modem_xmm7360_rpc_response_processor_final },
    { 0 }
};

static void
initialization_started (MMBroadbandModem    *modem,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    GError *error = NULL;
    MMBroadbandModemXmm7360 *self = MM_BROADBAND_MODEM_XMM7360 (modem);
    InitializationStartedContext *ctx;
    GTask *task;
    MMPortSerialAt *at_port;
    g_autoptr(MMPortScheduler) sched = NULL;

    task = g_task_new (self,
                       NULL,
                       (GAsyncReadyCallback)initialization_started_ready,
                       g_task_new (self, NULL, callback, user_data));

    ctx = g_slice_new0 (InitializationStartedContext);
    ctx->port = mm_broadband_modem_xmm7360_get_port_xmmrpc (self);
    ctx->is_sim_initialized = FALSE;
    ctx->timeout_id = 0;
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_started_context_free);

    at_port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!at_port) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                                 "no primary AT port");
        g_object_unref (task);
        return;
    }

    /* Open XMMRPC port for initialization */
    if (!mm_port_serial_open (MM_PORT_SERIAL (ctx->port), &error)) {
        g_prefix_error (&error, "Couldn't open XMMRPC port during initialization: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->unsol_handler_id = mm_port_serial_xmmrpc_xmm7360_add_unsolicited_msg_handler (
        ctx->port,
        (MMPortSerialXmmrpcXmm7360UnsolicitedMsgFn)init_unsol_handler,
        task,
        NULL);

    g_object_get (G_OBJECT (ctx->port), MM_PORT_SERIAL_SCHEDULER, &sched, NULL);
    g_object_set (sched,
                  MM_PORT_SCHEDULER_RR_INTER_PORT_DELAY,
                  300,
                  NULL);
    g_object_set (G_OBJECT (at_port), MM_PORT_SERIAL_SCHEDULER, sched, NULL);

    mm_obj_dbg (self, "running init sequence...");
    mm_broadband_modem_xmm7360_rpc_sequence_full (MM_BROADBAND_MODEM_XMM7360 (self),
                                                  ctx->port,
                                                  init_sequence,
                                                  NULL, /* cancellable */
                                                  (GAsyncReadyCallback)init_sequence_ready,
                                                  task);
}

/*****************************************************************************/

static void
nmea_received (MMPortSerialAt *port,
               GMatchInfo     *info_full,
               MMBroadbandModemXmm7360 *self)
{
    g_autofree gchar *trace_full = NULL;
    g_autoptr(GMatchInfo) info = NULL;

    trace_full = g_match_info_fetch (info_full, 1);
    if (!trace_full) {
        return;
    }

    g_regex_match (self->priv->nmea_regex_trace, trace_full, 0, &info);
    while (g_match_info_matches (info)) {
        g_autofree gchar *trace = NULL;

        trace = g_match_info_fetch (info, 1);
        if (!trace) {
            mm_obj_err (self, "fetching NMEA trace failed");
        } else {
            mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), trace);
        }

        g_match_info_next (info, NULL);
    }
}

static void
nmea_parser_register (MMSharedXmm *self_shared_xmm, MMPortSerialAt *gps_port,
                      gboolean is_register)
{
    MMBroadbandModemXmm7360 *self = MM_BROADBAND_MODEM_XMM7360 (self_shared_xmm);

    if (is_register) {
        mm_port_serial_at_add_unsolicited_msg_handler (gps_port,
                                                       self->priv->nmea_regex_full,
                                                       (MMPortSerialAtUnsolicitedMsgFn)nmea_received,
                                                       self,
                                                       NULL);
    } else {
        mm_port_serial_at_add_unsolicited_msg_handler (gps_port,
                                                       self->priv->nmea_regex_full,
                                                       NULL, NULL, NULL);
    }
}


/*****************************************************************************/

MMBroadbandModemXmm7360 *
mm_broadband_modem_xmm7360_new (const gchar  *device,
                                const gchar  *physdev,
                                const gchar **drivers,
                                const gchar  *plugin,
                                guint16       vendor_id,
                                guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_XMM7360,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_PHYSDEV,    physdev,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_xmm7360_init (MMBroadbandModemXmm7360 *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_XMM7360,
                                              MMBroadbandModemXmm7360Private);

    self->priv->nmea_regex_full = g_regex_new ("(?:\\r\\n){2}((?:\\$G.*?\\r\\n)+)(?:\\r\\n){2}OK(?:\\r\\n)",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0,
                                               NULL);
    self->priv->nmea_regex_trace = g_regex_new ("(\\$G.*?)\\r\\n",
                                                G_REGEX_RAW | G_REGEX_OPTIMIZE, 0,
                                                NULL);
}

static void
dispose (GObject *object)
{
    MMBroadbandModemXmm7360 *self = MM_BROADBAND_MODEM_XMM7360 (object);

    g_clear_pointer (&self->priv->nmea_regex_trace, g_regex_unref);
    g_clear_pointer (&self->priv->nmea_regex_full, g_regex_unref);
    g_clear_object (&self->priv->unlock_retries);

    G_OBJECT_CLASS (mm_broadband_modem_xmm7360_parent_class)->dispose (object);
}


static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->create_bearer = create_bearer;
    iface->create_bearer_finish = create_bearer_finish;
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gppInterface *iface)
{
    iface->set_initial_eps_bearer_settings = modem_3gpp_set_initial_eps_bearer_settings;
    iface->set_initial_eps_bearer_settings_finish = modem_3gpp_set_initial_eps_bearer_settings_finish;
}

static void
iface_shared_xmm_init (MMSharedXmmInterface *iface)
{
    iface->nmea_parser_register = nmea_parser_register;
}

static void
mm_broadband_modem_xmm7360_class_init (MMBroadbandModemXmm7360Class *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemXmm7360Private));

    object_class->dispose = dispose;

    broadband_modem_class->initialization_started = initialization_started;
    broadband_modem_class->initialization_started_finish = initialization_started_finish;
}
