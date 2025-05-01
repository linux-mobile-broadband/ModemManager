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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>n
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-cell-broadcast.h"
#include "mm-cbm-list.h"
#include "mm-log-object.h"
#include "mm-error-helpers.h"
#include "mm-modem-helpers.h"

#define SUPPORT_CHECKED_TAG "cell-broadcast-support-checked-tag"
#define SUPPORTED_TAG       "cell-broadcast-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

G_DEFINE_INTERFACE (MMIfaceModemCellBroadcast, mm_iface_modem_cell_broadcast, MM_TYPE_IFACE_MODEM)

/*****************************************************************************/

void
mm_iface_modem_cell_broadcast_bind_simple_status (MMIfaceModemCellBroadcast *self,
                                                  MMSimpleStatus *status)
{
}

/*****************************************************************************/


typedef struct {
    MmGdbusModemCellBroadcast *skeleton;
    GDBusMethodInvocation     *invocation;
    MMIfaceModemCellBroadcast *self;
    GArray                    *channels;
} HandleSetChannelsCellBroadcastContext;

static void
handle_set_channels_context_free (HandleSetChannelsCellBroadcastContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_array_unref (ctx->channels);
    g_slice_free (HandleSetChannelsCellBroadcastContext, ctx);
}

static void
set_channels_ready (MMIfaceModemCellBroadcast             *self,
                    GAsyncResult                          *res,
                    HandleSetChannelsCellBroadcastContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->set_channels_finish (self, res, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem_cell_broadcast_set_channels (ctx->skeleton,
                                                    mm_common_cell_broadcast_channels_garray_to_variant (ctx->channels));
        mm_gdbus_modem_cell_broadcast_complete_set_channels (ctx->skeleton, ctx->invocation);
    }

    handle_set_channels_context_free (ctx);
}

