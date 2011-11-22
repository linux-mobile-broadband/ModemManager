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
 * Copyright (C) 2011 Google, Inc.
 */


#include <ModemManager.h>

#include <mm-gdbus-modem.h>
#include <mm-errors-types.h>

#include "mm-iface-modem.h"
#include "mm-base-modem.h"
#include "mm-log.h"

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INTERFACE_STATUS_SHUTDOWN,
    INTERFACE_STATUS_INITIALIZING,
    INTERFACE_STATUS_INITIALIZED
} InterfaceStatus;

static gboolean
handle_create_bearer (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      GVariant *arg_properties,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_delete_bearer (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_bearer,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_list_bearers (MmGdbusModem *object,
                     GDBusMethodInvocation *invocation,
                     MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_enable (MmGdbusModem *object,
               GDBusMethodInvocation *invocation,
               gboolean arg_enable,
               MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_reset (MmGdbusModem *object,
              GDBusMethodInvocation *invocation,
              MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_factory_reset (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_code,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_set_allowed_bands (MmGdbusModem *object,
                          GDBusMethodInvocation *invocation,
                          guint64 arg_bands,
                          MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_set_allowed_modes (MmGdbusModem *object,
                          GDBusMethodInvocation *invocation,
                          guint arg_modes,
                          guint arg_preferred,
                          MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

/*****************************************************************************/

typedef struct _UnlockCheckContext UnlockCheckContext;
struct _UnlockCheckContext {
    MMIfaceModem *self;
    MMAtSerialPort *port;
    guint pin_check_tries;
    guint pin_check_timeout_id;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static UnlockCheckContext *
unlock_check_context_new (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    UnlockCheckContext *ctx;

    ctx = g_new0 (UnlockCheckContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             unlock_check_context_new);
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
unlock_check_context_free (UnlockCheckContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
set_lock_status (MMIfaceModem *self,
                 MmGdbusModem *skeleton,
                 MMModemLock lock)
{
    mm_gdbus_modem_set_unlock_required (skeleton, lock);
}

MMModemLock
mm_iface_modem_unlock_check_finish (MMIfaceModem *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error)) {
        return MM_MODEM_LOCK_UNKNOWN;
    }

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void unlock_check_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                UnlockCheckContext *ctx);

static gboolean
unlock_check_again  (UnlockCheckContext *ctx)
{
    ctx->pin_check_timeout_id = 0;

    MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required (
        ctx->self,
        (GAsyncReadyCallback)unlock_check_ready,
        ctx);
    return FALSE;
}

static void
unlock_check_ready (MMIfaceModem *self,
                    GAsyncResult *res,
                    UnlockCheckContext *ctx)
{
    GError *error = NULL;
    MMModemLock lock;

    lock = MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required_finish (self,
                                                                             res,
                                                                             &error);
    if (error) {
        /* Retry up to 3 times */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE &&
            ++ctx->pin_check_tries < 3) {

            if (ctx->pin_check_timeout_id)
                g_source_remove (ctx->pin_check_timeout_id);
            ctx->pin_check_timeout_id = g_timeout_add_seconds (
                2,
                (GSourceFunc)unlock_check_again,
                ctx);
            return;
        }

        /* If reached max retries and still reporting error, set UNKNOWN */
        lock = MM_MODEM_LOCK_UNKNOWN;
    }

    /* Update lock status and modem status if needed */
    set_lock_status (self, ctx->skeleton, lock);

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (lock),
                                               NULL);
    g_simple_async_result_complete (ctx->result);
    unlock_check_context_free (ctx);
}

void
mm_iface_modem_unlock_check (MMIfaceModem *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    UnlockCheckContext *ctx;

    ctx = unlock_check_context_new (self, callback, user_data);

    if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required &&
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required_finish) {
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required (
            self,
            (GAsyncReadyCallback)unlock_check_ready,
            ctx);
        return;
    }

    /* Just assume that no lock is required */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (MM_MODEM_LOCK_NONE),
                                               NULL);
    g_simple_async_result_complete_in_idle (ctx->result);
    unlock_check_context_free (ctx);
}

/*****************************************************************************/

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_MODEM_CAPABILITIES,
    INITIALIZATION_STEP_CURRENT_CAPABILITIES,
    INITIALIZATION_STEP_MAX_BEARERS,
    INITIALIZATION_STEP_MAX_ACTIVE_BEARERS,
    INITIALIZATION_STEP_MANUFACTURER,
    INITIALIZATION_STEP_MODEL,
    INITIALIZATION_STEP_REVISION,
    INITIALIZATION_STEP_EQUIPMENT_ID,
    INITIALIZATION_STEP_DEVICE_ID,
    INITIALIZATION_STEP_UNLOCK_REQUIRED,
    INITIALIZATION_STEP_UNLOCK_RETRIES,
    INITIALIZATION_STEP_SUPPORTED_MODES,
    INITIALIZATION_STEP_SUPPORTED_BANDS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem *self;
    MMAtSerialPort *port;
    InitializationStep step;
    guint pin_check_tries;
    guint pin_check_timeout_id;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static InitializationContext *
initialization_context_new (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_context_new);
    ctx->step = INITIALIZATION_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
interface_initialization_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         InitializationContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        val = MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_modem_set_##NAME (ctx->skeleton, val); \
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

#undef UINT_REPLY_READY_FN
#define UINT_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         InitializationContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        mm_gdbus_modem_set_##NAME (                                     \
            ctx->skeleton,                                              \
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error)); \
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

UINT_REPLY_READY_FN (modem_capabilities, "Modem Capabilities")
UINT_REPLY_READY_FN (current_capabilities, "Current Capabilities")
UINT_REPLY_READY_FN (max_bearers, "Max Bearers")
UINT_REPLY_READY_FN (max_active_bearers, "Max Active Bearers")
STR_REPLY_READY_FN (manufacturer, "Manufacturer")
STR_REPLY_READY_FN (model, "Model")
STR_REPLY_READY_FN (revision, "Revision")
STR_REPLY_READY_FN (equipment_identifier, "Equipment Identifier")
STR_REPLY_READY_FN (device_identifier, "Device Identifier")
UINT_REPLY_READY_FN (supported_modes, "Supported Modes")
UINT_REPLY_READY_FN (supported_bands, "Supported Bands")

static void
load_unlock_required_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            InitializationContext *ctx)
{
    GError *error = NULL;

    /* NOTE: we already propagated the lock state, no need to do it again */
    mm_iface_modem_unlock_check_finish (self, res, &error);
    if (error) {
        mm_warn ("couldn't load unlock required status: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

UINT_REPLY_READY_FN (unlock_retries, "Unlock Retries")

static void
interface_initialization_step (InitializationContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST: {
        /* Load device if not done before */
        if (!mm_gdbus_modem_get_device (ctx->skeleton)) {
            gchar *device;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_DEVICE, &device,
                          NULL);
            mm_gdbus_modem_set_device (ctx->skeleton, device);
            g_free (device);
        }
        /* Load driver if not done before */
        if (!mm_gdbus_modem_get_driver (ctx->skeleton)) {
            gchar *driver;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_DRIVER, &driver,
                          NULL);
            mm_gdbus_modem_set_driver (ctx->skeleton, driver);
            g_free (driver);
        }
        /* Load plugin if not done before */
        if (!mm_gdbus_modem_get_plugin (ctx->skeleton)) {
            gchar *plugin;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_PLUGIN, &plugin,
                          NULL);
            mm_gdbus_modem_set_plugin (ctx->skeleton, plugin);
            g_free (plugin);
        }
        break;
    }

    case INITIALIZATION_STEP_MODEM_CAPABILITIES:
        /* Modem capabilities are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_modem_capabilities (ctx->skeleton) == MM_MODEM_CAPABILITY_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_modem_capabilities_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_CURRENT_CAPABILITIES:
        /* In theory, this property is able to change during runtime, so if
         * possible we'll reload it. */
       if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_current_capabilities_ready,
                ctx);
            return;
        }
       /* If no specific way of getting current capabilities, assume they are
        * equal to the modem capabilities */
        mm_gdbus_modem_set_current_capabilities (
            ctx->skeleton,
            mm_gdbus_modem_get_current_capabilities (ctx->skeleton));
        break;

    case INITIALIZATION_STEP_MAX_BEARERS:
        /* Max bearers value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_max_bearers (ctx->skeleton) == 0 &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_bearers &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_bearers_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_bearers (
                ctx->self,
                (GAsyncReadyCallback)load_max_bearers_ready,
                ctx);
            return;
        }
        /* Default to one bearer */
        mm_gdbus_modem_set_max_bearers (ctx->skeleton, 1);
        break;

    case INITIALIZATION_STEP_MAX_ACTIVE_BEARERS:
        /* Max active bearers value is meant to be loaded only once during the
         * whole lifetime of the modem. Therefore, if we already have them
         * loaded, don't try to load them again. */
        if (mm_gdbus_modem_get_max_active_bearers (ctx->skeleton) == 0 &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_active_bearers &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_active_bearers_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_active_bearers (
                ctx->self,
                (GAsyncReadyCallback)load_max_active_bearers_ready,
                ctx);
            return;
        }
       /* If no specific way of getting max active bearers, assume they are
        * equal to the absolute max bearers */
        mm_gdbus_modem_set_max_active_bearers (
            ctx->skeleton,
            mm_gdbus_modem_get_max_bearers (ctx->skeleton));
        break;

    case INITIALIZATION_STEP_MANUFACTURER:
        /* Manufacturer is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_manufacturer (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer (
                ctx->self,
                (GAsyncReadyCallback)load_manufacturer_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_MODEL:
        /* Model is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_model (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model (
                ctx->self,
                (GAsyncReadyCallback)load_model_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_REVISION:
        /* Revision is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_revision (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision (
                ctx->self,
                (GAsyncReadyCallback)load_revision_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_EQUIPMENT_ID:
        /* Equipment ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_equipment_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_equipment_identifier_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_DEVICE_ID:
        /* Device ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_device_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_device_identifier_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_UNLOCK_REQUIRED:
        /* Only check unlock required if we were previously not unlocked */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE) {
            mm_iface_modem_unlock_check (ctx->self,
                                         (GAsyncReadyCallback)load_unlock_required_ready,
                                         ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_UNLOCK_RETRIES:
        if ((MMModemLock)mm_gdbus_modem_get_unlock_required (ctx->skeleton) == MM_MODEM_LOCK_NONE) {
            /* Default to 0 when unlocked */
            mm_gdbus_modem_set_unlock_retries (ctx->skeleton, 0);
        } else {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries (
                    ctx->self,
                    (GAsyncReadyCallback)load_unlock_retries_ready,
                    ctx);
                return;
            }

            /* Default to 999 when we cannot check it */
            mm_gdbus_modem_set_unlock_retries (ctx->skeleton, 999);
        }
        break;

    case INITIALIZATION_STEP_SUPPORTED_MODES:
        /* Supported modes are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_supported_modes (ctx->skeleton) == MM_MODEM_MODE_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes (
                ctx->self,
                (GAsyncReadyCallback)load_supported_modes_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_SUPPORTED_BANDS:
        /* Supported bands are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_supported_bands (ctx->skeleton) == MM_MODEM_BAND_UNKNOWN &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands (
                ctx->self,
                (GAsyncReadyCallback)load_supported_bands_ready,
                ctx);
            return;
        }
        break;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
        initialization_context_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    InitializationContext *ctx;
    GError *error = NULL;

    ctx = initialization_context_new (self, callback, user_data);

    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        initialization_context_free (ctx);
        return;
    }

    /* Try to disable echo */
    mm_at_serial_port_queue_command (ctx->port, "E0", 3, NULL, NULL);
    /* Try to get extended errors */
    mm_at_serial_port_queue_command (ctx->port, "+CMEE=1", 2, NULL, NULL);

    interface_initialization_step (ctx);
}

