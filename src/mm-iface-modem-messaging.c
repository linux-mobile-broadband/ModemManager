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
 * Copyright (C) 2012 Google, Inc.
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-messaging.h"
#include "mm-sms-list.h"
#include "mm-log.h"

#define SUPPORT_CHECKED_TAG "messaging-support-checked-tag"
#define SUPPORTED_TAG       "messaging-supported-tag"
#define STORAGE_CONTEXT_TAG "messaging-storage-context-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;
static GQuark storage_context_quark;

/*****************************************************************************/

guint8
mm_iface_modem_messaging_get_local_multipart_reference (MMIfaceModemMessaging *self,
                                                        const gchar *number,
                                                        GError **error)
{
    MMSmsList *list = NULL;
    guint8 reference;
    guint8 first;

    /* Start by looking for a random number */
    reference = g_random_int_range (1,255);

    /* Then, look for the given reference in user-created messages */
    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list)
        return reference;

    first = reference;
    do {
        if (!mm_sms_list_has_local_multipart_reference (list, number, reference)) {
            g_object_unref (list);
            return reference;
        }

        if (reference == 255)
            reference = 1;
        else
            reference++;
    }
    while (reference != first);

    g_object_unref (list);

    /* We were not able to find a new valid multipart reference :/
     * return an error */
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_TOO_MANY,
                 "Cannot create multipart SMS: No valid multipart reference "
                 "available for destination number '%s'",
                 number);
    return 0;
}

/*****************************************************************************/

void
mm_iface_modem_messaging_bind_simple_status (MMIfaceModemMessaging *self,
                                             MMSimpleStatus *status)
{
}

/*****************************************************************************/

MMBaseSms *
mm_iface_modem_messaging_create_sms (MMIfaceModemMessaging *self)
{
    g_assert (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->create_sms != NULL);

    return MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->create_sms (self);
}

/*****************************************************************************/

typedef struct {
    GArray *supported_mem1;
    GArray *supported_mem2;
    GArray *supported_mem3;
} StorageContext;

static void
storage_context_free (StorageContext *ctx)
{
    if (ctx->supported_mem1)
        g_array_unref (ctx->supported_mem1);
    if (ctx->supported_mem2)
        g_array_unref (ctx->supported_mem2);
    if (ctx->supported_mem3)
        g_array_unref (ctx->supported_mem3);
    g_free (ctx);
}

static StorageContext *
get_storage_context (MMIfaceModemMessaging *self)
{
    StorageContext *ctx;

    if (G_UNLIKELY (!storage_context_quark))
        storage_context_quark =  (g_quark_from_static_string (
                                      STORAGE_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), storage_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_new0 (StorageContext, 1);

        g_object_set_qdata_full (
            G_OBJECT (self),
            storage_context_quark,
            ctx,
            (GDestroyNotify)storage_context_free);
    }

    return ctx;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemMessaging *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemMessaging *self;
    gchar *path;
} HandleDeleteContext;

static void
handle_delete_context_free (HandleDeleteContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->path);
    g_free (ctx);
}

