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

/*****************************************************************************/

static void
impl_gsm_modem_sms_delete (MMModemGsmSms *modem,
                           guint idx,
                           DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

static void
impl_gsm_modem_sms_get (MMModemGsmSms *modem,
                        guint idx,
                        DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

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

static void
impl_gsm_modem_sms_set_smsc (MMModemGsmSms *modem,
                             const char *smsc,
                             DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

static void
impl_gsm_modem_sms_list (MMModemGsmSms *modem,
                         DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

static void
impl_gsm_modem_sms_save (MMModemGsmSms *modem,
                         GHashTable *properties,
                         DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
}

static void
impl_gsm_modem_sms_send (MMModemGsmSms *modem,
                         GHashTable *properties,
                         DBusGMethodInvocation *context)
{
    GValue *value;
    const char *number = NULL;
    const char *text = NULL ;
    const char *smsc = NULL;
    GError *error = NULL;
    guint validity = 0;
    guint class = 0;

    value = (GValue *) g_hash_table_lookup (properties, "number");
    if (value)
        number = g_value_get_string (value);

    value = (GValue *) g_hash_table_lookup (properties, "text");
    if (value)
        text = g_value_get_string (value);

    value = (GValue *) g_hash_table_lookup (properties, "smsc");
    if (value)
        smsc = g_value_get_string (value);

    value = (GValue *) g_hash_table_lookup (properties, "validity");
    if (value)
        validity = g_value_get_uint (value);

    value = (GValue *) g_hash_table_lookup (properties, "class");
    if (value)
        class = g_value_get_uint (value);

    if (!number)
        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                     "Missing number");
    else if (!text)
        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                     "Missing message text");

    if (error) {
        async_call_done (MM_MODEM (modem), error, context);
        g_error_free (error);
    } else
        mm_modem_gsm_sms_send (modem, number, text, smsc, validity, class, async_call_done, context);
}

static void
impl_gsm_modem_sms_send_from_storage (MMModemGsmSms *modem,
                                      guint idx,
                                      DBusGMethodInvocation *context)
{
    async_call_not_supported (modem, async_call_done, context);
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
    async_call_not_supported (modem, async_call_done, context);
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
