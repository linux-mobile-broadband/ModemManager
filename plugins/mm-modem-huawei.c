/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mm-modem-huawei.h"
#include "mm-modem-gsm-network.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-util.h"
#include "mm-serial-parsers.h"

static gpointer mm_modem_huawei_parent_class = NULL;

#define MM_MODEM_HUAWEI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_HUAWEI, MMModemHuaweiPrivate))

typedef struct {
    MMSerial *monitor_device;
    GRegex *status_regex;
    GRegex *reg_state_regex;
    gpointer std_parser;
} MMModemHuaweiPrivate;

MMModem *
mm_modem_huawei_new (const char *data_device,
                     const char *monitor_device,
                     const char *driver)
{
    g_return_val_if_fail (data_device != NULL, NULL);
    g_return_val_if_fail (monitor_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_HUAWEI,
                                   MM_SERIAL_DEVICE, monitor_device,
                                   MM_MODEM_DATA_DEVICE, data_device,
                                   MM_MODEM_DRIVER, driver,
                                   NULL));
}

static void
parent_enable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else
        /* Enable unsolicited registration state changes */
        mm_serial_queue_command (MM_SERIAL (modem), "+CREG=1", 5, NULL, NULL);

    mm_callback_info_schedule (info);
}

static void
enable (MMModem *modem,
        gboolean enable,
        MMModemFn callback,
        gpointer user_data)
{
    MMModem *parent_modem_iface;

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));

    if (enable) {
        MMCallbackInfo *info;

        info = mm_callback_info_new (modem, callback, user_data);
        parent_modem_iface->enable (modem, enable, parent_enable_done, info);
    } else
        parent_modem_iface->enable (modem, enable, callback, user_data);
}

static gboolean
parse_syscfg (const char *reply, int *mode_a, int *mode_b, guint32 *band, int *unknown1, int *unknown2)
{
    if (reply == NULL || strncmp (reply, "^SYSCFG:", 8))
        return FALSE;

    if (sscanf (reply + 8, "%d,%d,%x,%d,%d", mode_a, mode_b, band, unknown1, unknown2))
        return TRUE;

    return FALSE;
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
set_network_mode_get_done (MMSerial *serial,
                           GString *response,
                           GError *error,
                           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        int a, b, u1, u2;
        guint32 band;

        if (parse_syscfg (response->str, &a, &b, &band, &u1, &u2)) {
            char *command;

            a = GPOINTER_TO_INT (mm_callback_info_get_data (info, "mode-a"));
            b = GPOINTER_TO_INT (mm_callback_info_get_data (info, "mode-b"));
            command = g_strdup_printf ("AT^SYSCFG=%d,%d,%X,%d,%d", a, b, band, u1, u2);
            mm_serial_queue_command (serial, command, 3, set_network_mode_done, info);
            g_free (command);
        }
    }
}

static void
set_network_mode (MMModemGsmNetwork *modem,
                  MMModemGsmNetworkMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    switch (mode) {
    case MM_MODEM_GSM_NETWORK_MODE_ANY:
        /* Do nothing */
        mm_callback_info_schedule (info);
        return;
        break;
    case MM_MODEM_GSM_NETWORK_MODE_GPRS:
    case MM_MODEM_GSM_NETWORK_MODE_EDGE:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (13), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (1), NULL);
        break;
    case MM_MODEM_GSM_NETWORK_MODE_3G:
    case MM_MODEM_GSM_NETWORK_MODE_HSDPA:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (14), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (2), NULL);
        break;
    case MM_MODEM_GSM_NETWORK_MODE_PREFER_2G:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (2), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (1), NULL);
        break;
    case MM_MODEM_GSM_NETWORK_MODE_PREFER_3G:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (2), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (2), NULL);
        break;
    default:
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid mode.");
        mm_callback_info_schedule (info);
        return;
        break;
    }

    mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, set_network_mode_get_done, info);
}

static void
get_network_mode_done (MMSerial *serial,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else {
        int a, b, u1, u2;
        guint32 band;
        guint32 result = 0;

        if (parse_syscfg (response->str, &a, &b, &band, &u1, &u2)) {
            if (a == 2 && b == 1)
                result = MM_MODEM_GSM_NETWORK_MODE_PREFER_2G;
            else if (a == 2 && b == 2)
                result = MM_MODEM_GSM_NETWORK_MODE_PREFER_3G;
            else if (a == 13 && b == 1)
                result = MM_MODEM_GSM_NETWORK_MODE_GPRS;
            else if (a == 14 && b == 2)
                result = MM_MODEM_GSM_NETWORK_MODE_3G;
        }

        if (result == 0)
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                               "Could not parse network mode results");
        else
            mm_callback_info_set_result (info, GUINT_TO_POINTER (result), NULL);
    }

    mm_callback_info_schedule (info);
}

static void
get_network_mode (MMModemGsmNetwork *modem,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, get_network_mode_done, info);
}

static void
set_band_done (MMSerial *serial,
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
set_band_get_done (MMSerial *serial,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        int a, b, u1, u2;
        guint32 band;

        if (parse_syscfg (response->str, &a, &b, &band, &u1, &u2)) {
            char *command;

            band = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "band"));
            command = g_strdup_printf ("AT^SYSCFG=%d,%d,%X,%d,%d", a, b, band, u1, u2);
            mm_serial_queue_command (serial, command, 3, set_band_done, info);
            g_free (command);
        }
    }
}

