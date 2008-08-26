/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <dbus/dbus-glib.h>
#include "mm-gsm-modem.h"
#include "mm-modem-error.h"
#include "mm-callback-info.h"

static void impl_gsm_modem_set_pin (MMGsmModem *modem, const char *pin, DBusGMethodInvocation *context);
static void impl_gsm_modem_register (MMGsmModem *modem, const char *network_id, DBusGMethodInvocation *context);
static void impl_gsm_modem_get_reg_info (MMGsmModem *modem, DBusGMethodInvocation *context);
static void impl_gsm_modem_scan (MMGsmModem *modem, DBusGMethodInvocation *context);
static void impl_gsm_modem_set_apn (MMGsmModem *modem, const char *apn, DBusGMethodInvocation *context);
static void impl_gsm_modem_get_signal_quality (MMGsmModem *modem, DBusGMethodInvocation *context);
static void impl_gsm_modem_set_band (MMGsmModem *modem, guint32 band, DBusGMethodInvocation *context);
static void impl_gsm_modem_get_band (MMGsmModem *modem, DBusGMethodInvocation *context);
static void impl_gsm_modem_set_network_mode (MMGsmModem *modem, guint32 mode, DBusGMethodInvocation *context);
static void impl_gsm_modem_get_network_mode (MMGsmModem *modem, DBusGMethodInvocation *context);

#include "mm-gsm-modem-glue.h"

enum {
    SIGNAL_QUALITY,
    NETWORK_MODE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
async_op_not_supported (MMModem *self,
                        MMModemFn callback,
                        gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (self, callback, user_data);
    info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                               "%s", "Operation not supported");
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
uint_op_not_supported (MMModem *self,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (self, callback, user_data);
    info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                               "%s", "Operation not supported");
    mm_callback_info_schedule (info);
}

static void
uint_call_done (MMModem *modem, guint32 result, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, result);
}

void
mm_gsm_modem_need_authentication (MMGsmModem *self,
                                  MMModemFn callback,
                                  gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->need_authentication)
        MM_GSM_MODEM_GET_INTERFACE (self)->need_authentication (self, callback, user_data);
    else
        async_op_not_supported (MM_MODEM (self), callback, user_data);
}

void
mm_gsm_modem_set_pin (MMGsmModem *self,
                      const char *pin,
                      MMModemFn callback,
                      gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);
    g_return_if_fail (pin != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->set_pin)
        MM_GSM_MODEM_GET_INTERFACE (self)->set_pin (self, pin, callback, user_data);
    else
        async_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_set_pin (MMGsmModem *modem,
                        const char *pin,
                        DBusGMethodInvocation *context)
{
    mm_gsm_modem_set_pin (modem, pin, async_call_done, context);
}

void
mm_gsm_modem_register (MMGsmModem *self,
                       const char *network_id,
                       MMModemFn callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->do_register)
        MM_GSM_MODEM_GET_INTERFACE (self)->do_register (self, network_id, callback, user_data);
    else
        async_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_register (MMGsmModem *modem,
                         const char *network_id,
                         DBusGMethodInvocation *context)
{
    const char *id;

    /* DBus does not support NULL strings, so the caller should pass an empty string
       for manual registration. */
    if (strlen (network_id) < 1)
        id = NULL;
    else
        id = network_id;

    mm_gsm_modem_register (modem, id, async_call_done, context);
}

static void
reg_not_supported (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGsmModemRegInfoFn callback = (MMGsmModemRegInfoFn) mm_callback_info_get_data (info, "reg-info-callback");

    callback (MM_GSM_MODEM (modem), 0, NULL, NULL, error, mm_callback_info_get_data (info, "reg-info-data"));
}

void
mm_gsm_modem_get_reg_info (MMGsmModem *self,
                           MMGsmModemRegInfoFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->get_registration_info)
        MM_GSM_MODEM_GET_INTERFACE (self)->get_registration_info (self, callback, user_data);
    else {
        MMCallbackInfo *info;

        info = mm_callback_info_new (MM_MODEM (self), reg_not_supported, user_data);
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                   "%s", "Operation not supported");

        info->user_data = info;
        mm_callback_info_set_data (info, "reg-info-callback", callback, NULL);
        mm_callback_info_set_data (info, "reg-info-data", user_data, NULL);

        mm_callback_info_schedule (info);
    }
}

static void
get_reg_info_done (MMGsmModem *modem,
                   MMGsmModemRegStatus status,
                   const char *oper_code,
                   const char *oper_name,
                   GError *error,
                   gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else {
        dbus_g_method_return (context, status, 
                              oper_code ? oper_code : "",
                              oper_name ? oper_name : "");
    }
}

static void
impl_gsm_modem_get_reg_info (MMGsmModem *modem, DBusGMethodInvocation *context)
{
    mm_gsm_modem_get_reg_info (modem, get_reg_info_done, context);
}

void
mm_gsm_modem_scan (MMGsmModem *self,
                   MMGsmModemScanFn callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->scan)
        MM_GSM_MODEM_GET_INTERFACE (self)->scan (self, callback, user_data);
    else
        /* FIXME */ ;
}

