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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer-ublox.h"
#include "mm-base-modem-at.h"
#include "mm-log-object.h"
#include "mm-ublox-enums-types.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-ublox.h"

G_DEFINE_TYPE (MMBroadbandBearerUblox, mm_broadband_bearer_ublox, MM_TYPE_BROADBAND_BEARER)

enum {
    PROP_0,
    PROP_USB_PROFILE,
    PROP_NETWORKING_MODE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandBearerUbloxPrivate {
    MMUbloxUsbProfile        profile;
    MMUbloxNetworkingMode    mode;
    MMUbloxBearerAllowedAuth allowed_auths;
    FeatureSupport           statistics;
    FeatureSupport           cedata;
};

/*****************************************************************************/
/* Common connection context and task */

typedef struct {
    MMBroadbandModem *modem;
    MMPortSerialAt   *primary;
    MMPort           *data;
    guint             cid;
    gboolean          auth_required;
    MMBearerIpConfig *ip_config; /* For IPv4 settings */
} CommonConnectContext;

static void
common_connect_context_free (CommonConnectContext *ctx)
{
    if (ctx->ip_config)
        g_object_unref (ctx->ip_config);
    if (ctx->data)
        g_object_unref (ctx->data);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->primary);
    g_slice_free (CommonConnectContext, ctx);
}

static GTask *
common_connect_task_new (MMBroadbandBearerUblox  *self,
                         MMBroadbandModem        *modem,
                         MMPortSerialAt          *primary,
                         guint                    cid,
                         MMPort                  *data,
                         GCancellable            *cancellable,
                         GAsyncReadyCallback      callback,
                         gpointer                 user_data)
{
    CommonConnectContext *ctx;
    GTask                *task;

    ctx = g_slice_new0 (CommonConnectContext);
    ctx->modem   = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid     = cid;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) common_connect_context_free);

    /* We need a net data port */
    if (data)
        ctx->data = g_object_ref (data);
    else {
        ctx->data = mm_base_modem_get_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
        if (!ctx->data) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_NOT_FOUND,
                                     "No valid data port found to launch connection");
            g_object_unref (task);
            return NULL;
        }
    }

    return task;
}

/*****************************************************************************/
/* 3GPP IP config (sub-step of the 3GPP Connection sequence) */

static gboolean
get_ip_config_3gpp_finish (MMBroadbandBearer *self,
                           GAsyncResult      *res,
                           MMBearerIpConfig **ipv4_config,
                           MMBearerIpConfig **ipv6_config,
                           GError           **error)
{
    MMBearerConnectResult *configs;
    MMBearerIpConfig *ipv4;

    configs = g_task_propagate_pointer (G_TASK (res), error);
    if (!configs)
        return FALSE;

    /* Just IPv4 for now */
    ipv4 = mm_bearer_connect_result_peek_ipv4_config (configs);
    g_assert (ipv4);
    if (ipv4_config)
        *ipv4_config = g_object_ref (ipv4);
    if (ipv6_config)
        *ipv6_config = NULL;
    mm_bearer_connect_result_unref (configs);
    return TRUE;
}

static void
complete_get_ip_config_3gpp (GTask *task)
{
    CommonConnectContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (mm_bearer_ip_config_get_method (ctx->ip_config) != MM_BEARER_IP_METHOD_UNKNOWN);
    g_task_return_pointer (task,
                           mm_bearer_connect_result_new (ctx->data, ctx->ip_config, NULL),
                           (GDestroyNotify) mm_bearer_connect_result_unref);
    g_object_unref (task);
}

