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
 * Copyright (C) 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-bearer-list.h"
#include "mm-sim.h"
#include "mm-error-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-simple.h"
#include "mm-log.h"

/*****************************************************************************/
/* Register in either a CDMA or a 3GPP network (or both) */

typedef struct {
    GSimpleAsyncResult *result;
    MMIfaceModemSimple *self;
    gchar *operator_id;
    guint remaining_tries_cdma;
    guint remaining_tries_3gpp;
    guint max_try_time;
} RegisterInNetworkContext;

static void
register_in_network_context_complete_and_free (RegisterInNetworkContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_free (ctx->operator_id);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
register_in_3gpp_or_cdma_network_finish (MMIfaceModemSimple *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void check_next_registration (RegisterInNetworkContext *ctx);

static void
register_in_cdma_network_ready (MMIfaceModemCdma *self,
                                GAsyncResult *res,
                                RegisterInNetworkContext *ctx)
{
    ctx->remaining_tries_cdma--;

    if (!mm_iface_modem_cdma_register_in_network_finish (
            MM_IFACE_MODEM_CDMA (self), res, NULL)) {
        /* Retry check */
        check_next_registration (ctx);
        return;
    }

    /* Registered we are! */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    register_in_network_context_complete_and_free (ctx);
}

static void
register_in_3gpp_network_ready (MMIfaceModem3gpp *self,
                                GAsyncResult *res,
                                RegisterInNetworkContext *ctx)
{
    ctx->remaining_tries_3gpp--;

    if (!mm_iface_modem_3gpp_register_in_network_finish (
            MM_IFACE_MODEM_3GPP (self), res, NULL)) {
        /* Retry check */
        check_next_registration (ctx);
        return;
    }

    /* Registered we are! */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    register_in_network_context_complete_and_free (ctx);
}

static void
check_next_registration (RegisterInNetworkContext *ctx)
{
    if (ctx->remaining_tries_cdma > ctx->remaining_tries_3gpp &&
        ctx->remaining_tries_cdma > 0) {
        mm_iface_modem_cdma_register_in_network (
            MM_IFACE_MODEM_CDMA (ctx->self),
            ctx->max_try_time,
            (GAsyncReadyCallback)register_in_cdma_network_ready,
            ctx);
        return;
    }

    if (ctx->remaining_tries_3gpp > 0) {
        mm_iface_modem_3gpp_register_in_network (
            MM_IFACE_MODEM_3GPP (ctx->self),
            ctx->operator_id,
            ctx->max_try_time,
            (GAsyncReadyCallback)register_in_3gpp_network_ready,
            ctx);
        return;
    }

    /* No more tries of anything */
    g_simple_async_result_take_error (
        ctx->result,
        mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT));
    register_in_network_context_complete_and_free (ctx);
}

static void
register_in_3gpp_or_cdma_network (MMIfaceModemSimple *self,
                                  const gchar *operator_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    RegisterInNetworkContext *ctx;

    ctx = g_new0 (RegisterInNetworkContext, 1);
    ctx->self = g_object_ref (self);
    ctx->operator_id = g_strdup (operator_id);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             register_in_3gpp_or_cdma_network);

    /* 3GPP-only modems... */
    if (mm_iface_modem_is_3gpp_only (MM_IFACE_MODEM (ctx->self))) {
        ctx->max_try_time = 60;
        ctx->remaining_tries_cdma = 0;
        ctx->remaining_tries_3gpp = 1;
    }
    /* CDMA-only modems... */
    else if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (ctx->self))) {
        ctx->max_try_time = 60;
        ctx->remaining_tries_cdma = 1;
        ctx->remaining_tries_3gpp = 0;
    }
    /* Mixed 3GPP+CDMA modems... */
    else  {
        ctx->max_try_time = 10;
        ctx->remaining_tries_cdma = 6;
        ctx->remaining_tries_3gpp = 6;
    }

    check_next_registration (ctx);
}

/*****************************************************************************/

typedef enum {
    CONNECTION_STEP_FIRST,
    CONNECTION_STEP_UNLOCK_CHECK,
    CONNECTION_STEP_WAIT_FOR_INITIALIZED,
    CONNECTION_STEP_ENABLE,
    CONNECTION_STEP_WAIT_FOR_ENABLED,
    CONNECTION_STEP_ALLOWED_MODES,
    CONNECTION_STEP_BANDS,
    CONNECTION_STEP_REGISTER,
    CONNECTION_STEP_BEARER,
    CONNECTION_STEP_CONNECT,
    CONNECTION_STEP_LAST
} ConnectionStep;