static void
handle_delete_ready (MMSmsList *list,
                     GAsyncResult *res,
                     HandleDeleteContext *ctx)
{
    GError *error = NULL;

    if (!mm_sms_list_delete_sms_finish (list, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_messaging_complete_delete (ctx->skeleton, ctx->invocation);

    handle_delete_context_free (ctx);
}

static void
handle_delete_auth_ready (MMBaseModem *self,
                          GAsyncResult *res,
                          HandleDeleteContext *ctx)
{
    MMModemState modem_state = MM_MODEM_STATE_UNKNOWN;
    MMSmsList *list = NULL;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot delete SMS: device not yet enabled");
        handle_delete_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot delete SMS: missing SMS list");
        handle_delete_context_free (ctx);
        return;
    }

    mm_sms_list_delete_sms (list,
                            ctx->path,
                            (GAsyncReadyCallback)handle_delete_ready,
                            ctx);
    g_object_unref (list);
}

static gboolean
handle_delete (MmGdbusModemMessaging *skeleton,
               GDBusMethodInvocation *invocation,
               const gchar *path,
               MMIfaceModemMessaging *self)
{
    HandleDeleteContext *ctx;

    ctx = g_new (HandleDeleteContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->path = g_strdup (path);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_MESSAGING,
                             (GAsyncReadyCallback)handle_delete_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemMessaging *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemMessaging *self;
    GVariant *dictionary;
} HandleCreateContext;

static void
handle_create_context_free (HandleCreateContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->dictionary);
    g_free (ctx);
}

static void
handle_create_auth_ready (MMBaseModem *self,
                          GAsyncResult *res,
                          HandleCreateContext *ctx)
{
    MMModemState modem_state = MM_MODEM_STATE_UNKNOWN;
    MMSmsList *list = NULL;
    GError *error = NULL;
    MMSmsProperties *properties;
    MMBaseSms *sms;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot create SMS: device not yet enabled");
        handle_create_context_free (ctx);
        return;
    }

    /* Parse input properties */
    properties = mm_sms_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!properties) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    sms = mm_base_sms_new_from_properties (MM_BASE_MODEM (self),
                                           properties,
                                           &error);
    if (!sms) {
        g_object_unref (properties);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list) {
        g_object_unref (properties);
        g_object_unref (sms);
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot create SMS: missing SMS list");
        handle_create_context_free (ctx);
        return;
    }

    /* Add it to the list */
    mm_sms_list_add_sms (list, sms);

    /* Complete the DBus call */
    mm_gdbus_modem_messaging_complete_create (ctx->skeleton,
                                              ctx->invocation,
                                              mm_base_sms_get_path (sms));
    g_object_unref (sms);

    g_object_unref (properties);
    g_object_unref (list);

    handle_create_context_free (ctx);
}

static gboolean
handle_create (MmGdbusModemMessaging *skeleton,
               GDBusMethodInvocation *invocation,
               GVariant *dictionary,
               MMIfaceModemMessaging *self)
{
    HandleCreateContext *ctx;

    ctx = g_new (HandleCreateContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_MESSAGING,
                             (GAsyncReadyCallback)handle_create_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_list (MmGdbusModemMessaging *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModemMessaging *self)
{
    GStrv paths;
    MMSmsList *list = NULL;
    MMModemState modem_state;

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot list SMS messages: "
                                               "device not yet enabled");
        return TRUE;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot list SMS: missing SMS list");
        return TRUE;
    }

    paths = mm_sms_list_get_paths (list);
    mm_gdbus_modem_messaging_complete_list (skeleton,
                                            invocation,
                                            (const gchar *const *)paths);
    g_strfreev (paths);
    g_object_unref (list);
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_iface_modem_messaging_take_part (MMIfaceModemMessaging *self,
                                    MMSmsPart *sms_part,
                                    MMSmsState state,
                                    MMSmsStorage storage)
{
    MMSmsList *list = NULL;
    GError *error = NULL;
    gboolean added;

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list)
        return FALSE;

    added = mm_sms_list_take_part (list, sms_part, state, storage, &error);
    if (!added) {
        mm_dbg ("Couldn't take part in SMS list: '%s'", error->message);
        g_error_free (error);

        /* If part wasn't taken, we need to free the part ourselves */
        mm_sms_part_free (sms_part);
    }
    g_object_unref (list);

    return added;
}

/*****************************************************************************/

static gboolean
is_storage_supported (GArray *supported,
                      MMSmsStorage preferred,
                      const gchar *action,
                      GError **error)
{
    guint i;

    if (supported) {
        for (i = 0; i < supported->len; i++) {
            if (preferred == g_array_index (supported, MMSmsStorage, i))
                return TRUE;
        }
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_UNSUPPORTED,
                 "Storage '%s' is not supported for %s",
                 mm_sms_storage_get_string (preferred),
                 action);
    return FALSE;
}

gboolean
mm_iface_modem_messaging_is_storage_supported_for_storing (MMIfaceModemMessaging *self,
                                                           MMSmsStorage storage,
                                                           GError **error)
{
    /* mem2 is for storing */
    return is_storage_supported ((get_storage_context (self))->supported_mem2,
                                 storage,
                                 "storing",
                                 error);
}

