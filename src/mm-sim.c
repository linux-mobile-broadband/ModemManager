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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem.h"
#include "mm-sim.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (InitAsyncContext *ctx);
static void async_initable_iface_init     (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMSim, mm_sim, MM_GDBUS_TYPE_SIM_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               async_initable_iface_init));

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMSimPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* The modem which owns this SIM */
    MMBaseModem *modem;
    /* The path where the SIM object is exported */
    gchar *path;
};

/*****************************************************************************/

typedef struct {
    MMSim *self;
    GDBusMethodInvocation *invocation;
} DbusCallContext;

static void
dbus_call_context_free (DbusCallContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static DbusCallContext *
dbus_call_context_new (MMSim *self,
                       GDBusMethodInvocation *invocation)
{
    DbusCallContext *ctx;

    ctx = g_new0 (DbusCallContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    return ctx;
}

#undef NO_REPLY_READY_FN
#define NO_REPLY_READY_FN(NAME)                                         \
    static void                                                         \
    handle_##NAME##_ready (MMBaseModem *modem,                          \
                           GAsyncResult *res,                           \
                           DbusCallContext *ctx)                        \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        mm_base_modem_at_command_finish (MM_BASE_MODEM (modem), res, &error); \
        if (error)                                                      \
            g_dbus_method_invocation_take_error (ctx->invocation, error); \
        else                                                            \
            mm_gdbus_sim_complete_##NAME (MM_GDBUS_SIM (ctx->self), ctx->invocation); \
        dbus_call_context_free (ctx);                                   \
    }

/*****************************************************************************/
/* CHANGE PIN */

NO_REPLY_READY_FN (change_pin)

static gboolean
handle_change_pin (MMSim *self,
                   GDBusMethodInvocation *invocation,
                   const gchar *arg_old_pin,
                   const gchar *arg_new_pin)
{
    gchar *command;

    command = g_strdup_printf ("+CPWD=\"SC\",\"%s\",\"%s\"",
                               arg_old_pin,
                               arg_new_pin);
    mm_base_modem_at_command (MM_BASE_MODEM (self->priv->modem),
                              command,
                              3,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)handle_change_pin_ready,
                              dbus_call_context_new (self,
                                                     invocation));
    g_free (command);
    return TRUE;
}

/*****************************************************************************/
/* ENABLE PIN */

NO_REPLY_READY_FN (enable_pin)

static gboolean
handle_enable_pin (MMSim *self,
                   GDBusMethodInvocation *invocation,
                   const gchar *arg_pin,
                   gboolean arg_enabled)
{
    gchar *command;

    command = g_strdup_printf ("+CLCK=\"SC\",%d,\"%s\"",
                               arg_enabled ? 1 : 0,
                               arg_pin);
    mm_base_modem_at_command (MM_BASE_MODEM (self->priv->modem),
                              command,
                              3,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)handle_enable_pin_ready,
                              dbus_call_context_new (self,
                                                     invocation));
    g_free (command);
    return TRUE;
}

/*****************************************************************************/
/* SEND PIN/PUK */

typedef struct {
    MMSim *self;
    GSimpleAsyncResult *result;
    GError *save_error;
} SendPinPukContext;

static void
send_pin_puk_context_complete_and_free (SendPinPukContext *ctx)
{
    if (ctx->save_error)
        g_error_free (ctx->save_error);
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
}

