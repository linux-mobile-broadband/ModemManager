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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2016 Red Hat, Inc.
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_MODEM_HELPERS_LINKTOP_H
#define MM_MODEM_HELPERS_LINKTOP_H

#include <glib.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

typedef enum {
    LINKTOP_MODE_OFFLINE   = 0,
    LINKTOP_MODE_ANY       = 1,
    LINKTOP_MODE_LOW_POWER = 4,
    LINKTOP_MODE_2G        = 5,
    LINKTOP_MODE_3G        = 6,
} MMLinktopMode;

/* AT+CFUN? response parsers */
gboolean mm_linktop_parse_cfun_query_current_modes (const gchar  *response,
                                                    MMModemMode  *allowed,
                                                    GError      **error);

#endif  /* MM_MODEM_HELPERS_LINKTOP_H */