/*****************************************************************************/


static InterfaceStatus
get_status (MMIfaceModem *self)
{
    GObject *skeleton = NULL;

    /* Are we already disabled? */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return INTERFACE_STATUS_SHUTDOWN;
    g_object_unref (skeleton);

    /* Are we being initialized? (interface not yet exported) */
    skeleton = G_OBJECT (mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)));
    if (skeleton) {
        g_object_unref (skeleton);
        return INTERFACE_STATUS_INITIALIZED;
    }

    return INTERFACE_STATUS_INITIALIZING;
}

gboolean
mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
interface_initialization_ready (MMIfaceModem *self,
                                GAsyncResult *init_result,
                                GSimpleAsyncResult *op_result)
{
    GObject *skeleton = NULL;
    GError *inner_error = NULL;

    /* If initialization failed, remove the skeleton and return the error */
    if (!interface_initialization_finish (self,
                                          init_result,
                                          &inner_error)) {
        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                      NULL);
        g_simple_async_result_take_error (op_result, inner_error);
        g_simple_async_result_complete (op_result);
        g_object_unref (op_result);
        return;
    }

    /* Finish current initialization by setting up the DBus skeleton */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    g_assert (skeleton != NULL);

    /* Handle method invocations */
    g_signal_connect (skeleton,
                      "handle-create-bearer",
                      G_CALLBACK (handle_create_bearer),
                      self);
    g_signal_connect (skeleton,
                      "handle-delete-bearer",
                      G_CALLBACK (handle_delete_bearer),
                      self);
    g_signal_connect (skeleton,
                      "handle-list-bearers",
                      G_CALLBACK (handle_list_bearers),
                      self);
    g_signal_connect (skeleton,
                      "handle-enable",
                      G_CALLBACK (handle_enable),
                      self);
    g_signal_connect (skeleton,
                      "handle-reset",
                      G_CALLBACK (handle_reset),
                      self);
    g_signal_connect (skeleton,
                      "handle-factory-reset",
                      G_CALLBACK (handle_factory_reset),
                      self);
    g_signal_connect (skeleton,
                      "handle-set-allowed-bands",
                      G_CALLBACK (handle_set_allowed_bands),
                      self);
    g_signal_connect (skeleton,
                      "handle-set-allowed-modes",
                      G_CALLBACK (handle_set_allowed_modes),
                      self);

    /* Finally, export the new interface */
    mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self),
                                        MM_GDBUS_MODEM (skeleton));
    g_simple_async_result_set_op_res_gboolean (op_result, TRUE);
    g_simple_async_result_complete (op_result);
    g_object_unref (op_result);
}