static GError *
error_for_unlock_check (MMModemLock lock)
{
    static const MMMobileEquipmentError errors_for_locks [] = {
        MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,            /* MM_MODEM_LOCK_UNKNOWN */
        MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,            /* MM_MODEM_LOCK_NONE */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN,            /* MM_MODEM_LOCK_SIM_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2,           /* MM_MODEM_LOCK_SIM_PIN2 */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK,            /* MM_MODEM_LOCK_SIM_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2,           /* MM_MODEM_LOCK_SIM_PUK2 */
        MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN,        /* MM_MODEM_LOCK_PH_SP_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK,        /* MM_MODEM_LOCK_PH_SP_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN,        /* MM_MODEM_LOCK_PH_NET_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK,        /* MM_MODEM_LOCK_PH_NET_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN,         /* MM_MODEM_LOCK_PH_SIM_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN,           /* MM_MODEM_LOCK_PH_CORP_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK,           /* MM_MODEM_LOCK_PH_CORP_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN,        /* MM_MODEM_LOCK_PH_FSIM_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK,        /* MM_MODEM_LOCK_PH_FSIM_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN, /* MM_MODEM_LOCK_PH_NETSUB_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK, /* MM_MODEM_LOCK_PH_NETSUB_PUK */
    };
	GEnumClass *enum_class;
    GEnumValue *enum_value;
    GError *error;

    g_assert (lock >= MM_MODEM_LOCK_UNKNOWN);
    g_assert (lock <= MM_MODEM_LOCK_PH_NETSUB_PUK);

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_LOCK));
    enum_value = g_enum_get_value (enum_class, lock);
    error =  g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                          errors_for_locks[lock],
                          "Device is locked: '%s'",
                          enum_value->value_nick);

    mm_warn ("ERROR: %s", error->message);

    g_type_class_unref (enum_class);
    return error;
}

