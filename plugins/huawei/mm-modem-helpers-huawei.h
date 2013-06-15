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
 * Copyright (C) 2013 Huawei Technologies Co., Ltd
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_MODEM_HELPERS_HUAWEI_H
#define MM_MODEM_HELPERS_HUAWEI_H

#include "glib.h"

/* ^NDISSTATQRY response parser */
gboolean mm_huawei_parse_ndisstatqry_response (const gchar *response,
                                               gboolean *ipv4_available,
                                               gboolean *ipv4_connected,
                                               gboolean *ipv6_available,
                                               gboolean *ipv6_connected,
                                               GError **error);

#endif  /* MM_MODEM_HELPERS_HUAWEI_H */