typedef struct {
    MmGdbusModemSimple *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemSimple *self;
    ConnectionStep step;
    gulong state_changed_id;
    guint state_changed_wait_id;

    /* Expected input properties */
    GVariant *dictionary;
    MMSimpleConnectProperties *properties;

    /* Results to set */
    MMBearer *bearer;
} ConnectionContext;

static void
connection_context_free (ConnectionContext *ctx)
{
    g_assert (ctx->state_changed_id == 0);
    g_assert (ctx->state_changed_wait_id == 0);

    g_variant_unref (ctx->dictionary);
    if (ctx->properties)
        g_object_unref (ctx->properties);
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void connection_step (ConnectionContext *ctx);

static void
connect_bearer_ready (MMBearer *bearer,
                      GAsyncResult *res,
                      ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_bearer_connect_finish (bearer, res, &error)) {
        mm_dbg ("Couldn't connect bearer: '%s'", error->message);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Bearer connected.... all done!!!!! */
    ctx->step++;
    connection_step (ctx);
}

static void
create_bearer_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     ConnectionContext *ctx)
{
    GError *error = NULL;

    /* ownership for the caller */
    ctx->bearer = mm_iface_modem_create_bearer_finish (self, res, &error);
    if (!ctx->bearer) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Bearer available! */
    ctx->step++;
    connection_step (ctx);
}

static void
register_in_3gpp_or_cdma_network_ready (MMIfaceModemSimple *self,
                                        GAsyncResult *res,
                                        ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!register_in_3gpp_or_cdma_network_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Registered now! */
    ctx->step++;
    connection_step (ctx);
}

static gboolean
after_set_allowed_modes_timeout_cb (ConnectionContext *ctx)
{
    /* Allowed modes set... almost there! */
    ctx->step++;
    connection_step (ctx);
    return FALSE;
}

static void
set_allowed_modes_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_allowed_modes_finish (MM_IFACE_MODEM (self), res, &error)) {
        if (g_error_matches (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED)) {
            g_error_free (error);
            /* If setting bands is unsupported, keep on without sleep */
            ctx->step++;
            connection_step (ctx);
        } else {
            g_dbus_method_invocation_take_error (ctx->invocation, error);
            connection_context_free (ctx);
        }
        return;
    }

    /* Setting allowed modes will reset the current registration, so we'll need
     * a couple of seconds to settle down. This sleep time just makes sure that
     * the modem has enough time to report being unregistered. */
    mm_dbg ("Will wait to settle down after updating allowed modes");
    g_timeout_add_seconds (2, (GSourceFunc)after_set_allowed_modes_timeout_cb, ctx);
}

static gboolean
after_set_bands_timeout_cb (ConnectionContext *ctx)
{
    /* Bands set... almost there! */
    ctx->step++;
    connection_step (ctx);
    return FALSE;
}

static void
set_bands_ready (MMBaseModem *self,
                 GAsyncResult *res,
                 ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_bands_finish (MM_IFACE_MODEM (self), res, &error)) {
        if (g_error_matches (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED)) {
            g_error_free (error);
            /* If setting bands is unsupported, keep on without sleep */
            ctx->step++;
            connection_step (ctx);
        } else {
            g_dbus_method_invocation_take_error (ctx->invocation, error);
            connection_context_free (ctx);
        }
        return;
    }

    /* Setting bands will reset the current registration, so we'll need a couple
     * of seconds to settle down. This sleep time just makes sure that the modem
     * has enough time to report being unregistered. */
    mm_dbg ("Will wait to settle down after updating bands");
    g_timeout_add_seconds (2, (GSourceFunc)after_set_bands_timeout_cb, ctx);
}

static void
wait_for_enabled_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        ConnectionContext *ctx)
{
    GError *error = NULL;
    MMModemState state;

    state = mm_iface_modem_wait_for_final_state_finish (self, res, &error);
    if (error) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    if (state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (
            ctx->invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_WRONG_STATE,
            "Couldn't wait for 'enabled': new state is '%s'",
            mm_modem_state_get_string (state));
        connection_context_free (ctx);
        return;
    }

    /* Enabled now, cool. */
    ctx->step++;
    connection_step (ctx);
}

