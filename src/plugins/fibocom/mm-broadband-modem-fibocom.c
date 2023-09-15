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
 * Copyright (C) 2022 Disruptive Technologies Research AS
 */

#include <config.h>

#include "mm-broadband-modem-fibocom.h"
#include "mm-broadband-bearer-fibocom-ecm.h"
#include "mm-broadband-modem.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-log.h"

static void iface_modem_init                      (MMIfaceModem                   *iface);
static void iface_modem_3gpp_init                 (MMIfaceModem3gpp               *iface);
static void iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface);

static MMIfaceModem3gppProfileManager *iface_modem_3gpp_profile_manager_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemFibocom, mm_broadband_modem_fibocom, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, iface_modem_3gpp_profile_manager_init))

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED,
} FeatureSupport;

struct _MMBroadbandModemFibocomPrivate {
    FeatureSupport  gtrndis_support;
    GRegex         *sim_ready_regex;
    FeatureSupport  initial_eps_bearer_support;
    gint            initial_eps_bearer_cid;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_fibocom_ecm_new_ready (GObject *source,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError       *error = NULL;

    bearer = mm_broadband_bearer_fibocom_ecm_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError       *error = NULL;

    bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

static void
common_create_bearer (GTask *task)
{
    MMBroadbandModemFibocom *self;

    self = g_task_get_source_object (task);

    switch (self->priv->gtrndis_support) {
    case FEATURE_SUPPORTED:
        mm_obj_dbg (self, "+GTRNDIS supported, creating Fibocom ECM bearer");
        mm_broadband_bearer_fibocom_ecm_new (self,
                                             g_task_get_task_data (task),
                                             NULL, /* cancellable */
                                             (GAsyncReadyCallback) broadband_bearer_fibocom_ecm_new_ready,
                                             task);
        return;
    case FEATURE_NOT_SUPPORTED:
        mm_obj_dbg (self, "+GTRNDIS not supported, creating generic PPP bearer");
        mm_broadband_bearer_new (MM_BROADBAND_MODEM (self),
                                 g_task_get_task_data (task),
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback) broadband_bearer_new_ready,
                                 task);
        return;
    case FEATURE_SUPPORT_UNKNOWN:
    default:
        g_assert_not_reached ();
    }
}

static void
gtrndis_test_ready (MMBaseModem  *_self,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);

    if (!mm_base_modem_at_command_finish (_self, res, NULL)) {
        mm_obj_dbg (self, "+GTRNDIS unsupported");
        self->priv->gtrndis_support = FEATURE_NOT_SUPPORTED;
    } else {
        mm_obj_dbg (self, "+GTRNDIS supported");
        self->priv->gtrndis_support = FEATURE_SUPPORTED;
    }

    /* Go on and create the bearer */
    common_create_bearer (task);
}

static void
modem_create_bearer (MMIfaceModem       *_self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer            user_data)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);
    GTask                   *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, g_object_ref (properties), g_object_unref);

    if (self->priv->gtrndis_support != FEATURE_SUPPORT_UNKNOWN) {
        common_create_bearer (task);
        return;
    }

    if (!mm_base_modem_peek_best_data_port (MM_BASE_MODEM (self), MM_PORT_TYPE_NET)) {
        mm_obj_dbg (self, "skipping +GTRNDIS check as no data port is available");
        self->priv->gtrndis_support = FEATURE_NOT_SUPPORTED;
        common_create_bearer (task);
        return;
    }

    mm_obj_dbg (self, "checking +GTRNDIS support...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+GTRNDIS=?",
                              6, /* timeout [s] */
                              TRUE, /* allow_cached */
                              (GAsyncReadyCallback) gtrndis_test_ready,
                              task);
}

/*****************************************************************************/
/* Reset / Power (Modem interface)                                           */

static gboolean
modem_common_power_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_reset (MMIfaceModem *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=15",
                              15,
                              FALSE,
                              callback,
                              user_data);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              15,
                              FALSE,
                              callback,
                              user_data);
}

