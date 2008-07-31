/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "mm-generic-cdma.h"
#include "mm-modem-error.h"
#include "mm-callback-info.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMGenericCdma, mm_generic_cdma, MM_TYPE_SERIAL,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

#define MM_GENERIC_CDMA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_GENERIC_CDMA, MMGenericCdmaPrivate))

typedef struct {
    char *driver;
} MMGenericCdmaPrivate;

MMModem *
mm_generic_cdma_new (const char *serial_device, const char *driver)
{
    g_return_val_if_fail (serial_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_GENERIC_CDMA,
                                   MM_SERIAL_DEVICE, serial_device,
                                   MM_MODEM_DRIVER, driver,
                                   NULL));
}

/*****************************************************************************/

static void
init_done (MMSerial *serial,
           int reply_index,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* success */
        break;
    case -1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Modem initialization timed out.");
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Modem initialization failed");
    }

    mm_callback_info_schedule (info);
}

static void
flash_done (MMSerial *serial, gpointer user_data)
{
    char *responses[] = { "OK", "ERROR", "ERR", NULL };
    guint id = 0;

    if (mm_serial_send_command_string (serial, "AT E0"))
        id = mm_serial_wait_for_reply (serial, 10, responses, responses, init_done, user_data);

    if (!id) {
        MMCallbackInfo *info = (MMCallbackInfo *) user_data;

        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Turning modem echo off failed.");
        mm_callback_info_schedule (info);
    }
}

static void
enable (MMModem *modem,
        gboolean enable,
        MMModemFn callback,
        gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);

    if (!enable) {
        mm_serial_close (MM_SERIAL (modem));
        mm_callback_info_schedule (info);
        return;
    }

    if (mm_serial_open (MM_SERIAL (modem))) {
        guint id;

        id = mm_serial_flash (MM_SERIAL (modem), 100, flash_done, info);
        if (!id)
            info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                       "%s", "Could not communicate with serial device.");
    } else
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Could not open serial device.");

    if (info->error)
        mm_callback_info_schedule (info);
}

static void
dial_done (MMSerial *serial,
           int reply_index,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* success */
        break;
    case 1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Dial failed: Busy");
        break;
    case 2:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Dial failed: No dial tone");
        break;
    case 3:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Dial failed: No carrier");
        break;
    case -1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Dialing timed out");
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Dialing failed");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
connect (MMModem *modem,
         const char *number,
         const char *apn,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    char *responses[] = { "CONNECT", "BUSY", "NO DIAL TONE", "NO CARRIER", NULL };
    guint id = 0;

    info = mm_callback_info_new (modem, callback, user_data);

    command = g_strconcat ("ATDT", number, NULL);
    if (mm_serial_send_command_string (MM_SERIAL (modem), command))
        id = mm_serial_wait_for_reply (MM_SERIAL (modem), 60, responses, responses, dial_done, info);

    g_free (command);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Dialing failed.");
        mm_callback_info_schedule (info);
    }
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);
    mm_serial_close (MM_SERIAL (modem));
    mm_callback_info_schedule (info);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    /* interface implementation */
    modem_class->enable = enable;
    modem_class->connect = connect;
    modem_class->disconnect = disconnect;
}

static void
mm_generic_cdma_init (MMGenericCdma *self)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    switch (prop_id) {
    case MM_MODEM_PROP_DRIVER:
        /* Construct only */
        MM_GENERIC_CDMA_GET_PRIVATE (object)->driver = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_DATA_DEVICE:
    case MM_MODEM_PROP_TYPE:
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
    switch (prop_id) {
    case MM_MODEM_PROP_DATA_DEVICE:
        g_value_set_string (value, mm_serial_get_device (MM_SERIAL (object)));
        break;
    case MM_MODEM_PROP_DRIVER:
        g_value_set_string (value, MM_GENERIC_CDMA_GET_PRIVATE (object)->driver);
        break;
    case MM_MODEM_PROP_TYPE:
        g_value_set_uint (value, MM_MODEM_TYPE_CDMA);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (object);

    g_free (priv->driver);

    G_OBJECT_CLASS (mm_generic_cdma_parent_class)->finalize (object);
}

static void
mm_generic_cdma_class_init (MMGenericCdmaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMGenericCdmaPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    /* Properties */
    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DATA_DEVICE,
                                      MM_MODEM_DATA_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DRIVER,
                                      MM_MODEM_DRIVER);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_TYPE,
                                      MM_MODEM_TYPE);

    mm_modem_install_dbus_info (G_TYPE_FROM_CLASS (klass));
}