static void
cgcontrdp_ready (MMBaseModem  *modem,
                 GAsyncResult *res,
                 GTask        *task)
{
    MMBroadbandBearerUblox *self;
    const gchar            *response;
    GError                 *error = NULL;
    CommonConnectContext   *ctx;
    gchar                  *local_address = NULL;
    gchar                  *subnet = NULL;
    gchar                  *dns_addresses[3] = { NULL, NULL, NULL };

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response || !mm_3gpp_parse_cgcontrdp_response (response,
                                                        NULL, /* cid */
                                                        NULL, /* bearer id */
                                                        NULL, /* apn */
                                                        &local_address,
                                                        &subnet,
                                                        NULL, /* gateway_address */
                                                        &dns_addresses[0],
                                                        &dns_addresses[1],
                                                        &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "IPv4 address retrieved: %s", local_address);
    mm_bearer_ip_config_set_address (ctx->ip_config, local_address);
    mm_obj_dbg (self, "IPv4 subnet retrieved: %s", subnet);
    mm_bearer_ip_config_set_prefix (ctx->ip_config, mm_netmask_to_cidr (subnet));
    if (dns_addresses[0])
        mm_obj_dbg (self, "primary DNS retrieved: %s", dns_addresses[0]);
    if (dns_addresses[1])
        mm_obj_dbg (self, "secondary DNS retrieved: %s", dns_addresses[1]);
    mm_bearer_ip_config_set_dns (ctx->ip_config, (const gchar **) dns_addresses);

    g_free (local_address);
    g_free (subnet);
    g_free (dns_addresses[0]);
    g_free (dns_addresses[1]);

    mm_obj_dbg (self, "finished IP settings retrieval for PDP context #%u...", ctx->cid);

    complete_get_ip_config_3gpp (task);
}

static void
uipaddr_ready (MMBaseModem  *modem,
               GAsyncResult *res,
               GTask        *task)
{
    MMBroadbandBearerUblox *self;
    const gchar            *response;
    gchar                  *cmd;
    GError                 *error = NULL;
    CommonConnectContext   *ctx;
    gchar                  *gw_ipv4_address = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response || !mm_ublox_parse_uipaddr_response (response,
                                                       NULL, /* cid */
                                                       NULL, /* if_name */
                                                       &gw_ipv4_address,
                                                       NULL, /* ipv4_subnet */
                                                       NULL, /* ipv6_global_address */
                                                       NULL, /* ipv6_link_local_address */
                                                       &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "IPv4 gateway address retrieved: %s", gw_ipv4_address);
    mm_bearer_ip_config_set_gateway (ctx->ip_config, gw_ipv4_address);
    g_free (gw_ipv4_address);

    cmd = g_strdup_printf ("+CGCONTRDP=%u", ctx->cid);
    mm_obj_dbg (self, "gathering IP and DNS information for PDP context #%u...", ctx->cid);
    mm_base_modem_at_command (MM_BASE_MODEM (modem),
                              cmd,
                              10,
                              FALSE,
                              (GAsyncReadyCallback) cgcontrdp_ready,
                              task);
    g_free (cmd);
}

