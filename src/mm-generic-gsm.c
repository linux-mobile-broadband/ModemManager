/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mm-generic-gsm.h"
#include "mm-gsm-modem.h"
#include "mm-modem-error.h"
#include "mm-callback-info.h"

static gpointer mm_generic_gsm_parent_class = NULL;

#define MM_GENERIC_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_GENERIC_GSM, MMGenericGsmPrivate))

typedef struct {
    char *driver;
    guint32 cid;
    guint32 pending_id;
} MMGenericGsmPrivate;

static void register_auto (MMGsmModem *modem, MMCallbackInfo *info);

MMModem *
mm_generic_gsm_new (const char *serial_device, const char *driver)
{
    g_return_val_if_fail (serial_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_GENERIC_GSM,
                                   MM_SERIAL_DEVICE, serial_device,
                                   MM_MODEM_DRIVER, driver,
                                   NULL));
}

guint32
mm_generic_gsm_get_cid (MMGenericGsm *modem)
{
    g_return_val_if_fail (MM_IS_GENERIC_GSM (modem), 0);

    return MM_GENERIC_GSM_GET_PRIVATE (modem)->cid;
}

/*****************************************************************************/

static void
check_pin_done (MMSerial *serial,
                int reply_index,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* success */
        break;
    case 1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_PIN_NEEDED, "%s", "PIN needed");
        break;
    case 2:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_PUK_NEEDED, "%s", "PUK needed");
        break;
    case -1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "PIN checking timed out.");
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "PIN checking failed.");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
check_pin (MMSerial *serial, gpointer user_data)
{
    char *responses[] = { "READY", "SIM PIN", "SIM PUK", "ERROR", "ERR", NULL };
    char *terminators[] = { "OK", "ERROR", "ERR", NULL };
    guint id = 0;

    if (mm_serial_send_command_string (serial, "AT+CPIN?"))
        id = mm_serial_wait_for_reply (serial, 3, responses, terminators, check_pin_done, user_data);

    if (!id) {
        MMCallbackInfo *info = (MMCallbackInfo *) user_data;

        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "PIN checking failed.");
        mm_callback_info_schedule (info);
    }
}

static void
init_done (MMSerial *serial,
           int reply_index,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* success */
        check_pin (serial, user_data);
        break;
    case -1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Modem initialization timed out.");
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Modem initialization failed");
    }

    if (info->error)
        mm_callback_info_schedule (info);
}

static void
flash_done (MMSerial *serial, gpointer user_data)
{
    char *responses[] = { "OK", "ERROR", "ERR", NULL };
    guint id = 0;

    if (mm_serial_send_command_string (serial, "ATZ E0"))
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
set_pin_done (MMSerial *serial,
              int reply_index,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* success */
        break;
    case -1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                   "%s", "Did not receive response for secret");
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_INVALID_SECRET, "%s", "Invalid secret");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
set_pin (MMGsmModem *modem,
         const char *pin,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    char *responses[] = { "OK", "ERROR", "ERR", NULL };
    guint id = 0;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    command = g_strdup_printf ("AT+CPIN=\"%s\"", pin);
    if (mm_serial_send_command_string (MM_SERIAL (modem), command))
        id = mm_serial_wait_for_reply (MM_SERIAL (modem), 3, responses, responses, set_pin_done, info);

    g_free (command);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "PIN checking failed.");
        mm_callback_info_schedule (info);
    }
}

static void
register_manual_done (MMSerial *serial,
                      int reply_index,
                      gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* success */
        break;
    case -1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Manual registration timed out");
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Manual registration failed");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
register_manual (MMGsmModem *modem, const char *network_id, MMCallbackInfo *info)
{
    char *command;
    char *responses[] = { "OK", "ERROR", "ERR", NULL };
    guint id = 0;

    command = g_strdup_printf ("AT+COPS=1,2,\"%s\"", network_id);
    if (mm_serial_send_command_string (MM_SERIAL (modem), command))
        id = mm_serial_wait_for_reply (MM_SERIAL (modem), 30, responses, responses,
                                       register_manual_done, info);

    g_free (command);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Manual registration failed.");
        mm_callback_info_schedule (info);
    }
}

static gboolean
automatic_registration_again (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;

	register_auto (MM_GSM_MODEM (mm_callback_info_get_data (info, "modem")), info);

    mm_callback_info_set_data (info, "modem", NULL, NULL);
	
    return FALSE;
}