static void
modem_power_off (MMIfaceModem *self,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPWROFF",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load initial EPS bearer properties (as agreed with network)               */

static MMBearerProperties *
modem_3gpp_load_initial_eps_bearer_finish (MMIfaceModem3gpp  *self,
                                           GAsyncResult      *res,
                                           GError           **error)
{
    return MM_BEARER_PROPERTIES (g_task_propagate_pointer (G_TASK (res), error));
}

static void
load_initial_eps_cgcontrdp_ready (MMBaseModem  *self,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    GError                  *error = NULL;
    const gchar             *response;
    g_autofree gchar        *apn = NULL;
    MMBearerProperties      *properties;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response || !mm_3gpp_parse_cgcontrdp_response (response, NULL, NULL, &apn, NULL, NULL, NULL, NULL, NULL, &error))
        g_task_return_error (task, error);
    else {
        properties = mm_bearer_properties_new ();
        mm_bearer_properties_set_apn (properties, apn);
        g_task_return_pointer (task, properties, g_object_unref);
    }

    g_object_unref (task);
}

static void
modem_3gpp_load_initial_eps_bearer (MMIfaceModem3gpp    *_self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);
    GTask                   *task;
    g_autofree gchar        *cmd = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->initial_eps_bearer_support != FEATURE_SUPPORTED) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Initial EPS bearer context ID unknown");
        g_object_unref (task);
        return;
    }

    g_assert (self->priv->initial_eps_bearer_cid >= 0);
    cmd = g_strdup_printf ("+CGCONTRDP=%d", self->priv->initial_eps_bearer_cid);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback) load_initial_eps_cgcontrdp_ready,
                              task);
}

/*****************************************************************************/
/* Load initial EPS bearer settings (currently configured in modem)          */

static MMBearerProperties *
modem_3gpp_load_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                    GAsyncResult      *res,
                                                    GError           **error)
{
    return MM_BEARER_PROPERTIES (g_task_propagate_pointer (G_TASK (res), error));
}

static void
load_initial_eps_bearer_get_profile_ready (MMIfaceModem3gppProfileManager *self,
                                           GAsyncResult                   *res,
                                           GTask                          *task)
{
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile = NULL;
    MMBearerProperties       *properties;

    profile = mm_iface_modem_3gpp_profile_manager_get_profile_finish (self, res, &error);
    if (!profile) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    properties = mm_bearer_properties_new_from_profile (profile, &error);
    if (!properties)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, properties, g_object_unref);
    g_object_unref (task);
}

static void
modem_3gpp_load_initial_eps_bearer_settings (MMIfaceModem3gpp    *_self,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);
    MMPortSerialAt          *port;
    MMKernelDevice          *device;
    GTask                   *task;

    /* Initial EPS bearer CID initialization run once only */
    if (G_UNLIKELY (self->priv->initial_eps_bearer_support == FEATURE_SUPPORT_UNKNOWN)) {
        /* There doesn't seem to be a programmatic way to find the initial EPS
         * bearer's CID, so we'll use a udev variable. */
        port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
        device = mm_port_peek_kernel_device (MM_PORT (port));
        if (mm_kernel_device_has_global_property (device, "ID_MM_FIBOCOM_INITIAL_EPS_CID")) {
            self->priv->initial_eps_bearer_support = FEATURE_SUPPORTED;
            self->priv->initial_eps_bearer_cid = mm_kernel_device_get_global_property_as_int (
                device, "ID_MM_FIBOCOM_INITIAL_EPS_CID");
        }
        else
            self->priv->initial_eps_bearer_support = FEATURE_NOT_SUPPORTED;
    }

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->initial_eps_bearer_support != FEATURE_SUPPORTED) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Initial EPS bearer context ID unknown");
        g_object_unref (task);
        return;
    }

    g_assert (self->priv->initial_eps_bearer_cid >= 0);
    mm_iface_modem_3gpp_profile_manager_get_profile (
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
        self->priv->initial_eps_bearer_cid,
        (GAsyncReadyCallback) load_initial_eps_bearer_get_profile_ready,
        task);
}

/*****************************************************************************/
/* Set initial EPS bearer settings                                           */

typedef enum {
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LOAD_POWER_STATE = 0,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_DOWN,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_MODIFY_PROFILE,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_UP,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FINISH,
} SetInitialEpsStep;

typedef struct {
    MM3gppProfile     *profile;
    SetInitialEpsStep  step;
    MMModemPowerState  power_state;
} SetInitialEpsContext;

static void
set_initial_eps_context_free (SetInitialEpsContext *ctx)
{
    g_object_unref (ctx->profile);
    g_slice_free (SetInitialEpsContext, ctx);
}