gboolean
mm_sim_send_pin_finish (MMSim *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
unlock_check_ready (MMIfaceModem *modem,
                    GAsyncResult *res,
                    SendPinPukContext *ctx)
{
    GError *error = NULL;
    MMModemLock lock;

    lock = mm_iface_modem_unlock_check_finish (modem, res, &error);
    if (lock != MM_MODEM_LOCK_NONE) {
        /* Device is locked. Now:
         *   - If we got an error in the original send-pin action, report it.
         *   - If we got an error in the pin-check action, report it.
         *   - Otherwise, build our own error from the lock code.
         */
        if (ctx->save_error) {
            g_simple_async_result_take_error (ctx->result, ctx->save_error);
            ctx->save_error = NULL;
            g_clear_error (&error);
        }
        else if (error)
            g_simple_async_result_take_error (ctx->result, error);
        else
            g_simple_async_result_take_error (ctx->result,
                                              error_for_unlock_check (lock));
    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    send_pin_puk_context_complete_and_free (ctx);
}

static void
send_pin_puk_ready (MMBaseModem *modem,
                           GAsyncResult *res,
                           SendPinPukContext *ctx)
{
    mm_base_modem_at_command_finish (modem, res, &ctx->save_error);

    /* Once pin/puk has been sent, recheck lock */
    mm_iface_modem_unlock_check (MM_IFACE_MODEM (modem),
                                 (GAsyncReadyCallback)unlock_check_ready,
                                 ctx);
}

void
mm_sim_send_pin (MMSim *self,
                 const gchar *pin,
                 const gchar *puk,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    SendPinPukContext *ctx;
    gchar *command;

    ctx = g_new0 (SendPinPukContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_sim_send_pin);

    command = (puk ?
               g_strdup_printf ("+CPIN=\"%s\",\"%s\"", puk, pin) :
               g_strdup_printf ("+CPIN=\"%s\"", pin));

    mm_base_modem_at_command (MM_BASE_MODEM (self->priv->modem),
                              command,
                              3,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)send_pin_puk_ready,
                              ctx);
    g_free (command);
}

/*****************************************************************************/
/* SEND PIN */

static void
handle_send_pin_ready (MMSim *self,
                       GAsyncResult *res,
                       DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!mm_sim_send_pin_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_sim_complete_send_pin (MM_GDBUS_SIM (self), ctx->invocation);

    dbus_call_context_free (ctx);
}

static gboolean
handle_send_pin (MMSim *self,
                 GDBusMethodInvocation *invocation,
                 const gchar *pin)
{
    mm_sim_send_pin (self,
                     pin,
                     NULL,
                     (GAsyncReadyCallback)handle_send_pin_ready,
                     dbus_call_context_new (self,
                                            invocation));
    return TRUE;
}

/*****************************************************************************/
/* SEND PUK */

static void
handle_send_puk_ready (MMSim *self,
                       GAsyncResult *res,
                       DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!mm_sim_send_pin_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_sim_complete_send_puk (MM_GDBUS_SIM (self), ctx->invocation);

    dbus_call_context_free (ctx);
}

static gboolean
handle_send_puk (MMSim *self,
                 GDBusMethodInvocation *invocation,
                 const gchar *puk,
                 const gchar *pin)
{
    mm_sim_send_pin (self,
                     pin,
                     puk,
                     (GAsyncReadyCallback)handle_send_puk_ready,
                     dbus_call_context_new (self,
                                            invocation));
    return TRUE;
}

/*****************************************************************************/

static void
mm_sim_export (MMSim *self)
{
    GError *error = NULL;

    /* Handle method invocations */
    g_signal_connect (self,
                      "handle-change-pin",
                      G_CALLBACK (handle_change_pin),
                      NULL);
    g_signal_connect (self,
                      "handle-enable-pin",
                      G_CALLBACK (handle_enable_pin),
                      NULL);
    g_signal_connect (self,
                      "handle-send-pin",
                      G_CALLBACK (handle_send_pin),
                      NULL);
    g_signal_connect (self,
                      "handle-send-puk",
                      G_CALLBACK (handle_send_puk),
                      NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_warn ("couldn't export SIM at '%s': '%s'",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
}

static void
mm_sim_unexport (MMSim *self)
{
    /* Only unexport if currently exported */
    if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
}

/*****************************************************************************/

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME)                                        \
    static void                                                         \
    NAME##_command_ready (MMBaseModem *modem,                           \
                          GAsyncResult *res,                            \
                          GSimpleAsyncResult *operation_result)         \
    {                                                                   \
        GError *error = NULL;                                           \
        const gchar *response;                                          \
                                                                        \
        response = mm_base_modem_at_command_finish (modem, res, &error); \
        if (error)                                                      \
            g_simple_async_result_take_error (operation_result, error); \
        else                                                            \
            g_simple_async_result_set_op_res_gpointer (operation_result, \
                                                       (gpointer)response, \
                                                       NULL);           \
                                                                        \
        g_simple_async_result_complete (operation_result);              \
        g_object_unref (operation_result);                              \
    }

/*****************************************************************************/
/* SIM IDENTIFIER */

static gchar *
parse_iccid (const gchar *response,
             GError **error)
{
    gchar buf[21];
    gchar swapped[21];
    const gchar *str;
    gint sw1;
    gint sw2;
    gboolean success = FALSE;

    memset (buf, 0, sizeof (buf));
    str = mm_strip_tag (response, "+CRSM:");
    if (sscanf (str, "%d,%d,\"%20c\"", &sw1, &sw2, (char *) &buf) == 3)
        success = TRUE;
    else {
        /* May not include quotes... */
        if (sscanf (str, "%d,%d,%20c", &sw1, &sw2, (char *) &buf) == 3)
            success = TRUE;
    }

    if (!success) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Could not parse the CRSM response");
        return NULL;
    }

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        gsize len = 0;
        gint f_pos = -1;
        gint i;

        /* Make sure the buffer is only digits or 'F' */
        for (len = 0; len < sizeof (buf) && buf[len]; len++) {
            if (isdigit (buf[len]))
                continue;
            if (buf[len] == 'F' || buf[len] == 'f') {
                buf[len] = 'F';  /* canonicalize the F */
                f_pos = len;
                continue;
            }
            if (buf[len] == '\"') {
                buf[len] = 0;
                break;
            }

            /* Invalid character */
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "CRSM ICCID response contained invalid character '%c'",
                         buf[len]);
            return NULL;
        }

        /* BCD encoded ICCIDs are 20 digits long */
        if (len != 20) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Invalid +CRSM ICCID response size (was %zd, expected 20)",
                         len);
            return NULL;
        }

        /* Ensure if there's an 'F' that it's second-to-last */
        if ((f_pos >= 0) && (f_pos != len - 2)) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Invalid +CRSM ICCID length (unexpected F)");
            return NULL;
        }

        /* Swap digits in the EFiccid response to get the actual ICCID, each
         * group of 2 digits is reversed in the +CRSM response.  i.e.:
         *
         *    21436587 -> 12345678
         */
        memset (swapped, 0, sizeof (swapped));
        for (i = 0; i < 10; i++) {
            swapped[i * 2] = buf[(i * 2) + 1];
            swapped[(i * 2) + 1] = buf[i * 2];
        }

        /* Zero out the F for 19 digit ICCIDs */
        if (swapped[len - 1] == 'F')
            swapped[len - 1] = 0;


        return g_strdup (swapped);
    } else {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "SIM failed to handle CRSM request (sw1 %d sw2 %d)",
                     sw1, sw2);
        return NULL;
    }
}