static void
register_auto_done (MMSerial *serial,
                    int reply_index,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                   "%s", "Automatic registration failed: not registered and not searching.");
        break;
    case 1:
        g_message ("Registered on Home network");
        break;
    case 2:
        mm_callback_info_set_data (info, "modem", g_object_ref (serial), g_object_unref);
        MM_GENERIC_GSM_GET_PRIVATE (serial)->pending_id = g_timeout_add (1000, automatic_registration_again, info);
        return;
        break;
    case 3:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", 
                                   "Automatic registration failed: registration denied");
        break;
    case 4:
        g_message ("Registered on Roaming network");
        break;
    case -1:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Automatic registration timed out");
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Automatic registration failed");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
register_auto (MMGsmModem *modem, MMCallbackInfo *info)
{
    char *responses[] = { "+CREG: 0,0", "+CREG: 0,1", "+CREG: 0,2", "+CREG: 0,3", "+CREG: 0,5", NULL };
    char *terminators[] = { "OK", "ERROR", "ERR", NULL };
    guint id = 0;

    if (mm_serial_send_command_string (MM_SERIAL (modem), "AT+CREG?"))
        id = mm_serial_wait_for_reply (MM_SERIAL (modem), 60, responses, terminators,
                                       register_auto_done, info);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Automatic registration failed.");
        mm_callback_info_schedule (info);
    }
}

static void
do_register (MMGsmModem *modem,
             const char *network_id,
             MMModemFn callback,
             gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    if (network_id)
        register_manual (modem, network_id, info);
    else
        register_auto (modem, info);
}

static void
connect_done (MMSerial *serial,
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
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    char *responses[] = { "CONNECT", "BUSY", "NO DIAL TONE", "NO CARRIER", NULL };
    guint id = 0;
    guint32 cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (modem));

    info = mm_callback_info_new (modem, callback, user_data);

    if (cid > 0) {
        GString *str;

        str = g_string_new ("ATD");
        if (g_str_has_suffix (number, "#"))
            str = g_string_append_len (str, number, strlen (number) - 1);
        else
            str = g_string_append (str, number);

        g_string_append_printf (str, "***%d#", cid);
        command = g_string_free (str, FALSE);
    } else
        command = g_strconcat ("ATDT", number, NULL);

    if (mm_serial_send_command_string (MM_SERIAL (modem), command))
        id = mm_serial_wait_for_reply (MM_SERIAL (modem), 60, responses, responses, connect_done, info);

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

static void
scan_callback_wrapper (MMModem *modem,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGsmModemScanFn scan_fn;
    GPtrArray *results;
    gpointer data;

    scan_fn = (MMGsmModemScanFn) mm_callback_info_get_data (info, "scan-callback");
    results = (GPtrArray *) mm_callback_info_get_data (info, "scan-results");
    data = mm_callback_info_get_data (info, "scan-data");

    scan_fn (MM_GSM_MODEM (modem), results, error, data);
}

static void
destroy_scan_data (gpointer data)
{
    GPtrArray *results = (GPtrArray *) data;

    g_ptr_array_foreach (results, (GFunc) g_hash_table_destroy, NULL);
    g_ptr_array_free (results, TRUE);
}

static void
scan_done (MMSerial *serial, const char *reply, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GPtrArray *results;

    results = g_ptr_array_new ();

    if (!strncmp (reply, "+COPS: ", 7)) {
        /* Got valid reply */
		GRegex *r;
		GMatchInfo *match_info;
		GError *err = NULL;

		reply += 7;

		/* Pattern without crazy escaping using | for matching: (|\d|,"|.+|","|.+|","|.+|",|\d|) */
		r = g_regex_new ("\\((\\d),\"(.+)\",\"(.+)\",\"(.+)\",(\\d)\\)", G_REGEX_UNGREEDY, 0, &err);
		if (err) {
			g_error ("Invalid regular expression: %s", err->message);
			g_error_free (err);
            info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Could not parse scan results.");
            goto out;
		}

		g_regex_match (r, reply, 0, &match_info);
		while (g_match_info_matches (match_info)) {
            GHashTable *hash;

            hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
            g_hash_table_insert (hash, g_strdup ("status"), g_match_info_fetch (match_info, 1));
            g_hash_table_insert (hash, g_strdup ("operator-long"), g_match_info_fetch (match_info, 2));
            g_hash_table_insert (hash, g_strdup ("operator-short"), g_match_info_fetch (match_info, 3));
            g_hash_table_insert (hash, g_strdup ("operator-num"), g_match_info_fetch (match_info, 4));

            g_ptr_array_add (results, hash);
			g_match_info_next (match_info, NULL);
		}

		g_match_info_free (match_info);
		g_regex_unref (r);
    } else
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Could not parse scan results");

    mm_callback_info_set_data (info, "scan-results", results, destroy_scan_data);

 out:
    mm_callback_info_schedule (info);
}

