/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <stdio.h>

#include "mm-generic-cdma.h"
#include "mm-modem-cdma.h"
#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-parsers.h"

static gpointer mm_generic_cdma_parent_class = NULL;

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
enable_error_reporting_done (MMSerial *serial,
                             GString *response,
                             GError *error,
                             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        g_warning ("Your CDMA modem does not support +CMEE command");

    /* Ignore errors, see FIXME in init_done() */
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
    } else {
        /* Try to enable better error reporting. My experience so far indicates
           there's some CDMA modems that does not support that.
        FIXME: It's mandatory by spec, so it really shouldn't be optional. Figure
        out which CDMA modems have problems with it and implement plugin for them.
        */
        mm_serial_queue_command (serial, "+CMEE=1", 3, enable_error_reporting_done, user_data);
    }
}

static void
flash_done (MMSerial *serial, gpointer user_data)
{
    mm_serial_queue_command (serial, "Z E0 V1 X4 &C1", 3, init_done, user_data);
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

    if (mm_serial_open (MM_SERIAL (modem), &info->error))
        mm_serial_flash (MM_SERIAL (modem), 100, flash_done, info);

    if (info->error)
        mm_callback_info_schedule (info);
}

static void
dial_done (MMSerial *serial,
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
connect (MMModem *modem,
         const char *number,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (modem, callback, user_data);
    command = g_strconcat ("DT", number, NULL);
    mm_serial_queue_command (MM_SERIAL (modem), command, 60, dial_done, info);
    g_free (command);
}

static void
disconnect_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);
    mm_serial_flash (MM_SERIAL (modem), 1000, disconnect_flash_done, info);
}

static void
get_signal_quality_done (MMSerial *serial,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply = response->str;

    if (error)
        info->error = g_error_copy (error);
    else if (!strncmp (reply, "+CSQ: ", 6)) {
        /* Got valid reply */
        int quality;
        int ber;

        reply += 6;

        if (sscanf (reply, "%d,%d", &quality, &ber)) {
            /* 99 means unknown */
            if (quality != 99)
                /* Normalize the quality */
                quality = quality * 100 / 31;
            
            mm_callback_info_set_result (info, GUINT_TO_POINTER (quality), NULL);
        } else
            info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                       "%s", "Could not parse signal quality results");
    }

    mm_callback_info_schedule (info);
}

static void
get_signal_quality (MMModemCdma *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "+CSQ", 3, get_signal_quality_done, info);
}

/*****************************************************************************/
/* MMModemSimple interface */

typedef enum {
    SIMPLE_STATE_BEGIN = 0,
    SIMPLE_STATE_ENABLE,
    SIMPLE_STATE_CONNECT,
    SIMPLE_STATE_DONE
} SimpleState;

static const char *
simple_get_string_property (MMCallbackInfo *info, const char *name, GError **error)
{
    GHashTable *properties = (GHashTable *) mm_callback_info_get_data (info, "simple-connect-properties");
    GValue *value;

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (!value)
        return NULL;

    if (G_VALUE_HOLDS_STRING (value))
        return g_value_get_string (value);

    g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                 "Invalid property type for '%s': %s (string expected)",
                 name, G_VALUE_TYPE_NAME (value));

    return NULL;
}

static void
simple_state_machine (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *str;
    SimpleState state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "simple-connect-state"));

    if (error) {
        info->error = g_error_copy (error);
		goto out;
	}

	switch (state) {
	case SIMPLE_STATE_BEGIN:
		state = SIMPLE_STATE_ENABLE;
        mm_modem_enable (modem, TRUE, simple_state_machine, info);
        break;
    case SIMPLE_STATE_ENABLE:
        str = simple_get_string_property (info, "number", &info->error);
		state = SIMPLE_STATE_CONNECT;
        mm_modem_connect (modem, str, simple_state_machine, info);
		break;
	case SIMPLE_STATE_CONNECT:
        state = SIMPLE_STATE_DONE;
        break;
    case SIMPLE_STATE_DONE:
        break;
    }

 out:
    if (info->error || state == SIMPLE_STATE_DONE)
        mm_callback_info_schedule (info);
    else
        mm_callback_info_set_data (info, "simple-connect-state", GUINT_TO_POINTER (state), NULL);
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (simple), callback, user_data);
    mm_callback_info_set_data (info, "simple-connect-properties", 
                               g_hash_table_ref (properties),
                               (GDestroyNotify) g_hash_table_unref);

    /* At least number must be present */
    if (!simple_get_string_property (info, "number", &info->error)) {
        if (!info->error)
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Missing number property");
    }

    simple_state_machine (MM_MODEM (simple), NULL, info);
}

