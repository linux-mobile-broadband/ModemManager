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
 * Copyright (C) 2015-2019 Telit.
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */
#ifndef MM_MODEM_HELPERS_TELIT_H
#define MM_MODEM_HELPERS_TELIT_H

#include <glib.h>
#include "ModemManager.h"

typedef enum {
    MM_TELIT_MODEL_DEFAULT,
    MM_TELIT_MODEL_FN980,
    MM_TELIT_MODEL_LE910C1,
    MM_TELIT_MODEL_LM940,
    MM_TELIT_MODEL_LM960,
    MM_TELIT_MODEL_LN920,
    MM_TELIT_MODEL_FN990,
} MMTelitModel;

typedef struct {
    gboolean      modem_is_2g;
    gboolean      modem_is_3g;
    gboolean      modem_is_4g;
    gboolean      modem_alternate_3g_bands;
    gboolean      modem_has_hex_format_4g_bands;
    gboolean      modem_ext_4g_bands;
} MMTelitBNDParseConfig;

typedef enum {
    MM_TELIT_SW_REV_CMP_INVALID,
    MM_TELIT_SW_REV_CMP_UNSUPPORTED,
    MM_TELIT_SW_REV_CMP_OLDER,
    MM_TELIT_SW_REV_CMP_EQUAL,
    MM_TELIT_SW_REV_CMP_NEWER,
} MMTelitSwRevCmp;

/* #BND response parsers and request builder */
GArray *mm_telit_parse_bnd_query_response (const gchar            *response,
                                           MMTelitBNDParseConfig  *config,
                                           gpointer                log_object,
                                           GError                **error);
GArray *mm_telit_parse_bnd_test_response  (const gchar            *response,
                                           MMTelitBNDParseConfig  *config,
                                           gpointer                log_object,
                                           GError                **error);
gchar  *mm_telit_build_bnd_request        (GArray                 *bands_array,
                                           MMTelitBNDParseConfig  *config,
                                           GError                **error);

/* #QSS? response parser */
typedef enum { /*< underscore_name=mm_telit_qss_status >*/
    QSS_STATUS_UNKNOWN = -1,
    QSS_STATUS_SIM_REMOVED,
    QSS_STATUS_SIM_INSERTED,
    QSS_STATUS_SIM_INSERTED_AND_UNLOCKED,
    QSS_STATUS_SIM_INSERTED_AND_READY,
} MMTelitQssStatus;

MMTelitQssStatus mm_telit_parse_qss_query (const gchar *response, GError **error);

/* CSIM lock state */
typedef enum { /*< underscore_name=mm_telit_csim_lock_state >*/
    CSIM_LOCK_STATE_UNKNOWN,
    CSIM_LOCK_STATE_UNLOCKED,
    CSIM_LOCK_STATE_LOCK_REQUESTED,
    CSIM_LOCK_STATE_LOCKED,
} MMTelitCsimLockState;

GArray *mm_telit_build_modes_list (void);

gchar *mm_telit_parse_swpkgv_response (const gchar *response);

MMTelitModel mm_telit_model_from_revision (const gchar *revision);

MMTelitSwRevCmp mm_telit_software_revision_cmp (const gchar *reference,
                                                const gchar *revision);

#endif  /* MM_MODEM_HELPERS_TELIT_H */
