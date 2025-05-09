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
#include "mm-error-helpers.h"
#include "mm-log-object.h"
#include "mm-broadband-modem.h"

#define SUPPORT_CHECKED_TAG "messaging-support-checked-tag"
#define SUPPORTED_TAG       "messaging-supported-tag"
#define STORAGE_CONTEXT_TAG "messaging-storage-context-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;
static GQuark storage_context_quark;

G_DEFINE_INTERFACE (MMIfaceModemMessaging, mm_iface_modem_messaging, MM_TYPE_IFACE_MODEM)

/*****************************************************************************/

void
mm_iface_modem_messaging_bind_simple_status (MMIfaceModemMessaging *self,
                                             MMSimpleStatus *status)
{
}

/*****************************************************************************/

void
mm_iface_modem_messaging_lock_storages (MMIfaceModemMessaging *self,
                                        MMSmsStorage           mem1,
                                        MMSmsStorage           mem2,
                                        GAsyncReadyCallback    callback,
                                        gpointer               user_data)
{
    g_assert (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->lock_storages != NULL);

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->lock_storages (self,
                                                              mem1,
                                                              mem2,
                                                              callback,
                                                              user_data);
}

gboolean
mm_iface_modem_messaging_lock_storages_finish (MMIfaceModemMessaging  *self,
                                               GAsyncResult           *res,
                                               GError                **error)

{
    g_assert (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->lock_storages_finish != NULL);

    return MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->lock_storages_finish (self,
                                                                            res,
                                                                            error);
}

void
mm_iface_modem_messaging_unlock_storages (MMIfaceModemMessaging *self,
                                          gboolean               mem1,
                                          gboolean               mem2)
{
    g_assert (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->unlock_storages != NULL);

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->unlock_storages (self,
                                                                mem1,
                                                                mem2);
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
    gchar                 *path;
} HandleDeleteContext;

static void
handle_delete_context_free (HandleDeleteContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->path);
    g_slice_free (HandleDeleteContext, ctx);
}

static void
handle_delete_ready (MMSmsList           *list,
                     GAsyncResult        *res,
                     HandleDeleteContext *ctx)
{
    GError *error = NULL;

    if (!mm_sms_list_delete_sms_finish (list, res, &error)) {
        mm_obj_warn (ctx->self, "failed deleting SMS message '%s': %s", ctx->path, error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_obj_info (ctx->self, "deleted SMS message '%s'", ctx->path);
        mm_gdbus_modem_messaging_complete_delete (ctx->skeleton, ctx->invocation);
    }
    handle_delete_context_free (ctx);
}

static void
handle_delete_auth_ready (MMIfaceAuth         *auth,
                          GAsyncResult        *res,
                          HandleDeleteContext *ctx)
{
    MMIfaceModemMessaging *self = MM_IFACE_MODEM_MESSAGING (auth);
    g_autoptr(MMSmsList)   list = NULL;
    GError                *error = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    /* We do allow deleting SMS messages while enabling or disabling, it doesn't
     * interfere with the state transition logic to do so. The main reason to allow
     * this is that during modem enabling we're emitting "Added" signals before we
     * reach the enabled state, and so users listening to the signal may want to
     * delete the SMS message as soon as it's read. */
    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_DISABLING)) {
        handle_delete_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                                        "Cannot delete SMS: missing SMS list");
        handle_delete_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to delete SMS message '%s'...", ctx->path);
    mm_sms_list_delete_sms (list,
                            ctx->path,
                            (GAsyncReadyCallback)handle_delete_ready,
                            ctx);
}

static gboolean
handle_delete (MmGdbusModemMessaging *skeleton,
               GDBusMethodInvocation *invocation,
               const gchar           *path,
               MMIfaceModemMessaging *self)
{
    HandleDeleteContext *ctx;

    ctx = g_slice_new0 (HandleDeleteContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->path = g_strdup (path);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
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
    GVariant              *dictionary;
} HandleCreateContext;

static void
handle_create_context_free (HandleCreateContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->dictionary);
    g_slice_free (HandleCreateContext, ctx);
}

