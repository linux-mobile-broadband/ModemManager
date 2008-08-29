/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-gsm-network.h"
#include "mm-modem-error.h"
#include "mm-callback-info.h"

static void impl_gsm_modem_register (MMModemGsmNetwork *modem,
                                     const char *network_id,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_scan (MMModemGsmNetwork *modem,
                                 DBusGMethodInvocation *context);

static void impl_gsm_modem_set_apn (MMModemGsmNetwork *modem,
                                    const char *apn,
                                    DBusGMethodInvocation *context);

static void impl_gsm_modem_get_signal_quality (MMModemGsmNetwork *modem,
                                               DBusGMethodInvocation *context);

static void impl_gsm_modem_set_band (MMModemGsmNetwork *modem,
                                     MMModemGsmNetworkBand band,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_get_band (MMModemGsmNetwork *modem,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_set_network_mode (MMModemGsmNetwork *modem,
                                             MMModemGsmNetworkMode mode,
                                             DBusGMethodInvocation *context);

static void impl_gsm_modem_get_network_mode (MMModemGsmNetwork *modem,
                                             DBusGMethodInvocation *context);

static void impl_gsm_modem_get_reg_info (MMModemGsmNetwork *modem,
                                         DBusGMethodInvocation *context);

#include "mm-modem-gsm-network-glue.h"

/*****************************************************************************/

enum {
    SIGNAL_QUALITY,
    REGISTRATION_INFO,
    NETWORK_MODE,

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
async_call_not_supported (MMModemGsmNetwork *self,
                          MMModemFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
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

static void
uint_call_not_supported (MMModemGsmNetwork *self,
                         MMModemUIntFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);
    info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                               "%s", "Operation not supported");
    mm_callback_info_schedule (info);
}

static void
reg_info_call_done (MMModemGsmNetwork *self,
                    MMModemGsmNetworkRegStatus status,
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
reg_info_not_supported_wrapper (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemGsmNetworkRegInfoFn callback;

    callback = (MMModemGsmNetworkRegInfoFn) mm_callback_info_get_data (info, "reg-info-callback");
    callback (MM_MODEM_GSM_NETWORK (modem), 0, NULL, NULL, error, mm_callback_info_get_data (info, "reg-info-data"));
}

static void
reg_info_call_not_supported (MMModemGsmNetwork *self,
                             MMModemGsmNetworkRegInfoFn callback,
                             gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), reg_info_not_supported_wrapper, NULL);
    info->user_data = info;
    mm_callback_info_set_data (info, "reg-info-callback", callback, NULL);
    mm_callback_info_set_data (info, "reg-info-data", user_data, NULL);
    info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                               "%s", "Operation not supported");

    mm_callback_info_schedule (info);
}

static void
scan_call_done (MMModemGsmNetwork *self,
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

static void
scan_not_supported_wrapper (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemGsmNetworkScanFn callback;

    callback = (MMModemGsmNetworkScanFn) mm_callback_info_get_data (info, "scan-callback");
    callback (MM_MODEM_GSM_NETWORK (modem), NULL, error, mm_callback_info_get_data (info, "scan-data"));
}

static void
scan_call_not_supported (MMModemGsmNetwork *self,
                         MMModemGsmNetworkScanFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), scan_not_supported_wrapper, NULL);
    info->user_data = info;
    mm_callback_info_set_data (info, "scan-callback", callback, NULL);
    mm_callback_info_set_data (info, "scan-data", user_data, NULL);
    info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                               "%s", "Operation not supported");

    mm_callback_info_schedule (info);
}

/*****************************************************************************/