static void
simple_free_gvalue (gpointer data)
{
    g_value_unset ((GValue *) data);
    g_slice_free (GValue, data);
}

static GValue *
simple_uint_value (guint32 i)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_UINT);
    g_value_set_uint (val, i);

    return val;
}

static void
simple_status_got_signal_quality (MMModem *modem,
                                  guint32 result,
                                  GError *error,
                                  gpointer user_data)
{
    if (error)
        g_warning ("Error getting signal quality: %s", error->message);
    else
        g_hash_table_insert ((GHashTable *) user_data, "signal_quality", simple_uint_value (result));
}

static void
simple_get_status_invoke (MMCallbackInfo *info)
{
    MMModemSimpleGetStatusFn callback = (MMModemSimpleGetStatusFn) info->callback;

    callback (MM_MODEM_SIMPLE (info->modem),
              (GHashTable *) mm_callback_info_get_data (info, "simple-get-status"),
              info->error, info->user_data);
}

static void
simple_get_status (MMModemSimple *simple,
                   MMModemSimpleGetStatusFn callback,
                   gpointer user_data)
{
    GHashTable *properties;
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (simple),
                                      simple_get_status_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, simple_free_gvalue);
    mm_callback_info_set_data (info, "simple-get-status", properties, (GDestroyNotify) g_hash_table_unref);
    mm_modem_cdma_get_signal_quality (MM_MODEM_CDMA (simple), simple_status_got_signal_quality, properties);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->enable = enable;
    modem_class->connect = connect;
    modem_class->disconnect = disconnect;
}

static void
modem_cdma_init (MMModemCdma *cdma_modem_class)
{
    cdma_modem_class->get_signal_quality = get_signal_quality;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
    class->get_status = simple_get_status;
}

static void
mm_generic_cdma_init (MMGenericCdma *self)
{
    mm_serial_set_response_parser (MM_SERIAL (self),
                                   mm_serial_parser_v1_parse,
                                   mm_serial_parser_v1_new (),
                                   mm_serial_parser_v1_destroy);
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

    mm_generic_cdma_parent_class = g_type_class_peek_parent (klass);
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
}

GType
mm_generic_cdma_get_type (void)
{
    static GType generic_cdma_type = 0;

    if (G_UNLIKELY (generic_cdma_type == 0)) {
        static const GTypeInfo generic_cdma_type_info = {
            sizeof (MMGenericCdmaClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_generic_cdma_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMGenericCdma),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_generic_cdma_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };
        
        static const GInterfaceInfo modem_cdma_iface_info = {
            (GInterfaceInitFunc) modem_cdma_init
        };

        static const GInterfaceInfo modem_simple_info = {
            (GInterfaceInitFunc) modem_simple_init
        };

        generic_cdma_type = g_type_register_static (MM_TYPE_SERIAL, "MMGenericCdma", &generic_cdma_type_info, 0);

        g_type_add_interface_static (generic_cdma_type, MM_TYPE_MODEM, &modem_iface_info);
        g_type_add_interface_static (generic_cdma_type, MM_TYPE_MODEM_CDMA, &modem_cdma_iface_info);
        g_type_add_interface_static (generic_cdma_type, MM_TYPE_MODEM_SIMPLE, &modem_simple_info);
    }

    return generic_cdma_type;
}
