/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mm-modem-huawei.h"
#include "mm-modem-error.h"
#include "mm-callback-info.h"

static MMModem *parent_class_iface = NULL;

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemHuawei, mm_modem_huawei, MM_TYPE_GENERIC_GSM,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

#define MM_MODEM_HUAWEI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_HUAWEI, MMModemHuaweiPrivate))

typedef struct {
    MMSerial *monitor_device;
    guint watch_id;
} MMModemHuaweiPrivate;

enum {
    PROP_0,
    PROP_MONITOR_DEVICE,

    LAST_PROP
};

MMModem *
mm_modem_huawei_new (const char *data_device,
                     const char *monitor_device,
                     const char *driver)
{
    g_return_val_if_fail (data_device != NULL, NULL);
    g_return_val_if_fail (monitor_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_HUAWEI,
                                   MM_SERIAL_DEVICE, data_device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_HUAWEI_MONITOR_DEVICE, monitor_device,
                                   NULL));
}


/*****************************************************************************/

static void
parse_monitor_line (MMModem *modem, char *buf)
{
    char **lines;
    char **iter;

    lines = g_strsplit (buf, "\r\n", 0);

    for (iter = lines; iter && *iter; iter++) {
        char *line = *iter;

        g_strstrip (line);
        if (strlen (line) < 1 || line[0] != '^')
            continue;

        line += 1;

        if (!strncmp (line, "RSSI:", 5)) {
            int quality = atoi (line + 5);

            if (quality == 99)
                /* 99 means unknown */
                quality = 0;
            else
                /* Normalize the quality */
                quality = quality * 100 / 31;

            g_debug ("Signal quality: %d", quality);
            mm_modem_signal_quality (modem, (guint32) quality);
        } else if (!strncmp (line, "MODE:", 5)) {
            MMModemNetworkMode mode = 0;
            int a;
            int b;

            if (sscanf (line + 5, "%d,%d", &a, &b)) {
                if (a == 3 && b == 2)
                    mode = MM_MODEM_NETWORK_MODE_GPRS;
                else if (a == 3 && b == 3)
                    mode = MM_MODEM_NETWORK_MODE_EDGE;
                else if (a == 5 && b == 4)
                    mode = MM_MODEM_NETWORK_MODE_3G;
                else if (a ==5 && b == 5)
                    mode = MM_MODEM_NETWORK_MODE_HSDPA;

                if (mode) {
                    g_debug ("Mode: %d", mode);
                    mm_modem_network_mode (modem, mode);
                }
            }
        }
    }

    g_strfreev (lines);
}

static gboolean
monitor_device_got_data (GIOChannel *source,
                         GIOCondition condition,
                         gpointer data)
{
	gsize bytes_read;
	char buf[4096];
	GIOStatus status;

	if (condition & G_IO_IN) {
		do {
			status = g_io_channel_read_chars (source, buf, 4096, &bytes_read, NULL);

			if (bytes_read) {
				buf[bytes_read] = '\0';
                parse_monitor_line (MM_MODEM (data), buf);
			}
		} while (bytes_read == 4096 || status == G_IO_STATUS_AGAIN);
	}

	if (condition & G_IO_HUP || condition & G_IO_ERR) {
		return FALSE;
	}

	return TRUE;
}

static void
enable (MMModem *modem,
        gboolean enable,
        MMModemFn callback,
        gpointer user_data)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (modem);

    parent_class_iface->enable (modem, enable, callback, user_data);

    if (enable) {
        GIOChannel *channel;

        if (priv->watch_id == 0) {
            mm_serial_open (priv->monitor_device);

            channel = mm_serial_get_io_channel (priv->monitor_device);
            priv->watch_id = g_io_add_watch (channel, G_IO_IN | G_IO_ERR | G_IO_HUP,
                                             monitor_device_got_data, modem);

            g_io_channel_unref (channel);
        }
    } else {
        if (priv->watch_id) {
            g_source_remove (priv->watch_id);
            priv->watch_id = 0;
            mm_serial_close (priv->monitor_device);
        }
    }
}

static gboolean
parse_syscfg (const char *reply, int *mode_a, int *mode_b, guint32 *band, int *unknown1, int *unknown2)
{
    if (strncmp (reply, "^SYSCFG:", 8))
        return FALSE;

    if (sscanf (reply + 8, "%d,%d,%x,%d,%d", mode_a, mode_b, band, unknown1, unknown2))
        return TRUE;

    return FALSE;
}

