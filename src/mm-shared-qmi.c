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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <arpa/inet.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <libqmi-glib.h>

#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-shared-qmi.h"
#include "mm-modem-helpers-qmi.h"

/* Default session id to use in LOC operations */
#define DEFAULT_LOC_SESSION_ID 0x10

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-qmi-private-tag"
static GQuark private_quark;

typedef struct {
    /* Location helpers */
    MMIfaceModemLocation  *iface_modem_location_parent;
    MMModemLocationSource  enabled_sources;
    QmiClient             *pds_client;
    gulong                 pds_location_event_report_indication_id;
    QmiClient             *loc_client;
    gulong                 loc_location_nmea_indication_id;
} Private;

static void
private_free (Private *priv)
{
    if (priv->pds_location_event_report_indication_id)
        g_signal_handler_disconnect (priv->pds_client, priv->pds_location_event_report_indication_id);
    if (priv->pds_client)
        g_object_unref (priv->pds_client);
    if (priv->loc_location_nmea_indication_id)
        g_signal_handler_disconnect (priv->loc_client, priv->loc_location_nmea_indication_id);
    if (priv->loc_client)
        g_object_unref (priv->loc_client);
    g_slice_free (Private, priv);
}

static Private *
get_private (MMSharedQmi *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);

        /* Setup parent class' MMIfaceModemLocation */
        g_assert (MM_SHARED_QMI_GET_INTERFACE (self)->peek_parent_location_interface);
        priv->iface_modem_location_parent = MM_SHARED_QMI_GET_INTERFACE (self)->peek_parent_location_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/
/* Location: Set SUPL server */

typedef struct {
    QmiClient *client;
    gchar     *supl;
    glong      indication_id;
    guint      timeout_id;
} SetSuplServerContext;

static void
set_supl_server_context_free (SetSuplServerContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove  (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_slice_free (SetSuplServerContext, ctx);
}

static gboolean
parse_as_ip_port (const gchar *supl,
                  guint32     *out_ip,
                  guint32     *out_port)
{
    gboolean   valid = FALSE;
    gchar    **split;
    guint      port;
    guint32    ip;

    split = g_strsplit (supl, ":", -1);
    if (g_strv_length (split) != 2)
        goto out;

    if (!mm_get_uint_from_str (split[1], &port))
        goto out;
    if (port == 0 || port > G_MAXUINT16)
        goto out;
    if (inet_pton (AF_INET, split[0], &ip) <= 0)
        goto out;

    *out_ip = ip;
    *out_port = port;
    valid = TRUE;

out:
    g_strfreev (split);
    return valid;
}

static gboolean
parse_as_utf16_url (const gchar  *supl,
                    GArray      **out_url)
{
    gchar *utf16;
    gsize  utf16_len;

    utf16 = g_convert (supl, -1, "UTF-16BE", "UTF-8", NULL, &utf16_len, NULL);
    *out_url = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (guint8), utf16_len),
                                    utf16, utf16_len);
    g_free (utf16);
    return TRUE;
}


