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
 * Copyright (C) 2010 Guido Guenther <agx@sigxcpu.org>
 */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-gsm-ussd.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"

static void impl_modem_gsm_ussd_initiate(MMModemGsmUssd *modem,
                                         const char*command,
                                         DBusGMethodInvocation *context);

static void impl_modem_gsm_ussd_respond(MMModemGsmUssd *modem,
                                        const char *response,
                                        DBusGMethodInvocation *context);

static void impl_modem_gsm_ussd_cancel(MMModemGsmUssd *modem,
                                       DBusGMethodInvocation *context);

#include "mm-modem-gsm-ussd-glue.h"


/*****************************************************************************/

static void
str_call_done (MMModem *modem, const char *result, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, result);
}

static void
str_call_not_supported (MMModemGsmUssd *self,
                        MMModemStringFn callback,
                        gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (self), callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");

    mm_callback_info_schedule (info);
}

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
async_call_not_supported (MMModemGsmUssd *self,
                          MMModemFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");
    mm_callback_info_schedule (info);
}

/*****************************************************************************/

void
mm_modem_gsm_ussd_initiate (MMModemGsmUssd *self,
                            const char *command,
                            MMModemStringFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_USSD (self));
    g_return_if_fail (command != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_USSD_GET_INTERFACE (self)->initiate)
        MM_MODEM_GSM_USSD_GET_INTERFACE (self)->initiate(self, command, callback, user_data);
    else
        str_call_not_supported (self, callback, user_data);

}

void
mm_modem_gsm_ussd_respond (MMModemGsmUssd *self,
                           const char *command,
                           MMModemStringFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_USSD (self));
    g_return_if_fail (command != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_USSD_GET_INTERFACE (self)->respond)
        MM_MODEM_GSM_USSD_GET_INTERFACE (self)->respond(self, command, callback, user_data);
    else
        str_call_not_supported (self, callback, user_data);

}

void
mm_modem_gsm_ussd_cancel (MMModemGsmUssd *self,
                          MMModemFn callback,
                          gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_USSD (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_USSD_GET_INTERFACE (self)->cancel)
        MM_MODEM_GSM_USSD_GET_INTERFACE (self)->cancel(self, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);

}

/*****************************************************************************/

typedef struct {
    char *command;
} UssdAuthInfo;

static void
ussd_auth_info_destroy (gpointer data)
{
    UssdAuthInfo *info = data;

    g_free (info->command);
    g_free (info);
}

static UssdAuthInfo *
ussd_auth_info_new (const char* command)
{
    UssdAuthInfo *info;

    info = g_malloc0 (sizeof (UssdAuthInfo));
    info->command = g_strdup (command);

    return info;
}

/*****************************************************************************/

static void
ussd_initiate_auth_cb (MMAuthRequest *req,
                       GObject *owner,
                       DBusGMethodInvocation *context,
                       gpointer user_data)
{
    MMModemGsmUssd *self = MM_MODEM_GSM_USSD (owner);
    UssdAuthInfo *info = user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise initiate the USSD */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error))
        goto done;

    if (!info->command) {
        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                     "Missing USSD command");
    }

done:
    if (error) {
        str_call_done (MM_MODEM (self), NULL, error, context);
        g_error_free (error);
    } else
        mm_modem_gsm_ussd_initiate (self, info->command, str_call_done, context);
}

static void
impl_modem_gsm_ussd_initiate (MMModemGsmUssd *modem,
                              const char *command,
                              DBusGMethodInvocation *context)
{
    GError *error = NULL;
    UssdAuthInfo *info;

    info = ussd_auth_info_new (command);

    /* Make sure the caller is authorized to initiate the USSD */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_USSD,
                                context,
                                ussd_initiate_auth_cb,
                                info,
                                ussd_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

static void
ussd_respond_auth_cb (MMAuthRequest *req,
                       GObject *owner,
                       DBusGMethodInvocation *context,
                       gpointer user_data)
{
    MMModemGsmUssd *self = MM_MODEM_GSM_USSD (owner);
    UssdAuthInfo *info = user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise respond to the USSD */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error))
        goto done;

    if (!info->command) {
        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                     "Missing USSD command");
    }

done:
    if (error) {
        str_call_done (MM_MODEM (self), NULL, error, context);
        g_error_free (error);
    } else
        mm_modem_gsm_ussd_respond (self, info->command, str_call_done, context);
}

static void
impl_modem_gsm_ussd_respond (MMModemGsmUssd *modem,
                              const char *command,
                              DBusGMethodInvocation *context)
{
    GError *error = NULL;
    UssdAuthInfo *info;

    info = ussd_auth_info_new (command);

    /* Make sure the caller is authorized to respond to the USSD */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_USSD,
                                context,
                                ussd_respond_auth_cb,
                                info,
                                ussd_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

static void
ussd_cancel_auth_cb (MMAuthRequest *req,
                     GObject *owner,
                     DBusGMethodInvocation *context,
                     gpointer user_data)
{
    MMModemGsmUssd *self = MM_MODEM_GSM_USSD (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise cancel the USSD */
    mm_modem_auth_finish (MM_MODEM (self), req, &error);

    if (error) {
        str_call_done (MM_MODEM (self), NULL, error, context);
        g_error_free (error);
    } else
        mm_modem_gsm_ussd_cancel (self, async_call_done, context);
}

static void
impl_modem_gsm_ussd_cancel (MMModemGsmUssd *modem,
                            DBusGMethodInvocation *context)
{
    GError *error = NULL;
    UssdAuthInfo *info;

    info = ussd_auth_info_new (NULL);

    /* Make sure the caller is authorized to cancel USSD */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_USSD,
                                context,
                                ussd_cancel_auth_cb,
                                info,
                                ussd_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
mm_modem_gsm_ussd_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_GSM_USSD_STATE,
                              "State",
                              "Current state of USSD session",
                              NULL,
                              G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_GSM_USSD_NETWORK_NOTIFICATION,
                              "NetworkNotification",
                              "Network initiated request, no response required",
                              NULL,
                              G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_GSM_USSD_NETWORK_REQUEST,
                              "NetworkRequest",
                              "Network initiated request, reponse required",
                              NULL,
                              G_PARAM_READABLE));

    initialized = TRUE;
}

GType
mm_modem_gsm_ussd_get_type (void)
{
    static GType ussd_type = 0;

    if (!G_UNLIKELY (ussd_type)) {
        const GTypeInfo ussd_info = {
            sizeof (MMModemGsmUssd), /* class_size */
            mm_modem_gsm_ussd_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        ussd_type = g_type_register_static (G_TYPE_INTERFACE,
                                           "MMModemGsmUssd",
                                           &ussd_info, 0);

        g_type_interface_add_prerequisite (ussd_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (ussd_type, &dbus_glib_mm_modem_gsm_ussd_object_info);

        dbus_g_object_type_register_shadow_property (ussd_type,
                                                     "State",
                                                     MM_MODEM_GSM_USSD_STATE);
    }

    return ussd_type;
}
