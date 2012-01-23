/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ModemManager.h>
#include <mm-errors-types.h>
#include <mm-gdbus-modem.h>

#include "mm-base-modem.h"

#include "mm-log.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"

G_DEFINE_ABSTRACT_TYPE (MMBaseModem, mm_base_modem, MM_GDBUS_TYPE_OBJECT_SKELETON);

enum {
    PROP_0,
    PROP_VALID,
    PROP_MAX_TIMEOUTS,
    PROP_DEVICE,
    PROP_DRIVER,
    PROP_PLUGIN,
    PROP_VENDOR_ID,
    PROP_PRODUCT_ID,
    PROP_CONNECTION,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseModemPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;

    gchar *device;
    gchar *driver;
    gchar *plugin;

    guint vendor_id;
    guint product_id;

    gboolean valid;

    guint max_timeouts;
    guint set_invalid_unresponsive_modem_id;

    MMAuthProvider *authp;

    GHashTable *ports;
    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMQcdmSerialPort *qcdm;
    MMPort *data;
};

static gchar *
get_hash_key (const gchar *subsys,
              const gchar *name)
{
    return g_strdup_printf ("%s%s", subsys, name);
}

MMPort *
mm_base_modem_get_port (MMBaseModem *self,
                        const gchar *subsys,
                        const gchar *name)
{
    MMPort *port;
    gchar *key;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (subsys != NULL, NULL);

    /* Only 'net' or 'tty' should be given */
    g_return_val_if_fail (g_str_equal (subsys, "net") ||
                          g_str_equal (subsys, "tty"),
                          NULL);

    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (self->priv->ports, key);
    g_free (key);

    return port;
}

gboolean
mm_base_modem_owns_port (MMBaseModem *self,
                         const gchar *subsys,
                         const gchar *name)
{
    return !!mm_base_modem_get_port (self, subsys, name);
}

static gboolean
set_invalid_unresponsive_modem_cb (MMBaseModem *self)
{
    mm_base_modem_set_valid (self, FALSE);
    self->priv->set_invalid_unresponsive_modem_id = 0;
    return FALSE;
}

static void
serial_port_timed_out_cb (MMSerialPort *port,
                          guint n_consecutive_timeouts,
                          gpointer user_data)
{
    MMBaseModem *self = (MM_BASE_MODEM (user_data));

    if (self->priv->max_timeouts > 0 &&
        n_consecutive_timeouts >= self->priv->max_timeouts) {
        mm_warn ("(%s/%s) port timed out %u times, marking modem '%s' as disabled",
                 mm_port_type_to_name (mm_port_get_port_type (MM_PORT (port))),
                 mm_port_get_device (MM_PORT (port)),
                 n_consecutive_timeouts,
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (self)));

        /* Only set action to invalidate modem if not already done */
        if (!self->priv->set_invalid_unresponsive_modem_id)
            self->priv->set_invalid_unresponsive_modem_id =
                g_idle_add ((GSourceFunc)set_invalid_unresponsive_modem_cb, self);
    }
}

static void
initialize_ready (MMBaseModem *self,
                  GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_BASE_MODEM_GET_CLASS (self)->initialize_finish (self, res, &error)) {
        mm_warn ("couldn't initialize the modem: '%s'", error->message);
        mm_base_modem_set_valid (self, FALSE);
        g_error_free (error);
        return;
    }

    mm_dbg ("modem properly initialized");
    mm_base_modem_set_valid (self, TRUE);
}