static void
handle_set_channels_auth_ready (MMIfaceAuth                           *_self,
                                GAsyncResult                          *res,
                                HandleSetChannelsCellBroadcastContext *ctx)
{
    MMIfaceModemCellBroadcast *self = MM_IFACE_MODEM_CELL_BROADCAST (_self);
    GError *error = NULL;

    if (!mm_iface_auth_authorize_finish (_self, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_channels_context_free (ctx);
        return;
    }

    /* Validate channels (either number or range) */
    if (!mm_validate_cbs_channels (ctx->channels, &error)) {
        mm_dbus_method_invocation_return_gerror (ctx->invocation, error);
        handle_set_channels_context_free (ctx);
        return;
    }

    /* Check if plugin implements it */
    if (!MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->set_channels ||
        !MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->set_channels_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Cannot set channels: not implemented");
        handle_set_channels_context_free (ctx);
        return;
    }

    /* Request to change channels */
    mm_obj_info (self, "processing user request to set channels...");
    MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->set_channels (ctx->self,
                                                                  ctx->channels,
                                                                  (GAsyncReadyCallback)set_channels_ready,
                                                                  ctx);
}

static gboolean
handle_set_channels (MmGdbusModemCellBroadcast *skeleton,
                     GDBusMethodInvocation     *invocation,
                     GVariant                  *channels,
                     MMIfaceModemCellBroadcast *self)
{
    HandleSetChannelsCellBroadcastContext *ctx;

    ctx = g_slice_new0 (HandleSetChannelsCellBroadcastContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->channels = mm_common_cell_broadcast_channels_variant_to_garray (channels);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_channels_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemCellBroadcast *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemCellBroadcast *self;
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
handle_delete_ready (MMCbmList           *list,
                     GAsyncResult        *res,
                     HandleDeleteContext *ctx)
{
    GError *error = NULL;

    if (!mm_cbm_list_delete_cbm_finish (list, res, &error)) {
        mm_obj_warn (ctx->self, "failed deleting CBM message '%s': %s", ctx->path, error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_obj_info (ctx->self, "deleted CBM message '%s'", ctx->path);
        mm_gdbus_modem_cell_broadcast_complete_delete (ctx->skeleton, ctx->invocation);
    }
    handle_delete_context_free (ctx);
}

static void
handle_delete_auth_ready (MMIfaceAuth         *_self,
                          GAsyncResult        *res,
                          HandleDeleteContext *ctx)
{
    MMIfaceModemCellBroadcast *self = MM_IFACE_MODEM_CELL_BROADCAST (_self);
    g_autoptr(MMCbmList)  list = NULL;
    GError               *error = NULL;

    if (!mm_iface_auth_authorize_finish (_self, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    /* We do allow deleting CBMs while enabling or disabling, it doesn't
     * interfere with the state transition logic to do so. The main reason to allow
     * this is that during modem enabling we're emitting "Added" signals before we
     * reach the enabled state, and so users listening to the signal may want to
     * delete the CBM message as soon as it's read. */
    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_DISABLING)) {
        handle_delete_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_CELL_BROADCAST_CBM_LIST, &list,
                  NULL);
    if (!list) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                                        "Cannot delete CBM: missing CBM list");
        handle_delete_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to delete CBM message '%s'...", ctx->path);
    mm_cbm_list_delete_cbm (list,
                            ctx->path,
                            (GAsyncReadyCallback)handle_delete_ready,
                            ctx);
}

static gboolean
handle_delete (MmGdbusModemCellBroadcast *skeleton,
               GDBusMethodInvocation *invocation,
               const gchar           *path,
               MMIfaceModemCellBroadcast *self)
{
    HandleDeleteContext *ctx;

    ctx = g_slice_new0 (HandleDeleteContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->path = g_strdup (path);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_CELL_BROADCAST,
                             (GAsyncReadyCallback)handle_delete_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_list (MmGdbusModemCellBroadcast *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModemCellBroadcast *self)
{
    g_auto(GStrv)        paths = NULL;
    g_autoptr(MMCbmList) list = NULL;

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              invocation,
                                                              MM_MODEM_STATE_ENABLED))
        return TRUE;

    g_object_get (self,
                  MM_IFACE_MODEM_CELL_BROADCAST_CBM_LIST, &list,
                  NULL);
    if (!list) {
        mm_dbus_method_invocation_return_error_literal (invocation, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                                        "Cannot list CBM: missing CBM list");
        return TRUE;
    }

    mm_obj_info (self, "processing user request to list CBM messages...");
    paths = mm_cbm_list_get_paths (list);
    mm_gdbus_modem_cell_broadcast_complete_list (skeleton,
                                                 invocation,
                                                 (const gchar *const *)paths);
    mm_obj_info (self, "reported %u CBM messages available", paths ? g_strv_length (paths) : 0);
    return TRUE;
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModemCellBroadcast *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_cell_broadcast_initialize_finish (MMIfaceModemCellBroadcast *self,
                                                 GAsyncResult *res,
                                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
check_support_ready (MMIfaceModemCellBroadcast *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->check_support_finish (self,
                                                                                  res,
                                                                                  &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "cell broadcast support check failed: %s", error->message);
            g_error_free (error);
        }
    } else {
        /* CellBroadcast is supported! */
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
interface_initialization_step (GTask *task)
{
    MMIfaceModemCellBroadcast *self;
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

            if (MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->check_support &&
                MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->check_support (
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
                                     "CellBroadcast not supported");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-set-channels",
                          G_CALLBACK (handle_set_channels),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-delete",
                          G_CALLBACK (handle_delete),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-list",
                          G_CALLBACK (handle_list),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_cell_broadcast (MM_GDBUS_OBJECT_SKELETON (self),
                                                          MM_GDBUS_MODEM_CELL_BROADCAST (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_cell_broadcast_initialize (MMIfaceModemCellBroadcast *self,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemCellBroadcast *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_CELL_BROADCAST_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_cell_broadcast_skeleton_new ();
        g_object_set (self,
                      MM_IFACE_MODEM_CELL_BROADCAST_DBUS_SKELETON, skeleton,
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
mm_iface_modem_cell_broadcast_shutdown (MMIfaceModemCellBroadcast *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_cell_broadcast (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_CELL_BROADCAST_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
update_message_list (MmGdbusModemCellBroadcast *skeleton,
                     MMCbmList *list)
{
    gchar **paths;

    paths = mm_cbm_list_get_paths (list);
    mm_gdbus_modem_cell_broadcast_set_cell_broadcasts (skeleton, (const gchar *const *)paths);
    g_strfreev (paths);

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
}

static void
cbm_added (MMCbmList                 *list,
           const gchar               *cbm_path,
           gboolean                   received,
           MmGdbusModemCellBroadcast *skeleton)
{
    update_message_list (skeleton, list);
    mm_gdbus_modem_cell_broadcast_emit_added (skeleton, cbm_path);
}

static void
cbm_deleted (MMCbmList             *list,
             const gchar           *cbm_path,
             MmGdbusModemCellBroadcast *skeleton)
{
    update_message_list (skeleton, list);
    mm_gdbus_modem_cell_broadcast_emit_deleted (skeleton, cbm_path);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (GTask *task);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_GET_CHANNELS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModemCellBroadcast *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_cell_broadcast_enable_finish (MMIfaceModemCellBroadcast *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModemCellBroadcast *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->setup_unsolicited_events_finish (self, res, &error);
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
enable_unsolicited_events_ready (MMIfaceModemCellBroadcast *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    /* Not critical! */
    if (!MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->enable_unsolicited_events_finish (self, res, &error)) {
        mm_obj_dbg (self, "Can't enable unsolicited events: %s", error->message);
        g_error_free (error);
    }

    /* Go on with next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
load_channels_ready (MMIfaceModemCellBroadcast *self,
                     GAsyncResult *res,
                     GTask *task)
{
    EnablingContext *ctx;
    g_autoptr (GError) error = NULL;
    g_autoptr (GArray) channels = NULL;

    ctx = g_task_get_task_data (task);
    channels = MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->load_channels_finish (self, res, &error);
    if (channels) {
        mm_gdbus_modem_cell_broadcast_set_channels (
            ctx->skeleton,
            mm_common_cell_broadcast_channels_garray_to_variant (channels));
    } else {
        /* Not critical! */
        mm_obj_warn (self, "Couldn't load current channel list: %s", error->message);
    }

    /* Go on with next step */
    ctx->step++;
    interface_enabling_step (task);
}

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModemCellBroadcast *self;
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
        g_autoptr (MMCbmList) list = NULL;

        list = mm_cbm_list_new (MM_BASE_MODEM (self), G_OBJECT (self));
        g_object_set (self,
                      MM_IFACE_MODEM_CELL_BROADCAST_CBM_LIST, list,
                      NULL);

        /* Connect to list's signals */
        g_signal_connect (list,
                          MM_CBM_ADDED,
                          G_CALLBACK (cbm_added),
                          ctx->skeleton);
        g_signal_connect (list,
                          MM_CBM_DELETED,
                          G_CALLBACK (cbm_deleted),
                          ctx->skeleton);
        ctx->step++;
    } /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow enabling unsolicited events */
        if (MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_GET_CHANNELS:
        /* Read current channel list */
        if (MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->load_channels &&
            MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->load_channels_finish) {
            MM_IFACE_MODEM_CELL_BROADCAST_GET_IFACE (self)->load_channels (
                self,
                (GAsyncReadyCallback)load_channels_ready,
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
mm_iface_modem_cell_broadcast_enable (MMIfaceModemCellBroadcast *self,
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
                  MM_IFACE_MODEM_CELL_BROADCAST_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Can't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_enabling_step (task);
}

/*****************************************************************************/

gboolean
mm_iface_modem_cell_broadcast_take_part (MMIfaceModemCellBroadcast  *self,
                                         GObject                    *bind_to,
                                         MMCbmPart                  *cbm_part,
                                         MMCbmState                  state,
                                         GError                    **error)
{
    g_autoptr(MMCbmList) list = NULL;
    gboolean             added = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_CELL_BROADCAST_CBM_LIST, &list,
                  NULL);
    if (list) {
        added = mm_cbm_list_take_part (list, bind_to, cbm_part, state, error);
        if (!added)
            g_prefix_error (error, "couldn't take part in CBM list: ");
    }

    /* If part wasn't taken, we need to free the part ourselves */
    if (!added)
        mm_cbm_part_free (cbm_part);

    return added;
}

/*****************************************************************************/

static void
mm_iface_modem_cell_broadcast_default_init (MMIfaceModemCellBroadcastInterface *iface)
{
    static gsize initialized = 0;

    if (!g_once_init_enter (&initialized))
        return;

    /* Properties */
    g_object_interface_install_property (
         iface,
         g_param_spec_object (MM_IFACE_MODEM_CELL_BROADCAST_DBUS_SKELETON,
                              "CellBroadcast DBus skeleton",
                              "DBus skeleton for the CellBroadcast interface",
                              MM_GDBUS_TYPE_MODEM_CELL_BROADCAST_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property (
         iface,
         g_param_spec_object (MM_IFACE_MODEM_CELL_BROADCAST_CBM_LIST,
                              "CBM list",
                              "List of CBM objects managed in the interface",
                              MM_TYPE_CBM_LIST,
                              G_PARAM_READWRITE));

    g_once_init_leave (&initialized, 1);
}
