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
 * Copyright (C) 2014 Dan Williams <dcbw@redhat.com>
 */

#ifndef MM_MODEM_HELPERS_MBM_H
#define MM_MODEM_HELPERS_MBM_H

#include "glib.h"

/* *E2IPCFG response parser */
gboolean mm_mbm_parse_e2ipcfg_response (const gchar *response,
                                        MMBearerIpConfig **out_ip4_config,
                                        MMBearerIpConfig **out_ip6_config,
                                        GError **error);

typedef enum {
    MBM_NETWORK_MODE_OFFLINE   = 0,
    MBM_NETWORK_MODE_ANY       = 1,
    MBM_NETWORK_MODE_LOW_POWER = 4,
    MBM_NETWORK_MODE_2G        = 5,
    MBM_NETWORK_MODE_3G        = 6,
} MbmNetworkMode;

/* AT+CFUN=? test parser
 * Returns a bitmask, bit index set for the supported modes reported */
gboolean mm_mbm_parse_cfun_test (const gchar  *response,
                                 gpointer      log_object,
                                 guint32      *supported_mask,
                                 GError      **error);

/* AT+CFUN? response parsers */
gboolean mm_mbm_parse_cfun_query_power_state   (const gchar        *response,
                                                MMModemPowerState  *out_state,
                                                GError            **error);
gboolean mm_mbm_parse_cfun_query_current_modes (const gchar        *response,
                                                MMModemMode        *allowed,
                                                gint               *mbm_mode,
                                                GError            **error);

#endif  /* MM_MODEM_HELPERS_MBM_H */