gboolean
mm_iface_modem_messaging_is_storage_supported_for_receiving (MMIfaceModemMessaging *self,
                                                             MMSmsStorage storage,
                                                             GError **error)
{
    /* mem3 is for receiving */
    return is_storage_supported ((get_storage_context (self))->supported_mem3,
                                 storage,
                                 "receiving",
                                 error);
}

/*****************************************************************************/

static void
update_message_list (MmGdbusModemMessaging *skeleton,
                     MMSmsList *list)
{
    gchar **paths;

    paths = mm_sms_list_get_paths (list);
    mm_gdbus_modem_messaging_set_messages (skeleton, (const gchar *const *)paths);
    g_strfreev (paths);
}

static void
sms_added (MMSmsList *list,
           const gchar *sms_path,
           gboolean received,
           MmGdbusModemMessaging *skeleton)
{
    mm_dbg ("Added %s SMS at '%s'",
            received ? "received" : "local",
            sms_path);
    update_message_list (skeleton, list);
    mm_gdbus_modem_messaging_emit_added (skeleton, sms_path, received);
}

static void
sms_deleted (MMSmsList *list,
             const gchar *sms_path,
             MmGdbusModemMessaging *skeleton)
{
    mm_dbg ("Deleted SMS at '%s'", sms_path);
    update_message_list (skeleton, list);
    mm_gdbus_modem_messaging_emit_deleted (skeleton, sms_path);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModemMessaging *self;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemMessaging *skeleton;
};

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_messaging_disable_finish (MMIfaceModemMessaging *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                  GAsyncResult *res,
                                  DisablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        disabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
cleanup_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                  GAsyncResult *res,
                                  DisablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        disabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->disable_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* Clear SMS list */
        g_object_set (ctx->self,
                      MM_IFACE_MODEM_MESSAGING_SMS_LIST, NULL,
                      NULL);

        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_messaging_disable (MMIfaceModemMessaging *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_messaging_disable);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        disabling_context_complete_and_free (ctx);
        return;
    }

    interface_disabling_step (ctx);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_SMS_FORMAT,
    ENABLING_STEP_STORAGE_DEFAULTS,
    ENABLING_STEP_LOAD_INITIAL_SMS_PARTS,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModemMessaging *self;
    EnablingStep step;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MmGdbusModemMessaging *skeleton;
    guint mem1_storage_index;
};

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
enabling_context_complete_and_free_if_cancelled (EnablingContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface enabling cancelled");
    enabling_context_complete_and_free (ctx);
    return TRUE;
}

