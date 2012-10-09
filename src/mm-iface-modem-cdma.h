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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_CDMA_H
#define MM_IFACE_MODEM_CDMA_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-at-serial-port.h"

#define MM_TYPE_IFACE_MODEM_CDMA               (mm_iface_modem_cdma_get_type ())
#define MM_IFACE_MODEM_CDMA(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_CDMA, MMIfaceModemCdma))
#define MM_IS_IFACE_MODEM_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_CDMA))
#define MM_IFACE_MODEM_CDMA_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_CDMA, MMIfaceModemCdma))

#define MM_IFACE_MODEM_CDMA_DBUS_SKELETON             "iface-modem-cdma-dbus-skeleton"
#define MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE "iface-modem-cdma-cdma1x-registration-state"
#define MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE   "iface-modem-cdma-evdo-registration-state"
#define MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED    "iface-modem-cdma-evdo-network-supported"
#define MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED  "iface-modem-cdma-cdma1x-network-supported"

#define MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK        \
    (MM_IFACE_MODEM_CDMA_ALL_CDMA1X_ACCESS_TECHNOLOGIES_MASK |  \
     MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK)

#define MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK   \
    (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 |                         \
     MM_MODEM_ACCESS_TECHNOLOGY_EVDOA |                         \
     MM_MODEM_ACCESS_TECHNOLOGY_EVDOB)

#define MM_IFACE_MODEM_CDMA_ALL_CDMA1X_ACCESS_TECHNOLOGIES_MASK \
    (MM_MODEM_ACCESS_TECHNOLOGY_1XRTT)

typedef struct _MMIfaceModemCdma MMIfaceModemCdma;

struct _MMIfaceModemCdma {
    GTypeInterface g_iface;

