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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm-modem-base.h"
#include "mm-modem.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-errors.h"
#include "mm-log.h"
#include "mm-properties-changed-signal.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"

static void modem_init (MMModem *modem_class);
static void pc_init (MMPropertiesChanged *pc_class);

G_DEFINE_TYPE_EXTENDED (MMModemBase, mm_modem_base,
                        G_TYPE_OBJECT,
                        G_TYPE_FLAG_VALUE_ABSTRACT,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_PROPERTIES_CHANGED, pc_init))

#define MM_MODEM_BASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_BASE, MMModemBasePrivate))

typedef struct {
    char *driver;
    char *plugin;
    char *device;
    char *equipment_ident;
    char *device_ident;
    char *unlock_required;
    guint32 unlock_retries;
    guint32 ip_method;
    gboolean valid;
    MMModemState state;

    guint vid;
    guint pid;
    char *manf;
    char *model;
    char *revision;
    char *ati;
    char *ati1;
    char *gsn;

    MMAuthProvider *authp;

    GHashTable *ports;
} MMModemBasePrivate;


static char *
get_hash_key (const char *subsys, const char *name)
{
    return g_strdup_printf ("%s%s", subsys, name);
}

MMPort *
mm_modem_base_get_port (MMModemBase *self,
                        const char *subsys,
                        const char *name)
{
    MMPort *port;
    char *key;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (subsys != NULL, NULL);

    g_return_val_if_fail (!strcmp (subsys, "net") || !strcmp (subsys, "tty"), NULL);

    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (MM_MODEM_BASE_GET_PRIVATE (self)->ports, key);
    g_free (key);
    return port;
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
mm_modem_base_add_port (MMModemBase *self,
                        const char *subsys,
                        const char *name,
                        MMPortType ptype)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);
    MMPort *port = NULL;
    char *key, *device;

    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);
    g_return_val_if_fail (subsys != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (ptype != MM_PORT_TYPE_UNKNOWN, NULL);

    g_return_val_if_fail (!strcmp (subsys, "net") || !strcmp (subsys, "tty"), NULL);

    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (priv->ports, key);
    g_free (key);
    g_return_val_if_fail (port == NULL, NULL);

    if (ptype == MM_PORT_TYPE_PRIMARY) {
        g_hash_table_foreach (priv->ports, find_primary, &port);
        g_return_val_if_fail (port == NULL, FALSE);
    }

    if (!strcmp (subsys, "tty")) {
        if (ptype == MM_PORT_TYPE_QCDM)
            port = MM_PORT (mm_qcdm_serial_port_new (name, ptype));
        else
            port = MM_PORT (mm_at_serial_port_new (name, ptype));
    } else if (!strcmp (subsys, "net")) {
        port = MM_PORT (g_object_new (MM_TYPE_PORT,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_NET,
                                      MM_PORT_TYPE, ptype,
                                      NULL));
    }

    if (!port)
        return NULL;

    device = mm_modem_get_device (MM_MODEM (self));
    mm_dbg ("(%s) type %s claimed by %s",
            name,
            mm_port_type_to_name (ptype),
            device);
    g_free (device);

    key = get_hash_key (subsys, name);
    g_hash_table_insert (priv->ports, key, port);
    return port;
}

gboolean
mm_modem_base_remove_port (MMModemBase *self, MMPort *port)
{
    MMModemBasePrivate *priv;
    char *device, *key, *name;
    const char *type_name, *subsys;
    gboolean removed;

    g_return_val_if_fail (MM_IS_MODEM_BASE (self), FALSE);
    g_return_val_if_fail (port != NULL, FALSE);

    priv = MM_MODEM_BASE_GET_PRIVATE (self);

    name = g_strdup (mm_port_get_device (port));
    subsys = mm_port_subsys_to_name (mm_port_get_subsys (port));
    type_name = mm_port_type_to_name (mm_port_get_port_type (port));

    key = get_hash_key (subsys, name);
    removed = g_hash_table_remove (priv->ports, key);
    if (removed) {
        /* Port may have already been destroyed by removal from the hash */
        device = mm_modem_get_device (MM_MODEM (self));
        mm_dbg ("(%s) type %s removed from %s", name, type_name, device);
        g_free (device);
    }
    g_free (key);
    g_free (name);

    return removed;
}

