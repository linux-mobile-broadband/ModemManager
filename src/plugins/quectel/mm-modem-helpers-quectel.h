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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_MODEM_HELPERS_QUECTEL_H
#define MM_MODEM_HELPERS_QUECTEL_H

#include <glib.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

gboolean mm_quectel_parse_ctzu_test_response (const gchar  *response,
                                              gpointer      log_object,
                                              gboolean     *supports_disable,
                                              gboolean     *supports_enable,
                                              gboolean     *supports_enable_update_rtc,
                                              GError      **error);

#endif  /* MM_MODEM_HELPERS_QUECTEL_H */