void
mm_modem_gsm_network_register (MMModemGsmNetwork *self,
                               const char *network_id,
                               MMModemFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->do_register)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->do_register (self, network_id, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_scan (MMModemGsmNetwork *self,
                           MMModemGsmNetworkScanFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->scan)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->scan (self, callback, user_data);
    else
        scan_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_set_apn (MMModemGsmNetwork *self,
                              const char *apn,
                              MMModemFn callback,
                              gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (apn != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_apn)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_apn (self, apn, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_get_signal_quality (MMModemGsmNetwork *self,
                                         MMModemUIntFn callback,
                                         gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_signal_quality)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_signal_quality (self, callback, user_data);
    else
        uint_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_set_band (MMModemGsmNetwork *self,
                               MMModemGsmNetworkBand band,
                               MMModemFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_band)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_band (self, band, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_get_band (MMModemGsmNetwork *self,
                               MMModemUIntFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_band)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_band (self, callback, user_data);
    else
        uint_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_set_mode (MMModemGsmNetwork *self,
                               MMModemGsmNetworkMode mode,
                               MMModemFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_network_mode)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_network_mode (self, mode, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_get_mode (MMModemGsmNetwork *self,
                               MMModemUIntFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_network_mode)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_network_mode (self, callback, user_data);
    else
        uint_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_get_registration_info (MMModemGsmNetwork *self,
                                            MMModemGsmNetworkRegInfoFn callback,
                                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_registration_info)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_registration_info (self, callback, user_data);
    else
        reg_info_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_signal_quality (MMModemGsmNetwork *self,
                                     guint32 quality)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));

    g_signal_emit (self, signals[SIGNAL_QUALITY], 0, quality);
}

void
mm_modem_gsm_network_registration_info (MMModemGsmNetwork *self,
                                        MMModemGsmNetworkRegStatus status,
                                        const char *oper_code,
                                        const char *oper_name)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));

    g_signal_emit (self, signals[REGISTRATION_INFO], 0, status, oper_code, oper_name);
}

void
mm_modem_gsm_network_mode (MMModemGsmNetwork *self,
                           MMModemGsmNetworkMode mode)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));

    g_signal_emit (self, signals[NETWORK_MODE], 0, mode);
}

/*****************************************************************************/

static void
impl_gsm_modem_register (MMModemGsmNetwork *modem,
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

    mm_modem_gsm_network_register (modem, id, async_call_done, context);
}

static void
impl_gsm_modem_scan (MMModemGsmNetwork *modem,
                     DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_scan (modem, scan_call_done, context);
}

static void
impl_gsm_modem_set_apn (MMModemGsmNetwork *modem,
                        const char *apn,
                        DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_set_apn (modem, apn, async_call_done, context);
}

static void
impl_gsm_modem_get_signal_quality (MMModemGsmNetwork *modem,
                                   DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_get_signal_quality (modem, uint_call_done, context);
}

static void
impl_gsm_modem_set_band (MMModemGsmNetwork *modem,
                         MMModemGsmNetworkBand band,
                         DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_set_band (modem, band, async_call_done, context);
}

static void
impl_gsm_modem_get_band (MMModemGsmNetwork *modem,
                         DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_get_band (modem, uint_call_done, context);
}

static void
impl_gsm_modem_set_network_mode (MMModemGsmNetwork *modem,
                                 MMModemGsmNetworkMode mode,
                                 DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_set_mode (modem, mode, async_call_done, context);
}

static void
impl_gsm_modem_get_network_mode (MMModemGsmNetwork *modem,
                                 DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_get_mode (modem, uint_call_done, context);
}

static void
impl_gsm_modem_get_reg_info (MMModemGsmNetwork *modem,
                             DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_get_registration_info (modem, reg_info_call_done, context);
}

/*****************************************************************************/

static void
mm_modem_gsm_network_init (gpointer g_iface)
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
                      G_STRUCT_OFFSET (MMModemGsmNetwork, signal_quality),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    /* FIXME */
#if 0
    signals[REGISTRATION_INFO] =
        g_signal_new ("registration-info",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemGsmNetwork, registration_info),
                      NULL, NULL,
                      mm_marshal_VOID__UINT_STRING_STRING,
                      G_TYPE_NONE, 3,
                      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
#endif

    signals[NETWORK_MODE] =
        g_signal_new ("network-mode",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemGsmNetwork, network_mode),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    initialized = TRUE;
}

GType
mm_modem_gsm_network_get_type (void)
{
    static GType network_type = 0;

    if (!G_UNLIKELY (network_type)) {
        const GTypeInfo network_info = {
            sizeof (MMModemGsmNetwork), /* class_size */
            mm_modem_gsm_network_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        network_type = g_type_register_static (G_TYPE_INTERFACE,
                                               "MMModemGsmNetwork",
                                               &network_info, 0);

        g_type_interface_add_prerequisite (network_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (network_type, &dbus_glib_mm_modem_gsm_network_object_info);
    }

    return network_type;
}