    /* Loading of the MEID property */
    void (*load_meid) (MMIfaceModemCdma *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gchar * (*load_meid_finish) (MMIfaceModemCdma *self,
                                 GAsyncResult *res,
                                 GError **error);

    /* Loading of the ESN property */
    void (*load_esn) (MMIfaceModemCdma *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data);
    gchar * (*load_esn_finish) (MMIfaceModemCdma *self,
                                GAsyncResult *res,
                                GError **error);

    /* Loading of the initial activation state */
    void (* load_activation_state) (MMIfaceModemCdma *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
    MMModemCdmaActivationState (* load_activation_state_finish) (MMIfaceModemCdma *self,
                                                                 GAsyncResult *res,
                                                                 GError **error);

    /* Asynchronous setting up unsolicited events */
    void (* setup_unsolicited_events) (MMIfaceModemCdma *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    gboolean (* setup_unsolicited_events_finish) (MMIfaceModemCdma *self,
                                                  GAsyncResult *res,
                                                  GError **error);


    /* Asynchronous enabling of unsolicited events */
    void (*enable_unsolicited_events) (MMIfaceModemCdma *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    gboolean (*enable_unsolicited_events_finish) (MMIfaceModemCdma *self,
                                                  GAsyncResult *res,
                                                  GError **error);

    /* Asynchronous cleaning up of unsolicited events */
    void (* cleanup_unsolicited_events) (MMIfaceModemCdma *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (* cleanup_unsolicited_events_finish) (MMIfaceModemCdma *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Asynchronous disabling of unsolicited events */
    void (*disable_unsolicited_events) (MMIfaceModemCdma *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (*disable_unsolicited_events_finish) (MMIfaceModemCdma *self,
                                                   GAsyncResult *res,
                                                   GError **error);

    /* OTA activation */
    void (* activate) (MMIfaceModemCdma *self,
                       const gchar *carrier_code,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gboolean (* activate_finish) (MMIfaceModemCdma *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Manual activation */
    void (* activate_manual) (MMIfaceModemCdma *self,
                              GVariant *properties,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
    gboolean (* activate_manual_finish) (MMIfaceModemCdma *self,
                                         GAsyncResult *res,
                                         GError **error);

    /* Try to register in the CDMA network. This implementation is just making
     * sure that the modem is registered, and if it's not it will wait until it
     * is.
     */
    void (* register_in_network) (MMIfaceModemCdma *self,
                                  guint max_registration_time,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gboolean (*register_in_network_finish) (MMIfaceModemCdma *self,
                                            GAsyncResult *res,
                                            GError **error);

    /* Run CDMA1x/EV-DO registration state checks..
     * Note that no registration state is returned, implementations should call
     * mm_iface_modem_cdma_update_registration_state().
     *
     * NOTE: Plugins implementing this method will NOT execute the generic
     * registration check logic involving setup_registration_checks(),
     * get_call_manager_state(), get_hdr_state(), get_service_status(),
     * get_cdma1x_serving_system() and get_detailed_registration_state().
     *
     * In other words, it is fine to leave this callback to NULL if you want
     * the generic steps to run. This callback may be implemented if there
     * is a completely independent way/command which can gather both CDMA1x
     * and EV-DO registration states, SID and NID.
     */
    void (* run_registration_checks) (MMIfaceModemCdma *self,
                                      gboolean cdma1x_supported,
                                      gboolean evdo_supported,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
    gboolean (* run_registration_checks_finish) (MMIfaceModemCdma *self,
                                                 GAsyncResult *res,
                                                 GError **error);

    /* The following steps will only be run if run_registration_checks() is NOT
     * given by the object implementing the interface */

    /* Setup registration checks */
    void (* setup_registration_checks) (MMIfaceModemCdma *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (* setup_registration_checks_finish) (MMIfaceModemCdma *self,
                                                   GAsyncResult *res,
                                                   gboolean *skip_qcdm_call_manager_step,
                                                   gboolean *skip_qcdm_hdr_step,
                                                   gboolean *skip_at_cdma_service_status_step,
                                                   gboolean *skip_at_cdma1x_serving_system_step,
                                                   gboolean *skip_detailed_registration_state,
                                                   GError **error);
    /* Get call manager state */
    void (* get_call_manager_state) (MMIfaceModemCdma *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    gboolean (* get_call_manager_state_finish) (MMIfaceModemCdma *self,
                                                GAsyncResult *res,
                                                guint *operating_mode,
                                                guint *system_mode,
                                                GError **error);
    /* Get HDR state */
    void (* get_hdr_state) (MMIfaceModemCdma *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
    gboolean (* get_hdr_state_finish) (MMIfaceModemCdma *self,
                                       GAsyncResult *res,
                                       guint8 *hybrid_mode,
                                       guint8 *session_state,
                                       guint8 *almp_state,
                                       GError **error);
    /* Get service status */
    void (* get_service_status) (MMIfaceModemCdma *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
    gboolean (* get_service_status_finish) (MMIfaceModemCdma *self,
                                            GAsyncResult *res,
                                            gboolean *has_cdma_service,
                                            GError **error);
    /* Get CDMA1x serving system */
    void (* get_cdma1x_serving_system) (MMIfaceModemCdma *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (* get_cdma1x_serving_system_finish) (MMIfaceModemCdma *self,
                                                   GAsyncResult *res,
                                                   guint *class,
                                                   guint *band,
                                                   guint *sid,
                                                   guint *nid,
                                                   GError **error);
    /* Get detailed registration state */
    void (* get_detailed_registration_state) (MMIfaceModemCdma *self,
                                              MMModemCdmaRegistrationState cdma1x_state,
                                              MMModemCdmaRegistrationState evdo_state,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
    gboolean (* get_detailed_registration_state_finish) (MMIfaceModemCdma *self,
                                                         GAsyncResult *res,
                                                         MMModemCdmaRegistrationState *detailed_cdma1x_state,
                                                         MMModemCdmaRegistrationState *detailed_evdo_state,
                                                         GError **error);
};

GType mm_iface_modem_cdma_get_type (void);

/* Initialize CDMA interface (async) */
void     mm_iface_modem_cdma_initialize        (MMIfaceModemCdma *self,
                                                GCancellable *cancellable,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
gboolean mm_iface_modem_cdma_initialize_finish (MMIfaceModemCdma *self,
                                                GAsyncResult *res,
                                                GError **error);

/* Enable CDMA interface (async) */
void     mm_iface_modem_cdma_enable        (MMIfaceModemCdma *self,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_iface_modem_cdma_enable_finish (MMIfaceModemCdma *self,
                                            GAsyncResult *res,
                                            GError **error);

/* Disable CDMA interface (async) */
void     mm_iface_modem_cdma_disable        (MMIfaceModemCdma *self,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
gboolean mm_iface_modem_cdma_disable_finish (MMIfaceModemCdma *self,
                                             GAsyncResult *res,
                                             GError **error);

/* Shutdown CDMA interface */
void mm_iface_modem_cdma_shutdown (MMIfaceModemCdma *self);

/* Objects implementing this interface can report new registration states,
 * access technologies and activation state changes */
void mm_iface_modem_cdma_update_cdma1x_registration_state (MMIfaceModemCdma *self,
                                                           MMModemCdmaRegistrationState state,
                                                           guint sid,
                                                           guint nid);
void mm_iface_modem_cdma_update_evdo_registration_state (MMIfaceModemCdma *self,
                                                         MMModemCdmaRegistrationState state);
void mm_iface_modem_cdma_update_access_technologies (MMIfaceModemCdma *self,
                                                     MMModemAccessTechnology access_tech);

void mm_iface_modem_cdma_update_activation_state (MMIfaceModemCdma *self,
                                                  MMModemCdmaActivationState activation_state,
                                                  const GError *activation_error);

/* Run all registration checks */
void     mm_iface_modem_cdma_run_registration_checks        (MMIfaceModemCdma *self,
                                                             GAsyncReadyCallback callback,
                                                             gpointer user_data);
gboolean mm_iface_modem_cdma_run_registration_checks_finish (MMIfaceModemCdma *self,
                                                             GAsyncResult *res,
                                                             GError **error);

/* Register in network */
void     mm_iface_modem_cdma_register_in_network        (MMIfaceModemCdma *self,
                                                         guint max_registration_time,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);
gboolean mm_iface_modem_cdma_register_in_network_finish (MMIfaceModemCdma *self,
                                                         GAsyncResult *res,
                                                         GError **error);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_cdma_bind_simple_status (MMIfaceModemCdma *self,
                                             MMSimpleStatus *status);

#endif /* MM_IFACE_MODEM_CDMA_H */
