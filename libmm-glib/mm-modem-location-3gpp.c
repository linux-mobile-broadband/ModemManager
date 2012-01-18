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
 * Copyright (C) 2012 Google, Inc.
 */

#include "mm-modem-location-3gpp.h"

guint
mm_modem_location_3gpp_get_mobile_country_code (MMModemLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_COMMON_LOCATION_3GPP (self), 0);

    return mm_common_location_3gpp_get_mobile_country_code (self);
}

guint
mm_modem_location_3gpp_get_mobile_network_code (MMModemLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_COMMON_LOCATION_3GPP (self), 0);

    return mm_common_location_3gpp_get_mobile_network_code (self);
}

gulong
mm_modem_location_3gpp_get_location_area_code (MMModemLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_COMMON_LOCATION_3GPP (self), 0);

    return mm_common_location_3gpp_get_location_area_code (self);
}

gulong
mm_modem_location_3gpp_get_cell_id (MMModemLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_COMMON_LOCATION_3GPP (self), 0);

    return mm_common_location_3gpp_get_cell_id (self);
}
