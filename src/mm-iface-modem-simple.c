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
#include <libmm-common.h>

#include "mm-common-simple-properties.h"
#include "mm-bearer-list.h"
#include "mm-sim.h"
#include "mm-error-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-simple.h"
#include "mm-bearer-3gpp.h"
#include "mm-bearer-cdma.h"
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
    /* Mixed 3GPP(LTE)+CDMA modems... */
    else  {
        ctx->max_try_time = 10;
        ctx->remaining_tries_cdma = 6;
        ctx->remaining_tries_3gpp = 6;
    }

    check_next_registration (ctx);
}

/*****************************************************************************/

typedef struct {
    GSimpleAsyncResult *result;
    MMIfaceModemSimple *self;
    MMCommonBearerProperties *bearer_properties;
    gboolean create_cdma_bearer;
    gboolean create_3gpp_bearer;
    GList *list;
} CreateBearersContext;

static void
create_bearers_context_complete_and_free (CreateBearersContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_list_free_full (ctx->list, (GDestroyNotify)g_object_unref);
    g_object_unref (ctx->bearer_properties);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static GList *
create_3gpp_and_cdma_bearers_finish (MMIfaceModemSimple *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* We return the list itself. Note that there was no GDestroyNotify given when
     * the result was set, as we know that this finish() is always executed */
    return (GList *) g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void create_next_bearer (CreateBearersContext *ctx);

static void
create_bearer_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     CreateBearersContext *ctx)
{
    GError *error = NULL;
    MMBearer *bearer;

    bearer = mm_iface_modem_create_bearer_finish (self, res, &error);
    if (!bearer) {
        g_simple_async_result_take_error (ctx->result, error);
        create_bearers_context_complete_and_free (ctx);
        return;
    }

    /* Keep the new bearer */
    ctx->list = g_list_prepend (ctx->list, bearer);

    /* And see if we need to create a new one */
    create_next_bearer (ctx);
}

static void
create_next_bearer (CreateBearersContext *ctx)
{
    /* Create 3GPP bearer if needed */
    if (ctx->create_3gpp_bearer) {
        ctx->create_3gpp_bearer = FALSE;
        mm_iface_modem_create_bearer (MM_IFACE_MODEM (ctx->self),
                                      ctx->bearer_properties,
                                      (GAsyncReadyCallback)create_bearer_ready,
                                      ctx);
        return;
    }

    /* Create CDMA bearer if needed */
    if (ctx->create_cdma_bearer) {
        MMCommonBearerProperties *cdma_properties = NULL;

        ctx->create_cdma_bearer = FALSE;

        /* If the bearer properties has 'apn', we need to remove that before
         * trying to create the bearer. */
        if (mm_common_bearer_properties_get_apn (ctx->bearer_properties)) {
            cdma_properties = mm_common_bearer_properties_dup (ctx->bearer_properties);
            mm_common_bearer_properties_set_apn (cdma_properties, NULL);
        }

        mm_iface_modem_create_bearer (
            MM_IFACE_MODEM (ctx->self),
            (cdma_properties ? cdma_properties : ctx->bearer_properties),
            (GAsyncReadyCallback)create_bearer_ready,
            ctx);

        if (cdma_properties)
            g_object_unref (cdma_properties);
        return;
    }

    /* If no more bearers to create, we're done.
     * NOTE: we won't provide a GDestroyNotify to clear the gpointer passed as
     * result, as we know that finish() will ALWAYS be executed. */
    g_assert (ctx->list != NULL);
    g_simple_async_result_set_op_res_gpointer (ctx->result, ctx->list, NULL);
    ctx->list = NULL;
    create_bearers_context_complete_and_free (ctx);
}

static void
create_3gpp_and_cdma_bearers (MMIfaceModemSimple *self,
                              MMCommonBearerProperties *bearer_properties,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    guint n_bearers_needed;
    MMBearerList *list = NULL;
    CreateBearersContext *ctx;

    /* The implementation of this async method requires a valid callback, so
     * that we ensure that finish() will always be called. */
    g_assert (callback != NULL);

    ctx = g_new0 (CreateBearersContext, 1);
    ctx->self = g_object_ref (self);
    ctx->bearer_properties = g_object_ref (bearer_properties);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             create_3gpp_and_cdma_bearers);

    /* 3GPP-only modems... */
    if (mm_iface_modem_is_3gpp_only (MM_IFACE_MODEM (ctx->self)))
        ctx->create_3gpp_bearer = TRUE;
    /* CDMA-only modems... */
    else if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (ctx->self)))
        ctx->create_cdma_bearer = TRUE;
    /* Mixed CDMA+3GPP(LTE) modems */
    else {
        /* If we have APN, we'll create both 3GPP and CDMA bearers.
         * Otherwise we'll only create a CDMA bearer. */
        if (mm_common_bearer_properties_get_apn (ctx->bearer_properties)) {
            ctx->create_3gpp_bearer = TRUE;
        }
        ctx->create_cdma_bearer = TRUE;
    }

    n_bearers_needed = ctx->create_3gpp_bearer + ctx->create_cdma_bearer;
    if (n_bearers_needed == 0)
        g_assert_not_reached ();

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);

    /* TODO:  check if the bearers we want to create are already in the list */

    /* If we don't have enough space to create all needed bearers, try to remove
     * all existing ones first BUT only if that will give us enough space. */
    if (mm_bearer_list_get_max (list) < (mm_bearer_list_get_count (list) +
                                         n_bearers_needed)) {
        if (mm_bearer_list_get_max (list) < n_bearers_needed) {
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_TOO_MANY,
                "Cannot create bearers: need %u but only %u allowed",
                n_bearers_needed,
                mm_bearer_list_get_max (list));
            create_bearers_context_complete_and_free (ctx);
            g_object_unref (list);
            return;
        }

        /* We are told to force the creation of the new bearer.
         * We'll remove all existing bearers, and then go on creating the new one */
        mm_bearer_list_delete_all_bearers (list);
    }

    create_next_bearer (ctx);
    g_object_unref (list);
}