static void
get_ip_config_3gpp (MMBroadbandBearer   *_self,
                    MMBroadbandModem    *modem,
                    MMPortSerialAt      *primary,
                    MMPortSerialAt      *secondary,
                    MMPort              *data,
                    guint                cid,
                    MMBearerIpFamily     ip_family,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    MMBroadbandBearerUblox *self = MM_BROADBAND_BEARER_UBLOX (_self);
    GTask                  *task;
    CommonConnectContext   *ctx;

    if (!(task = common_connect_task_new (MM_BROADBAND_BEARER_UBLOX (self),
                                          MM_BROADBAND_MODEM (modem),
                                          primary,
                                          cid,
                                          data,
                                          NULL,
                                          callback,
                                          user_data)))
        return;

    ctx = g_task_get_task_data (task);
    ctx->ip_config = mm_bearer_ip_config_new ();

    /* If we're in BRIDGE mode, we need to ask for static IP addressing details:
     *  - AT+UIPADDR=[CID] will give us the default gateway address.
     *  - +CGCONTRDP?[CID] will give us the IP address, subnet and DNS addresses.
     */
    if (self->priv->mode == MM_UBLOX_NETWORKING_MODE_BRIDGE) {
        gchar *cmd;

        mm_bearer_ip_config_set_method (ctx->ip_config, MM_BEARER_IP_METHOD_STATIC);

        cmd = g_strdup_printf ("+UIPADDR=%u", cid);
        mm_obj_dbg (self, "gathering gateway information for PDP context #%u...", cid);
        mm_base_modem_at_command (MM_BASE_MODEM (modem),
                                  cmd,
                                  10,
                                  FALSE,
                                  (GAsyncReadyCallback) uipaddr_ready,
                                  task);
        g_free (cmd);
        return;
    }

    /* If we're in ROUTER networking mode, we just need to request DHCP on the
     * network interface. Early return with that result. */
    if (self->priv->mode == MM_UBLOX_NETWORKING_MODE_ROUTER) {
        mm_bearer_ip_config_set_method (ctx->ip_config, MM_BEARER_IP_METHOD_DHCP);
        complete_get_ip_config_3gpp (task);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

static MMPort *
dial_3gpp_finish (MMBroadbandBearer  *self,
                  GAsyncResult       *res,
                  GError           **error)
{
    return MM_PORT (g_task_propagate_pointer (G_TASK (res), error));
}

static void
cedata_activate_ready (MMBaseModem            *modem,
                       GAsyncResult           *res,
                       MMBroadbandBearerUblox *self)
{
    const gchar *response;
    GError      *error = NULL;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        mm_obj_warn (self, "ECM data connection attempt failed: %s", error->message);
        mm_base_bearer_report_connection_status (MM_BASE_BEARER (self),
                                                 MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
        g_error_free (error);
    }
    /* we received a full bearer object reference */
    g_object_unref (self);
}

static void
cgact_activate_ready (MMBaseModem  *modem,
                      GAsyncResult *res,
                      GTask        *task)
{
    const gchar          *response;
    GError               *error = NULL;
    CommonConnectContext *ctx;

    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, g_object_ref (ctx->data), g_object_unref);
    g_object_unref (task);
}

static void
activate_3gpp (GTask *task)
{
    MMBroadbandBearerUblox *self;
    CommonConnectContext   *ctx;
    g_autofree gchar       *cmd = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* SARA-U2xx and LISA-U20x only expose one CDC-ECM interface. Hence,
     * the fixed 0 as the interface index here. When we see modems with
     * multiple interfaces, this needs to be revisited. */
    if (self->priv->profile == MM_UBLOX_USB_PROFILE_ECM && self->priv->cedata == FEATURE_SUPPORTED) {
        cmd = g_strdup_printf ("+UCEDATA=%u,0", ctx->cid);
        mm_obj_dbg (self, "establishing ECM data connection for PDP context #%u...", ctx->cid);
        mm_base_modem_at_command (MM_BASE_MODEM (ctx->modem),
                                  cmd,
                                  MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                                  FALSE,
                                  (GAsyncReadyCallback) cedata_activate_ready,
                                  g_object_ref (self));

        /* We'll mark the task done here since the modem expects the DHCP
           discover packet while +UCEDATA runs. If the command fails, we'll
           mark the bearer disconnected later in the callback. */
        g_task_return_pointer (task, g_object_ref (ctx->data), g_object_unref);
        g_object_unref (task);
        return;
   }

    cmd = g_strdup_printf ("+CGACT=1,%u", ctx->cid);
    mm_obj_dbg (self, "activating PDP context #%u...", ctx->cid);
    mm_base_modem_at_command (MM_BASE_MODEM (ctx->modem),
                              cmd,
                              MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                              FALSE,
                              (GAsyncReadyCallback) cgact_activate_ready,
                              task);
}

static void
test_cedata_ready (MMBaseModem  *modem,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMBroadbandBearerUblox *self;
    const gchar            *response;

    self = g_task_get_source_object (task);

    response = mm_base_modem_at_command_finish (modem, res, NULL);
    if (response)
        self->priv->cedata = FEATURE_SUPPORTED;
    else
        self->priv->cedata = FEATURE_UNSUPPORTED;
    mm_obj_dbg (self, "+UCEDATA command%s available",
                (self->priv->cedata == FEATURE_SUPPORTED) ? "" : " not");

    activate_3gpp (task);
}

static void
test_cedata (GTask *task)
{
    MMBroadbandBearerUblox *self;
    CommonConnectContext   *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* We don't need to test for +UCEDATA if we're not using CDC-ECM or if we
       have tested before. Instead, we jump right to the activation. */
    if (self->priv->profile != MM_UBLOX_USB_PROFILE_ECM || self->priv->cedata != FEATURE_SUPPORT_UNKNOWN) {
        activate_3gpp (task);
        return;
    }

    mm_obj_dbg (self, "checking availability of +UCEDATA command...");
    mm_base_modem_at_command (MM_BASE_MODEM (ctx->modem),
                              "+UCEDATA=?",
                              3,
                              TRUE, /* allow_cached */
                              (GAsyncReadyCallback) test_cedata_ready,
                              task);
}

static void
uauthreq_ready (MMBaseModem  *modem,
                GAsyncResult *res,
                GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        CommonConnectContext *ctx;

        ctx = g_task_get_task_data (task);
        /* If authentication required and the +UAUTHREQ failed, abort */
        if (ctx->auth_required) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        /* Otherwise, ignore */
        g_error_free (error);
    }

    test_cedata (task);
}

