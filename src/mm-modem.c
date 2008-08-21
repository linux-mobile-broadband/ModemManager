/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <dbus/dbus-glib.h>
#include "mm-modem.h"
#include "mm-modem-error.h"
#include "mm-callback-info.h"

static void impl_modem_enable (MMModem *modem, gboolean enable, DBusGMethodInvocation *context);
static void impl_modem_connect (MMModem *modem, const char *number, DBusGMethodInvocation *context);
static void impl_modem_disconnect (MMModem *modem, DBusGMethodInvocation *context);

#include "mm-modem-glue.h"

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
mm_modem_connect (MMModem *self,
                  const char *number,
                  MMModemFn callback,
                  gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);
    g_return_if_fail (number != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->connect)
        MM_MODEM_GET_INTERFACE (self)->connect (self, number, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_connect (MMModem *modem,
                    const char *number,
                    DBusGMethodInvocation *context)
{
    mm_modem_connect (modem, number, async_call_done, context);
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


/*****************************************************************************/

static void
mm_modem_init (gpointer g_iface)
{
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

        dbus_g_object_type_install_info (modem_type, &dbus_glib_mm_modem_object_info);
    }

    return modem_type;
}