static void
set_band (MMModemGsmNetwork *modem,
          MMModemGsmNetworkBand band,
          MMModemFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    switch (band) {
    case MM_MODEM_GSM_NETWORK_BAND_ANY:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (0x3FFFFFFF), NULL);
        break;
    case MM_MODEM_GSM_NETWORK_BAND_EGSM:
    case MM_MODEM_GSM_NETWORK_BAND_DCS:
    case MM_MODEM_GSM_NETWORK_BAND_U2100:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (0x400380), NULL);
        break;
    case MM_MODEM_GSM_NETWORK_BAND_PCS:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (0x200000), NULL);
        break;
    default:
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid band.");
        mm_callback_info_schedule (info);
        return;
        break;
    }

    mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, set_band_get_done, info);
}

static void
get_band_done (MMSerial *serial,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else {
        int a, b, u1, u2;
        guint32 band;
        guint32 result = 0xdeadbeaf;

        if (parse_syscfg (response->str, &a, &b, &band, &u1, &u2)) {
            if (band == 0x3FFFFFFF)
                result = MM_MODEM_GSM_NETWORK_BAND_ANY;
            else if (band == 0x400380)
                result = MM_MODEM_GSM_NETWORK_BAND_DCS;
            else if (band == 0x200000)
                result = MM_MODEM_GSM_NETWORK_BAND_PCS;
        }

        if (result == 0xdeadbeaf)
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                               "Could not parse band results");
        else
            mm_callback_info_set_result (info, GUINT_TO_POINTER (result), NULL);
    }

    mm_callback_info_schedule (info);
}

static void
get_band (MMModemGsmNetwork *modem,
          MMModemUIntFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, get_band_done, info);
}

static void
handle_status_change (const char *str, gpointer data)
{
    if (g_str_has_prefix (str, "RSSI:")) {
        int quality = atoi (str + 5);

        if (quality == 99)
            /* 99 means unknown */
            quality = 0;
        else
            /* Normalize the quality */
            quality = quality * 100 / 31;

        g_debug ("Signal quality: %d", quality);
        mm_modem_gsm_network_signal_quality (MM_MODEM_GSM_NETWORK (data), (guint32) quality);
    } else if (g_str_has_prefix (str, "MODE:")) {
        MMModemGsmNetworkMode mode = 0;
        int a;
        int b;

        if (sscanf (str + 5, "%d,%d", &a, &b)) {
            if (a == 3 && b == 2)
                mode = MM_MODEM_GSM_NETWORK_MODE_GPRS;
            else if (a == 3 && b == 3)
                mode = MM_MODEM_GSM_NETWORK_MODE_EDGE;
            else if (a == 5 && b == 4)
                mode = MM_MODEM_GSM_NETWORK_MODE_3G;
            else if (a ==5 && b == 5)
                mode = MM_MODEM_GSM_NETWORK_MODE_HSDPA;

            if (mode) {
                g_debug ("Mode: %d", mode);
                mm_modem_gsm_network_mode (MM_MODEM_GSM_NETWORK (data), mode);
            }
        }
    }
}

static void
reg_state_changed (const char *str, gpointer data)
{
    int i;
    MMModemGsmNetworkRegStatus status;

    i = atoi (str);
    switch (i) {
    case 0:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE;
        break;
    case 1:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_HOME;
        break;
    case 2:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING;
        break;
    case 3:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED;
        break;
    case 4:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN;
        break;
    case 5:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING;
        break;
    default:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN;
        break;
    }

    g_print ("Registration state changed: %d\n", status);
    mm_generic_gsm_set_reg_status (MM_GENERIC_GSM (data), status);
}

static gboolean
huawei_parse_response (gpointer data, GString *response, GError **error)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (data);

    mm_util_strip_string (response, priv->status_regex, handle_status_change, data);
    mm_util_strip_string (response, priv->reg_state_regex, reg_state_changed, data);

    return mm_serial_parser_v1_parse (priv->std_parser, response, error);
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
    class->set_band = set_band;
    class->get_band = get_band;
}

static void
mm_modem_huawei_init (MMModemHuawei *self)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (self);

    priv->status_regex = g_regex_new ("\\r\\n\\^(.+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    priv->reg_state_regex = g_regex_new ("\\r\\n\\+CREG: (\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    priv->std_parser = mm_serial_parser_v1_new ();
    mm_serial_set_response_parser (MM_SERIAL (self), huawei_parse_response, self, NULL);
}

static void
finalize (GObject *object)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (object);

    mm_serial_parser_v1_destroy (priv->std_parser);
    g_regex_unref (priv->status_regex);
    g_regex_unref (priv->reg_state_regex);

    G_OBJECT_CLASS (mm_modem_huawei_parent_class)->finalize (object);
}

static void
mm_modem_huawei_class_init (MMModemHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_modem_huawei_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemHuaweiPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}

GType
mm_modem_huawei_get_type (void)
{
    static GType modem_huawei_type = 0;

    if (G_UNLIKELY (modem_huawei_type == 0)) {
        static const GTypeInfo modem_huawei_type_info = {
            sizeof (MMModemHuaweiClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_huawei_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemHuawei),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_huawei_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        static const GInterfaceInfo modem_gsm_network_info = {
            (GInterfaceInitFunc) modem_gsm_network_init
        };

        modem_huawei_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemHuawei", &modem_huawei_type_info, 0);

        g_type_add_interface_static (modem_huawei_type, MM_TYPE_MODEM, &modem_iface_info);
        g_type_add_interface_static (modem_huawei_type, MM_TYPE_MODEM_GSM_NETWORK, &modem_gsm_network_info);
    }

    return modem_huawei_type;
}
