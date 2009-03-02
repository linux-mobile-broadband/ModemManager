/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-novatel-cdma.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static gpointer mm_modem_novatel_cdma_parent_class = NULL;

MMModem *
mm_modem_novatel_cdma_new (const char *data_device,
                           const char *driver)
{
    g_return_val_if_fail (data_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_NOVATEL_CDMA,
                                   MM_SERIAL_DEVICE, data_device,
                                   MM_SERIAL_CARRIER_DETECT, FALSE,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_TYPE, MM_MODEM_TYPE_CDMA,
                                   NULL));
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
}

static void
mm_modem_novatel_cdma_init (MMModemNovatelCdma *self)
{
}

static void
mm_modem_novatel_cdma_class_init (MMModemNovatelCdmaClass *klass)
{
    mm_modem_novatel_cdma_parent_class = g_type_class_peek_parent (klass);
}

GType
mm_modem_novatel_cdma_get_type (void)
{
    static GType modem_novatel_cdma_type = 0;

    if (G_UNLIKELY (modem_novatel_cdma_type == 0)) {
        static const GTypeInfo modem_novatel_cdma_type_info = {
            sizeof (MMModemNovatelCdmaClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_novatel_cdma_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemNovatelCdma),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_novatel_cdma_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        modem_novatel_cdma_type = g_type_register_static (MM_TYPE_GENERIC_CDMA, "MMModemNovatelCdma", &modem_novatel_cdma_type_info, 0);
        g_type_add_interface_static (modem_novatel_cdma_type, MM_TYPE_MODEM, &modem_iface_info);
    }

    return modem_novatel_cdma_type;
}