static gboolean
modem_3gpp_set_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                   GAsyncResult      *res,
                                                   GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_initial_eps_step (GTask *task);

static void
set_initial_eps_bearer_power_up_ready (MMBaseModem  *_self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);
    SetInitialEpsContext    *ctx;
    GError                  *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up_finish (MM_IFACE_MODEM (self), res, &error)) {
        g_prefix_error (&error, "Couldn't power up modem: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_step (task);
}

static void
set_initial_eps_bearer_modify_profile_ready (MMIfaceModem3gppProfileManager *self,
                                             GAsyncResult                   *res,
                                             GTask                          *task)
{
    GError                   *error = NULL;
    SetInitialEpsContext     *ctx;
    g_autoptr(MM3gppProfile)  stored = NULL;

    ctx = g_task_get_task_data (task);

    stored = mm_iface_modem_3gpp_profile_manager_set_profile_finish (self, res, &error);
    if (!stored) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_step (task);
}

static void
set_initial_eps_bearer_power_down_ready (MMBaseModem  *self,
                                         GAsyncResult *res,
                                         GTask        *task)
{
    SetInitialEpsContext *ctx;
    GError               *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down_finish (MM_IFACE_MODEM (self), res, &error)) {
        g_prefix_error (&error, "Couldn't power down modem: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_step (task);
}

static void
set_initial_eps_bearer_load_power_state_ready (MMBaseModem  *self,
                                               GAsyncResult *res,
                                               GTask        *task)
{
    SetInitialEpsContext *ctx;
    GError               *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->power_state = MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state_finish (MM_IFACE_MODEM (self), res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_step (task);
}

static void
set_initial_eps_step (GTask *task)
{
    MMBroadbandModemFibocom *self;
    SetInitialEpsContext    *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LOAD_POWER_STATE:
        mm_obj_dbg (self, "querying current power state...");
        g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state);
        g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state_finish);
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state (
            MM_IFACE_MODEM (self),
            (GAsyncReadyCallback) set_initial_eps_bearer_load_power_state_ready,
            task);
        return;

    case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_DOWN:
        if (ctx->power_state == MM_MODEM_POWER_STATE_ON) {
            mm_obj_dbg (self, "powering down before changing initial EPS bearer settings...");
            g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down);
            g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down_finish);
            MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down (
                MM_IFACE_MODEM (self),
                (GAsyncReadyCallback) set_initial_eps_bearer_power_down_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_MODIFY_PROFILE:
        mm_obj_dbg (self, "modifying initial EPS bearer settings profile...");
        mm_iface_modem_3gpp_profile_manager_set_profile (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
                                                         ctx->profile,
                                                         "profile-id",
                                                         TRUE,
                                                         (GAsyncReadyCallback) set_initial_eps_bearer_modify_profile_ready,
                                                         task);
        return;

    case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_UP:
        if (ctx->power_state == MM_MODEM_POWER_STATE_ON) {
            mm_obj_dbg (self, "powering up after changing initial EPS bearer settings...");
            g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up);
            g_assert (MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up_finish);
            MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up (
                MM_IFACE_MODEM (self),
                (GAsyncReadyCallback) set_initial_eps_bearer_power_up_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FINISH:
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
modem_3gpp_set_initial_eps_bearer_settings (MMIfaceModem3gpp    *_self,
                                            MMBearerProperties  *properties,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);
    GTask                   *task;
    MM3gppProfile           *profile;
    MMBearerIpFamily         ip_family;
    SetInitialEpsContext    *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->initial_eps_bearer_support != FEATURE_SUPPORTED) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Initial EPS bearer context ID unknown");
        g_object_unref (task);
        return;
    }

    profile = mm_bearer_properties_peek_3gpp_profile (properties);
    g_assert (self->priv->initial_eps_bearer_cid >= 0);
    mm_3gpp_profile_set_profile_id (profile, self->priv->initial_eps_bearer_cid);
    ip_family = mm_3gpp_profile_get_ip_type (profile);
    if (ip_family == MM_BEARER_IP_FAMILY_NONE || ip_family == MM_BEARER_IP_FAMILY_ANY)
        mm_3gpp_profile_set_ip_type (profile, MM_BEARER_IP_FAMILY_IPV4);

    /* Setup context */
    ctx = g_slice_new0 (SetInitialEpsContext);
    ctx->profile = g_object_ref (profile);
    ctx->step = SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LOAD_POWER_STATE;
    g_task_set_task_data (task, ctx, (GDestroyNotify) set_initial_eps_context_free);

    set_initial_eps_step (task);
}

