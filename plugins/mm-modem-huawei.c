/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mm-modem-huawei.h"
#include "mm-modem-gsm-network.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-parsers.h"

static gpointer mm_modem_huawei_parent_class = NULL;

#define MM_MODEM_HUAWEI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_HUAWEI, MMModemHuaweiPrivate))

typedef struct {
    MMSerial *monitor_device;

    /* Cached state */
    guint signal_quality;
    MMModemGsmNetworkMode mode;
    MMModemGsmNetworkBand band;
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
                                   MM_MODEM_DEVICE, data_device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_TYPE, MM_MODEM_TYPE_GSM,
                                   NULL));
}

static gboolean
parse_syscfg (MMModemHuawei *self,
              const char *reply,
              int *mode_a,
              int *mode_b,
              guint32 *band,
              int *unknown1,
              int *unknown2)
{
    if (reply == NULL || strncmp (reply, "^SYSCFG:", 8))
        return FALSE;

    if (sscanf (reply + 8, "%d,%d,%x,%d,%d", mode_a, mode_b, band, unknown1, unknown2)) {
        MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (self);
                
        /* Network mode */
        if (*mode_a == 2 && *mode_b == 1)
            priv->mode = MM_MODEM_GSM_NETWORK_MODE_2G_PREFERRED;
        else if (*mode_a == 2 && *mode_b == 2)
            priv->mode = MM_MODEM_GSM_NETWORK_MODE_3G_PREFERRED;
        else if (*mode_a == 13 && *mode_b == 1)
            priv->mode = MM_MODEM_GSM_NETWORK_MODE_2G_ONLY;
        else if (*mode_a == 14 && *mode_b == 2)
            priv->mode = MM_MODEM_GSM_NETWORK_MODE_3G_ONLY;

        /* Band */
        if (*band == 0x3FFFFFFF)
            priv->band = MM_MODEM_GSM_NETWORK_BAND_ANY;
        else if (*band == 0x400380)
            priv->band = MM_MODEM_GSM_NETWORK_BAND_DCS;
        else if (*band == 0x200000)
            priv->band = MM_MODEM_GSM_NETWORK_BAND_PCS;

        return TRUE;
    }

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
    else
        /* Success, cache the value */
        MM_MODEM_HUAWEI_GET_PRIVATE (serial)->mode = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "mode"));

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

        if (parse_syscfg (MM_MODEM_HUAWEI (serial), response->str, &a, &b, &band, &u1, &u2)) {
            char *command;

            switch (GPOINTER_TO_UINT (mm_callback_info_get_data (info, "mode"))) {
            case MM_MODEM_GSM_NETWORK_MODE_GPRS:
            case MM_MODEM_GSM_NETWORK_MODE_EDGE:
            case MM_MODEM_GSM_NETWORK_MODE_2G_ONLY:
                a = 13;
                b = 1;
                break;
            case MM_MODEM_GSM_NETWORK_MODE_UMTS:
            case MM_MODEM_GSM_NETWORK_MODE_HSDPA:
            case MM_MODEM_GSM_NETWORK_MODE_HSUPA:
            case MM_MODEM_GSM_NETWORK_MODE_HSPA:
            case MM_MODEM_GSM_NETWORK_MODE_3G_ONLY:
                a = 14;
                b = 2;
                break;
            case MM_MODEM_GSM_NETWORK_MODE_2G_PREFERRED:
                a = 2;
                b = 1;
                break;
            case MM_MODEM_GSM_NETWORK_MODE_3G_PREFERRED:
                a = 2;
                b = 2;
                break;
            default:
                break;
            }

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
        break;
    case MM_MODEM_GSM_NETWORK_MODE_GPRS:
    case MM_MODEM_GSM_NETWORK_MODE_EDGE:
    case MM_MODEM_GSM_NETWORK_MODE_UMTS:
    case MM_MODEM_GSM_NETWORK_MODE_HSDPA:
    case MM_MODEM_GSM_NETWORK_MODE_HSUPA:
    case MM_MODEM_GSM_NETWORK_MODE_HSPA:
    case MM_MODEM_GSM_NETWORK_MODE_2G_PREFERRED:
    case MM_MODEM_GSM_NETWORK_MODE_3G_PREFERRED:
    case MM_MODEM_GSM_NETWORK_MODE_2G_ONLY:
    case MM_MODEM_GSM_NETWORK_MODE_3G_ONLY:
        /* Allowed values */
        mm_callback_info_set_data (info, "mode", GUINT_TO_POINTER (mode), NULL);
        mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, set_network_mode_get_done, info);
        return;
        break;
    default:
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid mode.");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
get_network_mode_done (MMSerial *serial,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int mode_a, mode_b, u1, u2;
    guint32 band;

    if (error)
        info->error = g_error_copy (error);
    else if (parse_syscfg (MM_MODEM_HUAWEI (serial), response->str, &mode_a, &mode_b, &band, &u1, &u2))
        mm_callback_info_set_result (info, GUINT_TO_POINTER (MM_MODEM_HUAWEI_GET_PRIVATE (serial)->mode), NULL);

    mm_callback_info_schedule (info);
}

