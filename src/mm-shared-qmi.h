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

#ifndef MM_SHARED_QMI_H
#define MM_SHARED_QMI_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <libqmi-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-location.h"
#include "mm-port-qmi.h"

#define MM_TYPE_SHARED_QMI            (mm_shared_qmi_get_type ())
#define MM_SHARED_QMI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SHARED_QMI, MMSharedQmi))
#define MM_IS_SHARED_QMI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SHARED_QMI))
#define MM_SHARED_QMI_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_SHARED_QMI, MMSharedQmi))

typedef struct _MMSharedQmi MMSharedQmi;

struct _MMSharedQmi {
    GTypeInterface g_iface;

    QmiClient * (* peek_client) (MMSharedQmi    *self,
                                 QmiService      service,
                                 MMPortQmiFlag   flag,
                                 GError        **error);

    /* Peek location interface of the parent class of the object */
    MMIfaceModemLocation * (* peek_parent_location_interface) (MMSharedQmi *self);
};

GType mm_shared_qmi_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSharedQmi, g_object_unref)

QmiClient *mm_shared_qmi_peek_client   (MMSharedQmi          *self,
                                        QmiService            service,
                                        MMPortQmiFlag         flag,
                                        GError              **error);

gboolean   mm_shared_qmi_ensure_client (MMSharedQmi          *self,
                                        QmiService            service,
                                        QmiClient           **o_client,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data);

/* Shared QMI 3GPP operations */

void     mm_shared_qmi_3gpp_register_in_network        (MMIfaceModem3gpp     *self,
                                                        const gchar          *operator_id,
                                                        GCancellable         *cancellable,
                                                        GAsyncReadyCallback   callback,
                                                        gpointer              user_data);
gboolean mm_shared_qmi_3gpp_register_in_network_finish (MMIfaceModem3gpp     *self,
                                                        GAsyncResult         *res,
                                                        GError              **error);

/* Shared QMI device management support */

void               mm_shared_qmi_load_supported_capabilities        (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
GArray            *mm_shared_qmi_load_supported_capabilities_finish (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_load_current_capabilities          (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
MMModemCapability  mm_shared_qmi_load_current_capabilities_finish   (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_set_current_capabilities           (MMIfaceModem         *self,
                                                                     MMModemCapability     capabilities,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_set_current_capabilities_finish    (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_load_supported_modes               (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
GArray            *mm_shared_qmi_load_supported_modes_finish        (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_load_current_modes                 (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_load_current_modes_finish          (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     MMModemMode          *allowed,
                                                                     MMModemMode          *preferred,
                                                                     GError              **error);
void               mm_shared_qmi_set_current_modes                  (MMIfaceModem         *self,
                                                                     MMModemMode           allowed,
                                                                     MMModemMode           preferred,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_set_current_modes_finish           (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_load_supported_bands               (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
GArray            *mm_shared_qmi_load_supported_bands_finish        (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_load_current_bands                 (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
GArray            *mm_shared_qmi_load_current_bands_finish          (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_set_current_bands                  (MMIfaceModem         *self,
                                                                     GArray               *bands_array,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_set_current_bands_finish           (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_reset                              (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_reset_finish                       (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_factory_reset                      (MMIfaceModem         *self,
                                                                     const gchar          *code,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_factory_reset_finish               (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_load_carrier_config                (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_load_carrier_config_finish         (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     gchar               **carrier_config_name,
                                                                     gchar               **carrier_config_revision,
                                                                     GError              **error);
void               mm_shared_qmi_setup_carrier_config               (MMIfaceModem         *self,
                                                                     const gchar          *imsi,
                                                                     const gchar          *carrier_config_mapping,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_setup_carrier_config_finish        (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_load_sim_slots                     (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_load_sim_slots_finish              (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GPtrArray           **sim_slots,
                                                                     guint                *primary_sim_slot,
                                                                     GError              **error);
void               mm_shared_qmi_set_primary_sim_slot               (MMIfaceModem         *self,
                                                                     guint                 sim_slot,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_set_primary_sim_slot_finish        (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_setup_sim_hot_swap                 (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_setup_sim_hot_swap_finish          (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);
void               mm_shared_qmi_fcc_unlock                         (MMIfaceModem         *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean           mm_shared_qmi_fcc_unlock_finish                  (MMIfaceModem         *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);

/* Shared QMI location support */

void                               mm_shared_qmi_location_load_capabilities                     (MMIfaceModemLocation   *self,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
MMModemLocationSource              mm_shared_qmi_location_load_capabilities_finish              (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);
void                               mm_shared_qmi_enable_location_gathering                      (MMIfaceModemLocation   *_self,
                                                                                                 MMModemLocationSource   source,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
gboolean                           mm_shared_qmi_enable_location_gathering_finish               (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);
void                               mm_shared_qmi_disable_location_gathering                     (MMIfaceModemLocation   *_self,
                                                                                                 MMModemLocationSource   source,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
gboolean                           mm_shared_qmi_disable_location_gathering_finish              (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);
void                               mm_shared_qmi_location_load_supl_server                      (MMIfaceModemLocation   *self,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
gchar                             *mm_shared_qmi_location_load_supl_server_finish               (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);
void                               mm_shared_qmi_location_set_supl_server                       (MMIfaceModemLocation   *self,
                                                                                                 const gchar            *supl,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
gboolean                           mm_shared_qmi_location_set_supl_server_finish                (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);
void                               mm_shared_qmi_location_load_supported_assistance_data        (MMIfaceModemLocation   *self,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
MMModemLocationAssistanceDataType  mm_shared_qmi_location_load_supported_assistance_data_finish (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);
void                               mm_shared_qmi_location_inject_assistance_data                (MMIfaceModemLocation   *self,
                                                                                                 const guint8           *data,
                                                                                                 gsize                   data_size,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
gboolean                           mm_shared_qmi_location_inject_assistance_data_finish         (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);
void                               mm_shared_qmi_location_load_assistance_data_servers          (MMIfaceModemLocation   *self,
                                                                                                 GAsyncReadyCallback     callback,
                                                                                                 gpointer                user_data);
gchar                            **mm_shared_qmi_location_load_assistance_data_servers_finish   (MMIfaceModemLocation   *self,
                                                                                                 GAsyncResult           *res,
                                                                                                 GError                **error);

#endif /* MM_SHARED_QMI_H */
