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

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm-charsets.h"
#include "mm-utils.h"

typedef struct {
    const char *gsm_name;
    const char *other_name;
    const char *iconv_from_name;
    const char *iconv_to_name;
    MMModemCharset charset;
} CharsetEntry;

static CharsetEntry charset_map[] = {
    { "UTF-8",   "UTF8",   "UTF-8",     "UTF-8//TRANSLIT",     MM_MODEM_CHARSET_UTF8 },
    { "UCS2",    NULL,     "UCS-2BE",   "UCS-2BE//TRANSLIT",   MM_MODEM_CHARSET_UCS2 },
    { "IRA",     "ASCII",  "ASCII",     "ASCII//TRANSLIT",     MM_MODEM_CHARSET_IRA },
    { "GSM",     NULL,     NULL,        NULL,                  MM_MODEM_CHARSET_GSM },
    { "8859-1",  NULL,     "ISO8859-1", "ISO8859-1//TRANSLIT", MM_MODEM_CHARSET_8859_1 },
    { "PCCP437", NULL,     NULL,        NULL,                  MM_MODEM_CHARSET_PCCP437 },
    { "PCDN",    NULL,     NULL,        NULL,                  MM_MODEM_CHARSET_PCDN },
    { "HEX",     NULL,     NULL,        NULL,                  MM_MODEM_CHARSET_HEX },
    { NULL,      NULL,     NULL,        NULL,                  MM_MODEM_CHARSET_UNKNOWN }
};

const char *
mm_modem_charset_to_string (MMModemCharset charset)
{
    CharsetEntry *iter = &charset_map[0];

    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    while (iter->gsm_name) {
        if (iter->charset == charset)
            return iter->gsm_name;
        iter++;
    }
    g_warn_if_reached ();
    return NULL;
}

MMModemCharset
mm_modem_charset_from_string (const char *string)
{
    CharsetEntry *iter = &charset_map[0];

    g_return_val_if_fail (string != NULL, MM_MODEM_CHARSET_UNKNOWN);

    while (iter->gsm_name) {
        if (strcasestr (string, iter->gsm_name))
            return iter->charset;
        if (iter->other_name && strcasestr (string, iter->other_name))
            return iter->charset;
        iter++;
    }
    return MM_MODEM_CHARSET_UNKNOWN;
}

static const char *
charset_iconv_to (MMModemCharset charset)
{
    CharsetEntry *iter = &charset_map[0];

    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    while (iter->gsm_name) {
        if (iter->charset == charset)
            return iter->iconv_to_name;
        iter++;
    }
    g_warn_if_reached ();
    return NULL;
}

static const char *
charset_iconv_from (MMModemCharset charset)
{
    CharsetEntry *iter = &charset_map[0];

    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    while (iter->gsm_name) {
        if (iter->charset == charset)
            return iter->iconv_from_name;
        iter++;
    }
    g_warn_if_reached ();
    return NULL;
}

gboolean
mm_modem_charset_byte_array_append (GByteArray *array,
                                    const char *string,
                                    gboolean quoted,
                                    MMModemCharset charset)
{
    const char *iconv_to;
    char *converted;
    GError *error = NULL;
    gsize written = 0;

    g_return_val_if_fail (array != NULL, FALSE);
    g_return_val_if_fail (string != NULL, FALSE);

    iconv_to = charset_iconv_to (charset);
    g_return_val_if_fail (iconv_to != NULL, FALSE);

    converted = g_convert (string,
                           g_utf8_strlen (string, -1),
                           iconv_to,
                           "UTF-8",
                           NULL,
                           &written,
                           &error);
    if (!converted) {
        if (error) {
            g_warning ("%s: failed to convert '%s' to %s character set: (%d) %s",
                       __func__, string, iconv_to,
                       error->code, error->message);
            g_error_free (error);
        }
        return FALSE;
    }

    if (quoted)
        g_byte_array_append (array, (const guint8 *) "\"", 1);
    g_byte_array_append (array, (const guint8 *) converted, written);
    if (quoted)
        g_byte_array_append (array, (const guint8 *) "\"", 1);

    g_free (converted);
    return TRUE;
}

char *
mm_modem_charset_hex_to_utf8 (const char *src, MMModemCharset charset)
{
    char *unconverted;
    const char *iconv_from;
    gsize unconverted_len = 0;

    g_return_val_if_fail (src != NULL, NULL);
    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    iconv_from = charset_iconv_from (charset);
    g_return_val_if_fail (iconv_from != NULL, FALSE);

    unconverted = utils_hexstr2bin (src, &unconverted_len);
    g_return_val_if_fail (unconverted != NULL, NULL);

    if (charset == MM_MODEM_CHARSET_UTF8 || charset == MM_MODEM_CHARSET_IRA)
        return unconverted;

    return g_convert (unconverted, unconverted_len, "UTF-8//TRANSLIT", iconv_from, NULL, NULL, NULL);
}

