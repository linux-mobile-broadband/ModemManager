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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "fake-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-voice.h"
#include "mm-call-list.h"
#include "fake-call.h"
#include "mm-bind.h"

#define MM_FAKE_MODEM_PATH "fake-modem-path"

static void iface_modem_init (MMIfaceModemInterface *iface);
static void iface_modem_voice_init (MMIfaceModemVoiceInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMFakeModem, mm_fake_modem, MM_TYPE_BASE_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_VOICE, iface_modem_voice_init))

enum {
    PROP_0,
    PROP_CONNECTION,
    PROP_PATH,
    PROP_MODEM_DBUS_SKELETON,
    PROP_MODEM_STATE,
    PROP_MODEM_SIM,
    PROP_MODEM_SIM_SLOTS,
    PROP_MODEM_SIM_HOT_SWAP_SUPPORTED,
    PROP_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED,
    PROP_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED,
    PROP_MODEM_PERIODIC_CALL_LIST_CHECK_DISABLED,
    PROP_MODEM_CARRIER_CONFIG_MAPPING,
    PROP_MODEM_BEARER_LIST,
    PROP_MODEM_INDICATION_CALL_LIST_RELOAD_ENABLED,
    PROP_MODEM_VOICE_DBUS_SKELETON,
    PROP_MODEM_VOICE_CALL_LIST,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMFakeModemPrivate {
    GDBusConnection *connection;
    guint            dbus_id;
    gchar           *path;

    GObject *modem_dbus_skeleton;
    MMModemState modem_state;
    GObject *modem_sim;
    GPtrArray *modem_sim_slots;
    gboolean sim_hot_swap_supported;
    gboolean periodic_signal_check_disabled;
    gboolean periodic_access_tech_check_disabled;
    gboolean periodic_call_list_check_disabled;
    gchar *carrier_config_mapping;
    MMBearerList *modem_bearer_list;
    gboolean indication_call_list_reload_enabled;
    GObject *modem_voice_dbus_skeleton;
    MMCallList *modem_voice_call_list;
};

/*****************************************************************************/

const gchar *
mm_fake_modem_get_path (MMFakeModem *self)
{
    return self->priv->path;
}

MMCallList *
mm_fake_modem_get_call_list (MMFakeModem *self)
{
    return self->priv->modem_voice_call_list;
}

gboolean
mm_fake_modem_export_interfaces (MMFakeModem *self, GError **error)
{
    g_assert (self->priv->path);
    g_assert (self->priv->connection);

    if (self->priv->modem_dbus_skeleton) {
        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->priv->modem_dbus_skeleton),
                                               self->priv->connection,
                                               self->priv->path,
                                               error))
            return FALSE;
    }

    if (self->priv->modem_voice_dbus_skeleton) {
        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->priv->modem_voice_dbus_skeleton),
                                               self->priv->connection,
                                               self->priv->path,
                                               error))
            return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

static gboolean
modem_voice_check_support_finish (MMIfaceModemVoice  *self,
                                  GAsyncResult       *res,
                                  GError            **error)
{
    gboolean foobar;

    foobar = g_task_propagate_boolean (G_TASK (res), error);
    return foobar;
}