gboolean
mm_base_modem_grab_port (MMBaseModem *self,
                         const gchar *subsys,
                         const gchar *name,
                         MMPortType suggested_type)
{
    MMPort *port;
    gchar *key;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);
    g_return_val_if_fail (subsys != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    /* Only allow 'tty' and 'net' ports */
    if (!g_str_equal (subsys, "net") &&
        !g_str_equal (subsys, "tty")) {
        mm_warn ("(%s/%s): cannot add port, unhandled subsystem",
                 subsys, name);
        return FALSE;
    }

    /* Don't allow more than one Primary port to be set */
    if (self->priv->primary &&
        suggested_type == MM_PORT_TYPE_PRIMARY) {
        mm_warn ("(%s/%s): cannot add port, primary port already exists",
                 subsys, name);
        return FALSE;
    }

    /* Check whether we already have it stored */
    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (self->priv->ports, key);
    if (port) {
        mm_warn ("(%s/%s): cannot add port, already exists",
                 subsys, name);
        g_free (key);
        return FALSE;
    }

    /* If we have a tty, decide whether it will be primary, secondary, or none */
    if (g_str_equal (subsys, "tty")) {
        MMPortType ptype;

        /* Decide port type */
        if (suggested_type != MM_PORT_TYPE_UNKNOWN)
            ptype = suggested_type;
        else {
            if (!self->priv->primary)
                ptype = MM_PORT_TYPE_PRIMARY;
            else if (!self->priv->secondary)
                ptype = MM_PORT_TYPE_SECONDARY;
            else
                ptype = MM_PORT_TYPE_IGNORED;
        }

        if (ptype == MM_PORT_TYPE_QCDM) {
            /* QCDM port */
            port = MM_PORT (mm_qcdm_serial_port_new (name, ptype));
            if (!self->priv->qcdm)
                self->priv->qcdm = g_object_ref (port);
        } else {
            GRegex *regex;
            GPtrArray *array;
            int i;

            /* AT port */
            port = MM_PORT (mm_at_serial_port_new (name, ptype));

            /* Set common response parser */
            mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (port),
                                                   mm_serial_parser_v1_parse,
                                                   mm_serial_parser_v1_new (),
                                                   mm_serial_parser_v1_destroy);

            /* Set up CREG unsolicited message handlers, with NULL callbacks */
            array = mm_3gpp_creg_regex_get (FALSE);
            for (i = 0; i < array->len; i++) {
                mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port),
                                                               (GRegex *)g_ptr_array_index (array, i),
                                                               NULL,
                                                               NULL,
                                                               NULL);
            }
            mm_3gpp_creg_regex_destroy (array);

            /* Set up CIEV unsolicited message handler, with NULL callback */
            regex = mm_3gpp_ciev_regex_get ();
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port),
                                                           regex,
                                                           NULL,
                                                           NULL,
                                                           NULL);
            g_regex_unref (regex);

            /*     regex = g_regex_new ("\\r\\n\\+CMTI: \"(\\S+)\",(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL); */
            /*     mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, cmti_received, self, NULL); */
            /*     g_regex_unref (regex); */

            /* Set up CUSD unsolicited message handler, with NULL callback */
            regex = mm_3gpp_cusd_regex_get ();
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port),
                                                           regex,
                                                           NULL,
                                                           NULL,
                                                           NULL);
            g_regex_unref (regex);

            if (ptype == MM_PORT_TYPE_PRIMARY) {
                self->priv->primary = g_object_ref (port);

                /* Primary port, which will also be data port */
                if (!self->priv->data)
                    self->priv->data = g_object_ref (port);

                /* As soon as we get the primary AT port, we initialize the
                 * modem */
                MM_BASE_MODEM_GET_CLASS (self)->initialize (self,
                                                            MM_AT_SERIAL_PORT (port),
                                                            NULL, /* TODO: cancellable */
                                                            (GAsyncReadyCallback)initialize_ready,
                                                            NULL);

            } else if (ptype == MM_PORT_TYPE_SECONDARY)
                self->priv->secondary = g_object_ref (port);
        }

        /* For serial ports, enable port timeout checks */
        if (port)
            g_signal_connect (port,
                              "timed-out",
                              G_CALLBACK (serial_port_timed_out_cb),
                              self);

        mm_dbg ("(%s/%s) port (%s) grabbed by %s",
                subsys,
                name,
                mm_port_type_to_name (ptype),
                mm_port_get_device (port));
    } else {
        /* Net */
        port = MM_PORT (g_object_new (MM_TYPE_PORT,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_NET,
                                      MM_PORT_TYPE, MM_PORT_TYPE_IGNORED,
                                      NULL));

        /* Net device (if any) is the preferred data port */
        if (!self->priv->data || MM_IS_AT_SERIAL_PORT (self->priv->data)) {
            g_clear_object (&self->priv->data);
            self->priv->data = g_object_ref (port);
        }

        mm_dbg ("(%s/%s) port grabbed by %s",
                subsys,
                name,
                mm_port_get_device (port));
    }

    /* Add it to the tracking HT.
     * Note: 'key' and 'port' now owned by the HT. */
    g_hash_table_insert (self->priv->ports, key, port);

    return TRUE;
}

