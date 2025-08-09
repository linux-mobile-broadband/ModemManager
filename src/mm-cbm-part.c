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

#include <glib.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-common-helpers.h"
#include "mm-helper-enums-types.h"
#include "mm-cbm-part.h"
#include "mm-charsets.h"
#include "mm-log.h"
#include "mm-sms-part-3gpp.h"

#define CBS_DATA_CODING_GROUP_MASK           0b11110000
#define CBS_DATA_CODING_LANG_MASK            0b00001111
#define CBS_DATA_CODING_LANG_GSM7            0b00000000
#define CBS_DATA_CODING_GSM7                 0b00010000
#define CBS_DATA_CODING_UCS2                 0b00010001
#define CBS_DATA_CODING_OTHER                0b00100000
#define CBS_DATA_CODING_OTHER_CZECH          0b00100000
#define CBS_DATA_CODING_OTHER_HEBREW         0b00100001
#define CBS_DATA_CODING_OTHER_ARABIC         0b00100010
#define CBS_DATA_CODING_OTHER_RUSSIAN        0b00100011
#define CBS_DATA_CODING_OTHER_ICELANDIC      0b00100100
#define CBS_DATA_CODING_UNSPECIFIED          0b00110000
#define CBS_DATA_CODING_GENERAL_NO_CLASS     0b01000000
#define CBS_DATA_CODING_GENERAL_CLASS        0b01010000
#define CBS_DATA_CODING_GENERAL_CHARSET_MASK 0b00001100
#define CBS_DATA_CODING_GENERAL_GSM7         0b00000000
#define CBS_DATA_CODING_GENERAL_8BIT         0b00000100
#define CBS_DATA_CODING_GENERAL_UCS2         0b00001000
#define CBS_DATA_CODING_UDH                  0b10010000

struct _MMCbmPart {
    guint16 serial;
    guint16 channel;

    guint8 num_parts;
    guint8 part_num;

    gchar *text;
    gchar *language;
    MMSmsEncoding encoding;
};


static gchar *
mm_cbm_part_coding_group_to_language (guint8 group)
{
    switch (group & CBS_DATA_CODING_LANG_MASK) {
    case 0b0000:
        return g_strdup ("de");
    case 0b0001:
        return g_strdup ("en");
    case 0b0010:
        return g_strdup ("it");
    case 0b0011:
        return g_strdup ("fr");
    case 0b0100:
        return g_strdup ("es");
    case 0b0101:
        return g_strdup ("nl");
    case 0b0110:
        return g_strdup ("sv");
    case 0b0111:
        return g_strdup ("da");
    case 0b1000:
        return g_strdup ("pt");
    case 0b1001:
        return g_strdup ("fi");
    case 0b1010:
        return g_strdup ("no");
    case 0b1011:
        return g_strdup ("el");
    case 0b1110:
        return g_strdup ("pl");
    case 0b1111:
    default:
        return NULL;
    };
}


MMCbmPart *
mm_cbm_part_new_from_pdu (const gchar  *hexpdu,
                          gpointer      log_object,
                          GError      **error)
{
    g_autofree guint8 *pdu = NULL;
    gsize              pdu_len;

    /* Convert PDU from hex to binary */
    pdu = mm_utils_hexstr2bin (hexpdu, -1, &pdu_len, error);
    if (!pdu) {
        g_prefix_error (error, "Couldn't convert 3GPP PDU from hex to binary: ");
        return NULL;
    }

    return mm_cbm_part_new_from_binary_pdu (pdu, pdu_len, log_object, error);
}