static void
get_network_mode_done (MMSerial *serial, const char *reply, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int a, b, u1, u2;
    guint32 band;

    if (parse_syscfg (reply, &a, &b, &band, &u1, &u2)) {
        if (a == 2 && b == 1)
            info->uint_result = MM_MODEM_NETWORK_MODE_PREFER_2G;
        else if (a == 2 && b == 2)
            info->uint_result = MM_MODEM_NETWORK_MODE_PREFER_3G;
        else if (a == 13 && b == 1)
            info->uint_result = MM_MODEM_NETWORK_MODE_GPRS;
        else if (a == 14 && b == 2)
            info->uint_result = MM_MODEM_NETWORK_MODE_3G;
    }

    if (info->uint_result == 0)
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                   "%s", "Could not parse network mode results");

    mm_callback_info_schedule (info);
}

static void
get_network_mode (MMModem *modem,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    char *terminators = "\r\n";
    guint id = 0;

    info = mm_callback_info_uint_new (modem, callback, user_data);

    if (mm_serial_send_command_string (MM_SERIAL (modem), "AT^SYSCFG?"))
        id = mm_serial_get_reply (MM_SERIAL (modem), 10, terminators, get_network_mode_done, info);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Getting network mode failed.");
        mm_callback_info_schedule (info);
    }
}

static void
set_network_mode_done (MMSerial *serial,
                       int reply_index,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* Success */
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Setting network mode failed");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
set_network_mode_get_done (MMSerial *serial, const char *reply, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int a, b, u1, u2;
    guint32 band;
    guint id = 0;

    if (parse_syscfg (reply, &a, &b, &band, &u1, &u2)) {
        char *responses[] = { "OK", "ERROR", NULL };
        char *command;

        a = GPOINTER_TO_INT (mm_callback_info_get_data (info, "mode-a"));
        b = GPOINTER_TO_INT (mm_callback_info_get_data (info, "mode-b"));
        command = g_strdup_printf ("AT^SYSCFG=%d,%d,%X,%d,%d", a, b, band, u1, u2);

        if (mm_serial_send_command_string (serial, command))
            id = mm_serial_wait_for_reply (serial, 3, responses, responses, set_network_mode_done, info);

        g_free (command);
    }

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                   "%s", "Could not set network mode");
        mm_callback_info_schedule (info);
    }
}

static void
set_network_mode (MMModem *modem,
                  MMModemNetworkMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    char *terminators = "\r\n";
    guint id = 0;

    info = mm_callback_info_new (modem, callback, user_data);

    switch (mode) {
    case MM_MODEM_NETWORK_MODE_ANY:
        /* Do nothing */
        mm_callback_info_schedule (info);
        return;
        break;
    case MM_MODEM_NETWORK_MODE_GPRS:
    case MM_MODEM_NETWORK_MODE_EDGE:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (13), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (1), NULL);
        break;
    case MM_MODEM_NETWORK_MODE_3G:
    case MM_MODEM_NETWORK_MODE_HSDPA:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (14), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (2), NULL);
        break;
    case MM_MODEM_NETWORK_MODE_PREFER_2G:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (2), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (1), NULL);
        break;
    case MM_MODEM_NETWORK_MODE_PREFER_3G:
        mm_callback_info_set_data (info, "mode-a", GINT_TO_POINTER (2), NULL);
        mm_callback_info_set_data (info, "mode-b", GINT_TO_POINTER (2), NULL);
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Invalid mode.");
        mm_callback_info_schedule (info);
        return;
        break;
    }

    if (mm_serial_send_command_string (MM_SERIAL (modem), "AT^SYSCFG?"))
        id = mm_serial_get_reply (MM_SERIAL (modem), 10, terminators, set_network_mode_get_done, info);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Setting network mode failed.");
        mm_callback_info_schedule (info);
    }
}

static void
get_band_done (MMSerial *serial, const char *reply, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int a, b, u1, u2;
    guint32 band;

    info->uint_result = 0xdeadbeaf;

    if (parse_syscfg (reply, &a, &b, &band, &u1, &u2)) {
        if (band == 0x3FFFFFFF)
            info->uint_result = MM_MODEM_BAND_ANY;
        else if (band == 0x400380)
            info->uint_result = MM_MODEM_BAND_DCS;
        else if (band == 0x200000)
            info->uint_result = MM_MODEM_BAND_PCS;
    }

    if (info->uint_result == 0xdeadbeaf) {
        info->uint_result = 0;
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                   "%s", "Could not parse band results");
    }

    mm_callback_info_schedule (info);
}