void
mm_modem_base_set_valid (MMModemBase *self, gboolean new_valid)
{
    MMModemBasePrivate *priv;

    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);

    if (priv->valid != new_valid) {
        priv->valid = new_valid;

        /* Modem starts off in disabled state, and jumps to disabled when
         * it's no longer valid.
         */
        mm_modem_set_state (MM_MODEM (self),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);

        g_object_notify (G_OBJECT (self), MM_MODEM_VALID);
    }
}

gboolean
mm_modem_base_get_valid (MMModemBase *self)
{
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), FALSE);

    return MM_MODEM_BASE_GET_PRIVATE (self)->valid;
}

const char *
mm_modem_base_get_equipment_identifier (MMModemBase *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);

    return MM_MODEM_BASE_GET_PRIVATE (self)->equipment_ident;
}

void
mm_modem_base_set_equipment_identifier (MMModemBase *self, const char *ident)
{
    MMModemBasePrivate *priv;
    const char *dbus_path;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);

    /* Only do something if the value changes */
    if (  (priv->equipment_ident == ident)
       || (priv->equipment_ident && ident && !strcmp (priv->equipment_ident, ident)))
       return;

    g_free (priv->equipment_ident);
    priv->equipment_ident = g_strdup (ident);

    dbus_path = (const char *) g_object_get_data (G_OBJECT (self), DBUS_PATH_TAG);
    if (dbus_path) {
        if (priv->equipment_ident)
            mm_info ("Modem %s: Equipment identifier set (%s)", dbus_path, priv->equipment_ident);
        else
            mm_warn ("Modem %s: Equipment identifier not set", dbus_path);
    }

    g_object_notify (G_OBJECT (self), MM_MODEM_EQUIPMENT_IDENTIFIER);
}

const char *
mm_modem_base_get_unlock_required (MMModemBase *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);

    return MM_MODEM_BASE_GET_PRIVATE (self)->unlock_required;
}

void
mm_modem_base_set_unlock_required (MMModemBase *self, const char *unlock_required)
{
    MMModemBasePrivate *priv;
    const char *dbus_path;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);

    /* Only do something if the value changes */
    if (  (priv->unlock_required == unlock_required)
       || (   priv->unlock_required
           && unlock_required
           && !strcmp (priv->unlock_required, unlock_required)))
       return;

    g_free (priv->unlock_required);
    priv->unlock_required = g_strdup (unlock_required);

    dbus_path = (const char *) g_object_get_data (G_OBJECT (self), DBUS_PATH_TAG);
    if (dbus_path) {
        if (priv->unlock_required)
            mm_info ("Modem %s: unlock required (%s)", dbus_path, priv->unlock_required);
        else
            mm_info ("Modem %s: unlock no longer required", dbus_path);
    }

    g_object_notify (G_OBJECT (self), MM_MODEM_UNLOCK_REQUIRED);
}

guint32
mm_modem_base_get_unlock_retries (MMModemBase *self)
{
    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), 0);

    return MM_MODEM_BASE_GET_PRIVATE (self)->unlock_retries;
}

void
mm_modem_base_set_unlock_retries (MMModemBase *self, guint unlock_retries)
{
    MMModemBasePrivate *priv;
    const char *dbus_path;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);

    /* Only do something if the value changes */
    if (priv->unlock_retries == unlock_retries)
        return;

    priv->unlock_retries = unlock_retries;

    dbus_path = (const char *) g_object_get_data (G_OBJECT (self), DBUS_PATH_TAG);
    if (dbus_path) {
        if (priv->unlock_required) {
            mm_info ("Modem %s: # unlock retries for %s is %d",
                     dbus_path, priv->unlock_required, priv->unlock_retries);
        } else {
            mm_info ("Modem %s: # unlock retries is %d",
                     dbus_path, priv->unlock_retries);
        }
    }

    g_object_notify (G_OBJECT (self), MM_MODEM_UNLOCK_RETRIES);
}