static void
enable_ready (MMBaseModem *self,
              GAsyncResult *res,
              ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_enable_finish (MM_BASE_MODEM (self), res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Enabling done!, keep on!!! */
    ctx->step++;
    connection_step (ctx);
}

static void
wait_for_initialized_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            ConnectionContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_wait_for_final_state_finish (self, res, &error);
    if (error) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Initialized now, cool. */
    ctx->step++;
    connection_step (ctx);
}

static void
send_pin_ready (MMSim *sim,
                GAsyncResult *res,
                ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_sim_send_pin_finish (sim, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Sent pin, cool. */
    ctx->step++;
    connection_step (ctx);
}

static void
unlock_check_ready (MMIfaceModem *self,
                    GAsyncResult *res,
                    ConnectionContext *ctx)
{
    GError *error = NULL;
    MMModemLock lock;
    MMSim *sim;

    lock = mm_iface_modem_unlock_check_finish (self, res, &error);
    if (error) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* If we are already unlocked, go on to next step. Note that we do also
     * allow SIM-PIN2, as we don't need to unlock that in order to get
     * connected. */
    if (lock == MM_MODEM_LOCK_NONE ||
        lock == MM_MODEM_LOCK_SIM_PIN2) {
        ctx->step++;
        connection_step (ctx);
        return;
    }

    /* During simple connect we are only allowed to use SIM PIN */
    if (lock != MM_MODEM_LOCK_SIM_PIN ||
        !mm_simple_connect_properties_get_pin (ctx->properties)) {
        g_dbus_method_invocation_return_error (
            ctx->invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNAUTHORIZED,
            "Modem is locked with '%s' code; cannot unlock it",
            mm_modem_lock_get_string (lock));
        connection_context_free (ctx);
        return;
    }

    /* Try to unlock the modem providing the PIN */
    sim = NULL;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_SIM, &sim,
                  NULL);
    if (!sim) {
        g_dbus_method_invocation_return_error (
            ctx->invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Cannot unlock modem, couldn't get access to the SIM");
        connection_context_free (ctx);
        return;
    }

    mm_sim_send_pin (sim,
                     mm_simple_connect_properties_get_pin (ctx->properties),
                     (GAsyncReadyCallback)send_pin_ready,
                     ctx);
    g_object_unref (sim);
}

typedef struct {
    MMBearer *found;
} BearerListFindContext;

static void
bearer_list_find_disconnected (MMBearer *bearer,
                               BearerListFindContext *ctx)
{
    /* If already marked one to remove, do nothing */
    if (ctx->found)
        return;

    if (mm_bearer_get_status (bearer) == MM_BEARER_STATUS_DISCONNECTED)
        ctx->found = g_object_ref (bearer);
}

static void
connection_step (ConnectionContext *ctx)
{
    switch (ctx->step) {
    case CONNECTION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case CONNECTION_STEP_UNLOCK_CHECK:
        mm_info ("Simple connect state (%d/%d): Unlock check",
                 ctx->step, CONNECTION_STEP_LAST);
        mm_iface_modem_unlock_check (MM_IFACE_MODEM (ctx->self),
                                     (GAsyncReadyCallback)unlock_check_ready,
                                     ctx);
        return;

    case CONNECTION_STEP_WAIT_FOR_INITIALIZED:
        mm_info ("Simple connect state (%d/%d): Wait to get fully initialized",
                 ctx->step, CONNECTION_STEP_LAST);
        mm_iface_modem_wait_for_final_state (MM_IFACE_MODEM (ctx->self),
                                             MM_MODEM_STATE_DISABLED, /* disabled == initialized */
                                             (GAsyncReadyCallback)wait_for_initialized_ready,
                                             ctx);
        return;

    case CONNECTION_STEP_ENABLE:
        mm_info ("Simple connect state (%d/%d): Enable",
                 ctx->step, CONNECTION_STEP_LAST);
        mm_base_modem_enable (MM_BASE_MODEM (ctx->self),
                              (GAsyncReadyCallback)enable_ready,
                              ctx);
        return;

    case CONNECTION_STEP_WAIT_FOR_ENABLED:
        mm_info ("Simple connect state (%d/%d): Wait to get fully enabled",
                 ctx->step, CONNECTION_STEP_LAST);
        mm_iface_modem_wait_for_final_state (MM_IFACE_MODEM (ctx->self),
                                             MM_MODEM_STATE_UNKNOWN, /* just a final state */
                                             (GAsyncReadyCallback)wait_for_enabled_ready,
                                             ctx);
        return;

    case CONNECTION_STEP_ALLOWED_MODES: {
        MMModemMode allowed_modes = MM_MODEM_MODE_ANY;
        MMModemMode preferred_mode = MM_MODEM_MODE_NONE;

        mm_info ("Simple connect state (%d/%d): Allowed mode",
                 ctx->step, CONNECTION_STEP_LAST);

        /* Don't set modes unless explicitly requested to do so */
        if (mm_simple_connect_properties_get_allowed_modes (ctx->properties,
                                                            &allowed_modes,
                                                            &preferred_mode)) {
            mm_iface_modem_set_allowed_modes (MM_IFACE_MODEM (ctx->self),
                                              allowed_modes,
                                              preferred_mode,
                                              (GAsyncReadyCallback)set_allowed_modes_ready,
                                              ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;
    }

    case CONNECTION_STEP_BANDS: {
        const MMModemBand *bands = NULL;
        guint n_bands = 0;

        mm_info ("Simple connect state (%d/%d): Bands",
                 ctx->step, CONNECTION_STEP_LAST);

        /* Don't set bands unless explicitly requested to do so */
        if (mm_simple_connect_properties_get_bands (ctx->properties,
                                                    &bands,
                                                    &n_bands)) {
            GArray *array;
            guint i;

            if (bands && *bands) {
                array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n_bands);
                for (i = 0; i < n_bands; i++)
                    g_array_insert_val (array, i, bands[i]);

                mm_iface_modem_set_bands (MM_IFACE_MODEM (ctx->self),
                                          array,
                                          (GAsyncReadyCallback)set_bands_ready,
                                          ctx);
                g_array_unref (array);
                return;
            }
        }

        /* Fall down to next step */
        ctx->step++;
    }

    case CONNECTION_STEP_REGISTER:
        mm_info ("Simple connect state (%d/%d): Register",
                 ctx->step, CONNECTION_STEP_LAST);

        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self)) ||
            mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self))) {
            /* 3GPP or CDMA registration */
            register_in_3gpp_or_cdma_network (
                ctx->self,
                mm_simple_connect_properties_get_operator_id (ctx->properties),
                (GAsyncReadyCallback)register_in_3gpp_or_cdma_network_ready,
                ctx);
            return;
        }

        /* If not 3GPP and not CDMA, this will possibly be a POTS modem,
         * which won't require any specific registration anywhere.
         * So, fall down to next step */
        ctx->step++;

    case CONNECTION_STEP_BEARER: {
        MMBearerList *list = NULL;
        MMBearerProperties *bearer_properties;

        mm_info ("Simple connect state (%d/%d): Bearer",
                 ctx->step, CONNECTION_STEP_LAST);

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_BEARER_LIST, &list,
                      NULL);
        if (!list) {
            g_dbus_method_invocation_return_error (
                ctx->invocation,
                MM_CORE_ERROR,
                MM_CORE_ERROR_FAILED,
                "Couldn't get the bearer list");
            connection_context_free (ctx);
            return;
        }

        bearer_properties = mm_simple_connect_properties_get_bearer_properties (ctx->properties);

        /* Check if the bearer we want to create is already in the list */
        ctx->bearer = mm_bearer_list_find (list, bearer_properties);
        if (!ctx->bearer) {
            mm_dbg ("Creating new bearer...");
            /* If we don't have enough space to create the bearer, try to remove
             * a disconnected bearer first. */
            if (mm_bearer_list_get_max (list) == mm_bearer_list_get_count (list)) {
                BearerListFindContext foreach_ctx;

                foreach_ctx.found = NULL;
                mm_bearer_list_foreach (list,
                                        (MMBearerListForeachFunc)bearer_list_find_disconnected,
                                        &foreach_ctx);

                /* Found a disconnected bearer, remove it */
                if (foreach_ctx.found) {
                    GError *error = NULL;

                    if (!mm_bearer_list_delete_bearer (list,
                                                       mm_bearer_get_path (foreach_ctx.found),
                                                       &error)) {
                        mm_dbg ("Couldn't delete disconnected bearer at '%s': '%s'",
                                mm_bearer_get_path (foreach_ctx.found),
                                error->message);
                        g_error_free (error);
                    } else
                        mm_dbg ("Deleted disconnected bearer at '%s'",
                                mm_bearer_get_path (foreach_ctx.found));
                    g_object_unref (foreach_ctx.found);
                }

                /* Re-check space, and if we still are in max, return an error */
                if (mm_bearer_list_get_max (list) == mm_bearer_list_get_count (list)) {
                    g_dbus_method_invocation_return_error (
                        ctx->invocation,
                        MM_CORE_ERROR,
                        MM_CORE_ERROR_TOO_MANY,
                        "Cannot create new bearer: all existing bearers are connected");
                    connection_context_free (ctx);
                    g_object_unref (list);
                    g_object_unref (bearer_properties);
                    return;
                }
            }

            mm_iface_modem_create_bearer (MM_IFACE_MODEM (ctx->self),
                                          bearer_properties,
                                          (GAsyncReadyCallback)create_bearer_ready,
                                          ctx);

            g_object_unref (list);
            g_object_unref (bearer_properties);
            return;
        }

        mm_dbg ("Using already existing bearer at '%s'...",
                mm_bearer_get_path (ctx->bearer));
        g_object_unref (list);
        g_object_unref (bearer_properties);
        /* Fall down to next step */
        ctx->step++;
    }

    case CONNECTION_STEP_CONNECT:
        mm_info ("Simple connect state (%d/%d): Connect",
                 ctx->step, CONNECTION_STEP_LAST);

        /* Wait... if we're already using an existing bearer, we need to check if it is
         * already connected; and if so, just don't do anything else */
        if (mm_bearer_get_status (ctx->bearer) != MM_BEARER_STATUS_CONNECTED) {
            mm_bearer_connect (ctx->bearer,
                               (GAsyncReadyCallback)connect_bearer_ready,
                               ctx);
            return;
        }

        mm_dbg ("Bearer at '%s' is already connected...",
                mm_bearer_get_path (ctx->bearer));

        /* Fall down to next step */
        ctx->step++;

    case CONNECTION_STEP_LAST:
        mm_info ("Simple connect state (%d/%d): All done",
                 ctx->step, CONNECTION_STEP_LAST);
        /* All done, yey! */
        mm_gdbus_modem_simple_complete_connect (
            ctx->skeleton,
            ctx->invocation,
            mm_bearer_get_path (ctx->bearer));
        connection_context_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static void