static void
authenticate_3gpp (GTask *task)
{
    MMBroadbandBearerUblox *self;
    CommonConnectContext   *ctx;
    g_autofree gchar       *cmd = NULL;
    MMBearerAllowedAuth     allowed_auth;
    gint                    ublox_auth = -1;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    allowed_auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

    if (!ctx->auth_required) {
        mm_obj_dbg (self, "not using authentication");
        ublox_auth = 0;
        goto out;
    }

    if (allowed_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN || allowed_auth == (MM_BEARER_ALLOWED_AUTH_PAP | MM_BEARER_ALLOWED_AUTH_CHAP)) {
        mm_obj_dbg (self, "using automatic authentication method");
        if (self->priv->allowed_auths & MM_UBLOX_BEARER_ALLOWED_AUTH_AUTO)
            ublox_auth = 3;
        else if (self->priv->allowed_auths & MM_UBLOX_BEARER_ALLOWED_AUTH_CHAP)
            ublox_auth = 2;
        else if (self->priv->allowed_auths & MM_UBLOX_BEARER_ALLOWED_AUTH_PAP)
            ublox_auth = 1;
        else if (self->priv->allowed_auths & MM_UBLOX_BEARER_ALLOWED_AUTH_NONE)
            ublox_auth = 0;
    } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_PAP) {
        mm_obj_dbg (self, "using PAP authentication method");
        ublox_auth = 1;
    } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_CHAP) {
        mm_obj_dbg (self, "using CHAP authentication method");
        ublox_auth = 2;
    }

out:

    if (ublox_auth < 0) {
        g_autofree gchar *str = NULL;

        str = mm_bearer_allowed_auth_build_string_from_mask (allowed_auth);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Cannot use any of the specified authentication methods (%s)", str);
        g_object_unref (task);
        return;
    }

    if (ublox_auth > 0) {
        const gchar      *user;
        const gchar      *password;
        g_autofree gchar *quoted_user = NULL;
        g_autofree gchar *quoted_password = NULL;

        user     = mm_bearer_properties_get_user     (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
        password = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

        quoted_user     = mm_port_serial_at_quote_string (user);
        quoted_password = mm_port_serial_at_quote_string (password);

        cmd = g_strdup_printf ("+UAUTHREQ=%u,%u,%s,%s",
                               ctx->cid,
                               ublox_auth,
                               quoted_user,
                               quoted_password);
    } else
        cmd = g_strdup_printf ("+UAUTHREQ=%u,0,\"\",\"\"", ctx->cid);

    mm_obj_dbg (self, "setting up authentication preferences in PDP context #%u...", ctx->cid);
    mm_base_modem_at_command (MM_BASE_MODEM (ctx->modem),
                              cmd,
                              10,
                              FALSE,
                              (GAsyncReadyCallback) uauthreq_ready,
                              task);
}