static gchar *
load_sim_identifier_finish (MMSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    const gchar *result;
    gchar *sim_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    sim_identifier = parse_iccid (result, error);
    if (!sim_identifier)
        return NULL;

    mm_dbg ("loaded SIM identifier: %s", sim_identifier);
    return sim_identifier;
}

STR_REPLY_READY_FN (load_sim_identifier)

static void
load_sim_identifier (MMSim *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_dbg ("loading SIM identifier...");

    /* READ BINARY of EFiccid (ICC Identification) ETSI TS 102.221 section 13.2 */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self->priv->modem),
        "+CRSM=176,12258,0,0,10",
        20,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_sim_identifier_command_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_sim_identifier));
}

/*****************************************************************************/
/* IMSI */

static gchar *
load_imsi_finish (MMSim *self,
                  GAsyncResult *res,
                  GError **error)
{
    gchar *imsi;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    imsi = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));

    mm_dbg ("loaded IMSI: %s", imsi);
    return imsi;
}

STR_REPLY_READY_FN (load_imsi)

static void
load_imsi (MMSim *self,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    mm_dbg ("loading IMSI...");

    mm_base_modem_at_command (
        MM_BASE_MODEM (self->priv->modem),
        "+CIMI",
        3,
        TRUE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_imsi_command_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_imsi));
}

/*****************************************************************************/
/* Operator ID */

static guint
parse_mnc_length (const gchar *response,
                  GError **error)
{
    gint sw1;
    gint sw2;
    gboolean success = FALSE;
    gchar hex[51];

    memset (hex, 0, sizeof (hex));
    if (sscanf (response, "+CRSM:%d,%d,\"%50c\"", &sw1, &sw2, (char *) &hex) == 3)
        success = TRUE;
    else {
        /* May not include quotes... */
        if (sscanf (response, "+CRSM:%d,%d,%50c", &sw1, &sw2, (char *) &hex) == 3)
            success = TRUE;
    }

    if (!success) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Could not parse the CRSM response");
        return 0;
    }

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        gsize buflen = 0;
        guint32 mnc_len;
        gchar *bin;

        /* Make sure the buffer is only hex characters */
        while (buflen < sizeof (hex) && hex[buflen]) {
            if (!isxdigit (hex[buflen])) {
                hex[buflen] = 0x0;
                break;
            }
            buflen++;
        }

        /* Convert hex string to binary */
        bin = utils_hexstr2bin (hex, &buflen);
        if (!bin || buflen < 4) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "SIM returned malformed response '%s'",
                         hex);
            g_free (bin);
            return 0;
        }

        /* MNC length is byte 4 of this SIM file */
        mnc_len = bin[3] & 0xFF;
        if (mnc_len == 2 || mnc_len == 3) {
            g_free (bin);
            return mnc_len;
        }

        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "SIM returned invalid MNC length %d (should be either 2 or 3)",
                     mnc_len);
        g_free (bin);
        return 0;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "SIM failed to handle CRSM request (sw1 %d sw2 %d)",
                 sw1, sw2);
    return 0;
}

static gchar *
load_operator_identifier_finish (MMSim *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    GError *inner_error = NULL;
    const gchar *imsi;
    const gchar *result;
    guint mnc_length;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    imsi = mm_gdbus_sim_get_imsi (MM_GDBUS_SIM (self));
    if (!imsi) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot load Operator ID without IMSI");
        return NULL;
    }

    mnc_length = parse_mnc_length (result, &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    /* Build Operator ID */
    return g_strndup (imsi, 3 + mnc_length);
}

STR_REPLY_READY_FN (load_operator_identifier)

static void
load_operator_identifier (MMSim *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_dbg ("loading Operator ID...");

    /* READ BINARY of EFad (Administrative Data) ETSI 51.011 section 10.3.18 */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self->priv->modem),
        "+CRSM=176,28589,0,0,4",
        3,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_operator_identifier_command_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_operator_identifier));
}

/*****************************************************************************/
/* Operator Name (Service Provider Name) */

