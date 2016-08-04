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

#ifndef MM_MODEM_HELPERS_UBLOX_H
#define MM_MODEM_HELPERS_UBLOX_H

#include <glib.h>

/*****************************************************************************/
/* UUSBCONF? response parser */

typedef enum { /*< underscore_name=mm_ublox_usb_profile >*/
    MM_UBLOX_USB_PROFILE_UNKNOWN,
    MM_UBLOX_USB_PROFILE_RNDIS,
    MM_UBLOX_USB_PROFILE_ECM,
    MM_UBLOX_USB_PROFILE_BACK_COMPATIBLE,
} MMUbloxUsbProfile;

gboolean mm_ublox_parse_uusbconf_response (const gchar        *response,
                                           MMUbloxUsbProfile  *out_profile,
                                           GError            **error);

/*****************************************************************************/
/* UBMCONF? response parser */

typedef enum { /*< underscore_name=mm_ublox_networking_mode >*/
    MM_UBLOX_NETWORKING_MODE_UNKNOWN,
    MM_UBLOX_NETWORKING_MODE_ROUTER,
    MM_UBLOX_NETWORKING_MODE_BRIDGE,
} MMUbloxNetworkingMode;

gboolean mm_ublox_parse_ubmconf_response (const gchar            *response,
                                          MMUbloxNetworkingMode  *out_mode,
                                          GError                **error);

/*****************************************************************************/
/* UIPADDR=N response parser */

gboolean mm_ublox_parse_uipaddr_response (const gchar  *response,
                                          guint        *out_cid,
                                          gchar       **out_if_name,
                                          gchar       **out_ipv4_address,
                                          gchar       **out_ipv4_subnet,
                                          gchar       **out_ipv6_global_address,
                                          gchar       **out_ipv6_link_local_address,
                                          GError      **error);

#endif  /* MM_MODEM_HELPERS_UBLOX_H */
