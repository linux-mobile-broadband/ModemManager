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
 * Copyright (C) 2013 Google Inc.
 *
 */

#ifndef MM_MODEM_HELPERS_ALTAIR_H
#define MM_MODEM_HELPERS_ALTAIR_H

#include <glib.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

/* Bands response parser */
GArray *mm_altair_parse_bands_response (const gchar *response);

/* +CEER response parser */
gchar *mm_altair_parse_ceer_response (const gchar *response,
                                      GError **error);

/* %CGINFO="cid",1 response parser */
gint mm_altair_parse_cid (const gchar *response, GError **error);

/* %PCOINFO response parser */
MMPco *mm_altair_parse_vendor_pco_info (const gchar *pco_info, GError **error);

#endif  /* MM_MODEM_HELPERS_ALTAIR_H */
