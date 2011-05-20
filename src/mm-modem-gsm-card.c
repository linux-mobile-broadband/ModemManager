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
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <dbus/dbus-glib.h>
#include <string.h>

#include "mm-modem-gsm-card.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-gsm.h"

static void impl_gsm_modem_get_imei (MMModemGsmCard *modem,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_get_imsi (MMModemGsmCard *modem,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_get_operator_id (MMModemGsmCard *modem,
                                            DBusGMethodInvocation *context);

static void impl_gsm_modem_get_spn (MMModemGsmCard *modem,
                                    DBusGMethodInvocation *context);

static void impl_gsm_modem_send_pin (MMModemGsmCard *modem,
                                     const char *pin,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_send_puk (MMModemGsmCard *modem,
                                     const char *puk,
                                     const char *pin,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_enable_pin (MMModemGsmCard *modem,
                                       const char *pin,
                                       gboolean enabled,
                                       DBusGMethodInvocation *context);

static void impl_gsm_modem_change_pin (MMModemGsmCard *modem,
                                       const char *old_pin,
                                       const char *new_pin,
                                       DBusGMethodInvocation *context);

#include "mm-modem-gsm-card-glue.h"

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
str_call_not_supported (MMModemGsmCard *self,
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
uint_call_not_supported (MMModemGsmCard *self,
                         MMModemUIntFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);
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
async_call_not_supported (MMModemGsmCard *self,
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
mm_modem_gsm_card_get_imei (MMModemGsmCard *self,
                            MMModemStringFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_imei)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_imei (self, callback, user_data);
    else
        str_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_card_get_imsi (MMModemGsmCard *self,
                            MMModemStringFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_imsi)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_imsi (self, callback, user_data);
    else
        str_call_not_supported (self, callback, user_data);
}

void mm_modem_gsm_card_get_unlock_retries (MMModemGsmCard *self,
                                           const char *pin_type,
                                           MMModemUIntFn callback,
                                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (pin_type != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_unlock_retries)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_unlock_retries (self, pin_type, callback, user_data);
    else
        uint_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_card_get_operator_id (MMModemGsmCard *self,
                                   MMModemStringFn callback,
                                   gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_operator_id)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_operator_id (self, callback, user_data);
    else
        str_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_card_get_spn (MMModemGsmCard *self,
                           MMModemStringFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_spn)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_spn (self, callback, user_data);
    else
        str_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_card_send_puk (MMModemGsmCard *self,
                            const char *puk,
                            const char *pin,
                            MMModemFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (puk != NULL);
    g_return_if_fail (pin != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->send_puk)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->send_puk (self, puk, pin, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_card_send_pin (MMModemGsmCard *self,
                            const char *pin,
                            MMModemFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (pin != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->send_pin)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->send_pin (self, pin, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_card_enable_pin (MMModemGsmCard *self,
                              const char *pin,
                              gboolean enabled,
                              MMModemFn callback,
                              gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (pin != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->enable_pin)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->enable_pin (self, pin, enabled, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_card_change_pin (MMModemGsmCard *self,
                              const char *old_pin,
                              const char *new_pin,
                              MMModemFn callback,
                              gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (old_pin != NULL);
    g_return_if_fail (new_pin != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->change_pin)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->change_pin (self, old_pin, new_pin, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

/*****************************************************************************/

static void
imei_auth_cb (MMAuthRequest *req,
              GObject *owner,
              DBusGMethodInvocation *context,
              gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise get the IMEI */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_get_imei (self, str_call_done, context);
}

static void
impl_gsm_modem_get_imei (MMModemGsmCard *modem, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to get the IMEI */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_INFO,
                                context,
                                imei_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
imsi_auth_cb (MMAuthRequest *req,
              GObject *owner,
              DBusGMethodInvocation *context,
              gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise get the IMSI */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_get_imsi (self, str_call_done, context);
}

static void
impl_gsm_modem_get_imsi (MMModemGsmCard *modem, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to get the IMSI */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_INFO,
                                context,
                                imsi_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
spn_auth_cb (MMAuthRequest *req,
             GObject *owner,
             DBusGMethodInvocation *context,
             gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise get the SPN */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_get_spn (self, str_call_done, context);
}

static void
impl_gsm_modem_get_spn (MMModemGsmCard *modem, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to get the SPN */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_INFO,
                                context,
                                spn_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
operator_id_auth_cb (MMAuthRequest *req,
                     GObject *owner,
                     DBusGMethodInvocation *context,
                     gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise get the operator id */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_get_operator_id (self, str_call_done, context);
}

static void
impl_gsm_modem_get_operator_id (MMModemGsmCard *modem, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to get the operator id */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_INFO,
                                context,
                                operator_id_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

typedef struct {
    char *puk;
    char *pin;
    char *pin2;
    gboolean enabled;
} SendPinPukInfo;

static void
send_pin_puk_info_destroy (gpointer data)
{
    SendPinPukInfo *info = data;

    g_free (info->puk);
    g_free (info->pin);
    g_free (info->pin2);
    memset (info, 0, sizeof (SendPinPukInfo));
    g_free (info);
}

static SendPinPukInfo *
send_pin_puk_info_new (const char *puk,
                       const char *pin,
                       const char *pin2,
                       gboolean enabled)
{
    SendPinPukInfo *info;

    info = g_malloc0 (sizeof (SendPinPukInfo));
    info->puk = g_strdup (puk);
    info->pin = g_strdup (pin);
    info->pin2 = g_strdup (pin2);
    info->enabled = enabled;
    return info;
}

/*****************************************************************************/

static void
send_puk_auth_cb (MMAuthRequest *req,
                  GObject *owner,
                  DBusGMethodInvocation *context,
                  gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    SendPinPukInfo *info = user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise send the PUK */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_send_puk (self, info->puk, info->pin, async_call_done, context);
}

static void
impl_gsm_modem_send_puk (MMModemGsmCard *modem,
                         const char *puk,
                         const char *pin,
                         DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SendPinPukInfo *info;

    info = send_pin_puk_info_new (puk, pin, NULL, FALSE);

    /* Make sure the caller is authorized to send the PUK */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_CONTROL,
                                context,
                                send_puk_auth_cb,
                                info,
                                send_pin_puk_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
send_pin_auth_cb (MMAuthRequest *req,
                  GObject *owner,
                  DBusGMethodInvocation *context,
                  gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    SendPinPukInfo *info = user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise unlock the modem */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_send_pin (self, info->pin, async_call_done, context);
}

static void
impl_gsm_modem_send_pin (MMModemGsmCard *modem,
                         const char *pin,
                         DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SendPinPukInfo *info;

    info = send_pin_puk_info_new (NULL, pin, NULL, FALSE);

    /* Make sure the caller is authorized to unlock the modem */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_CONTROL,
                                context,
                                send_pin_auth_cb,
                                info,
                                send_pin_puk_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
enable_pin_auth_cb (MMAuthRequest *req,
                    GObject *owner,
                    DBusGMethodInvocation *context,
                    gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    SendPinPukInfo *info = user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise enable the PIN */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_enable_pin (self, info->pin, info->enabled, async_call_done, context);
}

static void
impl_gsm_modem_enable_pin (MMModemGsmCard *modem,
                           const char *pin,
                           gboolean enabled,
                           DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SendPinPukInfo *info;

    info = send_pin_puk_info_new (NULL, pin, NULL, enabled);

    /* Make sure the caller is authorized to enable a PIN */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_CONTROL,
                                context,
                                enable_pin_auth_cb,
                                info,
                                send_pin_puk_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
change_pin_auth_cb (MMAuthRequest *req,
                    GObject *owner,
                    DBusGMethodInvocation *context,
                    gpointer user_data)
{
    MMModemGsmCard *self = MM_MODEM_GSM_CARD (owner);
    SendPinPukInfo *info = user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise change the PIN */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_card_change_pin (self, info->pin, info->pin2, async_call_done, context);
}

static void
impl_gsm_modem_change_pin (MMModemGsmCard *modem,
                           const char *old_pin,
                           const char *new_pin,
                           DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SendPinPukInfo *info;

    info = send_pin_puk_info_new (NULL, old_pin, new_pin, FALSE);

    /* Make sure the caller is authorized to change the PIN */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_CONTROL,
                                context,
                                change_pin_auth_cb,
                                info,
                                send_pin_puk_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
mm_modem_gsm_card_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (G_LIKELY (initialized))
        return;

    initialized = TRUE;

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_GSM_CARD_SIM_IDENTIFIER,
                               "SimIdentifier",
                               "An obfuscated identifier of the SIM",
                               NULL,
                               G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_GSM_CARD_SUPPORTED_BANDS,
                            "Supported Modes",
                            "Supported frequency bands of the card",
                            MM_MODEM_GSM_BAND_UNKNOWN,
                            G_MAXUINT32,
                            MM_MODEM_GSM_BAND_UNKNOWN,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_GSM_CARD_SUPPORTED_MODES,
                            "Supported Modes",
                            "Supported modes of the card (ex 2G preferred, 3G preferred, 2G only, etc",
                            MM_MODEM_GSM_MODE_UNKNOWN,
                            G_MAXUINT32,
                            MM_MODEM_GSM_MODE_UNKNOWN,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

GType
mm_modem_gsm_card_get_type (void)
{
    static GType card_type = 0;

    if (G_UNLIKELY (!card_type)) {
        const GTypeInfo card_info = {
            sizeof (MMModemGsmCard), /* class_size */
            mm_modem_gsm_card_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        card_type = g_type_register_static (G_TYPE_INTERFACE,
                                            "MMModemGsmCard",
                                            &card_info, 0);

        g_type_interface_add_prerequisite (card_type, G_TYPE_OBJECT);
        g_type_interface_add_prerequisite (card_type, MM_TYPE_MODEM);
        dbus_g_object_type_install_info (card_type, &dbus_glib_mm_modem_gsm_card_object_info);
    }

    return card_type;
}
