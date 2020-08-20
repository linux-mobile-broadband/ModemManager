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
 * Copyright (C) 2010 Red Hat, Inc.
 */

#ifndef MM_CHARSETS_H
#define MM_CHARSETS_H

#include <glib.h>

typedef enum {
    MM_MODEM_CHARSET_UNKNOWN = 0x00000000,
    MM_MODEM_CHARSET_GSM     = 0x00000001,
    MM_MODEM_CHARSET_IRA     = 0x00000002,
    MM_MODEM_CHARSET_8859_1  = 0x00000004,
    MM_MODEM_CHARSET_UTF8    = 0x00000008,
    MM_MODEM_CHARSET_UCS2    = 0x00000010,
    MM_MODEM_CHARSET_PCCP437 = 0x00000020,
    MM_MODEM_CHARSET_PCDN    = 0x00000040,
    MM_MODEM_CHARSET_HEX     = 0x00000080,
    MM_MODEM_CHARSET_UTF16   = 0x00000100,
} MMModemCharset;

const gchar    *mm_modem_charset_to_string   (MMModemCharset  charset);
MMModemCharset  mm_modem_charset_from_string (const gchar    *string);

/* Append the given string to the given byte array but re-encode it
 * into the given charset first.  The original string is assumed to be
 * UTF-8 encoded.
 */
gboolean mm_modem_charset_byte_array_append (GByteArray      *array,
                                             const gchar     *utf8,
                                             gboolean         quoted,
                                             MMModemCharset   charset,
                                             GError         **error);

/* Take a string encoded in the given charset in binary form, and
 * convert it to UTF-8. */
gchar *mm_modem_charset_byte_array_to_utf8 (GByteArray     *array,
                                            MMModemCharset  charset);

/* Take a string in hex representation ("00430052" or "A4BE11" for example)
 * and convert it from the given character set to UTF-8.
 */
gchar *mm_modem_charset_hex_to_utf8 (const gchar    *src,
                                    MMModemCharset  charset);

/* Take a string in UTF-8 and convert it to the given charset in hex
 * representation.
 */
gchar *mm_modem_charset_utf8_to_hex (const gchar    *src,
                                     MMModemCharset  charset);

guint8 *mm_charset_utf8_to_unpacked_gsm (const gchar  *utf8,
                                         guint32      *out_len);
guint8 *mm_charset_gsm_unpacked_to_utf8 (const guint8 *gsm,
                                         guint32       len);

/* Checks whether conversion to the given charset may be done without errors */
gboolean mm_charset_can_convert_to (const gchar    *utf8,
                                    MMModemCharset  charset);

guint8 *mm_charset_gsm_unpack (const guint8 *gsm,
                               guint32       num_septets,
                               guint8        start_offset,  /* in bits */
                               guint32      *out_unpacked_len);

guint8 *mm_charset_gsm_pack (const guint8 *src,
                             guint32       src_len,
                             guint8        start_offset,  /* in bits */
                             guint32      *out_packed_len);

gchar *mm_charset_take_and_convert_to_utf8 (gchar          *str,
                                            MMModemCharset  charset);
gchar *mm_utf8_take_and_convert_to_charset (gchar          *str,
                                            MMModemCharset  charset);

#endif /* MM_CHARSETS_H */