const char *
mm_modem_base_get_manf (MMModemBase *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);

    return MM_MODEM_BASE_GET_PRIVATE (self)->manf;
}

void
mm_modem_base_set_manf (MMModemBase *self, const char *manf)
{
    MMModemBasePrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);
    g_free (priv->manf);
    priv->manf = g_strdup (manf);
}

const char *
mm_modem_base_get_model (MMModemBase *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);

    return MM_MODEM_BASE_GET_PRIVATE (self)->model;
}

void
mm_modem_base_set_model (MMModemBase *self, const char *model)
{
    MMModemBasePrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);
    g_free (priv->model);
    priv->model = g_strdup (model);
}

const char *
mm_modem_base_get_revision (MMModemBase *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);

    return MM_MODEM_BASE_GET_PRIVATE (self)->revision;
}

void
mm_modem_base_set_revision (MMModemBase *self, const char *revision)
{
    MMModemBasePrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);
    g_free (priv->revision);
    priv->revision = g_strdup (revision);
}

/*************************************************************************/
static void
card_info_simple_invoke (MMCallbackInfo *info)
{
    MMModemBase *self = MM_MODEM_BASE (info->modem);
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);
    MMModemInfoFn callback = (MMModemInfoFn) info->callback;

    callback (info->modem, priv->manf, priv->model, priv->revision, info->error, info->user_data);
}

static void
card_info_cache_invoke (MMCallbackInfo *info)
{
    MMModemBase *self = MM_MODEM_BASE (info->modem);
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);
    MMModemInfoFn callback = (MMModemInfoFn) info->callback;
    const char *manf, *cmanf, *model, *cmodel, *rev, *crev, *ati, *ati1, *gsn, *cgsn;

    manf = mm_callback_info_get_data (info, "card-info-manf");
    cmanf = mm_callback_info_get_data (info, "card-info-c-manf");

    model = mm_callback_info_get_data (info, "card-info-model");
    cmodel = mm_callback_info_get_data (info, "card-info-c-model");

    rev = mm_callback_info_get_data (info, "card-info-revision");
    crev = mm_callback_info_get_data (info, "card-info-c-revision");

    /* Prefer the 'C' responses over the plain responses */
    g_free (priv->manf);
    priv->manf = g_strdup (cmanf ? cmanf : manf);
    g_free (priv->model);
    priv->model = g_strdup (cmodel ? cmodel : model);
    g_free (priv->revision);
    priv->revision = g_strdup (crev ? crev : rev);

    ati = mm_callback_info_get_data (info, "card-info-ati");
    g_free (priv->ati);
    priv->ati = g_strdup (ati);

    ati1 = mm_callback_info_get_data (info, "card-info-ati1");
    g_free (priv->ati1);
    priv->ati1 = g_strdup (ati1);

    gsn = mm_callback_info_get_data (info, "card-info-gsn");
    cgsn = mm_callback_info_get_data (info, "card-info-c-gsn");
    g_free (priv->gsn);
    priv->gsn = g_strdup (cgsn ? cgsn : gsn);

    /* Build up the device identifier */
    g_free (priv->device_ident);
    priv->device_ident = mm_create_device_identifier (priv->vid,
                                                      priv->pid,
                                                      priv->ati,
                                                      priv->ati1,
                                                      priv->gsn,
                                                      priv->revision,
                                                      priv->model,
                                                      priv->manf);
    g_object_notify (G_OBJECT (self), MM_MODEM_DEVICE_IDENTIFIER);

    callback (info->modem, priv->manf, priv->model, priv->revision, info->error, info->user_data);
}