connect_auth_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    ConnectionContext *ctx)
{
    GError *error = NULL;
    MMModemState current = MM_MODEM_STATE_UNKNOWN;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    ctx->properties = mm_simple_connect_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!ctx->properties) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* We may be able to skip some steps, so check that before doing anything */
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &current,
                  NULL);

    mm_info ("Simple connect started...");

    switch (current) {
    case MM_MODEM_STATE_FAILED:
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        /* If we need unlocking, start from the very beginning */
        ctx->step = CONNECTION_STEP_FIRST;
        break;

    case MM_MODEM_STATE_INITIALIZING:
    case MM_MODEM_STATE_DISABLING:
        /* If we are transitioning to the DISABLED (initialized) state,
         * wait to get there before going on */
        ctx->step = CONNECTION_STEP_WAIT_FOR_INITIALIZED;
        break;

    case MM_MODEM_STATE_DISABLED:
        ctx->step = CONNECTION_STEP_ENABLE;
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISCONNECTING:
        /* If we are transitioning to the ENABLED/REGISTERED state,
         * wait to get there before going on */
        ctx->step = CONNECTION_STEP_WAIT_FOR_ENABLED;
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        ctx->step = CONNECTION_STEP_ENABLE + 1;
        break;
    }
    connection_step (ctx);
}

