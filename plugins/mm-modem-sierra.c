/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-sierra.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static gpointer mm_modem_sierra_parent_class = NULL;

MMModem *
mm_modem_sierra_new (const char *data_device,
                     const char *driver)
{
    g_return_val_if_fail (data_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_SIERRA,
                                   MM_SERIAL_DEVICE, data_device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_TYPE, MM_MODEM_TYPE_GSM,
                                   NULL));
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static void
pin_check_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    mm_callback_info_schedule (info);
}

static gboolean
sierra_enabled (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;

    /* Now check the PIN explicitly, sierra doesn't seem to report
       that it needs it otherwise */
    mm_generic_gsm_check_pin (MM_GENERIC_GSM (info->modem), pin_check_done, info);

    return FALSE;
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
        /* Sierra returns OK on +CFUN=1 right away but needs some time
           to finish initialization */
        g_timeout_add_seconds (10, sierra_enabled, info);
}

static void
enable_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_serial_queue_command (serial, "Z E0 V1 X4 &C1 +CMEE=1", 3, init_done, user_data);
}

static void
disable_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_serial_close (serial);
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
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
    mm_callback_info_set_data (info, "sierra-enable", GINT_TO_POINTER (enable), NULL);

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
modem_init (MMModem *modem_class)
{
    modem_class->enable = enable;
}

static void
mm_modem_sierra_init (MMModemSierra *self)
{
}

static void
mm_modem_sierra_class_init (MMModemSierraClass *klass)
{
    mm_modem_sierra_parent_class = g_type_class_peek_parent (klass);
}

GType
mm_modem_sierra_get_type (void)
{
    static GType modem_sierra_type = 0;

    if (G_UNLIKELY (modem_sierra_type == 0)) {
        static const GTypeInfo modem_sierra_type_info = {
            sizeof (MMModemSierraClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_sierra_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemSierra),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_sierra_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        modem_sierra_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemSierra", &modem_sierra_type_info, 0);
        g_type_add_interface_static (modem_sierra_type, MM_TYPE_MODEM, &modem_iface_info);
    }

    return modem_sierra_type;
}