/*****************************************************************************/

typedef struct {
    MMIfaceModemSimple *self;
    GSimpleAsyncResult *result;
    GList *bearers;
    MMBearer *current;
} ConnectBearerContext;

static void
connect_bearer_context_complete_and_free (ConnectBearerContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->current)
        g_object_unref (ctx->current);
    g_list_free_full (ctx->bearers, (GDestroyNotify)g_object_unref);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static MMBearer *
connect_3gpp_or_cdma_bearer_finish (MMIfaceModemSimple *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_BEARER (g_object_ref (g_simple_async_result_get_op_res_gpointer (
                                        G_SIMPLE_ASYNC_RESULT (res))));
}

static void connect_next_bearer (ConnectBearerContext *ctx);

static void
connect_bearer_ready (MMBearer *bearer,
                      GAsyncResult *res,
                      ConnectBearerContext *ctx)
{
    GError *error = NULL;

    if (!mm_bearer_connect_finish (bearer, res, &error)) {
        mm_dbg ("Couldn't connect bearer: '%s'", error->message);
        g_error_free (error);
        /* We'll try with the next one */
        connect_next_bearer (ctx);
        return;
    }

    /* We got connected! */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               g_object_ref (ctx->current),
                                               (GDestroyNotify)g_object_unref);
    connect_bearer_context_complete_and_free (ctx);
}

static void
connect_next_bearer (ConnectBearerContext *ctx)
{
    GList *l;

    g_clear_object (&ctx->current);

    /* First, look for 3GPP bearers */
    for (l = ctx->bearers; l; l = g_list_next (l)) {
        if (MM_IS_BEARER_3GPP (l->data)) {
            /* Try to connect the current bearer. If the modem is not yet
             * registered in the 3GPP network, connection won't succeed.
             * Steal the reference from the list. */
            ctx->current = MM_BEARER (l->data);
            ctx->bearers = g_list_delete_link (ctx->bearers, l);
            mm_bearer_connect (MM_BEARER (ctx->current),
                               NULL, /* no number given */
                               (GAsyncReadyCallback)connect_bearer_ready,
                               ctx);
            return;
        }
    }

    /* Then, we look for CDMA bearers */
    for (l = ctx->bearers; l; l = g_list_next (l)) {
        if (MM_IS_BEARER_CDMA (l->data)) {
            /* Try to connect the current bearer. If the modem is not yet
             * registered in the 3GPP network, connection won't succeed.
             * Steal the reference from the list. */
            ctx->current = MM_BEARER (l->data);
            ctx->bearers = g_list_delete_link (ctx->bearers, l);
            mm_bearer_connect (MM_BEARER (ctx->current),
                               NULL, /* no number given */
                               (GAsyncReadyCallback)connect_bearer_ready,
                               ctx);
            return;
        }
    }

    /* Here we shouldn't have any remaining bearer.
     * POTS modem not yet supported */

    /* If we got here, we didn't get connected :-/ */
    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNAUTHORIZED,
                                     "Cannot connect any bearer");
    connect_bearer_context_complete_and_free (ctx);
}