static gboolean
handle_connect (MmGdbusModemSimple *skeleton,
                GDBusMethodInvocation *invocation,
                GVariant *dictionary,
                MMIfaceModemSimple *self)
{
    ConnectionContext *ctx;

    ctx = g_new0 (ConnectionContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)connect_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MMIfaceModemSimple *self;
    MmGdbusModemSimple *skeleton;
    GDBusMethodInvocation *invocation;
    gchar *bearer_path;
    GList *bearers;
    MMBearer *current;
} DisconnectionContext;

static void
disconnection_context_free (DisconnectionContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->bearer_path);
    if (ctx->current)
        g_object_unref (ctx->current);
    g_list_free_full (ctx->bearers, (GDestroyNotify) g_object_unref);
    g_free (ctx);
}

static void disconnect_next_bearer (DisconnectionContext *ctx);

static void
disconnect_ready (MMBearer *bearer,
                  GAsyncResult *res,
                  DisconnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_bearer_disconnect_finish (bearer, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        disconnection_context_free (ctx);
        return;
    }

    disconnect_next_bearer (ctx);
}

static void
disconnect_next_bearer (DisconnectionContext *ctx)
{
    if (ctx->current)
        g_clear_object (&ctx->current);

    /* No more bearers? all done! */
    if (!ctx->bearers) {
        mm_gdbus_modem_simple_complete_disconnect (ctx->skeleton,
                                                   ctx->invocation);
        disconnection_context_free (ctx);
        return;
    }

    ctx->current = MM_BEARER (ctx->bearers->data);
    ctx->bearers = g_list_delete_link (ctx->bearers, ctx->bearers);

    mm_bearer_disconnect (ctx->current,
                          (GAsyncReadyCallback)disconnect_ready,
                          ctx);
}