static void
uauthreq_test_ready (MMBaseModem  *modem,
                     GAsyncResult *res,
                     GTask        *task)
{
    MMBroadbandBearerUblox *self;
    const gchar            *response;
    GError                 *error = NULL;

    self = g_task_get_source_object (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response)
        goto out;

    self->priv->allowed_auths = mm_ublox_parse_uauthreq_test (response, self, &error);
out:
    if (error) {
        CommonConnectContext *ctx;

        ctx = g_task_get_task_data (task);
        /* If authentication required and the +UAUTHREQ test failed, abort */
        if (ctx->auth_required) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        /* Otherwise, ignore and jump to test_cedata directly as no auth setup
         * is needed */
        g_error_free (error);
        test_cedata (task);
        return;
    }

    authenticate_3gpp (task);
}

static void
check_supported_authentication_methods (GTask *task)
{
    MMBroadbandBearerUblox *self;
    CommonConnectContext   *ctx;
    const gchar            *user;
    const gchar            *password;
    MMBearerAllowedAuth     allowed_auth;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    user         = mm_bearer_properties_get_user         (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    password     = mm_bearer_properties_get_password     (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    allowed_auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

    /* Flag whether authentication is required. If it isn't, we won't fail
     * connection attempt if the +UAUTHREQ command fails */
    ctx->auth_required = (user && password && allowed_auth != MM_BEARER_ALLOWED_AUTH_NONE);

    /* If we already cached the support, not do it again */
    if (self->priv->allowed_auths != MM_UBLOX_BEARER_ALLOWED_AUTH_UNKNOWN) {
        authenticate_3gpp (task);
        return;
    }

    mm_obj_dbg (self, "checking supported authentication methods...");
    mm_base_modem_at_command (MM_BASE_MODEM (ctx->modem),
                              "+UAUTHREQ=?",
                              10,
                              TRUE, /* allow cached */
                              (GAsyncReadyCallback) uauthreq_test_ready,
                              task);
}

static void
dial_3gpp (MMBroadbandBearer   *self,
           MMBaseModem         *modem,
           MMPortSerialAt      *primary,
           guint                cid,
           GCancellable        *cancellable,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    GTask *task;

    if (!(task = common_connect_task_new (MM_BROADBAND_BEARER_UBLOX (self),
                                          MM_BROADBAND_MODEM (modem),
                                          primary,
                                          cid,
                                          NULL, /* data, unused */
                                          cancellable,
                                          callback,
                                          user_data)))
        return;

    check_supported_authentication_methods (task);
}

/*****************************************************************************/
/* 3GPP disconnection */

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer  *self,
                        GAsyncResult       *res,
                        GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
cgact_deactivate_ready (MMBaseModem  *modem,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMBroadbandBearerUblox *self;
    const gchar            *response;
    GError                 *error = NULL;

    self = g_task_get_source_object (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        /* TOBY-L4 and TOBY-L2 L2 don't allow to disconnect the last LTE bearer
         * as that would imply de-registration from the LTE network, so we just
         * assume that it's disconnected from the user point of view.
         *
         * TOBY-L4 reports this as a generic unknown Packet Domain Error, which
         * is a bit unfortunate:
         *    AT+CGACT=0,1
         *    +CME ERROR: 148
         *
         * TOBY-L2 reports this as "LAST PDN disconnection not allowed" but using
         * the legacy numeric value before 3GPP Rel 11 (i.e. 151 instead of 171).
         *   AT+CGACT=0,1
         *    +CME ERROR: 151
         */
        if (!g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN) &&
            !g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED) &&
            !g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED_LEGACY)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        mm_obj_dbg (self, "ignored error when disconnecting last LTE bearer: %s", error->message);
        g_clear_error (&error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disconnect_3gpp  (MMBroadbandBearer   *self,
                  MMBroadbandModem    *modem,
                  MMPortSerialAt      *primary,
                  MMPortSerialAt      *secondary,
                  MMPort              *data,
                  guint                cid,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    GTask            *task;
    g_autofree gchar *cmd = NULL;

    if (!(task = common_connect_task_new (MM_BROADBAND_BEARER_UBLOX (self),
                                          MM_BROADBAND_MODEM (modem),
                                          primary,
                                          cid,
                                          data,
                                          NULL,
                                          callback,
                                          user_data)))
        return;

    cmd = g_strdup_printf ("+CGACT=0,%u", cid);
    mm_obj_dbg (self, "deactivating PDP context #%u...", cid);
    mm_base_modem_at_command (MM_BASE_MODEM (modem),
                              cmd,
                              MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                              FALSE,
                              (GAsyncReadyCallback) cgact_deactivate_ready,
                              task);
}

/*****************************************************************************/
/* Reload statistics */

typedef struct {
    guint64 bytes_rx;
    guint64 bytes_tx;
} StatsResult;

static gboolean
reload_stats_finish (MMBaseBearer  *self,
                     guint64       *bytes_rx,
                     guint64       *bytes_tx,
                     GAsyncResult  *res,
                     GError       **error)
{
    StatsResult *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    if (bytes_rx)
        *bytes_rx = result->bytes_rx;
    if (bytes_tx)
        *bytes_tx = result->bytes_tx;
    g_free (result);
    return TRUE;
}

static void
ugcntrd_ready (MMBaseModem  *modem,
               GAsyncResult *res,
               GTask        *task)
{
    MMBroadbandBearerUblox *self;
    const gchar            *response;
    GError                 *error = NULL;
    guint64                 tx_bytes = 0;
    guint64                 rx_bytes = 0;
    guint                   cid;

    self = MM_BROADBAND_BEARER_UBLOX (g_task_get_source_object (task));

    cid = mm_broadband_bearer_get_3gpp_cid (MM_BROADBAND_BEARER (self));

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (response)
        mm_ublox_parse_ugcntrd_response_for_cid (response,
                                                 cid,
                                                 &tx_bytes, &rx_bytes,
                                                 NULL, NULL,
                                                 &error);

    if (error) {
        g_prefix_error (&error, "Couldn't load PDP context %u statistics: ", cid);
        g_task_return_error (task, error);
    } else {
        StatsResult *result;

        result = g_new (StatsResult, 1);
        result->bytes_rx = rx_bytes;
        result->bytes_tx = tx_bytes;
        g_task_return_pointer (task, result, g_free);
    }
    g_object_unref (task);
}

static void
run_reload_stats (MMBroadbandBearerUblox *self,
                  GTask                  *task)
{
    /* Unsupported? */
    if (self->priv->statistics == FEATURE_UNSUPPORTED) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Loading statistics isn't supported by this device");
        g_object_unref (task);
        return;
    }

    /* Supported */
    if (self->priv->statistics == FEATURE_SUPPORTED) {
        MMBaseModem *modem = NULL;

        g_object_get (MM_BASE_BEARER (self),
                      MM_BASE_BEARER_MODEM, &modem,
                      NULL);
        mm_base_modem_at_command (MM_BASE_MODEM (modem),
                                  "+UGCNTRD",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback) ugcntrd_ready,
                                  task);
        g_object_unref (modem);
        return;
    }

    g_assert_not_reached ();
}

static void
ugcntrd_test_ready (MMBaseModem  *modem,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMBroadbandBearerUblox *self;

    self = MM_BROADBAND_BEARER_UBLOX (g_task_get_source_object (task));

    if (!mm_base_modem_at_command_finish (modem, res, NULL))
        self->priv->statistics = FEATURE_UNSUPPORTED;
    else
        self->priv->statistics = FEATURE_SUPPORTED;

    run_reload_stats (self, task);
}

static void
reload_stats (MMBaseBearer        *self,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (MM_BROADBAND_BEARER_UBLOX (self)->priv->statistics == FEATURE_SUPPORT_UNKNOWN) {
        MMBaseModem *modem = NULL;

        g_object_get (MM_BASE_BEARER (self),
                      MM_BASE_BEARER_MODEM, &modem,
                      NULL);

        mm_base_modem_at_command (MM_BASE_MODEM (modem),
                                  "+UGCNTRD=?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback) ugcntrd_test_ready,
                                  task);
        g_object_unref (modem);
        return;
    }

    run_reload_stats (MM_BROADBAND_BEARER_UBLOX (self), task);
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_ublox_new_finish (GAsyncResult  *res,
                                      GError       **error)
{
    GObject *source;
    GObject *bearer;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

void
mm_broadband_bearer_ublox_new (MMBroadbandModem      *modem,
                               MMUbloxUsbProfile      profile,
                               MMUbloxNetworkingMode  mode,
                               MMBearerProperties    *config,
                               GCancellable          *cancellable,
                               GAsyncReadyCallback    callback,
                               gpointer               user_data)
{
    g_assert (mode == MM_UBLOX_NETWORKING_MODE_ROUTER || mode == MM_UBLOX_NETWORKING_MODE_BRIDGE);

    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_UBLOX,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        MM_BROADBAND_BEARER_UBLOX_USB_PROFILE, profile,
        MM_BROADBAND_BEARER_UBLOX_NETWORKING_MODE, mode,
        NULL);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMBroadbandBearerUblox *self = MM_BROADBAND_BEARER_UBLOX (object);

    switch (prop_id) {
    case PROP_USB_PROFILE:
        self->priv->profile = g_value_get_enum (value);
        break;
    case PROP_NETWORKING_MODE:
        self->priv->mode = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMBroadbandBearerUblox *self = MM_BROADBAND_BEARER_UBLOX (object);

    switch (prop_id) {
    case PROP_USB_PROFILE:
        g_value_set_enum (value, self->priv->profile);
        break;
    case PROP_NETWORKING_MODE:
        g_value_set_enum (value, self->priv->mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_bearer_ublox_init (MMBroadbandBearerUblox *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER_UBLOX,
                                              MMBroadbandBearerUbloxPrivate);

    /* Defaults */
    self->priv->profile       = MM_UBLOX_USB_PROFILE_UNKNOWN;
    self->priv->mode          = MM_UBLOX_NETWORKING_MODE_UNKNOWN;
    self->priv->allowed_auths = MM_UBLOX_BEARER_ALLOWED_AUTH_UNKNOWN;
    self->priv->statistics    = FEATURE_SUPPORT_UNKNOWN;
    self->priv->cedata        = FEATURE_SUPPORT_UNKNOWN;
}

static void
mm_broadband_bearer_ublox_class_init (MMBroadbandBearerUbloxClass *klass)
{
    GObjectClass           *object_class           = G_OBJECT_CLASS (klass);
    MMBaseBearerClass      *base_bearer_class      = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerUbloxPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    /* Note: the ublox plugin uses the generic AT+CGACT? based check to monitor
     * the connection status (i.e. default load_connection_status()) */
    base_bearer_class->reload_stats = reload_stats;
    base_bearer_class->reload_stats_finish = reload_stats_finish;

    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
    broadband_bearer_class->get_ip_config_3gpp = get_ip_config_3gpp;
    broadband_bearer_class->get_ip_config_3gpp_finish = get_ip_config_3gpp_finish;

    properties[PROP_USB_PROFILE] =
        g_param_spec_enum (MM_BROADBAND_BEARER_UBLOX_USB_PROFILE,
                           "USB profile",
                           "USB profile in use",
                           MM_TYPE_UBLOX_USB_PROFILE,
                           MM_UBLOX_USB_PROFILE_UNKNOWN,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_USB_PROFILE, properties[PROP_USB_PROFILE]);

    properties[PROP_NETWORKING_MODE] =
        g_param_spec_enum (MM_BROADBAND_BEARER_UBLOX_NETWORKING_MODE,
                           "Networking mode",
                           "Networking mode in use",
                           MM_TYPE_UBLOX_NETWORKING_MODE,
                           MM_UBLOX_NETWORKING_MODE_UNKNOWN,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_NETWORKING_MODE, properties[PROP_NETWORKING_MODE]);
}
