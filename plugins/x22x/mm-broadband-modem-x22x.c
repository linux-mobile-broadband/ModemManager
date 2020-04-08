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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-broadband-modem-x22x.h"

static void iface_modem_init (MMIfaceModem *iface);

static MMIfaceModem *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemX22x, mm_broadband_modem_x22x, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

struct _MMBroadbandModemX22xPrivate {
    GRegex *mode_regex;
    GRegex *sysinfo_regex;
    GRegex *specc_regex;
    GRegex *sperror_regex;
};

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    GError *error = NULL;
    GArray *all;
    GArray *combinations;
    GArray *filtered;
    MMModemModeCombination mode;

    all = iface_modem_parent->load_supported_modes_finish (self, res, &error);
    if (!all) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    /* Build list of combinations for 3GPP devices */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 3);

    /* 2G only */
    mode.allowed = MM_MODEM_MODE_2G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G only */
    mode.allowed = MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);

    /* Filter out those unsupported modes */
    filtered = mm_filter_supported_modes (all, combinations, self);
    g_array_unref (all);
    g_array_unref (combinations);

    g_task_return_pointer (task, filtered, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    /* Run parent's loading */
    iface_modem_parent->load_supported_modes (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_supported_modes_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    const gchar *response;
    gchar *str;
    gint mode = -1;
    GError *match_error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    r = g_regex_new ("\\+SYSSEL:\\s*(\\d+),(\\d+),(\\d+),(\\d+)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &match_error)) {
        if (match_error) {
            g_propagate_error (error, match_error);
        } else {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Couldn't match +SYSSEL reply: %s", response);
        }

        g_match_info_free (match_info);
        g_regex_unref (r);
        return FALSE;
    }

    str = g_match_info_fetch (match_info, 3);
    mode = atoi (str);
    g_free (str);
    g_match_info_free (match_info);
    g_regex_unref (r);

    switch (mode) {
    case 0:
        *allowed = MM_MODEM_MODE_ANY;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    case 1:
        *allowed = MM_MODEM_MODE_2G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    case 2:
        *allowed = MM_MODEM_MODE_3G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    default:
        break;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Failed to parse mode/tech response: Unexpected mode '%d'", mode);
    return FALSE;
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+SYSSEL?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
allowed_mode_update_ready (MMBroadbandModemX22x *self,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
set_current_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GTask *task;
    gchar *command;
    gint syssel = -1;

    task = g_task_new (self, NULL, callback, user_data);

    if (allowed == MM_MODEM_MODE_2G)
        syssel = 1;
    else if (allowed == MM_MODEM_MODE_3G)
        syssel = 2;
    else if (allowed == MM_MODEM_MODE_ANY &&
             preferred == MM_MODEM_MODE_NONE)
        syssel = 0;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G) &&
             preferred == MM_MODEM_MODE_NONE)
        syssel = 0;
    else {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested mode (allowed: '%s', preferred: '%s') not "
                                 "supported by the modem.",
                                 allowed_str,
                                 preferred_str);
        g_object_unref (task);
        g_free (allowed_str);
        g_free (preferred_str);
        return;
    }

    command = g_strdup_printf ("+SYSSEL=,%d,0", syssel);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    const gchar *result;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return FALSE;

    result = mm_strip_tag (result, "+SSND:");
    *access_technologies = mm_string_to_access_tech (result);
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+SSND?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
set_ignored_unsolicited_events_handlers (MMBroadbandModemX22x *self)
{
    MMPortSerialAt *ports[2];
    guint           i;

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable/disable unsolicited events in given port */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->mode_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->sysinfo_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->specc_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->sperror_regex,
            NULL, NULL, NULL);
    }
}

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_x22x_parent_class)->setup_ports (self);

    /* Unsolicited messages to always ignore */
    set_ignored_unsolicited_events_handlers (MM_BROADBAND_MODEM_X22X (self));
}

/*****************************************************************************/

MMBroadbandModemX22x *
mm_broadband_modem_x22x_new (const gchar *device,
                             const gchar **drivers,
                             const gchar *plugin,
                             guint16 vendor_id,
                             guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_X22X,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_x22x_init (MMBroadbandModemX22x *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_X22X,
                                              MMBroadbandModemX22xPrivate);

    self->priv->mode_regex    = g_regex_new ("\\r\\n\\^MODE:.+\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->sysinfo_regex = g_regex_new ("\\r\\n\\^SYSINFO:.+\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->specc_regex   = g_regex_new ("\\r\\n\\+SPECC\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->sperror_regex = g_regex_new ("\\r\\n\\+SPERROR:.+\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemX22x *self = MM_BROADBAND_MODEM_X22X (object);

    g_regex_unref (self->priv->mode_regex);
    g_regex_unref (self->priv->sysinfo_regex);
    g_regex_unref (self->priv->specc_regex);
    g_regex_unref (self->priv->sperror_regex);
    G_OBJECT_CLASS (mm_broadband_modem_x22x_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
}

static void
mm_broadband_modem_x22x_class_init (MMBroadbandModemX22xClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemX22xPrivate));

    object_class->finalize = finalize;

    broadband_modem_class->setup_ports = setup_ports;
}