static void
info_item_done (MMCallbackInfo *info,
                GString *response,
                GError *error,
                const char *tag,
                const char *tag2,
                const char *desc)
{
    const char *p;

    if (!error) {
        p = response->str;
        if (tag)
            p = mm_strip_tag (p, tag);
        if (tag2)
            p = mm_strip_tag (p, tag2);
        mm_callback_info_set_data (info, desc, strlen (p) ? g_strdup (p) : NULL, g_free);
    }

    mm_callback_info_chain_complete_one (info);
}

#define GET_INFO_RESP_FN(func_name, tag, tag2, desc)                    \
    static void                                                         \
    func_name (MMAtSerialPort *port,                                    \
               GString *response,                                       \
               GError *error,                                           \
               gpointer user_data)                                      \
    {                                                                   \
        if (mm_callback_info_check_modem_removed ((MMCallbackInfo *) user_data)) \
            return;                                                     \
        info_item_done ((MMCallbackInfo *) user_data, response, error, tag, tag2, desc ); \
    }

GET_INFO_RESP_FN(get_revision_done, "+GMR:", "AT+GMR", "card-info-revision")
GET_INFO_RESP_FN(get_model_done, "+GMM:", "AT+GMM", "card-info-model")
GET_INFO_RESP_FN(get_manf_done, "+GMI:", "AT+GMI", "card-info-manf")

GET_INFO_RESP_FN(get_c_revision_done, "+CGMR:", "AT+CGMR", "card-info-c-revision")
GET_INFO_RESP_FN(get_c_model_done, "+CGMM:", "AT+CGMM", "card-info-c-model")
GET_INFO_RESP_FN(get_c_manf_done, "+CGMI:", "AT+CGMI", "card-info-c-manf")

GET_INFO_RESP_FN(get_ati_done, NULL, "ATI", "card-info-ati")
GET_INFO_RESP_FN(get_ati1_done, NULL, "ATI1", "card-info-ati1")
GET_INFO_RESP_FN(get_gsn_done, "+GSN:", "AT+GSN", "card-info-gsn")
GET_INFO_RESP_FN(get_cgsn_done, "+CGSN:", "AT+CGSN", "card-info-c-gsn")

void
mm_modem_base_get_card_info (MMModemBase *self,
                             MMAtSerialPort *port,
                             GError *port_error,
                             MMModemInfoFn callback,
                             gpointer user_data)
{
    MMModemBasePrivate *priv;
    MMCallbackInfo *info;
    gboolean cached = FALSE;
    GError *error = NULL;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_BASE (self));
    /* Either we get a proper AT port, or we get a port_error */
    g_return_if_fail ((port != NULL && MM_IS_AT_SERIAL_PORT (port)) || port_error != NULL);
    g_return_if_fail (callback != NULL);

    priv = MM_MODEM_BASE_GET_PRIVATE (self);

    /* Cached info and errors schedule the callback immediately and do 
     * not hit up the card for it's model information.
     */
    if (priv->manf || priv->model || priv->revision)
        cached = TRUE;
    else if (port_error)
        error = g_error_copy (port_error);

    /* If we have cached info or an error, don't hit up the card */
    if (cached || error) {
        info = mm_callback_info_new_full (MM_MODEM (self),
                                          card_info_simple_invoke,
                                          G_CALLBACK (callback),
                                          user_data);
        info->error = error;
        mm_callback_info_schedule (info);
        return;
    }

    /* Otherwise, ask the card */
    info = mm_callback_info_new_full (MM_MODEM (self),
                                      card_info_cache_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    mm_callback_info_chain_start (info, 10);

    mm_at_serial_port_queue_command_cached (port, "+GMI", 3, get_manf_done, info);
    mm_at_serial_port_queue_command_cached (port, "+GMM", 3, get_model_done, info);
    mm_at_serial_port_queue_command_cached (port, "+GMR", 3, get_revision_done, info);
    mm_at_serial_port_queue_command_cached (port, "+CGMI", 3, get_c_manf_done, info);
    mm_at_serial_port_queue_command_cached (port, "+CGMM", 3, get_c_model_done, info);
    mm_at_serial_port_queue_command_cached (port, "+CGMR", 3, get_c_revision_done, info);

    mm_at_serial_port_queue_command_cached (port, "I", 3, get_ati_done, info);
    mm_at_serial_port_queue_command_cached (port, "I1", 3, get_ati1_done, info);
    mm_at_serial_port_queue_command_cached (port, "+GSN", 3, get_gsn_done, info);
    mm_at_serial_port_queue_command_cached (port, "+CGSN", 3, get_cgsn_done, info);
}