void
mm_base_modem_release_port (MMBaseModem *self,
                            const gchar *subsys,
                            const gchar *name)
{
    const gchar *type_name;
    const gchar *device;
    gchar *key;
    MMPort *port;

    g_return_if_fail (MM_IS_BASE_MODEM (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (subsys != NULL);

    if (!g_str_equal (subsys, "tty") &&
        !g_str_equal (subsys, "net"))
        return;

    key = get_hash_key (subsys, name);

    /* Find the port */
    port = g_hash_table_lookup (self->priv->ports, key);
    if (!port) {
        mm_warn ("(%s/%s): cannot release port, not found",
                 subsys, name);
        g_free (key);
        return;
    }

    if (port == (MMPort *)self->priv->primary)
        g_clear_object (&self->priv->primary);

    if (port == (MMPort *)self->priv->data)
        g_clear_object (&self->priv->data);

    if (port == (MMPort *)self->priv->secondary)
        g_clear_object (&self->priv->secondary);

    if (port == (MMPort *)self->priv->qcdm)
        g_clear_object (&self->priv->qcdm);

    /* Remove it from the tracking HT */
    type_name = mm_port_type_to_name (mm_port_get_port_type (port));
    device = mm_port_get_device (port);
    mm_dbg ("(%s/%s) type %s released from %s",
            subsys,
            name,
            type_name,
            device);
    g_hash_table_remove (self->priv->ports, key);
    g_free (key);

    /* TODO */
    /* check_valid (MM_GENERIC_GSM (modem)); */
}

void
mm_base_modem_set_valid (MMBaseModem *self,
                         gboolean new_valid)
{
    g_return_if_fail (MM_IS_BASE_MODEM (self));

    if (self->priv->valid != new_valid) {
        self->priv->valid = new_valid;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALID]);
    }
}

gboolean
mm_base_modem_get_valid (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    return self->priv->valid;
}

MMAtSerialPort *
mm_base_modem_get_port_primary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->primary;
}

MMAtSerialPort *
mm_base_modem_get_port_secondary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->secondary;
}

MMQcdmSerialPort *
mm_base_modem_get_port_qcdm (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->qcdm;
}

MMPort *
mm_base_modem_get_best_data_port (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    /* TODO: sometime we'll have a list of available data ports to use instead
     * of a single one */

    return (mm_port_get_connected (self->priv->data) ?
            NULL :
            self->priv->data);
}

MMAtSerialPort *
mm_base_modem_get_best_at_port (MMBaseModem *self,
                                GError **error)
{
    MMAtSerialPort *port;

    /* Decide which port to use */
    port = mm_base_modem_get_port_primary (self);
    g_assert (port);
    if (mm_port_get_connected (MM_PORT (port))) {
        /* If primary port is connected, check if we can get the secondary
         * port */
        port = mm_base_modem_get_port_secondary (self);
        if (!port) {
            /* If we don't have a secondary port, we need to halt the AT
             * operation */
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_CONNECTED,
                         "No port available to run command");
        }
    }

    return port;
}

gboolean
mm_base_modem_auth_request (MMBaseModem *self,
                            const gchar *authorization,
                            GDBusMethodInvocation *invocation,
                            MMAuthRequestCb callback,
                            gpointer callback_data,
                            GDestroyNotify notify,
                            GError **error)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    return !!mm_auth_provider_request_auth (self->priv->authp,
                                            authorization,
                                            G_OBJECT (self),
                                            invocation,
                                            callback,
                                            callback_data,
                                            notify,
                                            error);
}

gboolean
mm_base_modem_auth_finish (MMBaseModem *self,
                           MMAuthRequest *req,
                           GError **error)
{
    if (mm_auth_request_get_result (req) != MM_AUTH_RESULT_AUTHORIZED) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNAUTHORIZED,
                     "This request requires the '%s' authorization",
                     mm_auth_request_get_authorization (req));
        return FALSE;
    }

    return TRUE;
}

const gchar *
mm_base_modem_get_device (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->device;
}

const gchar *
mm_base_modem_get_driver (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->driver;
}

const gchar *
mm_base_modem_get_plugin (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->plugin;
}

guint
mm_base_modem_get_vendor_id  (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), 0);

    return self->priv->vendor_id;
}

guint
mm_base_modem_get_product_id (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), 0);

    return self->priv->product_id;
}

/*****************************************************************************/