void
mm_iface_modem_initialize (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_IS_IFACE_MODEM (self));

    /* Setup asynchronous result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_initialize);

    switch (get_status (self)) {
    case INTERFACE_STATUS_INITIALIZED:
    case INTERFACE_STATUS_SHUTDOWN: {
        MmGdbusModem *skeleton = NULL;

        /* Did we already create it? */
        g_object_get (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                      NULL);
        if (!skeleton) {
            skeleton = mm_gdbus_modem_skeleton_new ();

            /* Set all initial property defaults */
            mm_gdbus_modem_set_sim (skeleton, NULL);
            mm_gdbus_modem_set_modem_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
            mm_gdbus_modem_set_current_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
            mm_gdbus_modem_set_max_bearers (skeleton, 0);
            mm_gdbus_modem_set_max_active_bearers (skeleton, 0);
            mm_gdbus_modem_set_manufacturer (skeleton, NULL);
            mm_gdbus_modem_set_model (skeleton, NULL);
            mm_gdbus_modem_set_revision (skeleton, NULL);
            mm_gdbus_modem_set_device_identifier (skeleton, NULL);
            mm_gdbus_modem_set_device (skeleton, NULL);
            mm_gdbus_modem_set_driver (skeleton, NULL);
            mm_gdbus_modem_set_plugin (skeleton, NULL);
            mm_gdbus_modem_set_equipment_identifier (skeleton, NULL);
            mm_gdbus_modem_set_unlock_required (skeleton, MM_MODEM_LOCK_UNKNOWN);
            mm_gdbus_modem_set_unlock_retries (skeleton, 0);
            mm_gdbus_modem_set_access_technology (skeleton, MM_MODEM_ACCESS_TECH_UNKNOWN);
            mm_gdbus_modem_set_signal_quality (skeleton, g_variant_new ("(ub)", 0, FALSE));
            mm_gdbus_modem_set_supported_modes (skeleton, MM_MODEM_MODE_NONE);
            mm_gdbus_modem_set_allowed_modes (skeleton, MM_MODEM_MODE_ANY);
            mm_gdbus_modem_set_preferred_mode (skeleton, MM_MODEM_MODE_NONE);
            mm_gdbus_modem_set_supported_bands (skeleton, MM_MODEM_BAND_UNKNOWN);
            mm_gdbus_modem_set_allowed_bands (skeleton, MM_MODEM_BAND_ANY);
            mm_gdbus_modem_set_state (skeleton, MM_MODEM_STATE_UNKNOWN);

            /* Keep a reference to it */
            g_object_set (self,
                          MM_IFACE_MODEM_DBUS_SKELETON, skeleton,
                          NULL);
        }

        /* Perform async initialization here */
        interface_initialization (self,
                                  (GAsyncReadyCallback)interface_initialization_ready,
                                  result);
        g_object_unref (skeleton);
        return;
    }

    case INTERFACE_STATUS_INITIALIZING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_IN_PROGRESS,
                                         "Interface is already being enabled");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    g_return_if_reached ();
}

gboolean
mm_iface_modem_shutdown (MMIfaceModem *self,
                         GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM (self), FALSE);

    switch (get_status (self)) {
    case INTERFACE_STATUS_SHUTDOWN:
        return TRUE;
    case INTERFACE_STATUS_INITIALIZING:
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_IN_PROGRESS,
                     "Iinterface being currently initialized");
        return FALSE;
    case INTERFACE_STATUS_INITIALIZED:
        /* Unexport DBus interface and remove the skeleton */
        mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self), NULL);
        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                      NULL);
        return TRUE;
    }

    g_return_val_if_reached (FALSE);
}


/*****************************************************************************/

static void
iface_modem_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_DBUS_SKELETON,
                              "Modem DBus skeleton",
                              "DBus skeleton for the Modem interface",
                              MM_GDBUS_TYPE_MODEM_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_get_type (void)
{
    static GType iface_modem_type = 0;

    if (!G_UNLIKELY (iface_modem_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem), /* class_size */
            iface_modem_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                                   "MMIfaceModem",
                                                   &info,
                                                   0);

        g_type_interface_add_prerequisite (iface_modem_type, MM_TYPE_BASE_MODEM);
    }

    return iface_modem_type;
}