/*****************************************************************************/
/* Deactivate profile (3GPP profile management interface) */

static gboolean
modem_3gpp_profile_manager_deactivate_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                                      GAsyncResult                    *res,
                                                      GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
profile_manager_parent_deactivate_profile_ready (MMIfaceModem3gppProfileManager *self,
                                                 GAsyncResult                   *res,
                                                 GTask                          *task)
{
    GError *error = NULL;
    if (iface_modem_3gpp_profile_manager_parent->deactivate_profile_finish(self, res, &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_deactivate_profile (MMIfaceModem3gppProfileManager *_self,
                                               MM3gppProfile                  *profile,
                                               GAsyncReadyCallback             callback,
                                               gpointer                        user_data)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (_self);
    GTask                   *task;
    gint                     profile_id;

    task = g_task_new (self, NULL, callback, user_data);
    profile_id = mm_3gpp_profile_get_profile_id (profile);

    if (self->priv->initial_eps_bearer_support == FEATURE_SUPPORTED) {
        g_assert (self->priv->initial_eps_bearer_cid >= 0);
        if (self->priv->initial_eps_bearer_cid == profile_id) {
            mm_obj_dbg (self, "skipping profile deactivation (initial EPS bearer)");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
    }

    iface_modem_3gpp_profile_manager_parent->deactivate_profile (
        _self,
        profile,
        (GAsyncReadyCallback) profile_manager_parent_deactivate_profile_ready,
        task);
}

/*****************************************************************************/

static void
setup_ports (MMBroadbandModem *_self)
{
    MMBroadbandModemFibocom *self = (MM_BROADBAND_MODEM_FIBOCOM (_self));
    MMPortSerialAt          *ports[2];
    guint                    i;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_fibocom_parent_class)->setup_ports (_self);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->sim_ready_regex,
            NULL, NULL, NULL);
    }
}

/*****************************************************************************/

MMBroadbandModemFibocom *
mm_broadband_modem_fibocom_new (const gchar  *device,
                                const gchar  *physdev,
                                const gchar **drivers,
                                const gchar  *plugin,
                                guint16       vendor_id,
                                guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_FIBOCOM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_fibocom_init (MMBroadbandModemFibocom *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_FIBOCOM,
                                              MMBroadbandModemFibocomPrivate);

    self->priv->gtrndis_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->sim_ready_regex = g_regex_new ("\\r\\n\\+SIM READY\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->initial_eps_bearer_support = FEATURE_SUPPORT_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBroadbandModemFibocom *self = MM_BROADBAND_MODEM_FIBOCOM (object);

    g_regex_unref (self->priv->sim_ready_regex);

    G_OBJECT_CLASS (mm_broadband_modem_fibocom_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->reset = modem_reset;
    iface->reset_finish = modem_common_power_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_common_power_finish;
    iface->modem_power_off = modem_power_off;
    iface->modem_power_off_finish = modem_common_power_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface->load_initial_eps_bearer = modem_3gpp_load_initial_eps_bearer;
    iface->load_initial_eps_bearer_finish = modem_3gpp_load_initial_eps_bearer_finish;
    iface->load_initial_eps_bearer_settings = modem_3gpp_load_initial_eps_bearer_settings;
    iface->load_initial_eps_bearer_settings_finish = modem_3gpp_load_initial_eps_bearer_settings_finish;
    iface->set_initial_eps_bearer_settings = modem_3gpp_set_initial_eps_bearer_settings;
    iface->set_initial_eps_bearer_settings_finish = modem_3gpp_set_initial_eps_bearer_settings_finish;
}

static void
iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface)
{
    iface_modem_3gpp_profile_manager_parent = g_type_interface_peek_parent (iface);

    iface->deactivate_profile = modem_3gpp_profile_manager_deactivate_profile;
    iface->deactivate_profile_finish = modem_3gpp_profile_manager_deactivate_profile_finish;
}

static void
mm_broadband_modem_fibocom_class_init (MMBroadbandModemFibocomClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (G_OBJECT_CLASS (klass),
                              sizeof (MMBroadbandModemFibocomPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
