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
 */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-gsm-sms.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"

static void impl_gsm_modem_sms_delete (MMModemGsmSms *modem,
                                       guint idx,
                                       DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_get (MMModemGsmSms *modem,
                                    guint idx,
                                    DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_get_format (MMModemGsmSms *modem,
                                           DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_set_format (MMModemGsmSms *modem,
                                           guint format,
                                           DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_get_smsc (MMModemGsmSms *modem,
                                         DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_set_smsc (MMModemGsmSms *modem,
                                         const char *smsc,
                                         DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_list (MMModemGsmSms *modem,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_save (MMModemGsmSms *modem,
                                     GHashTable *properties,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_send (MMModemGsmSms *modem,
                                     GHashTable *properties,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_send_from_storage (MMModemGsmSms *modem,
                                                  guint idx,
                                                  DBusGMethodInvocation *context);

static void impl_gsm_modem_sms_set_indication (MMModemGsmSms *modem,
                                               guint mode,
                                               guint mt,
                                               guint bm,
                                               guint ds,
                                               guint bfr,
                                               DBusGMethodInvocation *context);

#include "mm-modem-gsm-sms-glue.h"

enum {
    SMS_RECEIVED,
    COMPLETED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/*****************************************************************************/

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
async_call_not_supported (MMModemGsmSms *self,
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
sms_get_done (MMModemGsmSms *self,
              GHashTable *properties,
              GError *error,
              gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, properties);
}

static void
sms_list_done (MMModemGsmSms *self,
              GPtrArray *results,
              GError *error,
              gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, results);
}

/*****************************************************************************/

void
mm_modem_gsm_sms_send (MMModemGsmSms *self,
                       const char *number,
                       const char *text,
                       const char *smsc,
                       guint validity,
                       guint class,
                       MMModemFn callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_SMS (self));
    g_return_if_fail (number != NULL);
    g_return_if_fail (text != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_SMS_GET_INTERFACE (self)->send)
        MM_MODEM_GSM_SMS_GET_INTERFACE (self)->send (self, number, text, smsc, validity, class, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);

}

static void
sms_get_invoke (MMCallbackInfo *info)
{
    MMModemGsmSmsGetFn callback = (MMModemGsmSmsGetFn) info->callback;

    callback (MM_MODEM_GSM_SMS (info->modem), NULL, info->error, info->user_data);
}

void
mm_modem_gsm_sms_get (MMModemGsmSms *self,
                      guint idx,
                      MMModemGsmSmsGetFn callback,
                      gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_SMS (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_SMS_GET_INTERFACE (self)->get)
        MM_MODEM_GSM_SMS_GET_INTERFACE (self)->get (self, idx, callback, user_data);
    else {
        MMCallbackInfo *info;

        info = mm_callback_info_new_full (MM_MODEM (self),
                                          sms_get_invoke,
                                          G_CALLBACK (callback),
                                          user_data);

        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
}

void
mm_modem_gsm_sms_delete (MMModemGsmSms *self,
                         guint idx,
                         MMModemFn callback,
                         gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_SMS (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_SMS_GET_INTERFACE (self)->delete)
        MM_MODEM_GSM_SMS_GET_INTERFACE (self)->delete (self, idx, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

static void
sms_list_invoke (MMCallbackInfo *info)
{
    MMModemGsmSmsListFn callback = (MMModemGsmSmsListFn) info->callback;

    callback (MM_MODEM_GSM_SMS (info->modem), NULL, info->error, info->user_data);
}

void
mm_modem_gsm_sms_list (MMModemGsmSms *self,
                       MMModemGsmSmsListFn callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_SMS (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_SMS_GET_INTERFACE (self)->list)
        MM_MODEM_GSM_SMS_GET_INTERFACE (self)->list (self, callback, user_data);
    else {
        MMCallbackInfo *info;

        info = mm_callback_info_new_full (MM_MODEM (self),
                                          sms_list_invoke,
                                          G_CALLBACK (callback),
                                          user_data);

        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
}

void
mm_modem_gsm_sms_received (MMModemGsmSms *self,
                           guint idx,
                           gboolean complete)
{
    g_return_if_fail (MM_IS_MODEM_GSM_SMS (self));

    g_signal_emit (self, signals[SMS_RECEIVED], 0,
                   idx,
                   complete);
}

void
mm_modem_gsm_sms_completed (MMModemGsmSms *self,
                            guint idx,
                            gboolean complete)
{
    g_return_if_fail (MM_IS_MODEM_GSM_SMS (self));

    g_signal_emit (self, signals[COMPLETED], 0,
                   idx,
                   complete);
}

/*****************************************************************************/

typedef struct {
    guint num1;
    guint num2;
    guint num3;
    guint num4;
    guint num5;
    char *str;
    GHashTable *hash;
} SmsAuthInfo;

static void
sms_auth_info_destroy (gpointer data)
{
    SmsAuthInfo *info = data;

    if (info->hash)
        g_hash_table_destroy (info->hash);
    g_free (info->str);
    memset (info, 0, sizeof (SmsAuthInfo));
    g_free (info);
}

static void
destroy_gvalue (gpointer data)
{
    GValue *value = (GValue *) data;

    g_value_unset (value);
    g_slice_free (GValue, value);
}

static SmsAuthInfo *
sms_auth_info_new (guint num1,
                   guint num2,
                   guint num3,
                   guint num4,
                   guint num5,
                   const char *str,
                   GHashTable *hash)
{
    SmsAuthInfo *info;

    info = g_malloc0 (sizeof (SmsAuthInfo));
    info->num1 = num1;
    info->num2 = num2;
    info->num3 = num3;
    info->num4 = num4;
    info->num5 = num5;
    info->str = g_strdup (str);

    /* Copy the hash table if we're given one */
    if (hash) {
        GHashTableIter iter;
        gpointer key, value;

        info->hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, destroy_gvalue);

        g_hash_table_iter_init (&iter, hash);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
            const char *str_key = (const char *) key;
            GValue *src = (GValue *) value;
            GValue *dst;

            dst = g_slice_new0 (GValue);
            g_value_init (dst, G_VALUE_TYPE (src));
            g_value_copy (src, dst);
            g_hash_table_insert (info->hash, g_strdup (str_key), dst);
        }
    }

    return info;
}

/*****************************************************************************/

static void
sms_delete_auth_cb (MMAuthRequest *req,
                    GObject *owner,
                    DBusGMethodInvocation *context,
                    gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    SmsAuthInfo *info = user_data;
    GError *error = NULL;
    guint idx;

    /* Return any authorization error, otherwise delete the SMS */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else {
        idx = info->num1;
        mm_modem_gsm_sms_delete (self, idx, async_call_done, context);
    }
}

static void
impl_gsm_modem_sms_delete (MMModemGsmSms *modem,
                           guint idx,
                           DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SmsAuthInfo *info;

    info = sms_auth_info_new (idx, 0, 0, 0, 0, NULL, NULL);

    /* Make sure the caller is authorized to delete an SMS */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_delete_auth_cb,
                                info,
                                sms_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
sms_get_auth_cb (MMAuthRequest *req,
                 GObject *owner,
                 DBusGMethodInvocation *context,
                 gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    SmsAuthInfo *info = user_data;
    guint idx;
    GError *error = NULL;

    /* Return any authorization error, otherwise get the SMS */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else {
        idx = info->num1;
        mm_modem_gsm_sms_get (self, idx, sms_get_done, context);
    }
}

static void
impl_gsm_modem_sms_get (MMModemGsmSms *modem,
                        guint idx,
                        DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SmsAuthInfo *info;

    info = sms_auth_info_new (idx, 0, 0, 0, 0, NULL, NULL);

    /* Make sure the caller is authorized to get an SMS */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_get_auth_cb,
                                info,
                                sms_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
impl_gsm_modem_sms_get_format (MMModemGsmSms *modem,
                               DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

static void
impl_gsm_modem_sms_set_format (MMModemGsmSms *modem,
                               guint format,
                               DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

static void
impl_gsm_modem_sms_get_smsc (MMModemGsmSms *modem,
                             DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

/*****************************************************************************/

static void
sms_set_smsc_auth_cb (MMAuthRequest *req,
                      GObject *owner,
                      DBusGMethodInvocation *context,
                      gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise set the SMS service center */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        async_call_not_supported (self, async_call_done, context);
}

static void
impl_gsm_modem_sms_set_smsc (MMModemGsmSms *modem,
                             const char *smsc,
                             DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SmsAuthInfo *info;

    info = sms_auth_info_new (0, 0, 0, 0, 0, smsc, NULL);

    /* Make sure the caller is authorized to set the SMS service center */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_set_smsc_auth_cb,
                                info,
                                sms_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
sms_list_auth_cb (MMAuthRequest *req,
                  GObject *owner,
                  DBusGMethodInvocation *context,
                  gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise list SMSs */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_sms_list (self, sms_list_done, context);
}

static void
impl_gsm_modem_sms_list (MMModemGsmSms *modem,
                         DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to list SMSs */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_list_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
sms_save_auth_cb (MMAuthRequest *req,
                  GObject *owner,
                  DBusGMethodInvocation *context,
                  gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise save the SMS */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        async_call_not_supported (self, async_call_done, context);
}

static void
impl_gsm_modem_sms_save (MMModemGsmSms *modem,
                         GHashTable *properties,
                         DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SmsAuthInfo *info;

    info = sms_auth_info_new (0, 0, 0, 0, 0, NULL, properties);

    /* Make sure the caller is authorized to save the SMS */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_save_auth_cb,
                                info,
                                sms_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
sms_send_auth_cb (MMAuthRequest *req,
                  GObject *owner,
                  DBusGMethodInvocation *context,
                  gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    SmsAuthInfo *info = user_data;
    GError *error = NULL;
    GValue *value;
    const char *number = NULL;
    const char *text = NULL ;
    const char *smsc = NULL;
    guint validity = 0;
    guint class = 0;

    /* Return any authorization error, otherwise delete the SMS */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error))
        goto done;

    value = (GValue *) g_hash_table_lookup (info->hash, "number");
    if (value)
        number = g_value_get_string (value);

    value = (GValue *) g_hash_table_lookup (info->hash, "text");
    if (value)
        text = g_value_get_string (value);

    value = (GValue *) g_hash_table_lookup (info->hash, "smsc");
    if (value)
        smsc = g_value_get_string (value);

    value = (GValue *) g_hash_table_lookup (info->hash, "validity");
    if (value)
        validity = g_value_get_uint (value);

    value = (GValue *) g_hash_table_lookup (info->hash, "class");
    if (value)
        class = g_value_get_uint (value);

    if (!number) {
        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                     "Missing number");
    } else if (!text) {
        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                     "Missing message text");
    }

done:
    if (error) {
        async_call_done (MM_MODEM (self), error, context);
        g_error_free (error);
    } else
        mm_modem_gsm_sms_send (self, number, text, smsc, validity, class, async_call_done, context);
}

static void
impl_gsm_modem_sms_send (MMModemGsmSms *modem,
                         GHashTable *properties,
                         DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SmsAuthInfo *info;

    info = sms_auth_info_new (0, 0, 0, 0, 0, NULL, properties);

    /* Make sure the caller is authorized to send the PUK */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_send_auth_cb,
                                info,
                                sms_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
sms_send_from_storage_auth_cb (MMAuthRequest *req,
                               GObject *owner,
                               DBusGMethodInvocation *context,
                               gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise delete the SMS */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        async_call_not_supported (self, async_call_done, context);
}

static void
impl_gsm_modem_sms_send_from_storage (MMModemGsmSms *modem,
                                      guint idx,
                                      DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SmsAuthInfo *info;

    info = sms_auth_info_new (idx, 0, 0, 0, 0, NULL, NULL);

    /* Make sure the caller is authorized to send the PUK */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_send_from_storage_auth_cb,
                                info,
                                sms_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
sms_set_indication_auth_cb (MMAuthRequest *req,
                            GObject *owner,
                            DBusGMethodInvocation *context,
                            gpointer user_data)
{
    MMModemGsmSms *self = MM_MODEM_GSM_SMS (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise delete the SMS */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        async_call_not_supported (self, async_call_done, context);
}

static void
impl_gsm_modem_sms_set_indication (MMModemGsmSms *modem,
                                   guint mode,
                                   guint mt,
                                   guint bm,
                                   guint ds,
                                   guint bfr,
                                   DBusGMethodInvocation *context)
{
    GError *error = NULL;
    SmsAuthInfo *info;

    info = sms_auth_info_new (mode, mt, bm, ds, bfr, NULL, NULL);

    /* Make sure the caller is authorized to send the PUK */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_SMS,
                                context,
                                sms_set_indication_auth_cb,
                                info,
                                sms_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
mm_modem_gsm_sms_init (gpointer g_iface)
{
    GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Signals */
    signals[SMS_RECEIVED] =
        g_signal_new ("sms-received",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemGsmSms, sms_received),
                      NULL, NULL,
                      mm_marshal_VOID__UINT_BOOLEAN,
                      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_BOOLEAN);

    signals[COMPLETED] =
        g_signal_new ("completed",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemGsmSms, completed),
                      NULL, NULL,
                      mm_marshal_VOID__UINT_BOOLEAN,
                      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_BOOLEAN);

    initialized = TRUE;
}

GType
mm_modem_gsm_sms_get_type (void)
{
    static GType sms_type = 0;

    if (!G_UNLIKELY (sms_type)) {
        const GTypeInfo sms_info = {
            sizeof (MMModemGsmSms), /* class_size */
            mm_modem_gsm_sms_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        sms_type = g_type_register_static (G_TYPE_INTERFACE,
                                           "MMModemGsmSms",
                                           &sms_info, 0);

        g_type_interface_add_prerequisite (sms_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (sms_type, &dbus_glib_mm_modem_gsm_sms_object_info);
    }

    return sms_type;
}
