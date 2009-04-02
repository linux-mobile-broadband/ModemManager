/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-nokia.h"
#include "mm-serial-parsers.h"

static gpointer mm_modem_nokia_parent_class = NULL;

MMModem *
mm_modem_nokia_new (const char *data_device,
                    const char *driver)
{
    g_return_val_if_fail (data_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_NOKIA,
                                   MM_SERIAL_DEVICE, data_device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_TYPE, MM_MODEM_TYPE_GSM,
                                   NULL));
}

/*****************************************************************************/

static void
mm_modem_nokia_init (MMModemNokia *self)
{
    mm_serial_set_response_parser (MM_SERIAL (self),
                                   mm_serial_parser_v1_e1_parse,
                                   mm_serial_parser_v1_e1_new (),
                                   mm_serial_parser_v1_e1_destroy);
}

static void
mm_modem_nokia_class_init (MMModemNokiaClass *klass)
{
    mm_modem_nokia_parent_class = g_type_class_peek_parent (klass);
}

GType
mm_modem_nokia_get_type (void)
{
    static GType modem_nokia_type = 0;

    if (G_UNLIKELY (modem_nokia_type == 0)) {
        static const GTypeInfo modem_nokia_type_info = {
            sizeof (MMModemNokiaClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_nokia_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemNokia),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_nokia_init,
        };

        modem_nokia_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemNokia", &modem_nokia_type_info, 0);
    }

    return modem_nokia_type;
}
