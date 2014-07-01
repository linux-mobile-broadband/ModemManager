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
 * Copyright (C) 2014 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_COMMON_CINTERION_H
#define MM_COMMON_CINTERION_H

#include "glib.h"
#include "mm-broadband-modem.h"
#include "mm-iface-modem-location.h"

void                  mm_common_cinterion_location_load_capabilities        (MMIfaceModemLocation *self,
                                                                             GAsyncReadyCallback callback,
                                                                             gpointer user_data);
MMModemLocationSource mm_common_cinterion_location_load_capabilities_finish (MMIfaceModemLocation *self,
                                                                             GAsyncResult *res,
                                                                             GError **error);

void                  mm_common_cinterion_enable_location_gathering         (MMIfaceModemLocation *self,
                                                                             MMModemLocationSource source,
                                                                             GAsyncReadyCallback callback,
                                                                             gpointer user_data);
gboolean              mm_common_cinterion_enable_location_gathering_finish  (MMIfaceModemLocation *self,
                                                                             GAsyncResult *res,
                                                                             GError **error);

void                  mm_common_cinterion_disable_location_gathering        (MMIfaceModemLocation *self,
                                                                             MMModemLocationSource source,
                                                                             GAsyncReadyCallback callback,
                                                                             gpointer user_data);
gboolean              mm_common_cinterion_disable_location_gathering_finish (MMIfaceModemLocation *self,
                                                                             GAsyncResult *res,
                                                                             GError **error);

void                  mm_common_cinterion_setup_gps_port                    (MMBroadbandModem *self);

void                  mm_common_cinterion_peek_parent_location_interface    (MMIfaceModemLocation *iface);

#endif  /* MM_COMMON_CINTERION_H */