MMCbmPart *
mm_cbm_part_new_from_binary_pdu (const guint8  *pdu,
                                 gsize          pdu_len,
                                 gpointer       log_object,
                                 GError       **error)
{
    g_autoptr (MMCbmPart) cbm_part = NULL;
    MMCbmGeoScope scope;
    guint offset = 0;
    guint16 serial, group;
    int len;
    g_autofree gchar *text = NULL;
    gboolean has_lang = FALSE, has_7bit_lang = FALSE;

    cbm_part = mm_cbm_part_new ();

    mm_obj_dbg (log_object, "parsing CBM...");

#define PDU_SIZE_CHECK(required_size, check_descr_str)                 \
    if (pdu_len < required_size) {                                     \
        g_set_error (error,                                            \
                     MM_CORE_ERROR,                                    \
                     MM_CORE_ERROR_FAILED,                             \
                     "PDU too short, %s: %" G_GSIZE_FORMAT " < %u",    \
                     check_descr_str,                                  \
                     pdu_len,                                          \
                     required_size);                                   \
        return NULL;                                                   \
    }

    /* Serial number (2 bytes) */
    PDU_SIZE_CHECK (offset + 2, "cannot read serial number");
    serial = pdu[offset] << 8 | pdu[offset+1];
    scope = CBM_SERIAL_GEO_SCOPE (serial);
    switch (scope) {
    case MM_CBM_GEO_SCOPE_CELL_NORMAL:
        mm_obj_dbg (log_object, "  normal cell cbm scope");
        break;
    case MM_CBM_GEO_SCOPE_PLMN:
        mm_obj_dbg (log_object, "  plmn cbm scope");
        break;
    case MM_CBM_GEO_SCOPE_AREA:
        mm_obj_dbg (log_object, "  area cbm scope");
        break;
    case MM_CBM_GEO_SCOPE_CELL_IMMEDIATE:
        mm_obj_dbg (log_object, "  immediate cell cbm scope");
        break;
    default:
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unhandled cbm message scope: 0x%02x",
                     scope);
        return NULL;
    }
    cbm_part->serial = serial;
    offset += 2;

    /* Channel / Message identifier */
    PDU_SIZE_CHECK (offset + 2, "cannot read channel");
    cbm_part->channel = pdu[offset] << 8 | pdu[offset+1];
    offset += 2;

    PDU_SIZE_CHECK (offset + 1, "cannot read encoding scheme");
    group = pdu[offset] & CBS_DATA_CODING_GROUP_MASK;
    /* Order matches 3GPP TS 23.038 Chapter 5 */
    if (group == CBS_DATA_CODING_LANG_GSM7) {
        cbm_part->encoding = MM_SMS_ENCODING_GSM7;
        cbm_part->language = mm_cbm_part_coding_group_to_language (pdu[offset]);
    } else if (pdu[offset] == CBS_DATA_CODING_GSM7) {
        has_lang = TRUE;
        cbm_part->encoding = MM_SMS_ENCODING_GSM7;
    } else if (pdu[offset] == CBS_DATA_CODING_UCS2) {
        has_7bit_lang = TRUE;
        cbm_part->encoding = MM_SMS_ENCODING_UCS2;
    } else if (group == CBS_DATA_CODING_OTHER) {
        switch (group) {
        case CBS_DATA_CODING_OTHER_CZECH:
            cbm_part->encoding = MM_SMS_ENCODING_GSM7;
            cbm_part->language = g_strdup ("cs");
            break;
        case CBS_DATA_CODING_OTHER_HEBREW:
            cbm_part->encoding = MM_SMS_ENCODING_UCS2;
            cbm_part->language = g_strdup ("he");
            break;
        case CBS_DATA_CODING_OTHER_ARABIC:
            cbm_part->encoding = MM_SMS_ENCODING_UCS2;
            cbm_part->language = g_strdup ("ar");
            break;
        case CBS_DATA_CODING_OTHER_RUSSIAN:
            cbm_part->encoding = MM_SMS_ENCODING_UCS2;
            cbm_part->language = g_strdup ("ru");
            break;
        case CBS_DATA_CODING_OTHER_ICELANDIC:
            cbm_part->encoding = MM_SMS_ENCODING_GSM7;
            cbm_part->language = g_strdup ("is");
            break;
        default:
            cbm_part->encoding = MM_SMS_ENCODING_GSM7;
        }
    } else if (group == CBS_DATA_CODING_UNSPECIFIED) {
        cbm_part->encoding = MM_SMS_ENCODING_GSM7;
    } else if ((group == CBS_DATA_CODING_GENERAL_CLASS) ||
               (group == CBS_DATA_CODING_GENERAL_NO_CLASS) ||
               (group == CBS_DATA_CODING_UDH)) {
        guint16 charset = pdu[offset] & CBS_DATA_CODING_GENERAL_CHARSET_MASK;
        /* We don't handle compression or 8 bit data */
        if (charset == CBS_DATA_CODING_GENERAL_GSM7)
            cbm_part->encoding = MM_SMS_ENCODING_GSM7;
        else if (charset == CBS_DATA_CODING_GENERAL_UCS2)
            cbm_part->encoding = MM_SMS_ENCODING_UCS2;
    } else {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unhandled cbm message encoding: 0x%02x",
                     pdu[offset]);
        return NULL;
    }
    offset++;

    PDU_SIZE_CHECK (offset + 1, "cannot read page parameter");
    cbm_part->num_parts = (pdu[offset] & 0x0F);
    cbm_part->part_num = (pdu[offset] & 0xF0) >> 4;
    offset++;

    if (has_lang) {
        PDU_SIZE_CHECK (offset + 4, "cannot read lang");
        if (pdu[offset+2] != '\r') {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse lang");
            return NULL;
        }
        cbm_part->language = g_strdup_printf ("%c%c", pdu[offset], pdu[offset+1]);
        offset += 3;
    } else if (has_7bit_lang) {
        PDU_SIZE_CHECK (offset + 3, "cannot read 7bit lang");
        cbm_part->language = mm_sms_decode_text (&pdu[offset],
                                                 2,
                                                 MM_SMS_ENCODING_GSM7,
                                                 0,
                                                 log_object,
                                                 NULL);
        if (!cbm_part->language) {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse lang");
            return NULL;
        }
        offset += 2;
    }

    switch (cbm_part->encoding) {
    case MM_SMS_ENCODING_GSM7:
        len = ((pdu_len - offset) * 8) / 7;
        break;
    case MM_SMS_ENCODING_UCS2:
        len = pdu_len - offset;
        break;
    case MM_SMS_ENCODING_8BIT:
    case MM_SMS_ENCODING_UNKNOWN:
    default:
        g_assert_not_reached ();
    }
    PDU_SIZE_CHECK (offset + 1, "cannot read message text");
    text = mm_sms_decode_text (&pdu[offset],
                               len,
                               cbm_part->encoding,
                               0,
                               log_object,
                               error);
    if (!text) {
        return NULL;
    }
    cbm_part->text = g_steal_pointer (&text);

    return g_steal_pointer (&cbm_part);
}

MMCbmPart *
mm_cbm_part_new (void)
{
    return g_slice_new0 (MMCbmPart);
}

void
mm_cbm_part_free (MMCbmPart *part)
{
  g_clear_pointer (&part->language, g_free);
  g_clear_pointer (&part->text, g_free);
  g_slice_free (MMCbmPart, part);
}

#define PART_GET_FUNC(type, name)             \
    type                                      \
    mm_cbm_part_get_##name (MMCbmPart *self)  \
    {                                         \
        return self->name;                    \
    }

PART_GET_FUNC (guint, part_num)
PART_GET_FUNC (guint, num_parts)
PART_GET_FUNC (const char *, text)
PART_GET_FUNC (guint16, channel)
PART_GET_FUNC (guint16, serial)
PART_GET_FUNC (const char *, language)