static void
scan (MMGsmModem *modem,
      MMGsmModemScanFn callback,
      gpointer user_data)
{
    MMCallbackInfo *info;
    char *terminators = "\r\n";
    guint id = 0;

    info = mm_callback_info_new (MM_MODEM (modem), scan_callback_wrapper, NULL);
    info->user_data = info;
    mm_callback_info_set_data (info, "scan-callback", callback, NULL);
    mm_callback_info_set_data (info, "scan-data", user_data, NULL);

    if (mm_serial_send_command_string (MM_SERIAL (modem), "AT+COPS=?"))
        id = mm_serial_get_reply (MM_SERIAL (modem), 60, terminators, scan_done, info);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Scanning failed.");
        mm_callback_info_schedule (info);
    }
}

static void
set_apn_done (MMSerial *serial,
              int reply_index,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    switch (reply_index) {
    case 0:
        /* success */
        MM_GENERIC_GSM_GET_PRIVATE (serial)->cid = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "CID"));
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Setting APN failed");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
set_apn (MMGsmModem *modem,
         const char *apn,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    char *responses[] = { "OK", "ERROR", NULL };
    guint cid = 1;
    guint id = 0;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_callback_info_set_data (info, "CID", GUINT_TO_POINTER (cid), NULL);

    command = g_strdup_printf ("AT+CGDCONT=%d, \"IP\", \"%s\"", cid, apn);
    if (mm_serial_send_command_string (MM_SERIAL (modem), command))
        id = mm_serial_wait_for_reply (MM_SERIAL (modem), 3, responses, responses, set_apn_done, info);

    g_free (command);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Setting APN failed.");
        mm_callback_info_schedule (info);
    }
}

static void
get_signal_quality_done (MMSerial *serial, const char *reply, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint32 result = 0;

    if (!strncmp (reply, "+CSQ: ", 6)) {
        /* Got valid reply */
        int quality;
        int ber;

        reply += 6;

        if (sscanf (reply, "%d,%d", &quality, &ber)) {
            /* 99 means unknown */
            if (quality != 99)
                /* Normalize the quality */
                result = quality * 100 / 31;
        } else
            info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                       "%s", "Could not parse signal quality results");
    } else
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                   "%s", "Could not parse signal quality results");

    info->uint_result = result;
    mm_callback_info_schedule (info);
}

static void
get_signal_quality (MMGsmModem *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMCallbackInfo *info;
    char *terminators = "\r\n";
    guint id = 0;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    if (mm_serial_send_command_string (MM_SERIAL (modem), "AT+CSQ"))
        id = mm_serial_get_reply (MM_SERIAL (modem), 10, terminators, get_signal_quality_done, info);

    if (!id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Getting signal quality failed.");
        mm_callback_info_schedule (info);
    }
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
gsm_modem_init (MMGsmModem *gsm_modem_class)
{
    gsm_modem_class->set_pin = set_pin;
    gsm_modem_class->do_register = do_register;
    gsm_modem_class->set_apn = set_apn;
    gsm_modem_class->scan = scan;
    gsm_modem_class->get_signal_quality = get_signal_quality;
}

static void
mm_generic_gsm_init (MMGenericGsm *self)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    switch (prop_id) {
    case MM_MODEM_PROP_DRIVER:
        /* Construct only */
        MM_GENERIC_GSM_GET_PRIVATE (object)->driver = g_value_dup_string (value);
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
        g_value_set_string (value, MM_GENERIC_GSM_GET_PRIVATE (object)->driver);
        break;
    case MM_MODEM_PROP_TYPE:
        g_value_set_uint (value, MM_MODEM_TYPE_GSM);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (object);

    if (priv->pending_id) {
        g_source_remove (priv->pending_id);
        priv->pending_id = 0;
    }

    g_free (priv->driver);

    G_OBJECT_CLASS (mm_generic_gsm_parent_class)->finalize (object);
}

static void
mm_generic_gsm_class_init (MMGenericGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_generic_gsm_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMGenericGsmPrivate));

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
mm_generic_gsm_get_type (void)
{
    static GType generic_gsm_type = 0;

    if (G_UNLIKELY (generic_gsm_type == 0)) {
        static const GTypeInfo generic_gsm_type_info = {
            sizeof (MMGenericGsmClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_generic_gsm_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMGenericGsm),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_generic_gsm_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };
        
        static const GInterfaceInfo gsm_modem_iface_info = {
            (GInterfaceInitFunc) gsm_modem_init
        };

        generic_gsm_type = g_type_register_static (MM_TYPE_SERIAL, "MMGenericGsm", &generic_gsm_type_info, 0);

        g_type_add_interface_static (generic_gsm_type, MM_TYPE_MODEM, &modem_iface_info);
        g_type_add_interface_static (generic_gsm_type, MM_TYPE_GSM_MODEM, &gsm_modem_iface_info);
    }

    return generic_gsm_type;
}
