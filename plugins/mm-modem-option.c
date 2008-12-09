/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-option.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static gpointer mm_modem_option_parent_class = NULL;

MMModem *
mm_modem_option_new (const char *data_device,
                     const char *driver)
{
    g_return_val_if_fail (data_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_OPTION,
                                   MM_SERIAL_DEVICE, data_device,
                                   MM_MODEM_DRIVER, driver,
                                   NULL));
}

static void
pin_check_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    mm_callback_info_schedule (info);
}

static gboolean
option_enabled (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;

    /* Now check the PIN explicitly, option doesn't seem to report
       that it needs it otherwise */
    mm_generic_gsm_check_pin (MM_GENERIC_GSM (info->modem), pin_check_done, info);

    return FALSE;
}

static void
parent_enable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else if (GPOINTER_TO_INT (mm_callback_info_get_data (info, "option-enable"))) {
        /* Option returns OK on +CFUN=1 right away but needs some time
           to finish initialization */
        g_timeout_add_seconds (10, option_enabled, info);
        return;
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
    mm_callback_info_set_data (info, "option-enable", GINT_TO_POINTER (enable), NULL);

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
    parent_modem_iface->enable (modem, enable, parent_enable_done, info);
}

static void
get_network_mode_done (MMSerial *serial,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean parsed = FALSE;

    if (error)
        info->error = g_error_copy (error);
    else if (!g_str_has_prefix (response->str, "_OPSYS: ")) {
        int a, b;

        if (sscanf (response->str + 8, "%d,%d", &a, &b)) {
            MMModemGsmNetworkMode mode = MM_MODEM_GSM_NETWORK_MODE_ANY;

            switch (a) {
            case 0:
                mode = MM_MODEM_GSM_NETWORK_MODE_GPRS;
                break;
            case 1:
                mode = MM_MODEM_GSM_NETWORK_MODE_3G;
                break;
            case 2:
                mode = MM_MODEM_GSM_NETWORK_MODE_PREFER_2G;
                break;
            case 3:
                mode = MM_MODEM_GSM_NETWORK_MODE_PREFER_3G;
                break;
            default:
                break;
            }

            mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
            parsed = TRUE;
        }
    }

    if (!error && !parsed)
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse network mode results");

    mm_callback_info_schedule (info);
}

static void
get_network_mode (MMModemGsmNetwork *modem,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "AT_OPSYS?", 3, get_network_mode_done, info);
}

static void
set_network_mode_done (MMSerial *serial,
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
set_network_mode (MMModemGsmNetwork *modem,
                  MMModemGsmNetworkMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    int i;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    switch (mode) {
    case MM_MODEM_GSM_NETWORK_MODE_ANY:
    case MM_MODEM_GSM_NETWORK_MODE_GPRS:
        i = 0;
        break;
    case MM_MODEM_GSM_NETWORK_MODE_3G:
        i = 1;
        break;
    case MM_MODEM_GSM_NETWORK_MODE_PREFER_2G:
        i = 2;
        break;
    case MM_MODEM_GSM_NETWORK_MODE_PREFER_3G:
        i = 3;
        break;
    default:
        i = 5;
        break;
    }

    command = g_strdup_printf ("AT_OPSYS=%d,2", i);
    mm_serial_queue_command (MM_SERIAL (modem), command, 3, set_network_mode_done, info);
    g_free (command);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->enable = enable;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->set_network_mode = set_network_mode;
    class->get_network_mode = get_network_mode;
}

static void
mm_modem_option_init (MMModemOption *self)
{
}

static void
mm_modem_option_class_init (MMModemOptionClass *klass)
{
    mm_modem_option_parent_class = g_type_class_peek_parent (klass);
}

GType
mm_modem_option_get_type (void)
{
    static GType modem_option_type = 0;

    if (G_UNLIKELY (modem_option_type == 0)) {
        static const GTypeInfo modem_option_type_info = {
            sizeof (MMModemOptionClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_option_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemOption),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_option_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        static const GInterfaceInfo modem_gsm_network_info = {
            (GInterfaceInitFunc) modem_gsm_network_init
        };

        modem_option_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemOption", &modem_option_type_info, 0);

        g_type_add_interface_static (modem_option_type, MM_TYPE_MODEM, &modem_iface_info);
        g_type_add_interface_static (modem_option_type, MM_TYPE_MODEM_GSM_NETWORK, &modem_gsm_network_info);
    }

    return modem_option_type;
}