static void
mm_base_modem_init (MMBaseModem *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BASE_MODEM,
                                              MMBaseModemPrivate);

    self->priv->authp = mm_auth_provider_get ();

    self->priv->ports = g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               g_free,
                                               g_object_unref);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    switch (prop_id) {
    case PROP_VALID:
        mm_base_modem_set_valid (self, g_value_get_boolean (value));
        break;
    case PROP_MAX_TIMEOUTS:
        self->priv->max_timeouts = g_value_get_uint (value);
        break;
    case PROP_DEVICE:
        g_free (self->priv->device);
        self->priv->device = g_value_dup_string (value);
        break;
    case PROP_DRIVER:
        g_free (self->priv->driver);
        self->priv->driver = g_value_dup_string (value);
        break;
    case PROP_PLUGIN:
        g_free (self->priv->plugin);
        self->priv->plugin = g_value_dup_string (value);
        break;
    case PROP_VENDOR_ID:
        self->priv->vendor_id = g_value_get_uint (value);
        break;
    case PROP_PRODUCT_ID:
        self->priv->product_id = g_value_get_uint (value);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    switch (prop_id) {
    case PROP_VALID:
        g_value_set_boolean (value, self->priv->valid);
        break;
    case PROP_MAX_TIMEOUTS:
        g_value_set_uint (value, self->priv->max_timeouts);
        break;
    case PROP_DEVICE:
        g_value_set_string (value, self->priv->device);
        break;
    case PROP_DRIVER:
        g_value_set_string (value, self->priv->driver);
        break;
    case PROP_PLUGIN:
        g_value_set_string (value, self->priv->plugin);
        break;
    case PROP_VENDOR_ID:
        g_value_set_uint (value, self->priv->vendor_id);
        break;
    case PROP_PRODUCT_ID:
        g_value_set_uint (value, self->priv->product_id);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    mm_auth_provider_cancel_for_owner (self->priv->authp, object);

    mm_dbg ("Modem (%s) '%s' completely disposed",
            self->priv->plugin,
            self->priv->device);

    g_free (self->priv->device);
    g_free (self->priv->driver);
    g_free (self->priv->plugin);

    G_OBJECT_CLASS (mm_base_modem_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    g_clear_object (&self->priv->primary);
    g_clear_object (&self->priv->secondary);
    g_clear_object (&self->priv->data);
    g_clear_object (&self->priv->qcdm);

    if (self->priv->ports) {
        g_hash_table_destroy (self->priv->ports);
        self->priv->ports = NULL;
    }

    g_clear_object (&self->priv->connection);

    G_OBJECT_CLASS (mm_base_modem_parent_class)->dispose (object);
}

static void
mm_base_modem_class_init (MMBaseModemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseModemPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_MAX_TIMEOUTS] =
        g_param_spec_uint (MM_BASE_MODEM_MAX_TIMEOUTS,
                           "Max timeouts",
                           "Maximum number of consecutive timed out commands sent to "
                           "the modem before disabling it. If 0, this feature is disabled.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MAX_TIMEOUTS, properties[PROP_MAX_TIMEOUTS]);

    properties[PROP_VALID] =
        g_param_spec_boolean (MM_BASE_MODEM_VALID,
                              "Valid",
                              "Whether the modem is to be considered valid or not.",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_VALID, properties[PROP_VALID]);

    properties[PROP_DEVICE] =
        g_param_spec_string (MM_BASE_MODEM_DEVICE,
                             "Device",
                             "Master modem parent device of all the modem's ports",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DEVICE, properties[PROP_DEVICE]);

    properties[PROP_DRIVER] =
        g_param_spec_string (MM_BASE_MODEM_DRIVER,
                             "Driver",
                             "Kernel driver",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DRIVER, properties[PROP_DRIVER]);

    properties[PROP_PLUGIN] =
        g_param_spec_string (MM_BASE_MODEM_PLUGIN,
                             "Plugin",
                             "Name of the plugin managing this modem",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PLUGIN, properties[PROP_PLUGIN]);

    properties[PROP_VENDOR_ID] =
        g_param_spec_uint (MM_BASE_MODEM_VENDOR_ID,
                           "Hardware vendor ID",
                           "Hardware vendor ID. May be unknown for serial devices.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_VENDOR_ID, properties[PROP_VENDOR_ID]);

    properties[PROP_PRODUCT_ID] =
        g_param_spec_uint (MM_BASE_MODEM_PRODUCT_ID,
                           "Hardware product ID",
                           "Hardware product ID. May be unknown for serial devices.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PRODUCT_ID, properties[PROP_PRODUCT_ID]);

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BASE_MODEM_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);
}