static void
modem_voice_check_support (MMIfaceModemVoice   *self,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/

static MMBaseCall *
modem_voice_create_call (MMIfaceModemVoice *_self,
                         MMCallDirection    direction,
                         const gchar       *number,
                         const guint        dtmf_tone_duration)
{
    MMFakeModem *self = MM_FAKE_MODEM (_self);

    return MM_BASE_CALL (mm_fake_call_new (self->priv->connection,
                                           _self,
                                           direction,
                                           number,
                                           dtmf_tone_duration));
}

/*****************************************************************************/

MMFakeModem *
mm_fake_modem_new (GDBusConnection *connection)
{
    return MM_FAKE_MODEM (g_object_new (MM_TYPE_FAKE_MODEM,
                                        MM_BINDABLE_CONNECTION, connection,
                                        NULL));
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMFakeModem *self = MM_FAKE_MODEM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);
        break;
    case PROP_MODEM_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_dbus_skeleton);
        self->priv->modem_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_STATE:
        self->priv->modem_state = g_value_get_enum (value);
        break;
    case PROP_MODEM_SIM:
        g_clear_object (&self->priv->modem_sim);
        self->priv->modem_sim = g_value_dup_object (value);
        break;
    case PROP_MODEM_SIM_SLOTS:
        g_clear_pointer (&self->priv->modem_sim_slots, g_ptr_array_unref);
        self->priv->modem_sim_slots = g_value_dup_boxed (value);
        break;
    case PROP_MODEM_SIM_HOT_SWAP_SUPPORTED:
        self->priv->sim_hot_swap_supported = g_value_get_boolean (value);
        break;
    case PROP_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED:
        self->priv->periodic_signal_check_disabled = g_value_get_boolean (value);
        break;
    case PROP_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED:
        self->priv->periodic_access_tech_check_disabled = g_value_get_boolean (value);
        break;
    case PROP_MODEM_PERIODIC_CALL_LIST_CHECK_DISABLED:
        self->priv->periodic_call_list_check_disabled = g_value_get_boolean (value);
        break;
    case PROP_MODEM_CARRIER_CONFIG_MAPPING:
        self->priv->carrier_config_mapping = g_value_dup_string (value);
        break;
    case PROP_MODEM_BEARER_LIST:
        g_clear_object (&self->priv->modem_bearer_list);
        self->priv->modem_bearer_list = g_value_dup_object (value);
        break;
    case PROP_MODEM_INDICATION_CALL_LIST_RELOAD_ENABLED:
        self->priv->indication_call_list_reload_enabled = g_value_get_boolean (value);
        break;
    case PROP_MODEM_VOICE_DBUS_SKELETON:
        g_clear_object (&self->priv->modem_voice_dbus_skeleton);
        self->priv->modem_voice_dbus_skeleton = g_value_dup_object (value);
        break;
    case PROP_MODEM_VOICE_CALL_LIST:
        g_clear_object (&self->priv->modem_voice_call_list);
        self->priv->modem_voice_call_list = g_value_dup_object (value);
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
    MMFakeModem *self = MM_FAKE_MODEM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_dbus_skeleton);
        break;
    case PROP_MODEM_STATE:
        g_value_set_enum (value, self->priv->modem_state);
        break;
    case PROP_MODEM_SIM:
        g_value_set_object (value, self->priv->modem_sim);
        break;
    case PROP_MODEM_SIM_SLOTS:
        g_value_set_boxed (value, self->priv->modem_sim_slots);
        break;
    case PROP_MODEM_SIM_HOT_SWAP_SUPPORTED:
        g_value_set_boolean (value, self->priv->sim_hot_swap_supported);
        break;
    case PROP_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED:
        g_value_set_boolean (value, self->priv->periodic_signal_check_disabled);
        break;
    case PROP_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED:
        g_value_set_boolean (value, self->priv->periodic_access_tech_check_disabled);
        break;
    case PROP_MODEM_PERIODIC_CALL_LIST_CHECK_DISABLED:
        g_value_set_boolean (value, self->priv->periodic_call_list_check_disabled);
        break;
    case PROP_MODEM_CARRIER_CONFIG_MAPPING:
        g_value_set_string (value, self->priv->carrier_config_mapping);
        break;
    case PROP_MODEM_BEARER_LIST:
        g_value_set_object (value, self->priv->modem_bearer_list);
        break;
    case PROP_MODEM_INDICATION_CALL_LIST_RELOAD_ENABLED:
        g_value_set_boolean (value, self->priv->indication_call_list_reload_enabled);
        break;
    case PROP_MODEM_VOICE_DBUS_SKELETON:
        g_value_set_object (value, self->priv->modem_voice_dbus_skeleton);
        break;
    case PROP_MODEM_VOICE_CALL_LIST:
        g_value_set_object (value, self->priv->modem_voice_call_list);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_fake_modem_init (MMFakeModem *self)
{
    static guint id = 0;

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_FAKE_MODEM,
                                              MMFakeModemPrivate);

    /* Each modem is given a unique id to build its own DBus path */
    self->priv->dbus_id = id++;
    self->priv->path = g_strdup_printf (MM_DBUS_MODEM_PREFIX "/%d", self->priv->dbus_id);
}

