/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_LOCATION_3GPP_H_
#define _MM_MODEM_LOCATION_3GPP_H_

#include <ModemManager.h>
#include <glib-object.h>

#include <libmm-common.h>

G_BEGIN_DECLS

typedef MMCommonLocation3gpp           MMModemLocation3gpp;
#define MM_TYPE_MODEM_LOCATION_3GPP(o) MM_TYPE_LOCATION_3GPP (o)
#define MM_MODEM_LOCATION_3GPP(o)      MM_LOCATION_3GPP(o)
#define MM_IS_MODEM_LOCATION_3GPP(o)   MM_IS_LOCATION_3GPP(o)

guint  mm_modem_location_3gpp_get_mobile_country_code (MMModemLocation3gpp *self);
guint  mm_modem_location_3gpp_get_mobile_network_code (MMModemLocation3gpp *self);
gulong mm_modem_location_3gpp_get_location_area_code  (MMModemLocation3gpp *self);
gulong mm_modem_location_3gpp_get_cell_id             (MMModemLocation3gpp *self);

G_END_DECLS

#endif /* _MM_MODEM_LOCATION_3GPP_H_ */