static void
build_connected_bearer_list (MMBearer *bearer,
                             DisconnectionContext *ctx)
{
    if (!ctx->bearer_path ||
        g_str_equal (ctx->bearer_path, mm_bearer_get_path (bearer)))
        ctx->bearers = g_list_prepend (ctx->bearers, g_object_ref (bearer));
}

static void
disconnect_auth_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       DisconnectionContext *ctx)
{
    GError *error = NULL;
    MMBearerList *list = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        disconnection_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (
            ctx->invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't get the bearer list");
        disconnection_context_free (ctx);
        return;
    }

    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)build_connected_bearer_list,
                            ctx);
    g_object_unref (list);

    if (ctx->bearer_path &&
        !ctx->bearers) {
        g_dbus_method_invocation_return_error (
            ctx->invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_INVALID_ARGS,
            "Couldn't disconnect bearer '%s': not found",
            ctx->bearer_path);
        disconnection_context_free (ctx);
        return;
    }

    /* Go on disconnecting bearers */
    disconnect_next_bearer (ctx);
}

static gboolean
handle_disconnect (MmGdbusModemSimple *skeleton,
                   GDBusMethodInvocation *invocation,
                   const gchar *bearer_path,
                   MMIfaceModemSimple *self)
{
    DisconnectionContext *ctx;

    ctx = g_new0 (DisconnectionContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);

    if (bearer_path &&
        bearer_path[0] == '/' &&
        bearer_path[1]) {
        ctx->bearer_path = g_strdup (ctx->bearer_path);
    }

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)disconnect_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_get_status (MmGdbusModemSimple *skeleton,
                   GDBusMethodInvocation *invocation,
                   MMIfaceModemSimple *self)
{
    MMSimpleStatus *status = NULL;
    GVariant *dictionary;

    g_object_get (self,
                  MM_IFACE_MODEM_SIMPLE_STATUS, &status,
                  NULL);

    dictionary = mm_simple_status_get_dictionary (status);
    mm_gdbus_modem_simple_complete_get_status (skeleton, invocation, dictionary);
    g_variant_unref (dictionary);

    g_object_unref (status);
    return TRUE;
}

/*****************************************************************************/

void
mm_iface_modem_simple_initialize (MMIfaceModemSimple *self)
{
    MmGdbusModemSimple *skeleton = NULL;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_simple_skeleton_new ();

        g_object_set (self,
                      MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON, skeleton,
                      NULL);

        /* Handle method invocations */
        g_signal_connect (skeleton,
                          "handle-connect",
                          G_CALLBACK (handle_connect),
                          self);
        g_signal_connect (skeleton,
                          "handle-disconnect",
                          G_CALLBACK (handle_disconnect),
                          self);
        g_signal_connect (skeleton,
                          "handle-get-status",
                          G_CALLBACK (handle_get_status),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_simple (MM_GDBUS_OBJECT_SKELETON (self),
                                                   MM_GDBUS_MODEM_SIMPLE (skeleton));
    }
    g_object_unref (skeleton);
}

void
mm_iface_modem_simple_shutdown (MMIfaceModemSimple *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_simple (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_simple_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON,
                              "Simple DBus skeleton",
                              "DBus skeleton for the Simple interface",
                              MM_GDBUS_TYPE_MODEM_SIMPLE_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_SIMPLE_STATUS,
                              "Simple status",
                              "Compilation of status values",
                              MM_TYPE_SIMPLE_STATUS,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_simple_get_type (void)
{
    static GType iface_modem_simple_type = 0;

    if (!G_UNLIKELY (iface_modem_simple_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemSimple), /* class_size */
            iface_modem_simple_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_simple_type = g_type_register_static (G_TYPE_INTERFACE,
                                                          "MMIfaceModemSimple",
                                                          &info,
                                                          0);

        g_type_interface_add_prerequisite (iface_modem_simple_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_simple_type;
}