static gchar *
parse_spn (const gchar *response,
           GError **error)
{
    gint sw1;
    gint sw2;
    gboolean success = FALSE;
    gchar hex[51];

    memset (hex, 0, sizeof (hex));
    if (sscanf (response, "+CRSM:%d,%d,\"%50c\"", &sw1, &sw2, (char *) &hex) == 3)
        success = TRUE;
    else {
        /* May not include quotes... */
        if (sscanf (response, "+CRSM:%d,%d,%50c", &sw1, &sw2, (char *) &hex) == 3)
            success = TRUE;
    }

    if (!success) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Could not parse the CRSM response");
        return NULL;
    }

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        gsize buflen = 0;
        gchar *bin;
        gchar *utf8;

        /* Make sure the buffer is only hex characters */
        while (buflen < sizeof (hex) && hex[buflen]) {
            if (!isxdigit (hex[buflen])) {
                hex[buflen] = 0x0;
                break;
            }
            buflen++;
        }

        /* Convert hex string to binary */
        bin = utils_hexstr2bin (hex, &buflen);
        if (!bin) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "SIM returned malformed response '%s'",
                         hex);
            return NULL;
        }

        /* Remove the FF filler at the end */
        while (buflen > 1 && bin[buflen - 1] == (char)0xff)
            buflen--;

        /* First byte is metadata; remainder is GSM-7 unpacked into octets; convert to UTF8 */
        utf8 = (gchar *)mm_charset_gsm_unpacked_to_utf8 ((guint8 *)bin + 1, buflen - 1);
        g_free (bin);
        return utf8;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "SIM failed to handle CRSM request (sw1 %d sw2 %d)",
                 sw1, sw2);
    return NULL;
}

static gchar *
load_operator_name_finish (MMSim *self,
                           GAsyncResult *res,
                           GError **error)
{
    const gchar *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    return parse_spn (result, error);
}

STR_REPLY_READY_FN (load_operator_name)

static void
load_operator_name (MMSim *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_dbg ("loading Operator Name...");

    /* READ BINARY of EFspn (Service Provider Name) ETSI 51.011 section 10.3.11 */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self->priv->modem),
        "+CRSM=176,28486,0,0,17",
        3,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_operator_name_command_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_operator_name));
}

/*****************************************************************************/


typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_SIM_IDENTIFIER,
    INITIALIZATION_STEP_IMSI,
    INITIALIZATION_STEP_OPERATOR_ID,
    INITIALIZATION_STEP_OPERATOR_NAME,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMSim *self;
    InitializationStep step;
    guint sim_identifier_tries;
    MMAtSerialPort *port;
};