static void
handle_create_auth_ready (MMIfaceAuth         *auth,
                          GAsyncResult        *res,
                          HandleCreateContext *ctx)
{
    MMIfaceModemMessaging      *self = MM_IFACE_MODEM_MESSAGING (auth);
    GError                     *error = NULL;
    g_autoptr(MMSmsList)        list = NULL;
    g_autoptr(MMSmsProperties)  properties = NULL;
    g_autoptr(MMBaseSms)        sms = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_create_context_free (ctx);
        return;
    }

    /* Parse input properties */
    properties = mm_sms_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!properties) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    sms = mm_broadband_modem_create_sms (MM_BROADBAND_MODEM (self));
    if (!mm_base_sms_init_from_properties (sms, properties, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                                        "Cannot create SMS: missing SMS list");
        handle_create_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to create SMS message...");
    mm_sms_list_add_sms (list, sms);
    mm_gdbus_modem_messaging_complete_create (ctx->skeleton,
                                              ctx->invocation,
                                              mm_base_sms_get_path (sms));
    mm_obj_info (self, "created SMS message: %s", mm_base_sms_get_path (sms));
    handle_create_context_free (ctx);
}

static gboolean
handle_create (MmGdbusModemMessaging *skeleton,
               GDBusMethodInvocation *invocation,
               GVariant              *dictionary,
               MMIfaceModemMessaging *self)
{
    HandleCreateContext *ctx;

    ctx = g_slice_new0 (HandleCreateContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
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
    g_auto(GStrv)        paths = NULL;
    g_autoptr(MMSmsList) list = NULL;

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              invocation,
                                                              MM_MODEM_STATE_ENABLED))
        return TRUE;

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (!list) {
        mm_dbus_method_invocation_return_error_literal (invocation, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                                        "Cannot list SMS: missing SMS list");
        return TRUE;
    }

    mm_obj_info (self, "processing user request to list SMS messages...");
    paths = mm_sms_list_get_paths (list);
    mm_gdbus_modem_messaging_complete_list (skeleton,
                                            invocation,
                                            (const gchar *const *)paths);
    mm_obj_info (self, "reported %u SMS messages available", paths ? g_strv_length (paths) : 0);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemMessaging *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemMessaging *self;
    MMSmsStorage           storage;
} HandleSetDefaultStorageContext;

static void
handle_set_default_storage_context_free (HandleSetDefaultStorageContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetDefaultStorageContext, ctx);
}
static void
handle_set_default_storage_ready (MMIfaceModemMessaging          *self,
                                  GAsyncResult                   *res,
                                  HandleSetDefaultStorageContext *ctx)
{
    GError               *error = NULL;
    g_autoptr(MMSmsList)  list = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->set_default_storage_finish (self, res, &error)) {
        mm_obj_warn (self, "could not set default storage: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_default_storage_context_free (ctx);
        return;
    }

    g_object_set (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE, ctx->storage,
                  NULL);

    /* Update default storage in all SMSs too */
    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    if (list)
        mm_sms_list_set_default_storage (list, ctx->storage);

    mm_obj_info (self, "set the default storage successfully");
    mm_gdbus_modem_messaging_complete_set_default_storage (ctx->skeleton, ctx->invocation);
    handle_set_default_storage_context_free (ctx);
}

static void
handle_set_default_storage_auth_ready (MMIfaceAuth                    *auth,
                                       GAsyncResult                   *res,
                                       HandleSetDefaultStorageContext *ctx)
{
    MMIfaceModemMessaging *self = MM_IFACE_MODEM_MESSAGING (auth);
    GError *error = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_default_storage_context_free (ctx);
        return;
    }

    /* Check if the requested storage is supported or not */
    if (!mm_iface_modem_messaging_is_storage_supported_for_storing (ctx->self, ctx->storage, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_default_storage_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to set default storage '%s'...",
                 mm_sms_storage_get_string (ctx->storage));

    if (!(MM_IFACE_MODEM_MESSAGING_GET_IFACE (ctx->self)->set_default_storage) ||
        !(MM_IFACE_MODEM_MESSAGING_GET_IFACE (ctx->self)->set_default_storage_finish)) {
        mm_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set default storage: not supported");
        handle_set_default_storage_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (ctx->self)->set_default_storage (
        ctx->self,
        ctx->storage,
        (GAsyncReadyCallback)handle_set_default_storage_ready,
        ctx);
}

static gboolean
handle_set_default_storage (MmGdbusModemMessaging *skeleton,
                            GDBusMethodInvocation *invocation,
                            guint32                storage,
                            MMIfaceModemMessaging *self)
{
    HandleSetDefaultStorageContext *ctx;

    ctx = g_slice_new0 (HandleSetDefaultStorageContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->storage    = (MMSmsStorage)storage;

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_MESSAGING,
                             (GAsyncReadyCallback)handle_set_default_storage_auth_ready,
                             ctx);
    return TRUE;
}
/*****************************************************************************/

gboolean
mm_iface_modem_messaging_take_part (MMIfaceModemMessaging  *self,
                                    MMBaseSms              *uninitialized_sms,
                                    MMSmsPart              *sms_part,
                                    MMSmsState              state,
                                    MMSmsStorage            storage,
                                    GError                **error)
{
    g_autoptr(MMSmsList) list = NULL;
    gboolean             added = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);

    if (list) {
        added = mm_sms_list_take_part (list, uninitialized_sms, sms_part, state, storage, error);
        if (!added)
            g_prefix_error (error, "couldn't take part in SMS list: ");
    }

    /* If part wasn't taken, we need to free the part and SMS ourselves */
    if (!added)
        mm_sms_part_free (sms_part);

    /* Always drop original ref to the uninialized SMS, as MMSmsList will have
     * taken a reference to the SMS if it wants to keep it around.
     */
    g_object_unref (uninitialized_sms);

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

MMSmsStorage
mm_iface_modem_messaging_get_default_storage (MMIfaceModemMessaging *self)
{
    MMSmsStorage storage = MM_SMS_STORAGE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE, &storage,
                  NULL);
    g_assert (storage != MM_SMS_STORAGE_UNKNOWN);
    return storage;
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

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
}