/*****************************************************************************/

static gboolean
modem_auth_request (MMModem *modem,
                    const char *authorization,
                    DBusGMethodInvocation *context,
                    MMAuthRequestCb callback,
                    gpointer callback_data,
                    GDestroyNotify notify,
                    GError **error)
{
    MMModemBase *self = MM_MODEM_BASE (modem);
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);

    g_assert (priv->authp);
    return !!mm_auth_provider_request_auth (priv->authp,
                                            authorization,
                                            G_OBJECT (self),
                                            context,
                                            callback,
                                            callback_data,
                                            notify,
                                            error);
}

static gboolean
modem_auth_finish (MMModem *modem, MMAuthRequest *req, GError **error)
{
    if (mm_auth_request_get_result (req) != MM_AUTH_RESULT_AUTHORIZED) {
        g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_AUTHORIZATION_REQUIRED,
                     "This request requires the '%s' authorization",
                     mm_auth_request_get_authorization (req));
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

static void
mm_modem_base_init (MMModemBase *self)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);

    priv->authp = mm_auth_provider_get ();

    priv->ports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_ENABLED,
                                                    NULL,
                                                    MM_MODEM_DBUS_INTERFACE);
    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_EQUIPMENT_IDENTIFIER,
                                                    NULL,
                                                    MM_MODEM_DBUS_INTERFACE);
    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_DEVICE_IDENTIFIER,
                                                    NULL,
                                                    MM_MODEM_DBUS_INTERFACE);
    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_UNLOCK_REQUIRED,
                                                    NULL,
                                                    MM_MODEM_DBUS_INTERFACE);
    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_UNLOCK_RETRIES,
                                                    NULL,
                                                    MM_MODEM_DBUS_INTERFACE);
    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_IP_METHOD,
                                                    NULL,
                                                    MM_MODEM_DBUS_INTERFACE);
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->auth_request = modem_auth_request;
    modem_class->auth_finish = modem_auth_finish;
}

static void
pc_init (MMPropertiesChanged *pc_class)
{
}

