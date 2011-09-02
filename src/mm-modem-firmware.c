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
 * Copyright (C) 2011 The Chromium OS Authors
 */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-firmware.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"

static void impl_modem_firmware_list (MMModemFirmware *modem,
                                      DBusGMethodInvocation *context);

static void impl_modem_firmware_select (MMModemFirmware *modem,
                                        const char *slot,
                                        DBusGMethodInvocation *context);

static void impl_modem_firmware_install (MMModemFirmware *modem,
                                         const char *image,
                                         const char *slot,
                                         DBusGMethodInvocation *context);

#include "mm-modem-firmware-glue.h"

static void
async_call_done (MMModem *modem, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context);
}

static void
async_call_not_supported (MMModemFirmware *self,
                          MMModemFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");
    mm_callback_info_schedule (info);
}

static void
firmware_list_done (MMModemFirmware *self,
                    const char *selected,
                    GHashTable *installed,
                    GHashTable *available,
                    GError *error,
                    gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, selected, installed, available);
}

/*****************************************************************************/

static void
firmware_list_invoke (MMCallbackInfo *info)
{
    MMModemFirmwareListFn callback = (MMModemFirmwareListFn) info->callback;

    callback (MM_MODEM_FIRMWARE (info->modem), NULL, NULL, NULL, info->error, info->user_data);
}

void
mm_modem_firmware_list (MMModemFirmware *self,
                        MMModemFirmwareListFn callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_FIRMWARE (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_FIRMWARE_GET_INTERFACE (self)->list)
        MM_MODEM_FIRMWARE_GET_INTERFACE (self)->list (self, callback, user_data);
    else {
        MMCallbackInfo *info;

        info = mm_callback_info_new_full (MM_MODEM (self),
                                          firmware_list_invoke,
                                          G_CALLBACK (callback),
                                          user_data);

        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
}

void
mm_modem_firmware_select (MMModemFirmware *self,
                          const char *slot,
                          MMModemFn callback,
                          gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_FIRMWARE (self));
    g_return_if_fail (slot != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_FIRMWARE_GET_INTERFACE (self)->select)
        MM_MODEM_FIRMWARE_GET_INTERFACE (self)->select (self, slot, callback, user_data);
    else
        async_call_not_supported (self, async_call_done, user_data);
}

void
mm_modem_firmware_install (MMModemFirmware *self,
                           const char *image,
                           const char *slot,
                           MMModemFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_FIRMWARE (self));
    g_return_if_fail (slot != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_FIRMWARE_GET_INTERFACE (self)->install)
        MM_MODEM_FIRMWARE_GET_INTERFACE (self)->install (self, image, slot, callback, user_data);
    else
        async_call_not_supported (self, async_call_done, user_data);
}

typedef struct {
    char *image;
    char *slot;
} FirmwareAuthInfo;

static void
firmware_auth_info_destroy (gpointer data)
{
    FirmwareAuthInfo *info = data;

    g_free (info->image);
    g_free (info->slot);
    memset (info, 0, sizeof (FirmwareAuthInfo));
    g_free (info);
}

static FirmwareAuthInfo *
firmware_auth_info_new (const char *image,
                        const char *slot)
{
    FirmwareAuthInfo *info;

    info = g_malloc0 (sizeof (FirmwareAuthInfo));
    info->image = g_strdup(image);
    info->slot = g_strdup(slot);

    return info;
}

static void
firmware_list_auth_cb (MMAuthRequest *req,
                       GObject *owner,
                       DBusGMethodInvocation *context,
                       gpointer user_data)
{
    MMModemFirmware *self = MM_MODEM_FIRMWARE (owner);
    GError *error = NULL;

    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_firmware_list (self, firmware_list_done, context);
}

static void
impl_modem_firmware_list (MMModemFirmware *modem, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_FIRMWARE,
                                context,
                                (MMAuthRequestCb)firmware_list_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free(error);
    }
}

static void
firmware_select_auth_cb (MMAuthRequest *req,
                         GObject *owner,
                         DBusGMethodInvocation *context,
                         gpointer user_data)
{
    MMModemFirmware *self = MM_MODEM_FIRMWARE (owner);
    FirmwareAuthInfo *info = user_data;
    const char *slot = info->slot;
    GError *error = NULL;

    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_firmware_select (self, slot, async_call_done, context);
}

static void
impl_modem_firmware_select (MMModemFirmware *modem,
                            const char *slot,
                            DBusGMethodInvocation *context)
{
    GError *error = NULL;
    FirmwareAuthInfo *info;

    info = firmware_auth_info_new (NULL, slot);

    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_FIRMWARE,
                                context,
                                (MMAuthRequestCb)firmware_select_auth_cb,
                                info,
                                firmware_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

static void
firmware_install_auth_cb (MMAuthRequest *req,
                          GObject *owner,
                          DBusGMethodInvocation *context,
                          gpointer user_data)
{
    MMModemFirmware *self = MM_MODEM_FIRMWARE (owner);
    FirmwareAuthInfo *info = user_data;
    const char *image = info->image;
    const char *slot = info->slot;
    GError *error = NULL;

    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_firmware_install (self, image, slot, async_call_done, context);
}

static void
impl_modem_firmware_install (MMModemFirmware *modem,
                             const char *image,
                             const char *slot,
                             DBusGMethodInvocation *context)
{
    GError *error = NULL;
    FirmwareAuthInfo *info;

    info = firmware_auth_info_new (image, slot);

    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_FIRMWARE,
                                context,
                                (MMAuthRequestCb)firmware_install_auth_cb,
                                info,
                                firmware_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

GType
mm_modem_firmware_get_type (void)
{
    static GType firmware_type = 0;

    if (!G_UNLIKELY (firmware_type)) {
        const GTypeInfo firmware_info = {
            sizeof (MMModemFirmware), /* class_size */
            NULL,       /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        firmware_type = g_type_register_static (G_TYPE_INTERFACE,
                                           "MMModemFirmware",
                                           &firmware_info, 0);

        g_type_interface_add_prerequisite (firmware_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (firmware_type, &dbus_glib_mm_modem_firmware_object_info);
    }

    return firmware_type;
}
