/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <dbus/dbus-glib.h>
#include "mm-modem-hso.h"
#include "mm-serial.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void impl_hso_authenticate (MMModemHso *self,
                                   const char *username,
                                   const char *password,
                                   DBusGMethodInvocation *context);

static void impl_hso_get_ip4_config (MMModemHso *self,
                                     DBusGMethodInvocation *context);

#include "mm-modem-gsm-hso-glue.h"

static gpointer mm_modem_hso_parent_class = NULL;

#define MM_MODEM_HSO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_HSO, MMModemHsoPrivate))

typedef struct {
    char *network_device;
    gboolean authenticated;
} MMModemHsoPrivate;

enum {
    PROP_0,
    PROP_NETWORK_DEVICE,

    LAST_PROP
};

#define OWANDATA_TAG "_OWANDATA: "

MMModem *
mm_modem_hso_new (const char *serial_device,
                  const char *network_device,
                  const char *driver)
{
    g_return_val_if_fail (serial_device != NULL, NULL);
    g_return_val_if_fail (network_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_HSO,
                                   MM_SERIAL_DEVICE, serial_device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_HSO_NETWORK_DEVICE, network_device,
                                   NULL));
}

static void
hso_enable_done (MMSerial *serial,
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
hso_enable (MMModemHso *self,
            gboolean enabled,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    command = g_strdup_printf ("AT_OWANCALL=%d,%d,1",
                               mm_generic_gsm_get_cid (MM_GENERIC_GSM (self)),
                               enabled ? 1 : 0);

    mm_serial_queue_command (MM_SERIAL (self), command, 3, hso_enable_done, info);
    g_free (command);
}

static void
hso_enabled (MMModem *modem,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    
    mm_callback_info_schedule (info);
}

static void
hso_disabled (MMModem *modem,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else
        hso_enable (MM_MODEM_HSO (modem), TRUE, hso_enabled, info);
}

static void
auth_done (MMSerial *serial,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else
        /* success, kill any existing connections first */
        hso_enable (MM_MODEM_HSO (serial), FALSE, hso_disabled, info);
}

void
mm_hso_modem_authenticate (MMModemHso *self,
                           const char *username,
                           const char *password,
                           MMModemFn callback,
                           gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;

    g_return_if_fail (MM_IS_MODEM_HSO (self));
    g_return_if_fail (callback != NULL);

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    command = g_strdup_printf ("AT$QCPDPP=%d,1,\"%s\",\"%s\"",
	                           mm_generic_gsm_get_cid (MM_GENERIC_GSM (self)),
	                           password ? password : "",
	                           username ? username : "");

    mm_serial_queue_command (MM_SERIAL (self), command, 3, auth_done, info);
    g_free (command);
}

static void
free_dns_array (gpointer data)
{
    g_array_free ((GArray *) data, TRUE);
}

static void
ip4_callback_wrapper (MMModem *modem,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHsoIp4Fn callback;

    callback = mm_callback_info_get_data (info, "callback");
    callback (MM_MODEM_HSO (modem),
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, "ip4-address")),
              mm_callback_info_get_data (info, "ip4-dns"),
              error,
              mm_callback_info_get_data (info, "user-data"));
}

static void
get_ip4_config_done (MMSerial *serial,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
	char **items, **iter;
    GArray *dns_array;
    int i;
    guint32 tmp;
    guint cid;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    } else if (g_str_has_prefix (response->str, OWANDATA_TAG)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Retrieving failed: invalid response.");
        goto out;
    }

    cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (serial));
    dns_array = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 2);
    items = g_strsplit (response->str + strlen (OWANDATA_TAG), ", ", 0);

	for (iter = items, i = 0; *iter; iter++, i++) {
		if (i == 0) { /* CID */
			long int tmp;

			errno = 0;
			tmp = strtol (*iter, NULL, 10);
			if (errno != 0 || tmp < 0 || (guint) tmp != cid) {
				info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Unknown CID in OWANDATA response (got %d, expected %d)", (guint) tmp, cid);
				break;
			}
		} else if (i == 1) { /* IP address */
			if (inet_pton (AF_INET, *iter, &tmp) > 0)
                mm_callback_info_set_data (info, "ip4-address", GUINT_TO_POINTER (tmp), NULL);
		} else if (i == 3) { /* DNS 1 */
			if (inet_pton (AF_INET, *iter, &tmp) > 0)
				g_array_append_val (dns_array, tmp);
		} else if (i == 4) { /* DNS 2 */
			if (inet_pton (AF_INET, *iter, &tmp) > 0)
				g_array_append_val (dns_array, tmp);
		}
	}

    g_strfreev (items);
    mm_callback_info_set_data (info, "ip4-dns", dns_array, free_dns_array);

 out:
    mm_callback_info_schedule (info);
}