static void
finalize (GObject *object)
{
    MMFakeModem *self = MM_FAKE_MODEM (object);

    g_free (self->priv->path);
    g_free (self->priv->carrier_config_mapping);

    G_OBJECT_CLASS (mm_fake_modem_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMFakeModem *self = MM_FAKE_MODEM (object);

    if (self->priv->modem_dbus_skeleton) {
        mm_iface_modem_shutdown (MM_IFACE_MODEM (object));
        g_clear_object (&self->priv->modem_dbus_skeleton);
    }
    if (self->priv->modem_voice_dbus_skeleton) {
        mm_iface_modem_voice_shutdown (MM_IFACE_MODEM_VOICE (object));
        g_clear_object (&self->priv->modem_voice_dbus_skeleton);
    }
    g_clear_object (&self->priv->modem_sim);
    g_clear_pointer (&self->priv->modem_sim_slots, g_ptr_array_unref);
    g_clear_object (&self->priv->modem_bearer_list);
    g_clear_object (&self->priv->modem_voice_call_list);
    g_clear_object (&self->priv->connection);

    G_OBJECT_CLASS (mm_fake_modem_parent_class)->dispose (object);
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
}

static void
iface_modem_voice_init (MMIfaceModemVoiceInterface *iface)
{
    iface->check_support = modem_voice_check_support;
    iface->check_support_finish = modem_voice_check_support_finish;
    iface->create_call = modem_voice_create_call;

#if 0
    iface->load_call_list = modem_voice_load_call_list;
    iface->load_call_list_finish = modem_voice_load_call_list_finish;
    iface->hold_and_accept = modem_voice_hold_and_accept;
    iface->hold_and_accept_finish = modem_voice_hold_and_accept_finish;
    iface->hangup_and_accept = modem_voice_hangup_and_accept;
    iface->hangup_and_accept_finish = modem_voice_hangup_and_accept_finish;
    iface->hangup_all = modem_voice_hangup_all;
    iface->hangup_all_finish = modem_voice_hangup_all_finish;
    iface->join_multiparty = modem_voice_join_multiparty;
    iface->join_multiparty_finish = modem_voice_join_multiparty_finish;
    iface->leave_multiparty = modem_voice_leave_multiparty;
    iface->leave_multiparty_finish = modem_voice_leave_multiparty_finish;
    iface->transfer = modem_voice_transfer;
    iface->transfer_finish = modem_voice_transfer_finish;
    iface->call_waiting_setup = modem_voice_call_waiting_setup;
    iface->call_waiting_setup_finish = modem_voice_call_waiting_setup_finish;
    iface->call_waiting_query = modem_voice_call_waiting_query;
    iface->call_waiting_query_finish = modem_voice_call_waiting_query_finish;
#endif
}

static void
mm_fake_modem_class_init (MMFakeModemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMFakeModemPrivate));

    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BINDABLE_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_FAKE_MODEM_PATH,
                             "Path",
                             "DBus path of the call",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_DBUS_SKELETON,
                                      MM_IFACE_MODEM_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_STATE,
                                      MM_IFACE_MODEM_STATE);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_SIM,
                                      MM_IFACE_MODEM_SIM);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_SIM_SLOTS,
                                      MM_IFACE_MODEM_SIM_SLOTS);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_SIM_HOT_SWAP_SUPPORTED,
                                      MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED,
                                      MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED,
                                      MM_IFACE_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_PERIODIC_CALL_LIST_CHECK_DISABLED,
                                      MM_IFACE_MODEM_VOICE_PERIODIC_CALL_LIST_CHECK_DISABLED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_CARRIER_CONFIG_MAPPING,
                                      MM_IFACE_MODEM_CARRIER_CONFIG_MAPPING);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_BEARER_LIST,
                                      MM_IFACE_MODEM_BEARER_LIST);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_INDICATION_CALL_LIST_RELOAD_ENABLED,
                                      MM_IFACE_MODEM_VOICE_INDICATION_CALL_LIST_RELOAD_ENABLED);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_VOICE_DBUS_SKELETON,
                                      MM_IFACE_MODEM_VOICE_DBUS_SKELETON);

    g_object_class_override_property (object_class,
                                      PROP_MODEM_VOICE_CALL_LIST,
                                      MM_IFACE_MODEM_VOICE_CALL_LIST);
}
