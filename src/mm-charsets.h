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

/*****************************************************************************************/

typedef enum {
    MM_MODEM_CHARSET_UNKNOWN = 0,
    MM_MODEM_CHARSET_GSM     = 1 << 0,
    MM_MODEM_CHARSET_IRA     = 1 << 1,
    MM_MODEM_CHARSET_8859_1  = 1 << 2,
    MM_MODEM_CHARSET_UTF8    = 1 << 3,
    MM_MODEM_CHARSET_UCS2    = 1 << 4,
    MM_MODEM_CHARSET_PCCP437 = 1 << 5,
    MM_MODEM_CHARSET_PCDN    = 1 << 6,
    MM_MODEM_CHARSET_UTF16   = 1 << 7,
} MMModemCharset;

const gchar    *mm_modem_charset_to_string   (MMModemCharset  charset);
MMModemCharset  mm_modem_charset_from_string (const gchar    *string);

/*****************************************************************************************/

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

/*****************************************************************************************/

/*
 * Convert the given UTF-8 encoded string into the given charset.
 *
 * The output is given as a bytearray, because the target charset may allow
 * embedded NUL bytes (e.g. UTF-16).
 *
 * The output encoded string is not guaranteed to be NUL-terminated, instead
 * the bytearray length itself gives the correct string length.
 */
GByteArray *mm_modem_charset_bytearray_from_utf8 (const gchar     *utf8,
                                                  MMModemCharset   charset,
                                                  gboolean         translit,
                                                  GError         **error);

/*
 * Convert the given UTF-8 encoded string into the given charset.
 *
 * The output is given as a C string, and those charsets that allow
 * embedded NUL bytes (e.g. UTF-16) will be hex-encoded.
 *
 * The output encoded string is guaranteed to be NUL-terminated, and so no
 * explicit output length is returned.
 */
gchar *mm_modem_charset_str_from_utf8 (const gchar     *utf8,
                                       MMModemCharset   charset,
                                       gboolean         translit,
                                       GError         **error);

/*
 * Convert into an UTF-8 encoded string the input byte array, which is
 * encoded in the given charset.
 *
 * The output string is guaranteed to be valid UTF-8 and NUL-terminated.
 */
gchar *mm_modem_charset_bytearray_to_utf8 (GByteArray      *bytearray,
                                           MMModemCharset   charset,
                                           gboolean         translit,
                                           GError         **error);

/*
 * Convert into an UTF-8 encoded string the input string, which is
 * encoded in the given charset. Those charsets that allow embedded NUL
 * bytes (e.g. UTF-16) need to be hex-encoded.
 *
 * If the input string is NUL-terminated, len may be given as -1; otherwise
 * len needs to specify the number of valid bytes in the input string.
 *
 * The output string is guaranteed to be valid UTF-8 and NUL-terminated.
 */
gchar *mm_modem_charset_str_to_utf8 (const gchar     *str,
                                     gssize           len,
                                     MMModemCharset   charset,
                                     gboolean         translit,
                                     GError         **error);

#endif /* MM_CHARSETS_H */
