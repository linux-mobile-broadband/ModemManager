/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <dbus/dbus-glib.h>
#include "mm-modem.h"
#include "mm-modem-error.h"
#include "mm-callback-info.h"

static void impl_modem_enable (MMModem *modem, gboolean enable, DBusGMethodInvocation *context);
static void impl_modem_set_pin (MMModem *modem, const char *pin, DBusGMethodInvocation *context);
static void impl_modem_register (MMModem *modem, const char *network_id, DBusGMethodInvocation *context);
static void impl_modem_connect (MMModem *modem, const char *number, const char *apn, DBusGMethodInvocation *context);
static void impl_modem_disconnect (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_scan (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_get_signal_quality (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_set_band (MMModem *modem, guint32 band, DBusGMethodInvocation *context);
static void impl_modem_get_band (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_set_network_mode (MMModem *modem, guint32 mode, DBusGMethodInvocation *context);
static void impl_modem_get_network_mode (MMModem *modem, DBusGMethodInvocation *context);

#include "mm-modem-glue.h"

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
mm_modem_enable (MMModem *self,
                 gboolean enable,
                 MMModemFn callback,
                 gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->enable)
        MM_MODEM_GET_INTERFACE (self)->enable (self, enable, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_enable (MMModem *modem,
                   gboolean enable,
                   DBusGMethodInvocation *context)
{
    mm_modem_enable (modem, enable, async_call_done, context);
}

void
mm_modem_set_pin (MMModem *self,
                  const char *pin,
                  MMModemFn callback,
                  gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);
    g_return_if_fail (pin != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->set_pin)
        MM_MODEM_GET_INTERFACE (self)->set_pin (self, pin, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_set_pin (MMModem *modem,
                    const char *pin,
                    DBusGMethodInvocation *context)
{
    mm_modem_set_pin (modem, pin, async_call_done, context);
}

void
mm_modem_register (MMModem *self,
                   const char *network_id,
                   MMModemFn callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->do_register)
        MM_MODEM_GET_INTERFACE (self)->do_register (self, network_id, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_register (MMModem *modem,
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

    mm_modem_register (modem, id, async_call_done, context);
}

void
mm_modem_connect (MMModem *self,
                  const char *number,
                  const char *apn,
                  MMModemFn callback,
                  gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);
    g_return_if_fail (number != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->connect)
        MM_MODEM_GET_INTERFACE (self)->connect (self, number, apn, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_connect (MMModem *modem,
                    const char *number,
                    const char *apn,
                    DBusGMethodInvocation *context)
{
    const char *real_apn;

    /* DBus does not support NULL strings, so the caller should pass an empty string
       for no APN. */
    if (strlen (apn) < 1)
        real_apn = NULL;
    else
        real_apn = apn;

    mm_modem_connect (modem, number, real_apn, async_call_done, context);
}

void
mm_modem_disconnect (MMModem *self,
                     MMModemFn callback,
                     gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->disconnect)
        MM_MODEM_GET_INTERFACE (self)->disconnect (self, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_disconnect (MMModem *modem,
                       DBusGMethodInvocation *context)
{
    mm_modem_disconnect (modem, async_call_done, context);
}

void
mm_modem_scan (MMModem *self,
               MMModemScanFn callback,
               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->scan)
        MM_MODEM_GET_INTERFACE (self)->scan (self, callback, user_data);
    else
        /* FIXME */ ;
}

static void
impl_scan_done (MMModem *modem, GPtrArray *results, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, results);
}

static void
impl_modem_scan (MMModem *modem,
                 DBusGMethodInvocation *context)
{
    mm_modem_scan (modem, impl_scan_done, context);
}

void
mm_modem_get_signal_quality (MMModem *self,
                             MMModemUIntFn callback,
                             gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->get_signal_quality)
        MM_MODEM_GET_INTERFACE (self)->get_signal_quality (self, callback, user_data);
    else
        uint_op_not_supported (self, callback, user_data);
}

static void
impl_modem_get_signal_quality (MMModem *modem, DBusGMethodInvocation *context)
{
    mm_modem_get_signal_quality (modem, uint_call_done, context);
}

void
mm_modem_set_band (MMModem *self,
                   MMModemBand band,
                   MMModemFn callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->set_band)
        MM_MODEM_GET_INTERFACE (self)->set_band (self, band, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_set_band (MMModem *modem, guint32 band, DBusGMethodInvocation *context)
{
    mm_modem_set_band (modem, band, async_call_done, context);
}

void
mm_modem_get_band (MMModem *self,
                   MMModemUIntFn callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->get_band)
        MM_MODEM_GET_INTERFACE (self)->get_band (self, callback, user_data);
    else
        uint_op_not_supported (self, callback, user_data);
}

static void
impl_modem_get_band (MMModem *modem, DBusGMethodInvocation *context)
{
    mm_modem_get_band (modem, uint_call_done, context);
}

void
mm_modem_set_network_mode (MMModem *self,
                           MMModemNetworkMode mode,
                           MMModemFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->set_network_mode)
        MM_MODEM_GET_INTERFACE (self)->set_network_mode (self, mode, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_set_network_mode (MMModem *modem, guint32 mode, DBusGMethodInvocation *context)
{
    mm_modem_set_network_mode (modem, mode, async_call_done, context);
}

void
mm_modem_get_network_mode (MMModem *self,
                           MMModemUIntFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->get_network_mode)
        MM_MODEM_GET_INTERFACE (self)->get_network_mode (self, callback, user_data);
    else
        uint_op_not_supported (self, callback, user_data);
}

static void
impl_modem_get_network_mode (MMModem *modem, DBusGMethodInvocation *context)
{
    mm_modem_get_network_mode (modem, uint_call_done, context);
}


void
mm_modem_install_dbus_info (GType type)
{
    dbus_g_object_type_install_info (type, &dbus_glib_mm_modem_object_info);
}

void
mm_modem_signal_quality (MMModem *self,
                         guint32 quality)
{
    g_return_if_fail (MM_IS_MODEM (self));

    g_signal_emit (self, signals[SIGNAL_QUALITY], 0, quality);
}

void
mm_modem_network_mode (MMModem *self,
                       MMModemNetworkMode mode)
{
    g_return_if_fail (MM_IS_MODEM (self));

    g_signal_emit (self, signals[NETWORK_MODE], 0, mode);
}

/*****************************************************************************/

static void
mm_modem_init (gpointer g_iface)
{
    GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_DATA_DEVICE,
                              "DataDevice",
                              "DataDevice",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_DRIVER,
                              "Driver",
                              "Driver",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_TYPE,
                            "Type",
                            "Type",
                            0, G_MAXUINT32, 0,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    /* Signals */
    signals[SIGNAL_QUALITY] =
        g_signal_new ("signal-quality",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModem, signal_quality),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    signals[NETWORK_MODE] =
        g_signal_new ("network-mode",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModem, network_mode),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    initialized = TRUE;
}

GType
mm_modem_get_type (void)
{
    static GType modem_type = 0;

    if (!G_UNLIKELY (modem_type)) {
        const GTypeInfo modem_info = {
            sizeof (MMModem), /* class_size */
            mm_modem_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMModem",
                                             &modem_info, 0);

        g_type_interface_add_prerequisite (modem_type, G_TYPE_OBJECT);
    }

    return modem_type;
}