static void
get_network_mode (MMModemGsmNetwork *modem,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (modem);

    if (priv->mode != MM_MODEM_GSM_NETWORK_MODE_ANY) {
        /* have cached mode (from an unsolicited message). Use that */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->mode), NULL);
        mm_callback_info_schedule (info);
    } else {
        /* Get it from modem */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, get_network_mode_done, info);
    }
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
    else
        /* Success, cache the value */
        MM_MODEM_HUAWEI_GET_PRIVATE (serial)->band = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "band"));

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

        if (parse_syscfg (MM_MODEM_HUAWEI (serial), response->str, &a, &b, &band, &u1, &u2)) {
            char *command;

            switch (GPOINTER_TO_UINT (mm_callback_info_get_data (info, "band"))) {
            case MM_MODEM_GSM_NETWORK_BAND_ANY:
                band = 0x3FFFFFFF;
                break;
            case MM_MODEM_GSM_NETWORK_BAND_EGSM:
            case MM_MODEM_GSM_NETWORK_BAND_DCS:
            case MM_MODEM_GSM_NETWORK_BAND_U2100:
                band = 0x400380;
                break;
            case MM_MODEM_GSM_NETWORK_BAND_PCS:
                band = 0x200000;
                break;
            default:
                break;
            }

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
    case MM_MODEM_GSM_NETWORK_BAND_EGSM:
    case MM_MODEM_GSM_NETWORK_BAND_DCS:
    case MM_MODEM_GSM_NETWORK_BAND_U2100:
    case MM_MODEM_GSM_NETWORK_BAND_PCS:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (band), NULL);
        mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, set_band_get_done, info);
        break;
    default:
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid band.");
        mm_callback_info_schedule (info);
        break;
    }
}

static void
get_band_done (MMSerial *serial,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int mode_a, mode_b, u1, u2;
    guint32 band;

    if (error)
        info->error = g_error_copy (error);
    else if (parse_syscfg (MM_MODEM_HUAWEI (serial), response->str, &mode_a, &mode_b, &band, &u1, &u2))
        mm_callback_info_set_result (info, GUINT_TO_POINTER (MM_MODEM_HUAWEI_GET_PRIVATE (serial)->band), NULL);

    mm_callback_info_schedule (info);
}

static void
get_band (MMModemGsmNetwork *modem,
          MMModemUIntFn callback,
          gpointer user_data)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (modem);

    if (priv->band != MM_MODEM_GSM_NETWORK_BAND_ANY) {
        /* have cached mode (from an unsolicited message). Use that */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->band), NULL);
        mm_callback_info_schedule (info);
    } else {
        /* Get it from modem */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_serial_queue_command (MM_SERIAL (modem), "AT^SYSCFG?", 3, get_band_done, info);
    }
}

static void
get_signal_quality (MMModemGsmNetwork *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (modem);

    if (priv->signal_quality) {
        /* have cached signal quality (from an unsolicited message). Use that */
        MMCallbackInfo *info;

        info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->signal_quality), NULL);
        mm_callback_info_schedule (info);
    } else {
        /* Use the generic implementation */
        MMModemGsmNetwork *parent_gsm_network_iface;

        parent_gsm_network_iface = g_type_interface_peek_parent (MM_MODEM_GSM_NETWORK_GET_INTERFACE (modem));
        parent_gsm_network_iface->get_signal_quality (modem, callback, user_data);
    }
}

/* Unsolicited message handlers */