static void
init_async_context_free (InitAsyncContext *ctx,
                         gboolean close_port)
{
    if (close_port)
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

MMSim *
mm_sim_new_finish (GAsyncInitable *initable,
                   GAsyncResult  *res,
                   GError       **error)
{
    return MM_SIM (g_async_initable_new_finish (initable, res, error));
}

static gboolean
initable_init_finish (GAsyncInitable  *initable,
                      GAsyncResult    *result,
                      GError         **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
        return FALSE;

    return TRUE;
}

static void
load_sim_identifier_ready (MMSim *self,
                           GAsyncResult *res,
                           InitAsyncContext *ctx)
{
    GError *error = NULL;
    gchar *simid;

    simid  = load_sim_identifier_finish (self, res, &error);
    if (!simid) {
        /* Try one more time... Gobi 1K cards may reply to the first
         * request with '+CRSM: 106,134,""' which is bogus because
         * subsequent requests work fine.
         */
        if (++ctx->sim_identifier_tries < 2) {
            g_clear_error (&error);
            interface_initialization_step (ctx);
            return;
        }

        mm_warn ("couldn't load SIM identifier: '%s'",
                 error ? error->message : "unknown error");
        g_clear_error (&error);
    }

    mm_gdbus_sim_set_sim_identifier (MM_GDBUS_SIM (self), simid);
    g_free (simid);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    load_##NAME##_ready (MMSim *self,                                   \
                         GAsyncResult *res,                             \
                         InitAsyncContext *ctx)                         \
    {                                                                   \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        val = load_##NAME##_finish (self, res, &error);                 \
        mm_gdbus_sim_set_##NAME (MM_GDBUS_SIM (self), val);             \
        g_free (val);                                                   \
                                                                        \
        if (error) {                                                    \
            mm_warn ("couldn't load %s: '%s'", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (ctx);                            \
    }

STR_REPLY_READY_FN (imsi, "IMSI")
STR_REPLY_READY_FN (operator_identifier, "Operator identifier")
STR_REPLY_READY_FN (operator_name, "Operator name")

static void
interface_initialization_step (InitAsyncContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SIM_IDENTIFIER:
        /* SIM ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_sim_identifier (MM_GDBUS_SIM (ctx->self)) == NULL) {
            load_sim_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_sim_identifier_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_IMSI:
        /* IMSI is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_imsi (MM_GDBUS_SIM (ctx->self)) == NULL) {
            load_imsi (
                ctx->self,
                (GAsyncReadyCallback)load_imsi_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_OPERATOR_ID:
        /* Operator ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_operator_identifier (MM_GDBUS_SIM (ctx->self)) == NULL) {
            load_operator_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_operator_identifier_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_OPERATOR_NAME:
        /* Operator Name is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_operator_name (MM_GDBUS_SIM (ctx->self)) == NULL) {
            load_operator_name (
                ctx->self,
                (GAsyncReadyCallback)load_operator_name_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, TRUE);
        return;
    }


    g_assert_not_reached ();
}

static void
common_init_async (GAsyncInitable *initable,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)

{
    InitAsyncContext *ctx;
    GError *error = NULL;

    ctx = g_new (InitAsyncContext, 1);
    ctx->self = g_object_ref (initable);
    ctx->result = g_simple_async_result_new (G_OBJECT (initable),
                                             callback,
                                             user_data,
                                             common_init_async);
    ctx->cancellable = (cancellable ?
                        g_object_ref (cancellable) :
                        NULL);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->sim_identifier_tries = 0;

    ctx->port = mm_base_modem_get_port_primary (ctx->self->priv->modem);
    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    interface_initialization_step (ctx);
}

static void
initable_init_async (GAsyncInitable *initable,
                     int io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_gdbus_sim_set_sim_identifier (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_imsi (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_operator_identifier (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_operator_name (MM_GDBUS_SIM (initable), NULL);

    common_init_async (initable, cancellable, callback, user_data);
}

void
mm_sim_new (MMBaseModem *modem,
            GCancellable *cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    gchar *path;
    static guint32 id = 0;

    /* Build the unique path for the SIM, and create the object */
    path = g_strdup_printf (MM_DBUS_PATH"/SIMs/%d", id++);
    g_async_initable_new_async (MM_TYPE_SIM,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_SIM_PATH,  path,
                                MM_SIM_MODEM, modem,
                                NULL);
    g_free (path);
}

gboolean
mm_sim_initialize_finish (MMSim *self,
                          GAsyncResult *result,
                          GError **error)
{
    return initable_init_finish (G_ASYNC_INITABLE (self), result, error);
}

void
mm_sim_initialize (MMSim *self,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    common_init_async (G_ASYNC_INITABLE (self),
                       cancellable,
                       callback,
                       user_data);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMSim *self = MM_SIM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);
        break;
    case PROP_CONNECTION:
        if (self->priv->connection)
            g_object_unref (self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection */
        if (self->priv->connection)
            mm_sim_export (self);
        else
            mm_sim_unexport (self);
        break;
    case PROP_MODEM:
        if (self->priv->modem)
            g_object_unref (self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem) {
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the SIM's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_SIM_CONNECTION,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMSim *self = MM_SIM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_sim_init (MMSim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SIM,
                                              MMSimPrivate);
}

static void
finalize (GObject *object)
{
    MMSim *self = MM_SIM (object);

    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_sim_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMSim *self = MM_SIM (object);

    if (self->priv->connection) {
        /* If we arrived here with a valid connection, make sure we unexport
         * the object */
        mm_sim_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    if (self->priv->modem)
        g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_sim_parent_class)->dispose (object);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    iface->init_async = initable_init_async;
    iface->init_finish = initable_init_finish;
}

static void
mm_sim_class_init (MMSimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSimPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_SIM_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_SIM_PATH,
                             "Path",
                             "DBus path of the SIM",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_SIM_MODEM,
                             "Modem",
                             "The Modem which owns this SIM",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);
}
