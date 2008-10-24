/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
  Additions to NetworkManager, network-manager-applet and modemmanager
  for supporting Ericsson modules like F3507g.

  Author: Per Hallsmark <per@hallsmark.se>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the

  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <dbus/dbus-glib.h>
#include "mm-modem-mbm.h"
#include "mm-serial.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

#include "mm-modem-gsm-mbm-glue.h"

static gpointer mm_modem_mbm_parent_class = NULL;

#define MM_MODEM_MBM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_MBM, MMModemMbmPrivate))

typedef struct {
    char *network_device;
} MMModemMbmPrivate;

enum {
    PROP_0,
    PROP_NETWORK_DEVICE,

    LAST_PROP
};

MMModem *
mm_modem_mbm_new (const char *serial_device,
                  const char *network_device,
                  const char *driver)
{
    g_return_val_if_fail (serial_device != NULL, NULL);
    g_return_val_if_fail (network_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_MBM,
                                   MM_SERIAL_DEVICE, serial_device,
                                   MM_SERIAL_SEND_DELAY, (guint64) 10000,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_MBM_NETWORK_DEVICE, network_device,
                                   NULL));
}

static void
eiapsw_done (MMSerial *serial,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
eiac_done (MMSerial *serial,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *command;

    if (error)
        info->error = g_error_copy (error);

    command = g_strdup_printf ("AT*EIAPSW=1,1,\"%s\"",
                               (char *) mm_callback_info_get_data (info, "apn"));

    mm_serial_queue_command (serial, command, 10, eiapsw_done, info);
    g_free (command);
}

static void
eiad_done (MMSerial *serial,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

    mm_serial_queue_command (serial, "AT*EIAC=1", 10, eiac_done, info);
}

static void
set_apn (MMModemGsmNetwork *modem,
         const char *apn,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_callback_info_set_data (info, "apn", g_strdup (apn), g_free);

    mm_serial_queue_command (MM_SERIAL (modem), "AT*EIAD=0", 10, eiad_done, info);
}

static void
do_register_done (MMSerial *serial,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
do_register (MMModemGsmNetwork *modem,
             const char *network_id,
             MMModemFn callback,
             gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    sleep (4);

    mm_serial_queue_command (MM_SERIAL (modem), "AT*ENAP=1,1", 10, do_register_done, info);
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static void
enable_done (MMSerial *serial,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
init_done (MMSerial *serial,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else
        mm_serial_queue_command (serial, "+CFUN=1", 5, enable_done, user_data);
}

static void
enable_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_serial_queue_command (serial, "&F E0 +CMEE=1", 3, init_done, user_data);
}

static void
disable_done (MMSerial *serial,
              GString *response,
              GError *error,
              gpointer user_data)
{
    mm_serial_close (serial);
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
}

static void
disable_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_serial_queue_command (serial, "+CFUN=4", 5, disable_done, user_data);
}


static void
enable (MMModem *modem,
        gboolean enable,
        MMModemFn callback,
        gpointer user_data)
{
	MMCallbackInfo *info;

    /* First, reset the previously used CID */
    mm_generic_gsm_set_cid (MM_GENERIC_GSM (modem), 0);

    info = mm_callback_info_new (modem, callback, user_data);

    if (!enable) {
        if (mm_serial_is_connected (MM_SERIAL (modem)))
            mm_serial_flash (MM_SERIAL (modem), 1000, disable_flash_done, info);
        else
            disable_flash_done (MM_SERIAL (modem), info);
    } else {
        if (mm_serial_open (MM_SERIAL (modem), &info->error))
            mm_serial_flash (MM_SERIAL (modem), 100, enable_flash_done, info);

        if (info->error)
            mm_callback_info_schedule (info);
    }

}

/*****************************************************************************/

static void
mm_modem_mbm_init (MMModemMbm *self)
{
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->enable = enable;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->do_register = do_register;
    class->set_apn = set_apn;
}

static GObject*
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
    GObject *object;
    MMModemMbmPrivate *priv;

    object = G_OBJECT_CLASS (mm_modem_mbm_parent_class)->constructor (type,
                                                                      n_construct_params,
                                                                      construct_params);
    if (!object)
        return NULL;

    priv = MM_MODEM_MBM_GET_PRIVATE (object);

    if (!priv->network_device) {
        g_warning ("No network device provided");
        g_object_unref (object);
        return NULL;
    }

    return object;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NETWORK_DEVICE:
        /* Construct only */
        priv->network_device = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NETWORK_DEVICE:
        g_value_set_string (value, priv->network_device);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}
static void
finalize (GObject *object)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (object);

    g_free (priv->network_device);

    G_OBJECT_CLASS (mm_modem_mbm_parent_class)->finalize (object);
}

static void
mm_modem_mbm_class_init (MMModemMbmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_modem_mbm_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemMbmPrivate));

    /* Virtual methods */
    object_class->constructor = constructor;
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;
    /* Properties */
    g_object_class_install_property
        (object_class, PROP_NETWORK_DEVICE,
         g_param_spec_string (MM_MODEM_MBM_NETWORK_DEVICE,
                              "NetworkDevice",
                              "Network device",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

GType
mm_modem_mbm_get_type (void)
{
    static GType modem_mbm_type = 0;

    if (G_UNLIKELY (modem_mbm_type == 0)) {
        static const GTypeInfo modem_mbm_type_info = {
            sizeof (MMModemMbmClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_mbm_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemMbm),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_mbm_init,
        };

        static const GInterfaceInfo modem_iface_info = {
            (GInterfaceInitFunc) modem_init
        };

        static const GInterfaceInfo modem_gsm_network_info = {
            (GInterfaceInitFunc) modem_gsm_network_init
        };

        modem_mbm_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemMbm", &modem_mbm_type_info, 0);

        g_type_add_interface_static (modem_mbm_type, MM_TYPE_MODEM, &modem_iface_info);
        g_type_add_interface_static (modem_mbm_type, MM_TYPE_MODEM_GSM_NETWORK, &modem_gsm_network_info);
    }

    return modem_mbm_type;
}
