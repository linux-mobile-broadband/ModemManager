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
#include "mm-errors.h"
#include "mm-log.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"

G_DEFINE_ABSTRACT_TYPE (MMBaseModem, mm_base_modem, MM_GDBUS_TYPE_OBJECT_SKELETON);

enum {
    PROP_0,
    PROP_VALID,
    PROP_MAX_TIMEOUTS,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseModemPrivate {
    gboolean valid;

    guint max_timeouts;
    guint set_invalid_unresponsive_modem_id;

    MMAuthProvider *authp;

    GHashTable *ports;
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
        mm_warn ("Modem %s: Port (%s/%s) timed out %u times, marking modem as disabled",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (self)),
                 mm_port_type_to_name (mm_port_get_port_type (MM_PORT (port))),
                 mm_port_get_device (MM_PORT (port)),
                 n_consecutive_timeouts);

        /* Only set action to invalidate modem if not already done */
        if (!self->priv->set_invalid_unresponsive_modem_id)
            self->priv->set_invalid_unresponsive_modem_id =
                g_idle_add ((GSourceFunc)set_invalid_unresponsive_modem_cb, self);
    }
}

static void
find_primary (gpointer key, gpointer data, gpointer user_data)
{
    MMPort **found = user_data;
    MMPort *port = MM_PORT (data);

    if (!*found && (mm_port_get_port_type (port) == MM_PORT_TYPE_PRIMARY))
        *found = port;
}

MMPort *
mm_base_modem_add_port (MMBaseModem *self,
                        const gchar *subsys,
                        const gchar *name,
                        MMPortType ptype)
{
    MMPort *port = NULL;
    gchar *key, *device;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);
    g_return_val_if_fail (subsys != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (ptype != MM_PORT_TYPE_UNKNOWN, NULL);

    /* Only 'net' or 'tty' should be given */
    g_return_val_if_fail (g_str_equal (subsys, "net") ||
                          g_str_equal (subsys, "tty"),
                          NULL);

    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (self->priv->ports, key);

    if (port) {
        mm_warn ("cannot add port (%s/%s): already exists",
                 subsys, name);
        g_free (key);
        return NULL;
    }

    if (ptype == MM_PORT_TYPE_PRIMARY) {
        g_hash_table_foreach (self->priv->ports, find_primary, &port);
        if (port) {
            mm_warn ("cannot add port (%s/%s): primary port already exists",
                     subsys, name);
            g_free (key);
            return NULL;
        }
    }

    if (g_str_equal (subsys, "tty")) {
        if (ptype == MM_PORT_TYPE_QCDM)
            port = MM_PORT (mm_qcdm_serial_port_new (name, ptype));
        else
            port = MM_PORT (mm_at_serial_port_new (name, ptype));

        /* For serial ports, enable port timeout checks */
        if (port)
            g_signal_connect (port,
                              "timed-out",
                              G_CALLBACK (serial_port_timed_out_cb),
                              self);
    } else if (!strcmp (subsys, "net")) {
        port = MM_PORT (g_object_new (MM_TYPE_PORT,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_NET,
                                      MM_PORT_TYPE, ptype,
                                      NULL));
    }

    device = mm_modem_get_device (MM_MODEM (self));
    mm_dbg ("(%s/%s) type %s claimed by %s",
            subsys,
            name,
            mm_port_type_to_name (ptype),
            device);
    g_free (device);

    g_hash_table_insert (self->priv->ports, key, port);

    return port;
}

gboolean
mm_base_modem_remove_port (MMBaseModem *self, MMPort *port)
{
    gchar *device, *key, *name;
    const gchar *type_name, *subsys;
    gboolean removed;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);
    g_return_val_if_fail (MM_IS_PORT (port), FALSE);

    name = g_strdup (mm_port_get_device (port));
    subsys = mm_port_subsys_to_name (mm_port_get_subsys (port));
    type_name = mm_port_type_to_name (mm_port_get_port_type (port));

    key = get_hash_key (subsys, name);
    removed = g_hash_table_remove (self->priv->ports, key);
    if (removed) {
        /* Port may have already been destroyed by removal from the hash */
        device = mm_modem_get_device (MM_MODEM (self));
        mm_dbg ("(%s/%s) type %s removed from %s",
                subsys,
                name,
                type_name,
                device);
        g_free (device);
    }
    g_free (key);
    g_free (name);

    return removed;
}

void
mm_base_modem_set_valid (MMBaseModem *self,
                         gboolean new_valid)
{
    g_return_if_fail (MM_IS_BASE_MODEM (self));

    if (self->priv->valid != new_valid) {
        self->priv->valid = new_valid;

        /* TODO */
        /* /\* Modem starts off in disabled state, and jumps to disabled when */
        /*  * it's no longer valid. */
        /*  *\/ */
        /* mm_modem_set_state (MM_MODEM (self), */
        /*                     MM_MODEM_STATE_DISABLED, */
        /*                     MM_MODEM_STATE_REASON_NONE); */

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALID]);
    }
}

gboolean
mm_base_modem_get_valid (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    return self->priv->valid;
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

    G_OBJECT_CLASS (mm_base_modem_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    if (self->priv->ports) {
        g_hash_table_destroy (self->priv->ports);
        self->priv->ports = NULL;
    }

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
}