void
mm_hso_modem_get_ip4_config (MMModemHso *self,
                             MMModemHsoIp4Fn callback,
                             gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;

    g_return_if_fail (MM_IS_MODEM_HSO (self));
    g_return_if_fail (callback != NULL);

    info = mm_callback_info_new (MM_MODEM (self), ip4_callback_wrapper, NULL);
    info->user_data = info;
    mm_callback_info_set_data (info, "callback", callback, NULL);
    mm_callback_info_set_data (info, "user-data", user_data, NULL);

    command = g_strdup_printf ("AT_OWANDATA=%d", mm_generic_gsm_get_cid (MM_GENERIC_GSM (self)));
    mm_serial_queue_command (MM_SERIAL (self), command, 3, get_ip4_config_done, info);
    g_free (command);
}

/*****************************************************************************/

static void
modem_enable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModem *parent_modem_iface;

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
    parent_modem_iface->enable (modem,
                                GPOINTER_TO_INT (mm_callback_info_get_data (info, "enable")),
                                (MMModemFn) mm_callback_info_get_data (info, "callback"),
                                mm_callback_info_get_data (info, "user-data"));
}

static void
enable (MMModem *modem,
        gboolean enable,
        MMModemFn callback,
        gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, modem_enable_done, NULL);
    info->user_data = info;
    mm_callback_info_set_data (info, "enable", GINT_TO_POINTER (enable), NULL);
    mm_callback_info_set_data (info, "callback", callback, NULL);
    mm_callback_info_set_data (info, "user-data", user_data, NULL);

    if (enable)
        mm_callback_info_schedule (info);
    else
        hso_enable (MM_MODEM_HSO (modem), FALSE, modem_enable_done, info);
}

/*****************************************************************************/

static void
impl_hso_auth_done (MMModem *modem,
                    GError *error,
                    gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context);
}

static void
impl_hso_authenticate (MMModemHso *self,
                       const char *username,
                       const char *password,
                       DBusGMethodInvocation *context)
{
    mm_hso_modem_authenticate (self, username, password, impl_hso_auth_done, context);
}

static void
impl_hso_ip4_config_done (MMModemHso *modem,
                          guint32 address,
                          GArray *dns,
                          GError *error,
                          gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, address, dns);
}

static void
impl_hso_get_ip4_config (MMModemHso *self,
                         DBusGMethodInvocation *context)
{
    mm_hso_modem_get_ip4_config (self, impl_hso_ip4_config_done, context);
}

/*****************************************************************************/

static void
mm_modem_hso_init (MMModemHso *self)
{
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->enable = enable;
}

static GObject*
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
    GObject *object;
    MMModemHsoPrivate *priv;

    object = G_OBJECT_CLASS (mm_modem_hso_parent_class)->constructor (type,
                                                                      n_construct_params,
                                                                      construct_params);
    if (!object)
        return NULL;

    priv = MM_MODEM_HSO_GET_PRIVATE (object);

    if (!priv->network_device) {
        g_warning ("No network device provided");
        g_object_unref (object);
        return NULL;
    }

    return object;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NETWORK_DEVICE:
        /* Construct only */
        priv->network_device = g_value_dup_string (value);
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
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NETWORK_DEVICE:
        g_value_set_string (value, priv->network_device);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}
static void
finalize (GObject *object)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (object);

    g_free (priv->network_device);

    G_OBJECT_CLASS (mm_modem_hso_parent_class)->finalize (object);
}

static void
mm_modem_hso_class_init (MMModemHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_modem_hso_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemHsoPrivate));

    /* Virtual methods */
    object_class->constructor = constructor;
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;
    /* Properties */
    g_object_class_install_property
        (object_class, PROP_NETWORK_DEVICE,
         g_param_spec_string (MM_MODEM_HSO_NETWORK_DEVICE,
                              "NetworkDevice",
                              "Network device",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

GType
mm_modem_hso_get_type (void)
{
    static GType modem_hso_type = 0;

    if (G_UNLIKELY (modem_hso_type == 0)) {
        static const GTypeInfo modem_hso_type_info = {
            sizeof (MMModemHsoClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_hso_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemHso),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_hso_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        modem_hso_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemHso", &modem_hso_type_info, 0);
        g_type_add_interface_static (modem_hso_type, MM_TYPE_MODEM, &modem_iface_info);
    }

    return modem_hso_type;
}