static void
sms_added (MMSmsList             *list,
           const gchar           *sms_path,
           gboolean               received,
           MmGdbusModemMessaging *skeleton)
{
    update_message_list (skeleton, list);
    mm_gdbus_modem_messaging_emit_added (skeleton, sms_path, received);
}

static void
sms_deleted (MMSmsList             *list,
             const gchar           *sms_path,
             MmGdbusModemMessaging *skeleton)
{
    update_message_list (skeleton, list);
    mm_gdbus_modem_messaging_emit_deleted (skeleton, sms_path);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep step;
    MmGdbusModemMessaging *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_messaging_disable_finish (MMIfaceModemMessaging *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->disable_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
cleanup_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
interface_disabling_step (GTask *task)
{
    MMIfaceModemMessaging *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->cleanup_unsolicited_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_LAST:
        /* Clear SMS list */
        g_object_set (self,
                      MM_IFACE_MODEM_MESSAGING_SMS_LIST, NULL,
                      NULL);

        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_messaging_disable (MMIfaceModemMessaging *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    DisablingContext *ctx;
    GTask *task;

    ctx = g_new0 (DisablingContext, 1);
    ctx->step = DISABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_disabling_step (task);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (GTask *task);

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
    EnablingStep step;
    MmGdbusModemMessaging *skeleton;
    guint mem1_storage_index;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_messaging_enable_finish (MMIfaceModemMessaging *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_sms_format_ready (MMIfaceModemMessaging *self,
                        GAsyncResult *res,
                        GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_sms_format_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void load_initial_sms_parts_from_storages (GTask *task);

static void
load_initial_sms_parts_ready (MMIfaceModemMessaging *self,
                              GAsyncResult *res,
                              GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_initial_sms_parts_finish (self, res, &error);
    if (error) {
        StorageContext *storage_ctx;

        storage_ctx = get_storage_context (self);
        mm_obj_dbg (self, "couldn't load SMS parts from storage '%s': %s",
                    mm_sms_storage_get_string (g_array_index (storage_ctx->supported_mem1,
                                                              MMSmsStorage,
                                                              ctx->mem1_storage_index)),
                    error->message);
        g_error_free (error);
    }

    /* Go on with the storage iteration */
    ctx->mem1_storage_index++;
    load_initial_sms_parts_from_storages (task);
}

static void
set_default_storage_ready (MMIfaceModemMessaging *self,
                           GAsyncResult *res,
                           GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->set_default_storage_finish (self, res, &error)) {
        mm_obj_warn (self, "could not set default storage: %s", error->message);
        g_error_free (error);
    }

    /* Go on with next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
load_initial_sms_parts_from_storages (GTask *task)
{
    MMIfaceModemMessaging *self;
    EnablingContext *ctx;
    gboolean all_loaded = FALSE;
    StorageContext *storage_ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);
    storage_ctx = get_storage_context (self);

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
        interface_enabling_step (task);
        return;
    }

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_initial_sms_parts (
        self,
        g_array_index (storage_ctx->supported_mem1,
                       MMSmsStorage,
                       ctx->mem1_storage_index),
        (GAsyncReadyCallback)load_initial_sms_parts_ready,
        task);
}

static void
setup_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_events_ready (MMIfaceModemMessaging *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    /* Not critical! */
    if (!MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->enable_unsolicited_events_finish (self, res, &error)) {
        mm_obj_dbg (self, "couldn't enable unsolicited events: %s", error->message);
        g_error_free (error);
    }

    /* Go on with next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
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

static MMSmsStorage
get_single_default_sms_storage (MMIfaceModemMessaging *self)
{
    StorageContext *storage_ctx;

    storage_ctx = get_storage_context (self);

    /* If there is one single storage supported for storing and receiving, just
     * use that one. */
    if (storage_ctx->supported_mem2 && storage_ctx->supported_mem2->len == 1 &&
        storage_ctx->supported_mem3 && storage_ctx->supported_mem3->len == 1) {
        MMSmsStorage storing_default;

        storing_default = g_array_index (storage_ctx->supported_mem2, MMSmsStorage, 0);
        if (storing_default == g_array_index (storage_ctx->supported_mem3, MMSmsStorage, 0))
            return storing_default;
    }

    return MM_SMS_STORAGE_UNKNOWN;
}

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModemMessaging *self;
    EnablingContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case ENABLING_STEP_FIRST: {
        MMSmsList *list;

        list = mm_sms_list_new (G_OBJECT (self));
        g_object_set (self,
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

        ctx->step++;
    } /* fall through */

    case ENABLING_STEP_SETUP_SMS_FORMAT:
        /* Allow setting SMS format to use */
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_sms_format &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_sms_format_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_sms_format (
                self,
                (GAsyncReadyCallback)setup_sms_format_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_STORAGE_DEFAULTS: {
        MMSmsStorage default_storage;

        /* Get default storage that is set by init_current_storage */
        g_object_get (self,
                      MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE, &default_storage,
                      NULL);

        /* if current_storage is set to unknown, get the best initial
         * default_storage */
        if (default_storage == MM_SMS_STORAGE_UNKNOWN) {
            /* Is there only one single storage supported? if so, we don't care if
             * setting default storage is implemented or not. */
            default_storage = get_single_default_sms_storage (self);
            if (default_storage == MM_SMS_STORAGE_UNKNOWN)
                default_storage = get_best_initial_default_sms_storage (self);

            /* Already bound to the 'default-storage' property in the skeleton */
            g_object_set (self,
                          MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE, default_storage,
                          NULL);
        }

        if (default_storage == MM_SMS_STORAGE_UNKNOWN)
            mm_obj_warn (self, "cannot set default storage, none of the suggested ones supported");
        else if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->set_default_storage &&
                 MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->set_default_storage_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->set_default_storage (
                self,
                default_storage,
                (GAsyncReadyCallback)set_default_storage_ready,
                task);
            return;
        }

        ctx->step++;
    } /* fall through */

    case ENABLING_STEP_LOAD_INITIAL_SMS_PARTS:
        /* Allow loading the initial list of SMS parts */
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_initial_sms_parts &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_initial_sms_parts_finish) {
            load_initial_sms_parts_from_storages (task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
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
    GTask *task;

    ctx = g_new0 (EnablingContext, 1);
    ctx->step = ENABLING_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_enabling_step (task);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LOAD_SUPPORTED_STORAGES,
    INITIALIZATION_STEP_INIT_CURRENT_STORAGES,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModemMessaging *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
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
                               GTask *task)
{
    InitializationContext *ctx;
    StorageContext *storage_ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    storage_ctx = get_storage_context (self);
    if (!MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_supported_storages_finish (
            self,
            res,
            &storage_ctx->supported_mem1,
            &storage_ctx->supported_mem2,
            &storage_ctx->supported_mem3,
            &error)) {
        mm_obj_dbg (self, "couldn't load supported storages: %s", error->message);
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

        mem1 = mm_common_build_sms_storages_string ((MMSmsStorage *)(gpointer)storage_ctx->supported_mem1->data,
                                                    storage_ctx->supported_mem1->len);
        mem2 = mm_common_build_sms_storages_string ((MMSmsStorage *)(gpointer)storage_ctx->supported_mem2->data,
                                                    storage_ctx->supported_mem2->len);
        mem3 = mm_common_build_sms_storages_string ((MMSmsStorage *)(gpointer)storage_ctx->supported_mem3->data,
                                                    storage_ctx->supported_mem3->len);

        mm_obj_dbg (self, "supported storages loaded:");
        mm_obj_dbg (self, "  mem1 (list/read/delete) storages: '%s'", mem1);
        mm_obj_dbg (self, "  mem2 (write/send) storages:       '%s'", mem2);
        mm_obj_dbg (self, "  mem3 (reception) storages:        '%s'", mem3);
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
    interface_initialization_step (task);
}

static void
check_support_ready (MMIfaceModemMessaging *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->check_support_finish (self, res, &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "messaging support check failed: %s", error->message);
            g_error_free (error);
        }
    } else {
        /* Messaging is supported! */
        g_object_set_qdata (G_OBJECT (self),
                            supported_quark,
                            GUINT_TO_POINTER (TRUE));
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

static void
init_current_storages_ready (MMIfaceModemMessaging *self,
                             GAsyncResult *res,
                             GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    MMSmsStorage current_storage = MM_SMS_STORAGE_UNKNOWN;

    if (!MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->init_current_storages_finish (
            self,
            res,
            &current_storage,
            &error)) {
        mm_obj_dbg (self, "couldn't initialize current storages: %s", error->message);
        g_error_free (error);
    } else {
        g_object_set (self,
                      MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE, current_storage,
                      NULL);
        mm_obj_dbg (self, "current storages initialized");
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModemMessaging *self;
    InitializationContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!support_checked_quark))
            support_checked_quark = (g_quark_from_static_string (
                                         SUPPORT_CHECKED_TAG));
        if (G_UNLIKELY (!supported_quark))
            supported_quark = (g_quark_from_static_string (
                                   SUPPORTED_TAG));

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_CHECK_SUPPORT:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                   support_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (self),
                                support_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support it */
            g_object_set_qdata (G_OBJECT (self),
                                supported_quark,
                                GUINT_TO_POINTER (FALSE));

            if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->check_support &&
                MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->check_support (
                    self,
                    (GAsyncReadyCallback)check_support_ready,
                    task);
                return;
            }

            /* If there is no implementation to check support, assume we DON'T
             * support it. */
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                   supported_quark))) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "Messaging not supported");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LOAD_SUPPORTED_STORAGES:
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_supported_storages &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_supported_storages_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->load_supported_storages (
                self,
                (GAsyncReadyCallback)load_supported_storages_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_INIT_CURRENT_STORAGES:
        if (MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->init_current_storages &&
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->init_current_storages_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_IFACE (self)->init_current_storages (
                self,
                (GAsyncReadyCallback)init_current_storages_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-create",
                          G_CALLBACK (handle_create),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-delete",
                          G_CALLBACK (handle_delete),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-list",
                          G_CALLBACK (handle_list),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-default-storage",
                          G_CALLBACK (handle_set_default_storage),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_messaging (MM_GDBUS_OBJECT_SKELETON (self),
                                                      MM_GDBUS_MODEM_MESSAGING (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_messaging_initialize_finish (MMIfaceModemMessaging *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_messaging_initialize (MMIfaceModemMessaging *self,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemMessaging *skeleton = NULL;
    GTask *task;

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
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
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
mm_iface_modem_messaging_default_init (MMIfaceModemMessagingInterface *iface)
{
    static gsize initialized = 0;

    if (!g_once_init_enter (&initialized))
        return;

    /* Properties */
    g_object_interface_install_property (
        iface,
        g_param_spec_object (MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON,
                             "Messaging DBus skeleton",
                             "DBus skeleton for the Messaging interface",
                             MM_GDBUS_TYPE_MODEM_MESSAGING_SKELETON,
                             G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_object (MM_IFACE_MODEM_MESSAGING_SMS_LIST,
                             "SMS list",
                             "List of SMS objects managed in the interface",
                             MM_TYPE_SMS_LIST,
                             G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_boolean (MM_IFACE_MODEM_MESSAGING_SMS_PDU_MODE,
                              "PDU mode",
                              "Whether PDU mode should be used",
                              FALSE,
                              G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_enum (MM_IFACE_MODEM_MESSAGING_SMS_DEFAULT_STORAGE,
                           "SMS default storage",
                           "Default storage to be used when storing/receiving SMS messages",
                           MM_TYPE_SMS_STORAGE,
                           MM_SMS_STORAGE_ME,
                           G_PARAM_READWRITE));

    g_once_init_leave (&initialized, 1);
}
