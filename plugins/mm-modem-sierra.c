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
                                   NULL));
}

static void
parent_enable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else if (GPOINTER_TO_INT (mm_callback_info_get_data (info, "sierra-enable"))) {
        /* Sierra returns OK on +CFUN=1 right away but needs some time
           to finish initialization */
        sleep (10);
    }

    mm_callback_info_schedule (info);
}

static void
enable (MMModem *modem,
        gboolean enable,
        MMModemFn callback,
        gpointer user_data)
{
    MMModem *parent_modem_iface;
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);
    mm_callback_info_set_data (info, "sierra-enable", GINT_TO_POINTER (enable), NULL);

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
    parent_modem_iface->enable (modem, enable, parent_enable_done, info);
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
