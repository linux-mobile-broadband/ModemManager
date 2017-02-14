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
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_MODEM_HELPERS_CINTERION_H
#define MM_MODEM_HELPERS_CINTERION_H

#include <glib.h>

#include <ModemManager.h>

/*****************************************************************************/
/* ^SCFG test parser */

gboolean mm_cinterion_parse_scfg_test (const gchar *response,
                                       MMModemCharset charset,
                                       GArray **supported_bands,
                                       GError **error);

/*****************************************************************************/
/* ^SCFG response parser */

gboolean mm_cinterion_parse_scfg_response (const gchar *response,
                                           MMModemCharset charset,
                                           GArray **bands,
                                           GError **error);

/*****************************************************************************/
/* +CNMI test parser */

gboolean mm_cinterion_parse_cnmi_test (const gchar *response,
                                       GArray **supported_mode,
                                       GArray **supported_mt,
                                       GArray **supported_bm,
                                       GArray **supported_ds,
                                       GArray **supported_bfr,
                                       GError **error);

/*****************************************************************************/
/* Build Cinterion-specific band value */

gboolean mm_cinterion_build_band (GArray *bands,
                                  guint supported,
                                  gboolean only_2g,
                                  guint *out_band,
                                  GError **error);

/*****************************************************************************/
/* Single ^SIND response parser */

gboolean mm_cinterion_parse_sind_response (const gchar *response,
                                           gchar **description,
                                           guint *mode,
                                           guint *value,
                                           GError **error);

/*****************************************************************************/
/* ^SMONG response parser */

gboolean mm_cinterion_parse_smong_response (const gchar              *response,
                                            MMModemAccessTechnology  *access_tech,
                                            GError                  **error);

#endif  /* MM_MODEM_HELPERS_CINTERION_H */