gboolean
mm_shared_qmi_location_set_supl_server_finish (MMIfaceModemLocation  *self,
                                               GAsyncResult          *res,
                                               GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_set_agps_config_ready (QmiClientPds *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    QmiMessagePdsSetAgpsConfigOutput *output;
    GError                           *error = NULL;

    output = qmi_client_pds_set_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_agps_config_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    qmi_message_pds_set_agps_config_output_unref (output);
}

static void
pds_set_supl_server (GTask *task)
{
    MMSharedQmi                     *self;
    SetSuplServerContext            *ctx;
    QmiMessagePdsSetAgpsConfigInput *input;
    guint32                          ip;
    guint32                          port;
    GArray                          *url;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    input = qmi_message_pds_set_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    if (parse_as_ip_port (ctx->supl, &ip, &port))
        qmi_message_pds_set_agps_config_input_set_location_server_address (input, ip, port, NULL);
    else if (parse_as_utf16_url (ctx->supl, &url)) {
        qmi_message_pds_set_agps_config_input_set_location_server_url (input, url, NULL);
        g_array_unref (url);
    } else
        g_assert_not_reached ();

    qmi_client_pds_set_agps_config (
        QMI_CLIENT_PDS (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_set_agps_config_ready,
        task);
    qmi_message_pds_set_agps_config_input_unref (input);
}

static gboolean
loc_location_set_server_indication_timed_out (GTask *task)
{
    SetSuplServerContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive indication with the server update result");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_location_set_server_indication_cb (QmiClientLoc                    *client,
                                       QmiIndicationLocSetServerOutput *output,
                                       GTask                           *task)
{
    QmiLocIndicationStatus  status;
    GError                 *error = NULL;

    if (!qmi_indication_loc_set_server_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    mm_error_from_qmi_loc_indication_status (status, &error);

out:
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_set_server_ready (QmiClientLoc *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    SetSuplServerContext         *ctx;
    QmiMessageLocSetServerOutput *output;
    GError                       *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_set_server_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_set_server_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_set_server_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "set-server",
                                           G_CALLBACK (loc_location_set_server_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_set_server_indication_timed_out,
                                             task);

    qmi_message_loc_set_server_output_unref (output);
}

static void
loc_set_supl_server (GTask *task)
{
    MMSharedQmi                 *self;
    SetSuplServerContext        *ctx;
    QmiMessageLocSetServerInput *input;
    guint32                      ip;
    guint32                      port;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    input = qmi_message_loc_set_server_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_loc_set_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_UMTS_SLP, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_loc_set_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_CDMA_PDE, NULL);

    if (parse_as_ip_port (ctx->supl, &ip, &port))
        qmi_message_loc_set_server_input_set_ipv4 (input, ip, port, NULL);
    else
        qmi_message_loc_set_server_input_set_url (input, ctx->supl, NULL);

    qmi_client_loc_set_server (
        QMI_CLIENT_LOC (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)loc_set_server_ready,
        task);
    qmi_message_loc_set_server_input_unref (input);
}

void
mm_shared_qmi_location_set_supl_server (MMIfaceModemLocation *self,
                                        const gchar          *supl,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data)
{
    GTask                *task;
    SetSuplServerContext *ctx;
    QmiClient            *client = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (SetSuplServerContext);
    ctx->supl = g_strdup (supl);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_supl_server_context_free);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        pds_set_supl_server (task);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        loc_set_supl_server (task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: Load SUPL server */

typedef struct {
    QmiClient *client;
    glong      indication_id;
    guint      timeout_id;
} LoadSuplServerContext;

static void
load_supl_server_context_free (LoadSuplServerContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove  (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_slice_free (LoadSuplServerContext, ctx);
}

gchar *
mm_shared_qmi_location_load_supl_server_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
pds_get_agps_config_ready (QmiClientPds *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    QmiMessagePdsGetAgpsConfigOutput *output;
    GError                           *error = NULL;
    guint32                           ip = 0;
    guint32                           port = 0;
    GArray                           *url = NULL;
    gchar                            *str = NULL;

    output = qmi_client_pds_get_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    if (!qmi_message_pds_get_agps_config_output_get_result (output, &error))
        goto out;

    /* Prefer IP/PORT to URL */
    if (qmi_message_pds_get_agps_config_output_get_location_server_address (
            output,
            &ip,
            &port,
            NULL) &&
        ip != 0 &&
        port != 0) {
        struct in_addr a = { .s_addr = ip };
        gchar buf[INET_ADDRSTRLEN + 1];

        memset (buf, 0, sizeof (buf));

        if (!inet_ntop (AF_INET, &a, buf, sizeof (buf) - 1)) {
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot convert numeric IP address (%u) to string", ip);
            goto out;
        }

        str = g_strdup_printf ("%s:%u", buf, port);
        goto out;
    }

    if (qmi_message_pds_get_agps_config_output_get_location_server_url (
            output,
            &url,
            NULL) &&
        url->len > 0) {
        str = g_convert (url->data, url->len, "UTF-8", "UTF-16BE", NULL, NULL, NULL);
    }

    if (!str)
        str = g_strdup ("");

out:
    if (error)
        g_task_return_error (task, error);
    else {
        g_assert (str);
        g_task_return_pointer (task, str, g_free);
    }
    g_object_unref (task);

    if (output)
        qmi_message_pds_get_agps_config_output_unref (output);
}

static void
pds_load_supl_server (GTask *task)
{
    MMSharedQmi                     *self;
    LoadSuplServerContext           *ctx;
    QmiMessagePdsGetAgpsConfigInput *input;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    input = qmi_message_pds_get_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    qmi_client_pds_get_agps_config (
        QMI_CLIENT_PDS (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_get_agps_config_ready,
        task);
    qmi_message_pds_get_agps_config_input_unref (input);
}

static gboolean
loc_location_get_server_indication_timed_out (GTask *task)
{
    LoadSuplServerContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive indication with the current server settings");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_location_get_server_indication_cb (QmiClientLoc                    *client,
                                       QmiIndicationLocGetServerOutput *output,
                                       GTask                           *task)
{
    QmiLocIndicationStatus  status;
    const gchar            *url          = NULL;
    guint32                 ipv4_address = 0;
    guint16                 ipv4_port    = 0;
    GError                 *error        = NULL;
    gchar                  *str          = NULL;

    if (!qmi_indication_loc_get_server_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    if (!mm_error_from_qmi_loc_indication_status (status, &error))
        goto out;

    /* Prefer IP/PORT to URL */

    if (qmi_indication_loc_get_server_output_get_ipv4 (
            output,
            &ipv4_address,
            &ipv4_port,
            NULL) &&
        ipv4_address != 0 && ipv4_port != 0) {
        struct in_addr a = { .s_addr = ipv4_address };
        gchar buf[INET_ADDRSTRLEN + 1];

        memset (buf, 0, sizeof (buf));

        if (!inet_ntop (AF_INET, &a, buf, sizeof (buf) - 1)) {
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot convert numeric IP address (%u) to string", ipv4_address);
            goto out;
        }

        str = g_strdup_printf ("%s:%u", buf, ipv4_port);
        goto out;
    }

    if (qmi_indication_loc_get_server_output_get_url (
            output,
            &url,
            NULL) &&
        url && url [0]) {
        str = g_strdup (url);
    }

    if (!str)
        str = g_strdup ("");

out:
    if (error)
        g_task_return_error (task, error);
    else {
        g_assert (str);
        g_task_return_pointer (task, str, g_free);
    }
    g_object_unref (task);

}

static void
loc_get_server_ready (QmiClientLoc *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    LoadSuplServerContext        *ctx;
    QmiMessageLocGetServerOutput *output;
    GError                       *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_get_server_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_get_server_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_get_server_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "get-server",
                                           G_CALLBACK (loc_location_get_server_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_get_server_indication_timed_out,
                                             task);

    qmi_message_loc_get_server_output_unref (output);
}

static void
loc_load_supl_server (GTask *task)
{
    MMSharedQmi                 *self;
    LoadSuplServerContext       *ctx;
    QmiMessageLocGetServerInput *input;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    input = qmi_message_loc_get_server_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_loc_get_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_UMTS_SLP, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_loc_get_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_CDMA_PDE, NULL);

    qmi_message_loc_get_server_input_set_server_address_type (
        input,
        (QMI_LOC_SERVER_ADDRESS_TYPE_IPV4 | QMI_LOC_SERVER_ADDRESS_TYPE_URL),
        NULL);

    qmi_client_loc_get_server (
        QMI_CLIENT_LOC (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)loc_get_server_ready,
        task);
    qmi_message_loc_get_server_input_unref (input);
}

void
mm_shared_qmi_location_load_supl_server (MMIfaceModemLocation *self,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data)
{
    QmiClient             *client;
    GTask                 *task;
    LoadSuplServerContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (LoadSuplServerContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_supl_server_context_free);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        pds_load_supl_server (task);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        loc_load_supl_server (task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helper: stop gps engine */

static gboolean
stop_gps_engine_finish (MMSharedQmi   *self,
                        GAsyncResult  *res,
                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_gps_service_state_stop_ready (QmiClientPds *client,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    MMSharedQmi                           *self;
    Private                               *priv;
    QmiMessagePdsSetGpsServiceStateOutput *output;
    GError                                *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }

        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    if (priv->pds_client) {
        if (priv->pds_location_event_report_indication_id != 0) {
            g_signal_handler_disconnect (priv->pds_client, priv->pds_location_event_report_indication_id);
            priv->pds_location_event_report_indication_id = 0;
        }
        g_clear_object (&priv->pds_client);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_stop_ready (QmiClientLoc *client,
                GAsyncResult *res,
                GTask        *task)
{
    MMSharedQmi             *self;
    Private                 *priv;
    QmiMessageLocStopOutput *output;
    GError                  *error = NULL;

    output = qmi_client_loc_stop_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_stop_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't stop GPS engine: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_stop_output_unref (output);
        return;
    }

    qmi_message_loc_stop_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    if (priv->loc_client) {
        if (priv->loc_location_nmea_indication_id != 0) {
            g_signal_handler_disconnect (priv->loc_client, priv->loc_location_nmea_indication_id);
            priv->loc_location_nmea_indication_id = 0;
        }
        g_clear_object (&priv->loc_client);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
stop_gps_engine (MMSharedQmi         *self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    priv = get_private (self);

    task = g_task_new (self, NULL, callback, user_data);

    if (priv->pds_client) {
        QmiMessagePdsSetGpsServiceStateInput *input;

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, FALSE, NULL);
        qmi_client_pds_set_gps_service_state (
            QMI_CLIENT_PDS (priv->pds_client),
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_gps_service_state_stop_ready,
            task);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    if (priv->loc_client) {
        QmiMessageLocStopInput *input;

        input = qmi_message_loc_stop_input_new ();
        qmi_message_loc_stop_input_set_session_id (input, DEFAULT_LOC_SESSION_ID, NULL);
        qmi_client_loc_stop (QMI_CLIENT_LOC (priv->loc_client),
                             input,
                             10,
                             NULL,
                             (GAsyncReadyCallback) loc_stop_ready,
                             task);
        qmi_message_loc_stop_input_unref (input);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helpers: NMEA indication callbacks */

static void
pds_location_event_report_indication_cb (QmiClientPds                      *client,
                                         QmiIndicationPdsEventReportOutput *output,
                                         MMSharedQmi                       *self)
{
    QmiPdsPositionSessionStatus  session_status;
    const gchar                 *nmea;

    if (qmi_indication_pds_event_report_output_get_position_session_status (
            output,
            &session_status,
            NULL)) {
        mm_dbg ("[GPS] session status changed: '%s'",
                qmi_pds_position_session_status_get_string (session_status));
    }

    if (qmi_indication_pds_event_report_output_get_nmea_position (
            output,
            &nmea,
            NULL)) {
        mm_dbg ("[NMEA] %s", nmea);
        mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), nmea);
    }
}

static void
loc_location_nmea_indication_cb (QmiClientLoc               *client,
                                 QmiIndicationLocNmeaOutput *output,
                                 MMSharedQmi                *self)
{
    const gchar *nmea = NULL;

    qmi_indication_loc_nmea_output_get_nmea_string (output, &nmea, NULL);
    if (!nmea)
        return;

    mm_dbg ("[NMEA] %s", nmea);
    mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), nmea);
}

/*****************************************************************************/
/* Location: internal helper: start gps engine */

static gboolean
start_gps_engine_finish (MMSharedQmi   *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_ser_location_ready (QmiClientPds *client,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMSharedQmi                       *self;
    Private                           *priv;
    QmiMessagePdsSetEventReportOutput *output;
    GError                            *error = NULL;

    output = qmi_client_pds_set_event_report_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_event_report_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set event report: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_set_event_report_output_unref (output);
        return;
    }

    qmi_message_pds_set_event_report_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    g_assert (!priv->pds_client);
    g_assert (priv->pds_location_event_report_indication_id == 0);
    priv->pds_client = g_object_ref (client);
    priv->pds_location_event_report_indication_id =
        g_signal_connect (priv->pds_client,
                          "event-report",
                          G_CALLBACK (pds_location_event_report_indication_cb),
                          self);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
pds_auto_tracking_state_start_ready (QmiClientPds *client,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    QmiMessagePdsSetEventReportInput        *input;
    QmiMessagePdsSetAutoTrackingStateOutput *output = NULL;
    GError                                  *error = NULL;

    output = qmi_client_pds_set_auto_tracking_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_auto_tracking_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set auto-tracking state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_auto_tracking_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_auto_tracking_state_output_unref (output);

    /* Only gather standard NMEA traces */
    input = qmi_message_pds_set_event_report_input_new ();
    qmi_message_pds_set_event_report_input_set_nmea_position_reporting (input, TRUE, NULL);
    qmi_client_pds_set_event_report (
        client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)pds_ser_location_ready,
        task);
    qmi_message_pds_set_event_report_input_unref (input);
}

static void
pds_gps_service_state_start_ready (QmiClientPds *client,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    QmiMessagePdsSetAutoTrackingStateInput *input;
    QmiMessagePdsSetGpsServiceStateOutput  *output;
    GError                                 *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    /* Enable auto-tracking for a continuous fix */
    input = qmi_message_pds_set_auto_tracking_state_input_new ();
    qmi_message_pds_set_auto_tracking_state_input_set_state (input, TRUE, NULL);
    qmi_client_pds_set_auto_tracking_state (
        client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_auto_tracking_state_start_ready,
        task);
    qmi_message_pds_set_auto_tracking_state_input_unref (input);
}

static void
loc_register_events_ready (QmiClientLoc *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    MMSharedQmi                       *self;
    Private                           *priv;
    QmiMessageLocRegisterEventsOutput *output;
    GError                            *error = NULL;

   output = qmi_client_loc_register_events_finish (client, res, &error);
   if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
   }

    if (!qmi_message_loc_register_events_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't not register tracking events: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_register_events_output_unref (output);
        return;
    }

    qmi_message_loc_register_events_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    g_assert (!priv->loc_client);
    g_assert (!priv->loc_location_nmea_indication_id);
    priv->loc_client = g_object_ref (client);
    priv->loc_location_nmea_indication_id =
        g_signal_connect (client,
                          "nmea",
                          G_CALLBACK (loc_location_nmea_indication_cb),
                          self);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_start_ready (QmiClientLoc *client,
                 GAsyncResult *res,
                 GTask        *task)
{
    QmiMessageLocRegisterEventsInput *input;
    QmiMessageLocStartOutput         *output;
    GError                           *error = NULL;

    output = qmi_client_loc_start_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_start_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't start GPS engine: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_start_output_unref (output);
        return;
    }

    qmi_message_loc_start_output_unref (output);

    input = qmi_message_loc_register_events_input_new ();
    qmi_message_loc_register_events_input_set_event_registration_mask (
        input, QMI_LOC_EVENT_REGISTRATION_FLAG_NMEA, NULL);
    qmi_client_loc_register_events (client,
                                    input,
                                    10,
                                    NULL,
                                    (GAsyncReadyCallback) loc_register_events_ready,
                                    task);
    qmi_message_loc_register_events_input_unref (input);
}

static void
start_gps_engine (MMSharedQmi         *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    QmiClient *client;
    GTask     *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        QmiMessagePdsSetGpsServiceStateInput *input;

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, TRUE, NULL);
        qmi_client_pds_set_gps_service_state (
            QMI_CLIENT_PDS (client),
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_gps_service_state_start_ready,
            task);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        QmiMessageLocStartInput *input;

        input = qmi_message_loc_start_input_new ();
        qmi_message_loc_start_input_set_session_id (input, DEFAULT_LOC_SESSION_ID, NULL);
        qmi_message_loc_start_input_set_intermediate_report_state (input, QMI_LOC_INTERMEDIATE_REPORT_STATE_DISABLE, NULL);
        qmi_message_loc_start_input_set_minimum_interval_between_position_reports (input, 5000, NULL);
        qmi_message_loc_start_input_set_fix_recurrence_type (input, QMI_LOC_FIX_RECURRENCE_TYPE_REQUEST_PERIODIC_FIXES, NULL);
        qmi_client_loc_start (QMI_CLIENT_LOC (client),
                              input,
                              10,
                              NULL,
                              (GAsyncReadyCallback) loc_start_ready,
                              task);
        qmi_message_loc_start_input_unref (input);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helper: select operation mode (assisted/standalone) */

typedef enum {
    GPS_OPERATION_MODE_UNKNOWN,
    GPS_OPERATION_MODE_STANDALONE,
    GPS_OPERATION_MODE_ASSISTED,
} GpsOperationMode;

typedef struct {
    QmiClient        *client;
    GpsOperationMode  mode;
    glong             indication_id;
    guint             timeout_id;
} SetGpsOperationModeContext;

static void
set_gps_operation_mode_context_free (SetGpsOperationModeContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove  (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_slice_free (SetGpsOperationModeContext, ctx);
}

static gboolean
set_gps_operation_mode_finish (MMSharedQmi   *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_set_default_tracking_session_ready (QmiClientPds *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    SetGpsOperationModeContext                   *ctx;
    QmiMessagePdsSetDefaultTrackingSessionOutput *output;
    GError                                       *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_pds_set_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_set_default_tracking_session_output_unref (output);
        return;
    }

    qmi_message_pds_set_default_tracking_session_output_unref (output);

    mm_dbg ("A-GPS %s", ctx->mode == GPS_OPERATION_MODE_ASSISTED ? "enabled" : "disabled");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
pds_get_default_tracking_session_ready (QmiClientPds *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    MMSharedQmi                                  *self;
    SetGpsOperationModeContext                   *ctx;
    QmiMessagePdsSetDefaultTrackingSessionInput  *input;
    QmiMessagePdsGetDefaultTrackingSessionOutput *output;
    GError                                       *error = NULL;
    QmiPdsOperatingMode                           session_operation;
    guint8                                        data_timeout;
    guint32                                       interval;
    guint32                                       accuracy_threshold;

    output = qmi_client_pds_get_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_get_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_get_default_tracking_session_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    qmi_message_pds_get_default_tracking_session_output_get_info (
        output,
        &session_operation,
        &data_timeout,
        &interval,
        &accuracy_threshold,
        NULL);

    qmi_message_pds_get_default_tracking_session_output_unref (output);

    if (ctx->mode == GPS_OPERATION_MODE_ASSISTED) {
        if (session_operation == QMI_PDS_OPERATING_MODE_MS_ASSISTED) {
            mm_dbg ("A-GPS already enabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_dbg ("Need to enable A-GPS");
        session_operation = QMI_PDS_OPERATING_MODE_MS_ASSISTED;
    } else if (ctx->mode == GPS_OPERATION_MODE_STANDALONE) {
        if (session_operation == QMI_PDS_OPERATING_MODE_STANDALONE) {
            mm_dbg ("A-GPS already disabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_dbg ("Need to disable A-GPS");
        session_operation = QMI_PDS_OPERATING_MODE_STANDALONE;
    } else
        g_assert_not_reached ();

    input = qmi_message_pds_set_default_tracking_session_input_new ();
    qmi_message_pds_set_default_tracking_session_input_set_info (
        input,
        session_operation,
        data_timeout,
        interval,
        accuracy_threshold,
        NULL);
    qmi_client_pds_set_default_tracking_session (
        client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_set_default_tracking_session_ready,
        task);
    qmi_message_pds_set_default_tracking_session_input_unref (input);
}

static gboolean
loc_location_operation_mode_indication_timed_out (GTask *task)
{
    SetSuplServerContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive operation mode indication");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_location_set_operation_mode_indication_cb (QmiClientLoc                           *client,
                                               QmiIndicationLocSetOperationModeOutput *output,
                                               GTask                                  *task)
{
    SetGpsOperationModeContext *ctx;
    QmiLocIndicationStatus      status;
    GError                     *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!qmi_indication_loc_set_operation_mode_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mm_error_from_qmi_loc_indication_status (status, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_dbg ("A-GPS %s", ctx->mode == GPS_OPERATION_MODE_ASSISTED ? "enabled" : "disabled");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_set_operation_mode_ready (QmiClientLoc *client,
                              GAsyncResult *res,
                              GTask        *task)
{
    SetGpsOperationModeContext          *ctx;
    QmiMessageLocSetOperationModeOutput *output;
    GError                              *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_set_operation_mode_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_set_operation_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_set_operation_mode_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "set-operation-mode",
                                           G_CALLBACK (loc_location_set_operation_mode_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_operation_mode_indication_timed_out,
                                             task);

    qmi_message_loc_set_operation_mode_output_unref (output);
}

static void
loc_location_get_operation_mode_indication_cb (QmiClientLoc                           *client,
                                               QmiIndicationLocGetOperationModeOutput *output,
                                               GTask                                  *task)
{
    SetGpsOperationModeContext         *ctx;
    QmiLocIndicationStatus              status;
    GError                             *error = NULL;
    QmiLocOperationMode                 mode = QMI_LOC_OPERATION_MODE_DEFAULT;
    QmiMessageLocSetOperationModeInput *input;

    ctx = g_task_get_task_data (task);

    if (!qmi_indication_loc_get_operation_mode_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mm_error_from_qmi_loc_indication_status (status, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_indication_loc_get_operation_mode_output_get_operation_mode (output, &mode, NULL);

    if (ctx->mode == GPS_OPERATION_MODE_ASSISTED) {
        if (mode == QMI_LOC_OPERATION_MODE_MSA) {
            mm_dbg ("A-GPS already enabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_dbg ("Need to enable A-GPS");
        mode = QMI_LOC_OPERATION_MODE_MSA;
    } else if (ctx->mode == GPS_OPERATION_MODE_STANDALONE) {
        if (mode == QMI_LOC_OPERATION_MODE_STANDALONE) {
            mm_dbg ("A-GPS already disabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_dbg ("Need to disable A-GPS");
        mode = QMI_LOC_OPERATION_MODE_STANDALONE;
    } else
        g_assert_not_reached ();

    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }

    if (ctx->indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        ctx->indication_id = 0;
    }

    input = qmi_message_loc_set_operation_mode_input_new ();
    qmi_message_loc_set_operation_mode_input_set_operation_mode (input, mode, NULL);
    qmi_client_loc_set_operation_mode (
        QMI_CLIENT_LOC (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)loc_set_operation_mode_ready,
        task);
    qmi_message_loc_set_operation_mode_input_unref (input);
}

static void
loc_get_operation_mode_ready (QmiClientLoc *client,
                              GAsyncResult *res,
                              GTask        *task)
{
    SetGpsOperationModeContext          *ctx;
    QmiMessageLocGetOperationModeOutput *output;
    GError                              *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_get_operation_mode_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_get_operation_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_get_operation_mode_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "get-operation-mode",
                                           G_CALLBACK (loc_location_get_operation_mode_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_operation_mode_indication_timed_out,
                                             task);

    qmi_message_loc_get_operation_mode_output_unref (output);
}

static void
set_gps_operation_mode (MMSharedQmi         *self,
                        GpsOperationMode     mode,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    SetGpsOperationModeContext *ctx;
    GTask                      *task;
    QmiClient                  *client;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (SetGpsOperationModeContext);
    ctx->mode = mode;
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_gps_operation_mode_context_free);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        qmi_client_pds_get_default_tracking_session (
            QMI_CLIENT_PDS (ctx->client),
            NULL,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_get_default_tracking_session_ready,
            task);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        qmi_client_loc_get_operation_mode (
            QMI_CLIENT_LOC (ctx->client),
            NULL,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)loc_get_operation_mode_ready,
            task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: disable */

gboolean
mm_shared_qmi_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
stop_gps_engine_ready (MMSharedQmi  *self,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!stop_gps_engine_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources &= ~source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_gps_operation_mode_standalone_ready (MMSharedQmi  *self,
                                         GAsyncResult *res,
                                         GTask        *task)
{
    GError  *error = NULL;
    Private *priv;

    if (!set_gps_operation_mode_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    priv = get_private (self);

    priv->enabled_sources &= ~MM_MODEM_LOCATION_SOURCE_AGPS;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_qmi_disable_location_gathering (MMIfaceModemLocation  *_self,
                                          MMModemLocationSource  source,
                                          GAsyncReadyCallback    callback,
                                          gpointer               user_data)
{
    MMSharedQmi            *self;
    Private                *priv;
    GTask                  *task;
    MMModemLocationSource   tmp;

    self = MM_SHARED_QMI (_self);
    priv = get_private (self);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    /* NOTE: no parent disable_location_gathering() implementation */

    if (!(source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                    MM_MODEM_LOCATION_SOURCE_AGPS))) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_assert (!(priv->pds_client && priv->loc_client));

    /* Disable A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS) {
        set_gps_operation_mode (self,
                                GPS_OPERATION_MODE_STANDALONE,
                                (GAsyncReadyCallback)set_gps_operation_mode_standalone_ready,
                                task);
        return;
    }

    /* If no more GPS sources enabled, stop GPS */
    tmp = priv->enabled_sources;
    tmp &= ~source;
    if (!(tmp & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
        stop_gps_engine (self,
                         (GAsyncReadyCallback)stop_gps_engine_ready,
                         task);
        return;
    }

    /* Otherwise, we have more GPS sources enabled, we shouldn't stop GPS, just
     * return */
    priv->enabled_sources &= ~source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: enable */

gboolean
mm_shared_qmi_enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
start_gps_engine_ready (MMSharedQmi  *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!start_gps_engine_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources |= source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_gps_operation_mode_assisted_ready (MMSharedQmi  *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    GError  *error = NULL;
    Private *priv;

    if (!set_gps_operation_mode_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    priv = get_private (self);

    priv->enabled_sources |= MM_MODEM_LOCATION_SOURCE_AGPS;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    MMSharedQmi           *self = MM_SHARED_QMI (_self);
    Private               *priv;
    MMModemLocationSource  source;
    GError                *error = NULL;

    priv = get_private (self);

    if (!priv->iface_modem_location_parent->enable_location_gathering_finish (_self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* We only consider GPS related sources in this shared QMI implementation */
    if (!(source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_AGPS))) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Enabling A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS) {
        set_gps_operation_mode (self,
                                GPS_OPERATION_MODE_ASSISTED,
                                (GAsyncReadyCallback)set_gps_operation_mode_assisted_ready,
                                task);
        return;
    }

    /* Only start GPS engine if not done already */
    if (!(priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
        start_gps_engine (self,
                          (GAsyncReadyCallback)start_gps_engine_ready,
                          task);
        return;
    }

    /* GPS already started, we're done */
    priv->enabled_sources |= source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_qmi_enable_location_gathering (MMIfaceModemLocation  *self,
                                         MMModemLocationSource  source,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering_finish);

    /* Chain up parent's gathering enable */
    priv->iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        task);
}

/*****************************************************************************/
/* Location: load capabilities */

MMModemLocationSource
mm_shared_qmi_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_SOURCE_NONE;
    }
    return (MMModemLocationSource)value;
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult         *res,
                                GTask                *task)
{
    MMModemLocationSource  sources;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_QMI (self));

    sources = priv->iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own checks */

    /* If we have support for the PDS client, GPS and A-GPS location is supported */
    if (mm_shared_qmi_peek_client (MM_SHARED_QMI (self), QMI_SERVICE_PDS, MM_PORT_QMI_FLAG_DEFAULT, NULL))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                    MM_MODEM_LOCATION_SOURCE_AGPS);

    /* If we have support for the LOC client, GPS location is supported */
    if (mm_shared_qmi_peek_client (MM_SHARED_QMI (self), QMI_SERVICE_LOC, MM_PORT_QMI_FLAG_DEFAULT, NULL))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                    MM_MODEM_LOCATION_SOURCE_AGPS);

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

void
mm_shared_qmi_location_load_capabilities (MMIfaceModemLocation *self,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->load_capabilities);
    g_assert (priv->iface_modem_location_parent->load_capabilities_finish);

    priv->iface_modem_location_parent->load_capabilities (self,
                                                          (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                          task);
}

/*****************************************************************************/

QmiClient *
mm_shared_qmi_peek_client (MMSharedQmi    *self,
                           QmiService      service,
                           MMPortQmiFlag   flag,
                           GError        **error)
{
    g_assert (MM_SHARED_QMI_GET_INTERFACE (self)->peek_client);
    return MM_SHARED_QMI_GET_INTERFACE (self)->peek_client (self, service, flag, error);
}

gboolean
mm_shared_qmi_ensure_client (MMSharedQmi          *self,
                             QmiService            service,
                             QmiClient           **o_client,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
    GError    *error = NULL;
    QmiClient *client;

    client = mm_shared_qmi_peek_client (self, service, MM_PORT_QMI_FLAG_DEFAULT, &error);
    if (!client) {
        g_task_report_error (self, callback, user_data, mm_shared_qmi_ensure_client, error);
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

static void
shared_qmi_init (gpointer g_iface)
{
}

GType
mm_shared_qmi_get_type (void)
{
    static GType shared_qmi_type = 0;

    if (!G_UNLIKELY (shared_qmi_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedQmi),  /* class_size */
            shared_qmi_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_qmi_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedQmi", &info, 0);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM_LOCATION);
    }

    return shared_qmi_type;
}