static void
connect_3gpp_or_cdma_bearer (MMIfaceModemSimple *self,
                             GList *bearers,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    ConnectBearerContext *ctx;

    g_assert (bearers != NULL);

    ctx = g_new0 (ConnectBearerContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect_3gpp_or_cdma_bearer);
    ctx->bearers = g_list_copy (bearers);
    g_list_foreach (ctx->bearers, (GFunc)g_object_ref, NULL);

    connect_next_bearer (ctx);
}

/*****************************************************************************/

typedef enum {
    CONNECTION_STEP_FIRST,
    CONNECTION_STEP_UNLOCK_CHECK,
    CONNECTION_STEP_ENABLE,
    CONNECTION_STEP_ALLOWED_MODES,
    CONNECTION_STEP_ALLOWED_BANDS,
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

    /* Expected input properties */
    MMCommonConnectProperties *properties;

    /* Results to set */
    GList *bearers;
    MMBearer *connected_bearer;
} ConnectionContext;

static void
connection_context_free (ConnectionContext *ctx)
{
    g_list_free_full (ctx->bearers, (GDestroyNotify)g_object_unref);
    if (ctx->connected_bearer)
        g_object_unref (ctx->connected_bearer);
    g_object_unref (ctx->properties);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void connection_step (ConnectionContext *ctx);

static void
connect_3gpp_or_cdma_bearer_ready (MMIfaceModemSimple *self,
                                   GAsyncResult *res,
                                   ConnectionContext *ctx)
{
    GError *error = NULL;

    ctx->connected_bearer = connect_3gpp_or_cdma_bearer_finish (self, res, &error);
    if (!ctx->connected_bearer) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Bearer connected.... all done!!!!! */
    ctx->step++;
    connection_step (ctx);
}

static void
create_3gpp_and_cdma_bearers_ready (MMIfaceModemSimple *self,
                                    GAsyncResult *res,
                                    ConnectionContext *ctx)
{
    GError *error = NULL;

    /* List ownership for the caller */
    ctx->bearers = create_3gpp_and_cdma_bearers_finish (self, res, &error);
    if (!ctx->bearers) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Bearers available! */
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

static void
set_allowed_modes_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_allowed_modes_finish (MM_IFACE_MODEM (self), res, &error)) {
        /* If setting allowed modes is unsupported, keep on */
        if (!g_error_matches (error,
                              MM_CORE_ERROR,
                              MM_CORE_ERROR_UNSUPPORTED)) {
            g_dbus_method_invocation_take_error (ctx->invocation, error);
            connection_context_free (ctx);
            return;
        }
    }

    /* Allowed modes set... almost there! */
    ctx->step++;
    connection_step (ctx);
}

static void
set_allowed_bands_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_allowed_bands_finish (MM_IFACE_MODEM (self), res, &error)) {
        /* If setting allowed bands is unsupported, keep on */
        if (!g_error_matches (error,
                              MM_CORE_ERROR,
                              MM_CORE_ERROR_UNSUPPORTED)) {
            g_dbus_method_invocation_take_error (ctx->invocation, error);
            connection_context_free (ctx);
            return;
        }
    }

    /* Allowed bands set... almost there! */
    ctx->step++;
    connection_step (ctx);
}