static void
impl_scan_done (MMGsmModem *modem, GPtrArray *results, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, results);
}

static void
impl_gsm_modem_scan (MMGsmModem *modem,
                     DBusGMethodInvocation *context)
{
    mm_gsm_modem_scan (modem, impl_scan_done, context);
}

void
mm_gsm_modem_set_apn (MMGsmModem *self,
                      const char *apn,
                      MMModemFn callback,
                      gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (apn != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->set_apn)
        MM_GSM_MODEM_GET_INTERFACE (self)->set_apn (self, apn, callback, user_data);
    else
        async_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_set_apn (MMGsmModem *modem,
                        const char *apn,
                        DBusGMethodInvocation *context)
{
    mm_gsm_modem_set_apn (modem, apn, async_call_done, context);
}

void
mm_gsm_modem_get_signal_quality (MMGsmModem *self,
                                 MMModemUIntFn callback,
                                 gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->get_signal_quality)
        MM_GSM_MODEM_GET_INTERFACE (self)->get_signal_quality (self, callback, user_data);
    else
        uint_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_get_signal_quality (MMGsmModem *modem, DBusGMethodInvocation *context)
{
    mm_gsm_modem_get_signal_quality (modem, uint_call_done, context);
}

void
mm_gsm_modem_set_band (MMGsmModem *self,
                       MMGsmModemBand band,
                       MMModemFn callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->set_band)
        MM_GSM_MODEM_GET_INTERFACE (self)->set_band (self, band, callback, user_data);
    else
        async_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_set_band (MMGsmModem *modem,
                         guint32 band,
                         DBusGMethodInvocation *context)
{
    if (band >= MM_GSM_MODEM_BAND_ANY && band <= MM_GSM_MODEM_BAND_LAST) 
        mm_gsm_modem_set_band (modem, (MMGsmModemBand) band, async_call_done, context);
    else {
        GError *error;

        error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "%s", "Invalid band");
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

void
mm_gsm_modem_get_band (MMGsmModem *self,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->get_band)
        MM_GSM_MODEM_GET_INTERFACE (self)->get_band (self, callback, user_data);
    else
        uint_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_get_band (MMGsmModem *modem, DBusGMethodInvocation *context)
{
    mm_gsm_modem_get_band (modem, uint_call_done, context);
}

void
mm_gsm_modem_set_network_mode (MMGsmModem *self,
                               MMGsmModemNetworkMode mode,
                               MMModemFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->set_network_mode)
        MM_GSM_MODEM_GET_INTERFACE (self)->set_network_mode (self, mode, callback, user_data);
    else
        async_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_set_network_mode (MMGsmModem *modem,
                                 guint32 mode,
                                 DBusGMethodInvocation *context)
{
    if (mode >= MM_GSM_MODEM_NETWORK_MODE_ANY && mode <= MM_GSM_MODEM_NETWORK_MODE_LAST)
        mm_gsm_modem_set_network_mode (modem, (MMGsmModemNetworkMode) mode, async_call_done, context);
    else {
        GError *error;

        error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "%s", "Invalid network mode");
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

void
mm_gsm_modem_get_network_mode (MMGsmModem *self,
                               MMModemUIntFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_GSM_MODEM_GET_INTERFACE (self)->get_network_mode)
        MM_GSM_MODEM_GET_INTERFACE (self)->get_network_mode (self, callback, user_data);
    else
        uint_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_gsm_modem_get_network_mode (MMGsmModem *modem, DBusGMethodInvocation *context)
{
    mm_gsm_modem_get_network_mode (modem, uint_call_done, context);
}

void
mm_gsm_modem_signal_quality (MMGsmModem *self,
                             guint32 quality)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));

    g_signal_emit (self, signals[SIGNAL_QUALITY], 0, quality);
}

void
mm_gsm_modem_network_mode (MMGsmModem *self,
                           MMGsmModemNetworkMode mode)
{
    g_return_if_fail (MM_IS_GSM_MODEM (self));

    g_signal_emit (self, signals[NETWORK_MODE], 0, mode);
}


/*****************************************************************************/

static void
mm_gsm_modem_init (gpointer g_iface)
{
    GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Signals */
    signals[SIGNAL_QUALITY] =
        g_signal_new ("signal-quality",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMGsmModem, signal_quality),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    signals[NETWORK_MODE] =
        g_signal_new ("network-mode",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMGsmModem, network_mode),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    initialized = TRUE;
}

GType
mm_gsm_modem_get_type (void)
{
    static GType modem_type = 0;

    if (!G_UNLIKELY (modem_type)) {
        const GTypeInfo modem_info = {
            sizeof (MMGsmModem), /* class_size */
            mm_gsm_modem_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMGsmModem",
                                             &modem_info, 0);

        g_type_interface_add_prerequisite (modem_type, MM_TYPE_MODEM);

        dbus_g_object_type_install_info (modem_type, &dbus_glib_mm_gsm_modem_object_info);
    }

    return modem_type;
}
