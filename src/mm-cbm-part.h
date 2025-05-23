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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#ifndef MM_CBM_PART_H
#define MM_CBM_PART_H

#include <glib.h>
#include <ModemManager.h>

#include "mm-sms-part.h"

/* Serial number per ETSI TS 123 041 */
#define CBM_SERIAL_GEO_SCOPE(serial)           (((serial) & 0xC000) >> 14)
#define CBM_SERIAL_MESSAGE_CODE(serial)        (((serial) & 0x0FF0) >> 4)
#define CBM_SERIAL_MESSAGE_CODE_UPDATE(serial) ((serial) & 0x000F)
#define CBM_SERIAL_MESSAGE_CODE_ALERT(serial)  (!!((serial) & 0x2000))
#define CBM_SERIAL_MESSAGE_CODE_POPUP(serial)  (!!((serial) & 0x1000))

/**
 * MMCbmGeoScope:
 * @MM_CBM_GEO_SCOPE_CELL_IMMEDIATE: cell wide, immediate display
 * @MM_CBM_GEO_SCOPE_PLMN: PLMN wide, normal display
 * @MM_CBM_GEO_SCOPE_AREA: area wide, normal display
 * @MM_CBM_GEO_SCOPE_CELL_NORMAL: cell wide, normal display
 *
 * The geographical area of which a CBM is unique and whether to display
 * it immediately to the user.
 */
typedef enum _MMCbmGeoScope {
    MM_CBM_GEO_SCOPE_CELL_IMMEDIATE = 0,
    MM_CBM_GEO_SCOPE_PLMN           = 1,
    MM_CBM_GEO_SCOPE_AREA           = 2,
    MM_CBM_GEO_SCOPE_CELL_NORMAL    = 3,
} MMCbmGeoScope;


typedef struct _MMCbmPart MMCbmPart;

MMCbmPart *mm_cbm_part_new_from_pdu        (const gchar   *hexpdu,
                                            gpointer       log_object,
                                            GError       **error);
MMCbmPart *mm_cbm_part_new_from_binary_pdu (const guint8  *pdu,
                                            gsize          pdu_len,
                                            gpointer       log_object,
                                            GError       **error);

MMCbmPart  *mm_cbm_part_new (void);
void        mm_cbm_part_free (MMCbmPart *part);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCbmPart, mm_cbm_part_free)

guint       mm_cbm_part_get_part_num (MMCbmPart *part);
guint       mm_cbm_part_get_num_parts (MMCbmPart *part);
const char *mm_cbm_part_get_text (MMCbmPart *part);

guint16     mm_cbm_part_get_serial (MMCbmPart *part);
guint16     mm_cbm_part_get_channel (MMCbmPart *part);
const char *mm_cbm_part_get_language (MMCbmPart *part);

#endif
