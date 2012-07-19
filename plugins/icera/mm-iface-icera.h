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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_IFACE_ICERA_H
#define MM_IFACE_ICERA_H

#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-time.h"
#include "mm-broadband-modem.h"

#define MM_TYPE_IFACE_ICERA            (mm_iface_icera_get_type ())
#define MM_IFACE_ICERA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_ICERA, MMIfaceIcera))
#define MM_IS_IFACE_ICERA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_ICERA))
#define MM_IFACE_ICERA_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_ICERA, MMIfaceIcera))

typedef struct _MMIfaceIcera MMIfaceIcera;

struct _MMIfaceIcera {
    GTypeInterface g_iface;
};

GType mm_iface_icera_get_type (void);


void     mm_iface_icera_check_support        (MMBroadbandModem *self,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
gboolean mm_iface_icera_check_support_finish (MMBroadbandModem *self,
                                              GAsyncResult *res,
                                              GError **error);

/*****************************************************************************/
/* Modem interface specific implementations */

void     mm_iface_icera_modem_load_allowed_modes        (MMIfaceModem *self,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);
gboolean mm_iface_icera_modem_load_allowed_modes_finish (MMIfaceModem *self,
                                                         GAsyncResult *res,
                                                         MMModemMode *allowed,
                                                         MMModemMode *preferred,
                                                         GError **error);

void     mm_iface_icera_modem_set_allowed_modes        (MMIfaceModem *self,
                                                        MMModemMode allowed,
                                                        MMModemMode preferred,
                                                        GAsyncReadyCallback callback,
                                                        gpointer user_data);
gboolean mm_iface_icera_modem_set_allowed_modes_finish (MMIfaceModem *self,
                                                        GAsyncResult *res,
                                                        GError **error);

void     mm_iface_icera_modem_set_unsolicited_events_handlers (MMBroadbandModem *self,
                                                               gboolean enable);

void     mm_iface_icera_modem_load_access_technologies        (MMIfaceModem *self,
                                                               GAsyncReadyCallback callback,
                                                               gpointer user_data);
gboolean mm_iface_icera_modem_load_access_technologies_finish (MMIfaceModem *self,
                                                               GAsyncResult *res,
                                                               MMModemAccessTechnology *access_technologies,
                                                               guint *mask,
                                                               GError **error);

void      mm_iface_icera_modem_create_bearer        (MMIfaceModem *self,
                                                     MMBearerProperties *properties,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
MMBearer *mm_iface_icera_modem_create_bearer_finish (MMIfaceModem *self,
                                                     GAsyncResult *res,
                                                     GError **error);

void     mm_iface_icera_modem_reset        (MMIfaceModem *self,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_iface_icera_modem_reset_finish (MMIfaceModem *self,
                                            GAsyncResult *res,
                                            GError **error);


/*****************************************************************************/
/* Modem 3GPP interface specific implementations */

void mm_iface_icera_modem_3gpp_enable_unsolicited_events             (MMIfaceModem3gpp *self,
                                                                      GAsyncReadyCallback callback,
                                                                      gpointer user_data);
gboolean mm_iface_icera_modem_3gpp_enable_unsolicited_events_finish  (MMIfaceModem3gpp *self,
                                                                      GAsyncResult *res,
                                                                      GError **error);
void mm_iface_icera_modem_3gpp_disable_unsolicited_events            (MMIfaceModem3gpp *self,
                                                                      GAsyncReadyCallback callback,
                                                                      gpointer user_data);
gboolean mm_iface_icera_modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                                      GAsyncResult *res,
                                                                      GError **error);

/*****************************************************************************/
/* Modem Time interface specific implementations */

void   mm_iface_icera_modem_time_load_network_time        (MMIfaceModemTime *self,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data);
gchar *mm_iface_icera_modem_time_load_network_time_finish (MMIfaceModemTime *self,
                                                           GAsyncResult *res,
                                                           GError **error);

void               mm_iface_icera_modem_time_load_network_timezone        (MMIfaceModemTime *self,
                                                                           GAsyncReadyCallback callback,
                                                                           gpointer user_data);
MMNetworkTimezone *mm_iface_icera_modem_time_load_network_timezone_finish (MMIfaceModemTime *self,
                                                                           GAsyncResult *res,
                                                                           GError **error);

#endif /* MM_IFACE_ICERA_H */