static void
get_band (MMModem *modem,
          MMModemUIntFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    char *terminators = "\r\n";
    guint id = 0;

    info = mm_callback_info_uint_new (modem, callback, user_data);

    if (mm_serial_send_command_string (MM_SERIAL (modem), "AT^SYSCFG?"))
        id = mm_serial_get_reply (MM_SERIAL (modem), 10, terminators, get_band_done, info);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Getting band failed.");
        mm_callback_info_schedule (info);
    }
}

static void
set_band_done (MMSerial *serial,
                       int reply_index,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* Success */
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Setting band failed");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
set_band_get_done (MMSerial *serial, const char *reply, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int a, b, u1, u2;
    guint32 band;
    guint id = 0;

    if (parse_syscfg (reply, &a, &b, &band, &u1, &u2)) {
        char *responses[] = { "OK", "ERROR", NULL };
        char *command;

        band = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "band"));
        command = g_strdup_printf ("AT^SYSCFG=%d,%d,%X,%d,%d", a, b, band, u1, u2);

        if (mm_serial_send_command_string (serial, command))
            id = mm_serial_wait_for_reply (serial, 3, responses, responses, set_band_done, info);

        g_free (command);
    }

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                   "%s", "Could not set band");
        mm_callback_info_schedule (info);
    }
}

static void
set_band (MMModem *modem,
          MMModemBand band,
          MMModemFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    char *terminators = "\r\n";
    guint id = 0;

    info = mm_callback_info_new (modem, callback, user_data);

    switch (band) {
    case MM_MODEM_BAND_ANY:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (0x3FFFFFFF), NULL);
        break;
    case MM_MODEM_BAND_EGSM:
    case MM_MODEM_BAND_DCS:
    case MM_MODEM_BAND_U2100:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (0x400380), NULL);
        break;
    case MM_MODEM_BAND_PCS:
        mm_callback_info_set_data (info, "band", GUINT_TO_POINTER (0x200000), NULL);
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Invalid band.");
        mm_callback_info_schedule (info);
        return;
        break;
    }

    if (mm_serial_send_command_string (MM_SERIAL (modem), "AT^SYSCFG?"))
        id = mm_serial_get_reply (MM_SERIAL (modem), 10, terminators, set_band_get_done, info);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Setting band failed.");
        mm_callback_info_schedule (info);
    }
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    parent_class_iface = g_type_interface_peek_parent (modem_class);

    /* interface implementation */
    modem_class->enable = enable;
    modem_class->set_network_mode = set_network_mode;
    modem_class->get_network_mode = get_network_mode;
    modem_class->set_band = set_band;
    modem_class->get_band = get_band;
}

static void
mm_modem_huawei_init (MMModemHuawei *self)
{
}

static GObject*
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
    GObject *object;
    MMModemHuaweiPrivate *priv;

    object = G_OBJECT_CLASS (mm_modem_huawei_parent_class)->constructor (type,
                                                                         n_construct_params,
                                                                         construct_params);
    if (!object)
        return NULL;

    priv = MM_MODEM_HUAWEI_GET_PRIVATE (object);

    if (!priv->monitor_device) {
        g_warning ("No monitor device provided");
        g_object_unref (object);
        return NULL;
    }

    return object;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_MONITOR_DEVICE:
        /* Construct only */
        priv->monitor_device = MM_SERIAL (g_object_new (MM_TYPE_SERIAL,
                                                        MM_SERIAL_DEVICE, g_value_get_string (value),
                                                        NULL));
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
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_MONITOR_DEVICE:
        g_value_set_string (value, mm_serial_get_device (priv->monitor_device));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}
static void
finalize (GObject *object)
{
    MMModemHuaweiPrivate *priv = MM_MODEM_HUAWEI_GET_PRIVATE (object);

    if (priv->watch_id) {
        g_source_remove (priv->watch_id);
        priv->watch_id = 0;
        mm_serial_close (priv->monitor_device);
    }

    if (priv->monitor_device)
        g_object_unref (priv->monitor_device);

    G_OBJECT_CLASS (mm_modem_huawei_parent_class)->finalize (object);
}

static void
mm_modem_huawei_class_init (MMModemHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemHuaweiPrivate));

    /* Virtual methods */
    object_class->constructor = constructor;
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    /* Properties */
    g_object_class_install_property
        (object_class, PROP_MONITOR_DEVICE,
         g_param_spec_string (MM_MODEM_HUAWEI_MONITOR_DEVICE,
                              "MonitorDevice",
                              "Monitor device",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
