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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-huawei.h"
#include "mm-call-huawei.h"

G_DEFINE_TYPE (MMCallHuawei, mm_call_huawei, MM_TYPE_BASE_CALL)

struct _MMCallHuaweiPrivate {
    GRegex *conf_regex;
    GRegex *conn_regex;
    GRegex *cend_regex;
    GRegex *ddtmf_regex;
};

/*****************************************************************************/
/* In-call unsolicited events */

static void
huawei_voice_ringback_tone (MMPortSerialAt *port,
                            GMatchInfo     *match_info,
                            MMCallHuawei   *self)
{
    guint call_x = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &call_x))
        return;

    mm_dbg ("Ringback tone from call id '%u'", call_x);

    if (mm_gdbus_call_get_state (MM_GDBUS_CALL (self)) == MM_CALL_STATE_DIALING)
        mm_base_call_change_state (MM_BASE_CALL (self), MM_CALL_STATE_RINGING_OUT, MM_CALL_STATE_REASON_OUTGOING_STARTED);
}

static void
huawei_voice_call_connection (MMPortSerialAt *port,
                              GMatchInfo     *match_info,
                              MMCallHuawei   *self)
{
    guint call_x    = 0;
    guint call_type = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &call_x))
        return;

    if (!mm_get_uint_from_match_info (match_info, 2, &call_type))
        return;

    mm_dbg ("Call id '%u' of type '%u' connected", call_x, call_type);

    if (mm_gdbus_call_get_state (MM_GDBUS_CALL (self)) == MM_CALL_STATE_RINGING_OUT)
        mm_base_call_change_state (MM_BASE_CALL (self), MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
}

static void
huawei_voice_call_end (MMPortSerialAt *port,
                       GMatchInfo     *match_info,
                       MMCallHuawei   *self)
{
    guint call_x     = 0;
    guint duration   = 0;
    guint cc_cause   = 0;
    guint end_status = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &call_x))
        return;

    if (!mm_get_uint_from_match_info (match_info, 2, &duration))
        return;

    if (!mm_get_uint_from_match_info (match_info, 3, &end_status))
        return;

    /* This is optional */
    mm_get_uint_from_match_info (match_info, 4, &cc_cause);

    mm_dbg ("Call id '%u' terminated with status '%u' and cause '%u'. Duration of call '%d'",
            call_x, end_status, cc_cause, duration);

    mm_base_call_change_state (MM_BASE_CALL (self), MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);
}

static void
huawei_voice_received_dtmf (MMPortSerialAt *port,
                            GMatchInfo     *match_info,
                            MMCallHuawei   *self)
{
    gchar *key;

    key = g_match_info_fetch (match_info, 1);

    if (!key)
        return;

    mm_dbg ("Received DTMF '%s'", key);
    mm_base_call_received_dtmf (MM_BASE_CALL (self), key);
    g_free (key);
}

static gboolean
common_setup_cleanup_unsolicited_events (MMCallHuawei  *self,
                                         gboolean       enable,
                                         GError       **error)
{
    MMBaseModem *modem = NULL;
    GList       *ports, *l;

    if (G_UNLIKELY (!self->priv->conf_regex))
        self->priv->conf_regex = g_regex_new ("\\r\\n\\^CONF:\\s*(\\d+)\\r\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (G_UNLIKELY (!self->priv->conn_regex))
        self->priv->conn_regex = g_regex_new ("\\r\\n\\^CONN:\\s*(\\d+),(\\d+)\\r\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (G_UNLIKELY (!self->priv->cend_regex))
        self->priv->cend_regex = g_regex_new ("\\r\\n\\^CEND:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+),?\\s*(\\d*)\\r\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (G_UNLIKELY (!self->priv->ddtmf_regex))
        self->priv->ddtmf_regex = g_regex_new ("\\r\\n\\^DDTMF:\\s*([0-9A-D\\*\\#])\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    g_object_get (self,
                  MM_BASE_CALL_MODEM, &modem,
                  NULL);

    ports = mm_broadband_modem_huawei_get_at_port_list (MM_BROADBAND_MODEM_HUAWEI (modem));

    for (l = ports; l; l = g_list_next (l)) {
        MMPortSerialAt *port;

        port = MM_PORT_SERIAL_AT (l->data);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->conf_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_voice_ringback_tone : NULL,
            enable ? self : NULL,
            NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->conn_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_voice_call_connection : NULL,
            enable ? self : NULL,
            NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->cend_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_voice_call_end : NULL,
            enable ? self : NULL,
            NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->ddtmf_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_voice_received_dtmf: NULL,
            enable ? self : NULL,
            NULL);
    }

    g_list_free_full (ports, g_object_unref);
    g_object_unref (modem);
    return TRUE;
}

static gboolean
setup_unsolicited_events (MMBaseCall  *self,
                          GError     **error)
{
    return common_setup_cleanup_unsolicited_events (MM_CALL_HUAWEI (self), TRUE, error);
}

static gboolean
cleanup_unsolicited_events (MMBaseCall  *self,
                            GError     **error)
{
    return common_setup_cleanup_unsolicited_events (MM_CALL_HUAWEI (self), FALSE, error);
}

/*****************************************************************************/

MMBaseCall *
mm_call_huawei_new (MMBaseModem     *modem,
                    MMCallDirection  direction,
                    const gchar     *number)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_CALL_HUAWEI,
                                       MM_BASE_CALL_MODEM, modem,
                                       "direction",        direction,
                                       "number",           number,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
}

static void
mm_call_huawei_init (MMCallHuawei *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CALL_HUAWEI, MMCallHuaweiPrivate);
}

static void
finalize (GObject *object)
{
    MMCallHuawei *self = MM_CALL_HUAWEI (object);

    if (self->priv->conf_regex)
        g_regex_unref (self->priv->conf_regex);
    if (self->priv->conn_regex)
        g_regex_unref (self->priv->conn_regex);
    if (self->priv->cend_regex)
        g_regex_unref (self->priv->cend_regex);
    if (self->priv->ddtmf_regex)
        g_regex_unref (self->priv->ddtmf_regex);

    G_OBJECT_CLASS (mm_call_huawei_parent_class)->finalize (object);
}

static void
mm_call_huawei_class_init (MMCallHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallHuaweiPrivate));

    object_class->finalize = finalize;

    base_call_class->setup_unsolicited_events   = setup_unsolicited_events;
    base_call_class->cleanup_unsolicited_events = cleanup_unsolicited_events;
}