static void
handle_signal_quality_change (MMSerial *serial,
                              GMatchInfo *match_info,
                              gpointer user_data)
{
    char *str;
    int quality;

    str = g_match_info_fetch (match_info, 1);
    quality = atoi (str);
    g_free (str);

    if (quality == 99)
        /* 99 means unknown */
        quality = 0;
    else
        /* Normalize the quality */
        quality = quality * 100 / 31;

    g_debug ("Signal quality: %d", quality);
    MM_MODEM_HUAWEI_GET_PRIVATE (serial)->signal_quality = (guint32) quality;
    mm_modem_gsm_network_signal_quality (MM_MODEM_GSM_NETWORK (serial), (guint32) quality);
}

static void
handle_mode_change (MMSerial *serial,
                    GMatchInfo *match_info,
                    gpointer user_data)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (serial);
    char *str;
    int a;
    int b;

    str = g_match_info_fetch (match_info, 1);
    a = atoi (str);
    g_free (str);

    str = g_match_info_fetch (match_info, 2);
    b = atoi (str);
    g_free (str);

    if (a == 3 && b == 2)
        priv->mode = MM_MODEM_GSM_NETWORK_MODE_GPRS;
    else if (a == 3 && b == 3)
        priv->mode = MM_MODEM_GSM_NETWORK_MODE_EDGE;
    else if (a == 5 && b == 4)
        priv->mode = MM_MODEM_GSM_NETWORK_MODE_UMTS;
    else if (a == 5 && b == 5)
        priv->mode = MM_MODEM_GSM_NETWORK_MODE_HSDPA;
    else if (a == 5 && b == 6)
        priv->mode = MM_MODEM_GSM_NETWORK_MODE_HSUPA;
    else if (a == 5 && b == 7)
        priv->mode = MM_MODEM_GSM_NETWORK_MODE_HSPA;
    else {
        g_warning ("Couldn't parse mode change value: '%s'", str);
        return;
    }

    g_debug ("Mode: %d", priv->mode);
    mm_modem_gsm_network_mode (MM_MODEM_GSM_NETWORK (serial), priv->mode);
}

static void
handle_status_change (MMSerial *serial,
                      GMatchInfo *match_info,
                      gpointer user_data)
{
    char *str;
    int n1, n2, n3, n4, n5, n6, n7;

    str = g_match_info_fetch (match_info, 1);
    if (sscanf (str, "%x,%x,%x,%x,%x,%x,%x", &n1, &n2, &n3, &n4, &n5, &n6, &n7)) {
        g_debug ("Duration: %d Up: %d Kbps Down: %d Kbps Total: %d Total: %d\n",
                 n1, n2 * 8 / 1000, n3  * 8 / 1000, n4 / 1024, n5 / 1024);
    }
    g_free (str);
}

/*****************************************************************************/

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->set_network_mode = set_network_mode;
    class->get_network_mode = get_network_mode;
    class->set_band = set_band;
    class->get_band = get_band;
    class->get_signal_quality = get_signal_quality;
}

static void
mm_modem_huawei_init (MMModemHuawei *self)
{
    GRegex *regex;

    mm_generic_gsm_set_unsolicited_registration (MM_GENERIC_GSM (self), TRUE);

    regex = g_regex_new ("\\r\\n\\^RSSI:(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_serial_add_unsolicited_msg_handler (MM_SERIAL (self), regex, handle_signal_quality_change, NULL, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n\\^MODE:(\\d),(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_serial_add_unsolicited_msg_handler (MM_SERIAL (self), regex, handle_mode_change, NULL, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n\\^DSFLOWRPT:(.+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_serial_add_unsolicited_msg_handler (MM_SERIAL (self), regex, handle_status_change, NULL, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n\\^BOOT:.+\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_serial_add_unsolicited_msg_handler (MM_SERIAL (self), regex, NULL, NULL, NULL);
    g_regex_unref (regex);
}

static void
mm_modem_huawei_class_init (MMModemHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_modem_huawei_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemHuaweiPrivate));
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

        static const GInterfaceInfo modem_gsm_network_info = {
            (GInterfaceInitFunc) modem_gsm_network_init
        };

        modem_huawei_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemHuawei", &modem_huawei_type_info, 0);
        g_type_add_interface_static (modem_huawei_type, MM_TYPE_MODEM_GSM_NETWORK, &modem_gsm_network_info);
    }

    return modem_huawei_type;
}
