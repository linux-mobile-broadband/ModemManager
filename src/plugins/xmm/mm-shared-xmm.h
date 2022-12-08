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

#ifndef MM_SHARED_XMM_H
#define MM_SHARED_XMM_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-signal.h"
#include "mm-iface-modem-location.h"

#define MM_TYPE_SHARED_XMM               (mm_shared_xmm_get_type ())
#define MM_SHARED_XMM(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SHARED_XMM, MMSharedXmm))
#define MM_IS_SHARED_XMM(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SHARED_XMM))
#define MM_SHARED_XMM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_SHARED_XMM, MMSharedXmm))

typedef struct _MMSharedXmm MMSharedXmm;

struct _MMSharedXmm {
    GTypeInterface g_iface;

    /* Peek broadband modem class of the parent class of the object */
    MMBroadbandModemClass * (* peek_parent_broadband_modem_class) (MMSharedXmm *self);

    /* Peek location interface of the parent class of the object */
    MMIfaceModemLocation *  (* peek_parent_location_interface)    (MMSharedXmm *self);
};

GType mm_shared_xmm_get_type (void);

/* Shared XMM device setup */

void mm_shared_xmm_setup_ports (MMBroadbandModem *self);

/* Shared XMM device management support */

void      mm_shared_xmm_load_supported_modes        (MMIfaceModem         *self,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
GArray   *mm_shared_xmm_load_supported_modes_finish (MMIfaceModem         *self,
                                                     GAsyncResult         *res,
                                                     GError              **error);
void      mm_shared_xmm_load_current_modes          (MMIfaceModem         *self,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean  mm_shared_xmm_load_current_modes_finish   (MMIfaceModem         *self,
                                                     GAsyncResult         *res,
                                                     MMModemMode          *allowed,
                                                     MMModemMode          *preferred,
                                                     GError              **error);
void      mm_shared_xmm_set_current_modes           (MMIfaceModem         *self,
                                                     MMModemMode           allowed,
                                                     MMModemMode           preferred,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean  mm_shared_xmm_set_current_modes_finish    (MMIfaceModem         *self,
                                                     GAsyncResult         *res,
                                                     GError              **error);

void      mm_shared_xmm_load_supported_bands        (MMIfaceModem         *self,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
GArray   *mm_shared_xmm_load_supported_bands_finish (MMIfaceModem         *self,
                                                     GAsyncResult         *res,
                                                     GError              **error);
void      mm_shared_xmm_load_current_bands          (MMIfaceModem         *self,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
GArray   *mm_shared_xmm_load_current_bands_finish   (MMIfaceModem         *self,
                                                     GAsyncResult         *res,
                                                     GError              **error);
void      mm_shared_xmm_set_current_bands           (MMIfaceModem         *self,
                                                     GArray               *bands_array,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean  mm_shared_xmm_set_current_bands_finish    (MMIfaceModem         *self,
                                                     GAsyncResult         *res,
                                                     GError              **error);

void              mm_shared_xmm_load_power_state        (MMIfaceModem         *self,
                                                         GAsyncReadyCallback   callback,
                                                         gpointer              user_data);
MMModemPowerState mm_shared_xmm_load_power_state_finish (MMIfaceModem         *self,
                                                         GAsyncResult         *res,
                                                         GError              **error);
void              mm_shared_xmm_power_up                (MMIfaceModem         *self,
                                                         GAsyncReadyCallback   callback,
                                                         gpointer              user_data);
gboolean          mm_shared_xmm_power_up_finish         (MMIfaceModem         *self,
                                                         GAsyncResult         *res,
                                                         GError              **error);
void              mm_shared_xmm_power_down              (MMIfaceModem         *self,
                                                         GAsyncReadyCallback   callback,
                                                         gpointer              user_data);
gboolean          mm_shared_xmm_power_down_finish       (MMIfaceModem         *self,
                                                         GAsyncResult         *res,
                                                         GError              **error);
void              mm_shared_xmm_power_off               (MMIfaceModem         *self,
                                                         GAsyncReadyCallback   callback,
                                                         gpointer              user_data);
gboolean          mm_shared_xmm_power_off_finish        (MMIfaceModem         *self,
                                                         GAsyncResult         *res,
                                                         GError              **error);
void              mm_shared_xmm_reset                   (MMIfaceModem         *self,
                                                         GAsyncReadyCallback   callback,
                                                         gpointer              user_data);
gboolean          mm_shared_xmm_reset_finish            (MMIfaceModem         *self,
                                                         GAsyncResult         *res,
                                                         GError              **error);

gboolean          mm_shared_xmm_signal_check_support_finish    (MMIfaceModemSignal  *self,
                                                                GAsyncResult        *res,
                                                                GError             **error);

void              mm_shared_xmm_signal_check_support           (MMIfaceModemSignal  *self,
                                                                GAsyncReadyCallback  callback,
                                                                gpointer             user_data);
gboolean          mm_shared_xmm_signal_load_values_finish      (MMIfaceModemSignal  *self,
                                                                GAsyncResult        *res,
                                                                MMSignal           **cdma,
                                                                MMSignal           **evdo,
                                                                MMSignal           **gsm,
                                                                MMSignal           **umts,
                                                                MMSignal           **lte,
                                                                MMSignal           **nr5g,
                                                                GError             **error);
void              mm_shared_xmm_signal_load_values             (MMIfaceModemSignal  *self,
                                                                GCancellable        *cancellable,
                                                                GAsyncReadyCallback  callback,
                                                                gpointer             user_data);

void                  mm_shared_xmm_location_load_capabilities        (MMIfaceModemLocation   *self,
                                                                       GAsyncReadyCallback     callback,
                                                                       gpointer                user_data);
MMModemLocationSource mm_shared_xmm_location_load_capabilities_finish (MMIfaceModemLocation   *self,
                                                                       GAsyncResult           *res,
                                                                       GError                **error);
void                  mm_shared_xmm_enable_location_gathering         (MMIfaceModemLocation   *self,
                                                                       MMModemLocationSource   source,
                                                                       GAsyncReadyCallback     callback,
                                                                       gpointer                user_data);
gboolean              mm_shared_xmm_enable_location_gathering_finish  (MMIfaceModemLocation   *self,
                                                                       GAsyncResult           *res,
                                                                       GError                **error);
void                  mm_shared_xmm_disable_location_gathering        (MMIfaceModemLocation   *self,
                                                                       MMModemLocationSource   source,
                                                                       GAsyncReadyCallback     callback,
                                                                       gpointer                user_data);
gboolean              mm_shared_xmm_disable_location_gathering_finish (MMIfaceModemLocation   *self,
                                                                       GAsyncResult           *res,
                                                                       GError                **error);
void                   mm_shared_xmm_location_load_supl_server        (MMIfaceModemLocation   *self,
                                                                       GAsyncReadyCallback     callback,
                                                                       gpointer                user_data);
gchar                 *mm_shared_xmm_location_load_supl_server_finish (MMIfaceModemLocation   *self,
                                                                       GAsyncResult           *res,
                                                                       GError                **error);
void                   mm_shared_xmm_location_set_supl_server         (MMIfaceModemLocation   *self,
                                                                       const gchar            *supl,
                                                                       GAsyncReadyCallback     callback,
                                                                       gpointer                user_data);
gboolean               mm_shared_xmm_location_set_supl_server_finish  (MMIfaceModemLocation   *self,
                                                                       GAsyncResult           *res,
                                                                       GError                **error);

#endif /* MM_SHARED_XMM_H */