static void
enable_ready (MMBaseModem *self,
              GAsyncResult *res,
              ConnectionContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_MODEM_GET_CLASS (self)->enable_finish (MM_BASE_MODEM (self), res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        connection_context_free (ctx);
        return;
    }

    /* Enabling done!, keep on!!! */
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

    /* Sent pin and unlocked, cool. */
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

    /* If we are already unlocked, go on to next step */
    if (lock == MM_MODEM_LOCK_NONE) {
        ctx->step++;
        connection_step (ctx);
        return;
    }

    /* During simple connect we are only allowed to use SIM PIN */
    if (lock != MM_MODEM_LOCK_SIM_PIN ||
        !mm_common_connect_properties_get_pin (ctx->properties)) {
        GEnumClass *enum_class;
        GEnumValue *value;

        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_LOCK));
        value = g_enum_get_value (enum_class, lock);
        g_dbus_method_invocation_return_error (
            ctx->invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNAUTHORIZED,
            "Modem is locked with '%s' code; cannot unlock it",
            value->value_nick);
        g_type_class_unref (enum_class);
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
                     mm_common_connect_properties_get_pin (ctx->properties),
                     NULL,
                     (GAsyncReadyCallback)send_pin_ready,
                     ctx);
    g_object_unref (sim);
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

    case CONNECTION_STEP_ENABLE:
        mm_info ("Simple connect state (%d/%d): Enable",
                 ctx->step, CONNECTION_STEP_LAST);
        MM_BASE_MODEM_GET_CLASS (ctx->self)->enable (MM_BASE_MODEM (ctx->self),
                                                     NULL, /* cancellable */
                                                     (GAsyncReadyCallback)enable_ready,
                                                     ctx);
        return;

    case CONNECTION_STEP_ALLOWED_MODES: {
        MMModemMode allowed_modes = MM_MODEM_MODE_ANY;
        MMModemMode preferred_mode = MM_MODEM_MODE_NONE;

        mm_info ("Simple connect state (%d/%d): Allowed mode",
                 ctx->step, CONNECTION_STEP_LAST);

        mm_common_connect_properties_get_allowed_modes (ctx->properties,
                                                        &allowed_modes,
                                                        &preferred_mode);
        mm_iface_modem_set_allowed_modes (MM_IFACE_MODEM (ctx->self),
                                          allowed_modes,
                                          preferred_mode,
                                          (GAsyncReadyCallback)set_allowed_modes_ready,
                                          ctx);
        return;
    }

    case CONNECTION_STEP_ALLOWED_BANDS: {
        GArray *array;
        const MMModemBand *allowed_bands = NULL;
        guint n_allowed_bands = 0;
        guint i;

        mm_info ("Simple connect state (%d/%d): Allowed bands",
                 ctx->step, CONNECTION_STEP_LAST);

        mm_common_connect_properties_get_allowed_bands (ctx->properties,
                                                        &allowed_bands,
                                                        &n_allowed_bands);

        array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n_allowed_bands);
        for (i = 0; i < n_allowed_bands; i++)
            g_array_insert_val (array, i, allowed_bands[i]);

        mm_iface_modem_set_allowed_bands (MM_IFACE_MODEM (ctx->self),
                                          array,
                                          (GAsyncReadyCallback)set_allowed_bands_ready,
                                          ctx);
        g_array_unref (array);
        return;
    }

    case CONNECTION_STEP_REGISTER:
        mm_info ("Simple connect state (%d/%d): Register",
                 ctx->step, CONNECTION_STEP_LAST);

        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self)) ||
            mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self))) {
            /* 3GPP or CDMA registration */
            register_in_3gpp_or_cdma_network (
                ctx->self,
                mm_common_connect_properties_get_operator_id (ctx->properties),
                (GAsyncReadyCallback)register_in_3gpp_or_cdma_network_ready,
                ctx);
            return;
        }

        /* If not 3GPP and not CDMA, this will possibly be a POTS modem,
         * which won't require any specific registration anywhere.
         * So, fall down to next step */
        ctx->step++;

    case CONNECTION_STEP_BEARER:
        mm_info ("Simple connect state (%d/%d): Bearer",
                 ctx->step, CONNECTION_STEP_LAST);

        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self)) ||
            mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self))) {
            MMCommonBearerProperties *bearer_properties;

            bearer_properties = (mm_common_connect_properties_get_bearer_properties (
                                     ctx->properties));
            /* 3GPP and/or CDMA bearer creation */
            create_3gpp_and_cdma_bearers (
                ctx->self,
                bearer_properties,
                (GAsyncReadyCallback)create_3gpp_and_cdma_bearers_ready,
                ctx);

            g_object_unref (bearer_properties);
            return;
        }

        /* If not 3GPP and not CDMA, this will possibly be a POTS modem,
         * currently unsupported. So, just abort. */
        g_assert_not_reached ();
        return;

    case CONNECTION_STEP_CONNECT:
        mm_info ("Simple connect state (%d/%d): Connect",
                 ctx->step, CONNECTION_STEP_LAST);

        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self)) ||
            mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self))) {
            /* 3GPP or CDMA bearer connection */
            connect_3gpp_or_cdma_bearer (
                ctx->self,
                ctx->bearers,
                (GAsyncReadyCallback)connect_3gpp_or_cdma_bearer_ready,
                ctx);
            return;
        }

        /* If not 3GPP and not CDMA, this will possibly be a POTS modem,
         * currently unsupported. So, just abort. */
        g_assert_not_reached ();
        return;

    case CONNECTION_STEP_LAST:
        mm_info ("Simple connect state (%d/%d): All done",
                 ctx->step, CONNECTION_STEP_LAST);
        /* All done, yey! */
        mm_gdbus_modem_simple_complete_connect (
            ctx->skeleton,
            ctx->invocation,
            mm_bearer_get_path (ctx->connected_bearer));
        connection_context_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static gboolean