gboolean
mm_iface_modem_messaging_enable_finish (MMIfaceModemMessaging *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_sms_format_ready (MMIfaceModemMessaging *self,
                        GAsyncResult *res,
                        EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->setup_sms_format_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void load_initial_sms_parts_from_storages (EnablingContext *ctx);

static void
load_initial_sms_parts_ready (MMIfaceModemMessaging *self,
                              GAsyncResult *res,
                              EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->load_initial_sms_parts_finish (self, res, &error);
    if (error) {
        StorageContext *storage_ctx;

        storage_ctx = get_storage_context (ctx->self);
        mm_dbg ("Couldn't load SMS parts from storage '%s': '%s'",
                mm_sms_storage_get_string (g_array_index (storage_ctx->supported_mem1,
                                                          MMSmsStorage,
                                                          ctx->mem1_storage_index)),
                error->message);
        g_error_free (error);
    }

    /* Go on with the storage iteration */
    ctx->mem1_storage_index++;
    load_initial_sms_parts_from_storages (ctx);
}

static void
set_default_storage_ready (MMIfaceModemMessaging *self,
                           GAsyncResult *res,
                           EnablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->set_default_storage_finish (self, res, &error)) {
        mm_warn ("Could not set default storage: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on with next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
load_initial_sms_parts_from_storages (EnablingContext *ctx)
{
    gboolean all_loaded = FALSE;
    StorageContext *storage_ctx;

    storage_ctx = get_storage_context (ctx->self);

    if (!storage_ctx->supported_mem1 || ctx->mem1_storage_index >= storage_ctx->supported_mem1->len)
        all_loaded = TRUE;
    /* We'll skip the 'MT' storage, as that is a combination of 'SM' and 'ME'; but only if
     * this is not the only one in the list. */
    else if ((g_array_index (storage_ctx->supported_mem1,
                             MMSmsStorage,
                             ctx->mem1_storage_index) == MM_SMS_STORAGE_MT) &&
             (storage_ctx->supported_mem1->len > 1)) {
        ctx->mem1_storage_index++;
        if (ctx->mem1_storage_index >= storage_ctx->supported_mem1->len)
            all_loaded = TRUE;
    }

    if (all_loaded) {
        /* Go on with next step */
        ctx->step++;
        interface_enabling_step (ctx);
        return;
    }

    MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_initial_sms_parts (
        ctx->self,
        g_array_index (storage_ctx->supported_mem1,
                       MMSmsStorage,
                       ctx->mem1_storage_index),
        (GAsyncReadyCallback)load_initial_sms_parts_ready,
        ctx);
    return;
}

static void
setup_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                GAsyncResult *res,
                                EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
enable_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                 GAsyncResult *res,
                                 EnablingContext *ctx)
{
    GError *error = NULL;

    /* Not critical! */
    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error)) {
        mm_dbg ("Couldn't enable unsolicited events: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on with next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static MMSmsStorage
get_best_initial_default_sms_storage (MMIfaceModemMessaging *self)
{
    StorageContext *storage_ctx;
    guint i;
    MMSmsStorage default_storages_preference[] = {
        MM_SMS_STORAGE_MT, /* MT=ME+SM */
        MM_SMS_STORAGE_ME,
        MM_SMS_STORAGE_SM,
        MM_SMS_STORAGE_UNKNOWN
    };

    storage_ctx = get_storage_context (self);

    for (i = 0; default_storages_preference[i] != MM_SMS_STORAGE_UNKNOWN; i++) {
        /* Check if the requested storage is really supported in both mem2 and mem3 */
        if (is_storage_supported (storage_ctx->supported_mem2, default_storages_preference[i], "storing", NULL) &&
            is_storage_supported (storage_ctx->supported_mem3, default_storages_preference[i], "receiving", NULL)) {
            break;
        }
    }

    return default_storages_preference[i];
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (enabling_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case ENABLING_STEP_FIRST: {
        MMSmsList *list;

        list = mm_sms_list_new (MM_BASE_MODEM (ctx->self));
        g_object_set (ctx->self,
                      MM_IFACE_MODEM_MESSAGING_SMS_LIST, list,
                      NULL);

        /* Connect to list's signals */
        g_signal_connect (list,
                          MM_SMS_ADDED,
                          G_CALLBACK (sms_added),
                          ctx->skeleton);
        g_signal_connect (list,
                          MM_SMS_DELETED,
                          G_CALLBACK (sms_deleted),
                          ctx->skeleton);

        g_object_unref (list);

        /* Fall down to next step */
        ctx->step++;
    }

    case ENABLING_STEP_SETUP_SMS_FORMAT:
        /* Allow setting SMS format to use */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_sms_format &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_sms_format_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_sms_format (
                ctx->self,
                (GAsyncReadyCallback)setup_sms_format_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_STORAGE_DEFAULTS:
        /* Set storage defaults */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->set_default_storage &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->set_default_storage_finish) {
            MMSmsStorage default_storage;

            default_storage = get_best_initial_default_sms_storage (ctx->self);

            /* Already bound to the 'default-storage' property in the skeleton */
            g_object_set (ctx->self,
                          MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE, default_storage,
                          NULL);

            if (default_storage != MM_SMS_STORAGE_UNKNOWN) {
                MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->set_default_storage (
                    ctx->self,
                    default_storage,
                    (GAsyncReadyCallback)set_default_storage_ready,
                    ctx);
                return;
            }

            mm_info ("Cannot set default storage, none of the suggested ones supported");
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LOAD_INITIAL_SMS_PARTS:
        /* Allow loading the initial list of SMS parts */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_initial_sms_parts &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_initial_sms_parts_finish) {
            load_initial_sms_parts_from_storages (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->enable_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_messaging_enable (MMIfaceModemMessaging *self,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_messaging_enable);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        enabling_context_complete_and_free (ctx);
        return;
    }

    interface_enabling_step (ctx);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LOAD_SUPPORTED_STORAGES,
    INITIALIZATION_STEP_INIT_CURRENT_STORAGES,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModemMessaging *self;
    MmGdbusModemMessaging *skeleton;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    InitializationStep step;
};

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
initialization_context_complete_and_free_if_cancelled (InitializationContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface initialization cancelled");
    initialization_context_complete_and_free (ctx);
    return TRUE;
}

static void
skip_unknown_storages (GArray *mem)
{
    guint i = mem->len;

    if (!mem)
        return;

    /* Remove UNKNOWN from the list of supported storages */
    while (i-- > 0) {
        if (g_array_index (mem, MMSmsStorage, i) == MM_SMS_STORAGE_UNKNOWN)
            g_array_remove_index (mem, i);
    }
}

static void
load_supported_storages_ready (MMIfaceModemMessaging *self,
                               GAsyncResult *res,
                               InitializationContext *ctx)
{
    StorageContext *storage_ctx;
    GError *error = NULL;

    storage_ctx = get_storage_context (self);
    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->load_supported_storages_finish (
            self,
            res,
            &storage_ctx->supported_mem1,
            &storage_ctx->supported_mem2,
            &storage_ctx->supported_mem3,
            &error)) {
        mm_dbg ("Couldn't load supported storages: '%s'", error->message);
        g_error_free (error);
    } else {
        gchar *mem1;
        gchar *mem2;
        gchar *mem3;
        GArray *supported_storages;
        guint i;

        /* Never add unknown storages */
        skip_unknown_storages (storage_ctx->supported_mem1);
        skip_unknown_storages (storage_ctx->supported_mem2);
        skip_unknown_storages (storage_ctx->supported_mem3);

        mem1 = mm_common_build_sms_storages_string ((MMSmsStorage *)storage_ctx->supported_mem1->data,
                                                    storage_ctx->supported_mem1->len);
        mem2 = mm_common_build_sms_storages_string ((MMSmsStorage *)storage_ctx->supported_mem2->data,
                                                    storage_ctx->supported_mem2->len);
        mem3 = mm_common_build_sms_storages_string ((MMSmsStorage *)storage_ctx->supported_mem3->data,
                                                    storage_ctx->supported_mem3->len);

        mm_dbg ("Supported storages loaded:");
        mm_dbg ("  mem1 (list/read/delete) storages: '%s'", mem1);
        mm_dbg ("  mem2 (write/send) storages:       '%s'", mem2);
        mm_dbg ("  mem3 (reception) storages:        '%s'", mem3);
        g_free (mem1);
        g_free (mem2);
        g_free (mem3);

        /* We set in the interface the list of storages which are allowed for
         * both write/send and receive */
        supported_storages = g_array_sized_new (FALSE, FALSE, sizeof (guint32), storage_ctx->supported_mem2->len);
        for (i = 0; i < storage_ctx->supported_mem2->len; i++) {
            gboolean found = FALSE;
            guint j;

            for (j = 0; j < storage_ctx->supported_mem3->len && !found; j++) {
                if (g_array_index (storage_ctx->supported_mem3, MMSmsStorage, j) ==
                    g_array_index (storage_ctx->supported_mem2, MMSmsStorage, i))
                    found = TRUE;
            }

            if (found) {
                guint32 val;

                val = g_array_index (storage_ctx->supported_mem2, MMSmsStorage, i);
                g_array_append_val (supported_storages, val);
            }
        }

        mm_gdbus_modem_messaging_set_supported_storages (
            ctx->skeleton,
            mm_common_sms_storages_garray_to_variant (supported_storages));
        g_array_unref (supported_storages);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
check_support_ready (MMIfaceModemMessaging *self,
                     GAsyncResult *res,
                     InitializationContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->check_support_finish (self,
                                                                              res,
                                                                              &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_dbg ("Messaging support check failed: '%s'", error->message);
            g_error_free (error);
        }
    } else {
        /* Messaging is supported! */
        g_object_set_qdata (G_OBJECT (self),
                            supported_quark,
                            GUINT_TO_POINTER (TRUE));
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
init_current_storages_ready (MMIfaceModemMessaging *self,
                             GAsyncResult *res,
                             InitializationContext *ctx)
{
    StorageContext *storage_ctx;
    GError *error = NULL;

    storage_ctx = get_storage_context (self);
    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->init_current_storages_finish (
            self,
            res,
            &error)) {
        mm_dbg ("Couldn't initialize current storages: '%s'", error->message);
        g_error_free (error);
    } else {
        mm_dbg ("Current storages initialized");
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (initialization_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!support_checked_quark))
            support_checked_quark = (g_quark_from_static_string (
                                         SUPPORT_CHECKED_TAG));
        if (G_UNLIKELY (!supported_quark))
            supported_quark = (g_quark_from_static_string (
                                   SUPPORTED_TAG));

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CHECK_SUPPORT:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   support_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                support_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support it */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                supported_quark,
                                GUINT_TO_POINTER (FALSE));

            if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->check_support &&
                MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->check_support_finish) {
                MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->check_support (
                    ctx->self,
                    (GAsyncReadyCallback)check_support_ready,
                    ctx);
                return;
            }

            /* If there is no implementation to check support, assume we DON'T
             * support it. */
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   supported_quark))) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Messaging not supported");
            initialization_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LOAD_SUPPORTED_STORAGES:
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_supported_storages &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_supported_storages_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_supported_storages (
                ctx->self,
                (GAsyncReadyCallback)load_supported_storages_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_INIT_CURRENT_STORAGES:
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->init_current_storages &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->init_current_storages_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->init_current_storages (
                ctx->self,
                (GAsyncReadyCallback)init_current_storages_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-create",
                          G_CALLBACK (handle_create),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-delete",
                          G_CALLBACK (handle_delete),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-list",
                          G_CALLBACK (handle_list),
                          ctx->self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_messaging (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                      MM_GDBUS_MODEM_MESSAGING (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_messaging_initialize_finish (MMIfaceModemMessaging *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_messaging_initialize (MMIfaceModemMessaging *self,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemMessaging *skeleton = NULL;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_messaging_skeleton_new ();
        mm_gdbus_modem_messaging_set_supported_storages (skeleton, NULL);

        /* Bind our Default messaging property */
        g_object_bind_property (self, MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE,
                                skeleton, "default-storage",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_messaging_initialize);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    interface_initialization_step (ctx);
}

void
mm_iface_modem_messaging_shutdown (MMIfaceModemMessaging *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_messaging (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_messaging_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON,
                              "Messaging DBus skeleton",
                              "DBus skeleton for the Messaging interface",
                              MM_GDBUS_TYPE_MODEM_MESSAGING_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_MESSAGING_SMS_LIST,
                              "SMS list",
                              "List of SMS objects managed in the interface",
                              MM_TYPE_SMS_LIST,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_MESSAGING_SMS_PDU_MODE,
                               "PDU mode",
                               "Whether PDU mode should be used",
                               FALSE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE,
                            "SMS default storage",
                            "Default storage to be used when storing/receiving SMS messages",
                            MM_TYPE_SMS_STORAGE,
                            MM_SMS_STORAGE_ME,
                            G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_messaging_get_type (void)
{
    static GType iface_modem_messaging_type = 0;

    if (!G_UNLIKELY (iface_modem_messaging_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemMessaging), /* class_size */
            iface_modem_messaging_init,     /* base_init */
            NULL,                           /* base_finalize */
        };

        iface_modem_messaging_type = g_type_register_static (G_TYPE_INTERFACE,
                                                             "MMIfaceModemMessaging",
                                                             &info,
                                                             0);

        g_type_interface_add_prerequisite (iface_modem_messaging_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_messaging_type;
}