static gboolean
is_enabled (MMModemState state)
{
    return (state >= MM_MODEM_STATE_ENABLED);
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (object);
    gboolean old_enabled;

    switch (prop_id) {
    case MM_MODEM_PROP_STATE:
        /* Ensure we update the 'enabled' property when the state changes */
        old_enabled = is_enabled (priv->state);
        priv->state = g_value_get_uint (value);
        if (old_enabled != is_enabled (priv->state))
            g_object_notify (object, MM_MODEM_ENABLED);
        break;
    case MM_MODEM_PROP_DRIVER:
        /* Construct only */
        priv->driver = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_PLUGIN:
        /* Construct only */
        priv->plugin = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_MASTER_DEVICE:
        /* Construct only */
        priv->device = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_IP_METHOD:
        priv->ip_method = g_value_get_uint (value);
        break;
    case MM_MODEM_PROP_VALID:
    case MM_MODEM_PROP_TYPE:
    case MM_MODEM_PROP_ENABLED:
    case MM_MODEM_PROP_EQUIPMENT_IDENTIFIER:
    case MM_MODEM_PROP_DEVICE_IDENTIFIER:
    case MM_MODEM_PROP_UNLOCK_REQUIRED:
    case MM_MODEM_PROP_UNLOCK_RETRIES:
        break;
    case MM_MODEM_PROP_HW_VID:
        /* Construct only */
        priv->vid = g_value_get_uint (value);
        break;
    case MM_MODEM_PROP_HW_PID:
        /* Construct only */
        priv->pid = g_value_get_uint (value);
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
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (object);

    switch (prop_id) {
    case MM_MODEM_PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
    case MM_MODEM_PROP_MASTER_DEVICE:
        g_value_set_string (value, priv->device);
        break;
    case MM_MODEM_PROP_DATA_DEVICE:
        g_value_set_string (value, NULL);
        break;
    case MM_MODEM_PROP_DRIVER:
        g_value_set_string (value, priv->driver);
        break;
    case MM_MODEM_PROP_PLUGIN:
        g_value_set_string (value, priv->plugin);
        break;
    case MM_MODEM_PROP_TYPE:
        g_value_set_uint (value, MM_MODEM_TYPE_UNKNOWN);
        break;
    case MM_MODEM_PROP_IP_METHOD:
        g_value_set_uint (value, priv->ip_method);
        break;
    case MM_MODEM_PROP_VALID:
        g_value_set_boolean (value, priv->valid);
        break;
    case MM_MODEM_PROP_ENABLED:
        g_value_set_boolean (value, is_enabled (priv->state));
        break;
    case MM_MODEM_PROP_EQUIPMENT_IDENTIFIER:
        g_value_set_string (value, priv->equipment_ident);
        break;
    case MM_MODEM_PROP_DEVICE_IDENTIFIER:
        g_value_set_string (value, priv->device_ident);
        break;
    case MM_MODEM_PROP_UNLOCK_REQUIRED:
        g_value_set_string (value, priv->unlock_required);
        break;
    case MM_MODEM_PROP_UNLOCK_RETRIES:
        g_value_set_uint (value, priv->unlock_retries);
        break;
    case MM_MODEM_PROP_HW_VID:
        g_value_set_uint (value, priv->vid);
        break;
    case MM_MODEM_PROP_HW_PID:
        g_value_set_uint (value, priv->pid);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMModemBase *self = MM_MODEM_BASE (object);
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);

    mm_auth_provider_cancel_for_owner (priv->authp, object);

    g_hash_table_destroy (priv->ports);
    g_free (priv->driver);
    g_free (priv->plugin);
    g_free (priv->device);
    g_free (priv->equipment_ident);
    g_free (priv->device_ident);
    g_free (priv->unlock_required);
    g_free (priv->manf);
    g_free (priv->model);
    g_free (priv->revision);
    g_free (priv->ati);
    g_free (priv->ati1);
    g_free (priv->gsn);

    G_OBJECT_CLASS (mm_modem_base_parent_class)->finalize (object);
}

static void
mm_modem_base_class_init (MMModemBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemBasePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_STATE,
                                      MM_MODEM_STATE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_MASTER_DEVICE,
                                      MM_MODEM_MASTER_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DATA_DEVICE,
                                      MM_MODEM_DATA_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DRIVER,
                                      MM_MODEM_DRIVER);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_PLUGIN,
                                      MM_MODEM_PLUGIN);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_TYPE,
                                      MM_MODEM_TYPE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_IP_METHOD,
                                      MM_MODEM_IP_METHOD);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_VALID,
                                      MM_MODEM_VALID);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_ENABLED,
                                      MM_MODEM_ENABLED);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_EQUIPMENT_IDENTIFIER,
                                      MM_MODEM_EQUIPMENT_IDENTIFIER);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DEVICE_IDENTIFIER,
                                      MM_MODEM_DEVICE_IDENTIFIER);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_UNLOCK_REQUIRED,
                                      MM_MODEM_UNLOCK_REQUIRED);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_UNLOCK_RETRIES,
                                      MM_MODEM_UNLOCK_RETRIES);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_HW_VID,
                                      MM_MODEM_HW_VID);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_HW_PID,
                                      MM_MODEM_HW_PID);

    mm_properties_changed_signal_enable (object_class);
}