handle_connect (MmGdbusModemSimple *skeleton,
                GDBusMethodInvocation *invocation,
                GVariant *dictionary,
                MMIfaceModemSimple *self)
{
    GError *error = NULL;
    MMCommonConnectProperties *properties;
    ConnectionContext *ctx;

    properties = mm_common_connect_properties_new_from_dictionary (dictionary, &error);
    if (!properties) {
        g_dbus_method_invocation_take_error (invocation, error);
        return TRUE;
    }

    ctx = g_new0 (ConnectionContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->step = CONNECTION_STEP_FIRST;
    ctx->properties = properties;

    /* Start */
    connection_step (ctx);

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

static gboolean
handle_disconnect (MmGdbusModemSimple *skeleton,
                   GDBusMethodInvocation *invocation,
                   const gchar *bearer_path,
                   MMIfaceModemSimple *self)
{
    MMBearerList *list = NULL;
    DisconnectionContext *ctx;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);

    ctx = g_new0 (DisconnectionContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);

    if (bearer_path &&
        bearer_path[0] == '/' &&
        bearer_path[1]) {
        ctx->bearer_path = g_strdup (ctx->bearer_path);
    }

    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)build_connected_bearer_list,
                            ctx);
    g_object_unref (list);

    if (ctx->bearer_path &&
        !ctx->bearers) {
        g_dbus_method_invocation_return_error (
            invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_INVALID_ARGS,
            "Couldn't disconnect bearer '%s': not found",
            ctx->bearer_path);
        disconnection_context_free (ctx);
        return TRUE;
    }

    disconnect_next_bearer (ctx);
    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_get_status (MmGdbusModemSimple *skeleton,
                   GDBusMethodInvocation *invocation,
                   MMIfaceModemSimple *self)
{
    MMCommonSimpleProperties *properties = NULL;
    GVariant *dictionary;

    g_object_get (self,
                  MM_IFACE_MODEM_SIMPLE_STATUS, &properties,
                  NULL);

    dictionary = mm_common_simple_properties_get_dictionary (properties);
    mm_gdbus_modem_simple_complete_get_status (skeleton, invocation, dictionary);
    g_variant_unref (dictionary);

    g_object_unref (properties);
    return TRUE;
}

/*****************************************************************************/

void
mm_iface_modem_simple_initialize (MMIfaceModemSimple *self)
{
    MmGdbusModemSimple *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_SIMPLE (self));

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
    g_return_if_fail (MM_IS_IFACE_MODEM_SIMPLE (self));

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
                              MM_TYPE_COMMON_SIMPLE_PROPERTIES,
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
