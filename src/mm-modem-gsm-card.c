/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <dbus/dbus-glib.h>

#include "mm-modem-gsm-card.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void impl_gsm_modem_get_imei (MMModemGsmCard *modem,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_get_imsi (MMModemGsmCard *modem,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_get_info (MMModemGsmCard *modem,
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
info_call_done (MMModemGsmCard *self,
                const char *manufacturer,
                const char *model,
                const char *version,
                GError *error,
                gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else {
        GValueArray *array;
        GValue value = { 0, };

        array = g_value_array_new (3);

        /* Manufacturer */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, manufacturer);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* Model */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, model);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* Version */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, version);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        dbus_g_method_return (context, array);
    }
}

static void
info_invoke (MMCallbackInfo *info)
{
    MMModemGsmCardInfoFn callback = (MMModemGsmCardInfoFn) info->callback;

    callback (MM_MODEM_GSM_CARD (info->modem), NULL, NULL, NULL, info->error, info->user_data);
}

static void
info_call_not_supported (MMModemGsmCard *self,
                         MMModemGsmCardInfoFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self), info_invoke, G_CALLBACK (callback), user_data);
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

void
mm_modem_gsm_card_get_info (MMModemGsmCard *self,
                            MMModemGsmCardInfoFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_CARD (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_info)
        MM_MODEM_GSM_CARD_GET_INTERFACE (self)->get_info (self, callback, user_data);
    else
        info_call_not_supported (self, callback, user_data);
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
impl_gsm_modem_get_imei (MMModemGsmCard *modem,
                         DBusGMethodInvocation *context)
{
    mm_modem_gsm_card_get_imei (modem, str_call_done, context);
}

static void
impl_gsm_modem_get_imsi (MMModemGsmCard *modem,
                         DBusGMethodInvocation *context)
{
    mm_modem_gsm_card_get_imsi (modem, str_call_done, context);
}

static void
impl_gsm_modem_get_info (MMModemGsmCard *modem,
                         DBusGMethodInvocation *context)
{
    mm_modem_gsm_card_get_info (modem, info_call_done, context);
}

static void
   impl_gsm_modem_send_puk (MMModemGsmCard *modem,
                            const char *puk,
                            const char *pin,
                            DBusGMethodInvocation *context)
{
    mm_modem_gsm_card_send_puk (modem, puk, pin, async_call_done, context);
}

static void
impl_gsm_modem_send_pin (MMModemGsmCard *modem,
                         const char *pin,
                         DBusGMethodInvocation *context)
{
    mm_modem_gsm_card_send_pin (modem, pin, async_call_done, context);
}

static void
impl_gsm_modem_enable_pin (MMModemGsmCard *modem,
                           const char *pin,
                           gboolean enabled,
                           DBusGMethodInvocation *context)
{
    mm_modem_gsm_card_enable_pin (modem, pin, enabled, async_call_done, context);
}

static void
impl_gsm_modem_change_pin (MMModemGsmCard *modem,
                           const char *old_pin,
                           const char *new_pin,
                           DBusGMethodInvocation *context)
{
    mm_modem_gsm_card_change_pin (modem, old_pin, new_pin, async_call_done, context);
}

/*****************************************************************************/

static void
mm_modem_gsm_card_init (gpointer g_iface)
{
}

GType
mm_modem_gsm_card_get_type (void)
{
    static GType card_type = 0;

    if (!G_UNLIKELY (card_type)) {
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
        dbus_g_object_type_install_info (card_type, &dbus_glib_mm_modem_gsm_card_object_info);
    }

    return card_type;
}
