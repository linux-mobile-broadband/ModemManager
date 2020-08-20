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
#include <ctype.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-charsets.h"
#include "mm-log.h"

typedef struct {
    const gchar    *gsm_name;
    const gchar    *other_name;
    const gchar    *iconv_from_name;
    const gchar    *iconv_to_name;
    MMModemCharset  charset;
} CharsetEntry;

static const CharsetEntry charset_map[] = {
    { "UTF-8",   "UTF8",   "UTF-8",     "UTF-8//TRANSLIT",     MM_MODEM_CHARSET_UTF8    },
    { "UCS2",    NULL,     "UCS-2BE",   "UCS-2BE//TRANSLIT",   MM_MODEM_CHARSET_UCS2    },
    { "IRA",     "ASCII",  "ASCII",     "ASCII//TRANSLIT",     MM_MODEM_CHARSET_IRA     },
    { "GSM",     NULL,     NULL,        NULL,                  MM_MODEM_CHARSET_GSM     },
    { "8859-1",  NULL,     "ISO8859-1", "ISO8859-1//TRANSLIT", MM_MODEM_CHARSET_8859_1  },
    { "PCCP437", "CP437",  "CP437",     "CP437//TRANSLIT",     MM_MODEM_CHARSET_PCCP437 },
    { "PCDN",    "CP850",  "CP850",     "CP850//TRANSLIT",     MM_MODEM_CHARSET_PCDN    },
    { "HEX",     NULL,     NULL,        NULL,                  MM_MODEM_CHARSET_HEX     },
    { "UTF-16",  "UTF16",  "UTF-16BE",  "UTF-16BE//TRANSLIT",  MM_MODEM_CHARSET_UTF16   },
};

MMModemCharset
mm_modem_charset_from_string (const gchar *string)
{
    guint i;

    g_return_val_if_fail (string != NULL, MM_MODEM_CHARSET_UNKNOWN);

    for (i = 0; i < G_N_ELEMENTS (charset_map); i++) {
        if (strcasestr (string, charset_map[i].gsm_name))
            return charset_map[i].charset;
        if (charset_map[i].other_name && strcasestr (string, charset_map[i].other_name))
            return charset_map[i].charset;
    }
    return MM_MODEM_CHARSET_UNKNOWN;
}

static const CharsetEntry *
lookup_charset_by_id (MMModemCharset charset)
{
    guint i;

    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);
    for (i = 0; i < G_N_ELEMENTS (charset_map); i++) {
        if (charset_map[i].charset == charset)
            return &charset_map[i];
    }
    g_warn_if_reached ();
    return NULL;
}

const gchar *
mm_modem_charset_to_string (MMModemCharset charset)
{
    const CharsetEntry *entry;

    entry = lookup_charset_by_id (charset);
    return entry ? entry->gsm_name : NULL;
}

static const gchar *
charset_iconv_to (MMModemCharset charset)
{
    const CharsetEntry *entry;

    entry = lookup_charset_by_id (charset);
    return entry ? entry->iconv_to_name : NULL;
}

static const gchar *
charset_iconv_from (MMModemCharset charset)
{
    const CharsetEntry *entry;

    entry = lookup_charset_by_id (charset);
    return entry ? entry->iconv_from_name : NULL;
}

gboolean
mm_modem_charset_byte_array_append (GByteArray      *array,
                                    const gchar     *utf8,
                                    gboolean         quoted,
                                    MMModemCharset   charset,
                                    GError         **error)
{
    g_autofree gchar *converted = NULL;
    const gchar      *iconv_to;
    gsize             written = 0;

    g_return_val_if_fail (array != NULL, FALSE);
    g_return_val_if_fail (utf8 != NULL, FALSE);

    iconv_to = charset_iconv_to (charset);
    g_assert (iconv_to);

    converted = g_convert (utf8, -1, iconv_to, "UTF-8", NULL, &written, error);
    if (!converted) {
        g_prefix_error (error, "Failed to convert '%s' to %s character set",
                        utf8, iconv_to);
        return FALSE;
    }

    if (quoted)
        g_byte_array_append (array, (const guint8 *) "\"", 1);
    g_byte_array_append (array, (const guint8 *) converted, written);
    if (quoted)
        g_byte_array_append (array, (const guint8 *) "\"", 1);

    return TRUE;
}

gchar *
mm_modem_charset_byte_array_to_utf8 (GByteArray     *array,
                                     MMModemCharset  charset)
{
    const gchar       *iconv_from;
    g_autofree gchar  *converted = NULL;
    g_autoptr(GError)  error = NULL;

    g_return_val_if_fail (array != NULL, NULL);
    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    iconv_from = charset_iconv_from (charset);
    g_return_val_if_fail (iconv_from != NULL, FALSE);

    converted = g_convert ((const gchar *)array->data, array->len,
                           "UTF-8//TRANSLIT", iconv_from,
                           NULL, NULL, &error);
    if (!converted || error)
        return NULL;

    return g_steal_pointer (&converted);
}

gchar *
mm_modem_charset_hex_to_utf8 (const gchar    *src,
                              MMModemCharset  charset)
{
    const gchar      *iconv_from;
    g_autofree gchar *unconverted = NULL;
    g_autofree gchar *converted = NULL;
    g_autoptr(GError) error = NULL;
    gsize             unconverted_len = 0;

    g_return_val_if_fail (src != NULL, NULL);
    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    iconv_from = charset_iconv_from (charset);
    g_return_val_if_fail (iconv_from != NULL, FALSE);

    unconverted = mm_utils_hexstr2bin (src, &unconverted_len);
    if (!unconverted)
        return NULL;

    if (charset == MM_MODEM_CHARSET_UTF8 || charset == MM_MODEM_CHARSET_IRA)
        return g_steal_pointer (&unconverted);

    converted = g_convert (unconverted, unconverted_len,
                           "UTF-8//TRANSLIT", iconv_from,
                           NULL, NULL, &error);
    if (!converted || error)
        return NULL;

    return g_steal_pointer (&converted);
}

gchar *
mm_modem_charset_utf8_to_hex (const gchar    *src,
                              MMModemCharset  charset)
{
    const gchar      *iconv_to;
    g_autofree gchar *converted = NULL;
    g_autoptr(GError) error = NULL;
    gsize             converted_len = 0;

    g_return_val_if_fail (src != NULL, NULL);
    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    iconv_to = charset_iconv_from (charset);
    g_return_val_if_fail (iconv_to != NULL, FALSE);

    if (charset == MM_MODEM_CHARSET_UTF8 || charset == MM_MODEM_CHARSET_IRA)
        return g_strdup (src);

    converted = g_convert (src, strlen (src),
                           iconv_to, "UTF-8//TRANSLIT",
                           NULL, &converted_len, &error);
    if (!converted || error)
        return NULL;

    /* Get hex representation of the string */
    return mm_utils_bin2hexstr ((guint8 *)converted, converted_len);
}

/* GSM 03.38 encoding conversion stuff */

#define GSM_DEF_ALPHABET_SIZE 128
#define GSM_EXT_ALPHABET_SIZE 10

typedef struct GsmUtf8Mapping {
    gchar  chars[3];
    guint8 len;
    guint8 gsm;  /* only used for extended GSM charset */
} GsmUtf8Mapping;

#define ONE(a)     { {a, 0x00, 0x00}, 1, 0 }
#define TWO(a, b)  { {a, b,    0x00}, 2, 0 }

/**
 * gsm_def_utf8_alphabet:
 *
 * Mapping from GSM default alphabet to UTF-8.
 *
 * ETSI GSM 03.38, version 6.0.1, section 6.2.1; Default alphabet. Mapping to UCS-2.
 * Mapping according to http://unicode.org/Public/MAPPINGS/ETSI/GSM0338.TXT
 */
static const GsmUtf8Mapping gsm_def_utf8_alphabet[GSM_DEF_ALPHABET_SIZE] = {
    /* @             £                $                ¥   */
    ONE(0x40),       TWO(0xc2, 0xa3), ONE(0x24),       TWO(0xc2, 0xa5),
    /* è             é                ù                ì   */
    TWO(0xc3, 0xa8), TWO(0xc3, 0xa9), TWO(0xc3, 0xb9), TWO(0xc3, 0xac),
    /* ò             Ç                \n               Ø   */
    TWO(0xc3, 0xb2), TWO(0xc3, 0x87), ONE(0x0a),       TWO(0xc3, 0x98),
    /* ø             \r               Å                å   */
    TWO(0xc3, 0xb8), ONE(0x0d),       TWO(0xc3, 0x85), TWO(0xc3, 0xa5),
    /* Δ             _                Φ                Γ   */
    TWO(0xce, 0x94), ONE(0x5f),       TWO(0xce, 0xa6), TWO(0xce, 0x93),
    /* Λ             Ω                Π                Ψ   */
    TWO(0xce, 0x9b), TWO(0xce, 0xa9), TWO(0xce, 0xa0), TWO(0xce, 0xa8),
    /* Σ             Θ                Ξ                Escape Code */
    TWO(0xce, 0xa3), TWO(0xce, 0x98), TWO(0xce, 0x9e), ONE(0xa0),
    /* Æ             æ                ß                É   */
    TWO(0xc3, 0x86), TWO(0xc3, 0xa6), TWO(0xc3, 0x9f), TWO(0xc3, 0x89),
    /* ' '           !                "                #   */
    ONE(0x20),       ONE(0x21),       ONE(0x22),       ONE(0x23),
    /* ¤             %                &                '   */
    TWO(0xc2, 0xa4), ONE(0x25),       ONE(0x26),       ONE(0x27),
    /* (             )                *                +   */
    ONE(0x28),       ONE(0x29),       ONE(0x2a),       ONE(0x2b),
    /* ,             -                .                /   */
    ONE(0x2c),       ONE(0x2d),       ONE(0x2e),       ONE(0x2f),
    /* 0             1                2                3   */
    ONE(0x30),       ONE(0x31),       ONE(0x32),       ONE(0x33),
    /* 4             5                6                7   */
    ONE(0x34),       ONE(0x35),       ONE(0x36),       ONE(0x37),
    /* 8             9                :                ;   */
    ONE(0x38),       ONE(0x39),       ONE(0x3a),       ONE(0x3b),
    /* <             =                >                ?   */
    ONE(0x3c),       ONE(0x3d),       ONE(0x3e),       ONE(0x3f),
    /* ¡             A                B                C   */
    TWO(0xc2, 0xa1), ONE(0x41),       ONE(0x42),       ONE(0x43),
    /* D             E                F                G   */
    ONE(0x44),       ONE(0x45),       ONE(0x46),       ONE(0x47),
    /* H             I                J                K   */
    ONE(0x48),       ONE(0x49),       ONE(0x4a),       ONE(0x4b),
    /* L             M                N                O   */
    ONE(0x4c),       ONE(0x4d),       ONE(0x4e),       ONE(0x4f),
    /* P             Q                R                S   */
    ONE(0x50),       ONE(0x51),       ONE(0x52),       ONE(0x53),
    /* T             U                V                W   */
    ONE(0x54),       ONE(0x55),       ONE(0x56),       ONE(0x57),
    /* X             Y                Z                Ä   */
    ONE(0x58),       ONE(0x59),       ONE(0x5a),       TWO(0xc3, 0x84),
    /* Ö             Ñ                Ü                §   */
    TWO(0xc3, 0x96), TWO(0xc3, 0x91), TWO(0xc3, 0x9c), TWO(0xc2, 0xa7),
    /* ¿             a                b                c   */
    TWO(0xc2, 0xbf), ONE(0x61),       ONE(0x62),       ONE(0x63),
    /* d             e                f                g   */
    ONE(0x64),       ONE(0x65),       ONE(0x66),       ONE(0x67),
    /* h             i                j                k   */
    ONE(0x68),       ONE(0x69),       ONE(0x6a),       ONE(0x6b),
    /* l             m                n                o   */
    ONE(0x6c),       ONE(0x6d),       ONE(0x6e),       ONE(0x6f),
    /* p             q                r                s   */
    ONE(0x70),       ONE(0x71),       ONE(0x72),       ONE(0x73),
    /* t             u                v                w   */
    ONE(0x74),       ONE(0x75),       ONE(0x76),       ONE(0x77),
    /* x             y                z                ä   */
    ONE(0x78),       ONE(0x79),       ONE(0x7a),       TWO(0xc3, 0xa4),
    /* ö             ñ                ü                à   */
    TWO(0xc3, 0xb6), TWO(0xc3, 0xb1), TWO(0xc3, 0xbc), TWO(0xc3, 0xa0)
};

static guint8
gsm_def_char_to_utf8 (const guint8 gsm,
                      guint8       out_utf8[2])
{
    g_return_val_if_fail (gsm < GSM_DEF_ALPHABET_SIZE, 0);
    memcpy (&out_utf8[0], &gsm_def_utf8_alphabet[gsm].chars[0], gsm_def_utf8_alphabet[gsm].len);
    return gsm_def_utf8_alphabet[gsm].len;
}

static gboolean
utf8_to_gsm_def_char (const gchar *utf8,
                      guint32      len,
                      guint8      *out_gsm)
{
    gint i;

    if (len > 0 && len < 4) {
        for (i = 0; i < GSM_DEF_ALPHABET_SIZE; i++) {
            if (gsm_def_utf8_alphabet[i].len == len) {
                if (memcmp (&gsm_def_utf8_alphabet[i].chars[0], utf8, len) == 0) {
                    *out_gsm = i;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}


#define EONE(a, g)        { {a, 0x00, 0x00}, 1, g }
#define ETHR(a, b, c, g)  { {a, b,    c},    3, g }

/**
 * gsm_ext_utf8_alphabet:
 *
 * Mapping from GSM extended alphabet to UTF-8.
 *
 */
static const GsmUtf8Mapping gsm_ext_utf8_alphabet[GSM_EXT_ALPHABET_SIZE] = {
    /* form feed      ^                 {                 }  */
    EONE(0x0c, 0x0a), EONE(0x5e, 0x14), EONE(0x7b, 0x28), EONE(0x7d, 0x29),
    /* \              [                 ~                 ]  */
    EONE(0x5c, 0x2f), EONE(0x5b, 0x3c), EONE(0x7e, 0x3d), EONE(0x5d, 0x3e),
    /* |              €                                      */
    EONE(0x7c, 0x40), ETHR(0xe2, 0x82, 0xac, 0x65)
};

#define GSM_ESCAPE_CHAR 0x1b

static guint8
gsm_ext_char_to_utf8 (const guint8 gsm,
                      guint8       out_utf8[3])
{
    int i;

    for (i = 0; i < GSM_EXT_ALPHABET_SIZE; i++) {
        if (gsm == gsm_ext_utf8_alphabet[i].gsm) {
            memcpy (&out_utf8[0], &gsm_ext_utf8_alphabet[i].chars[0], gsm_ext_utf8_alphabet[i].len);
            return gsm_ext_utf8_alphabet[i].len;
        }
    }
    return 0;
}

static gboolean
utf8_to_gsm_ext_char (const gchar *utf8,
                      guint32      len,
                      guint8      *out_gsm)
{
    int i;

    if (len > 0 && len < 4) {
        for (i = 0; i < GSM_EXT_ALPHABET_SIZE; i++) {
            if (gsm_ext_utf8_alphabet[i].len == len) {
                if (memcmp (&gsm_ext_utf8_alphabet[i].chars[0], utf8, len) == 0) {
                    *out_gsm = gsm_ext_utf8_alphabet[i].gsm;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

guint8 *
mm_charset_gsm_unpacked_to_utf8 (const guint8 *gsm,
                                 guint32       len)
{
    guint       i;
    GByteArray *utf8;

    g_return_val_if_fail (gsm != NULL, NULL);
    g_return_val_if_fail (len < 4096, NULL);

    /* worst case initial length */
    utf8 = g_byte_array_sized_new (len * 2 + 1);

    for (i = 0; i < len; i++) {
        guint8 uchars[4];
        guint8 ulen;

        /*
         * 	0x00 is NULL (when followed only by 0x00 up to the
         * 	end of (fixed byte length) message, possibly also up to
         * 	FORM FEED.  But 0x00 is also the code for COMMERCIAL AT
         * 	when some other character (CARRIAGE RETURN if nothing else)
         * 	comes after the 0x00.
         *  http://unicode.org/Public/MAPPINGS/ETSI/GSM0338.TXT
         *
         * So, if we find a '@' (0x00) and all the next chars after that
         * are also 0x00, we can consider the string finished already.
         */
        if (gsm[i] == 0x00) {
            gsize j;

            for (j = i + 1; j < len; j++) {
                if (gsm[j] != 0x00)
                    break;
            }
            if (j == len)
                break;
        }

        if (gsm[i] == GSM_ESCAPE_CHAR) {
            /* Extended alphabet, decode next char */
            ulen = gsm_ext_char_to_utf8 (gsm[i+1], uchars);
            if (ulen)
                i += 1;
        } else {
            /* Default alphabet */
            ulen = gsm_def_char_to_utf8 (gsm[i], uchars);
        }

        if (ulen)
            g_byte_array_append (utf8, &uchars[0], ulen);
        else
            g_byte_array_append (utf8, (guint8 *) "?", 1);
    }

    /* Always make sure returned string is NUL terminated */
    g_byte_array_append (utf8, (guint8 *) "\0", 1);
    return g_byte_array_free (utf8, FALSE);
}

guint8 *
mm_charset_utf8_to_unpacked_gsm (const gchar *utf8,
                                 guint32     *out_len)
{
    GByteArray          *gsm;
    const gchar         *c;
    const gchar         *next;
    static const guint8  gesc = GSM_ESCAPE_CHAR;

    g_return_val_if_fail (utf8 != NULL, NULL);
    g_return_val_if_fail (g_utf8_validate (utf8, -1, NULL), NULL);

    /* worst case initial length */
    gsm = g_byte_array_sized_new (g_utf8_strlen (utf8, -1) * 2 + 1);

    if (*utf8 == 0x00) {
        /* Zero-length string */
        g_byte_array_append (gsm, (guint8 *) "\0", 1);
        if (out_len)
            *out_len = 0;
        return g_byte_array_free (gsm, FALSE);
    }

    next = utf8;
    c    = utf8;
    while (next && *next) {
        guint8 gch = 0x3f;  /* 0x3f == '?' */

        next = g_utf8_next_char (c);

        /* Try escaped chars first, then default alphabet */
        if (utf8_to_gsm_ext_char (c, next - c, &gch)) {
            /* Add the escape char */
            g_byte_array_append (gsm, &gesc, 1);
            g_byte_array_append (gsm, &gch, 1);
        } else if (utf8_to_gsm_def_char (c, next - c, &gch))
            g_byte_array_append (gsm, &gch, 1);

        c = next;
    }

    /* Output length doesn't consider terminating NUL byte */
    if (out_len)
        *out_len = gsm->len;

    /* Always make sure returned string is NUL terminated */
    g_byte_array_append (gsm, (guint8 *) "\0", 1);
    return g_byte_array_free (gsm, FALSE);
}

static gboolean
gsm_is_subset (gunichar     c,
               const gchar *utf8,
               gsize        ulen)
{
    guint8 gsm;

    if (utf8_to_gsm_def_char (utf8, ulen, &gsm))
        return TRUE;
    if (utf8_to_gsm_ext_char (utf8, ulen, &gsm))
        return TRUE;
    return FALSE;
}

static gboolean
ira_is_subset (gunichar     c,
               const gchar *utf8,
               gsize        ulen)
{
    return (ulen == 1);
}

static gboolean
ucs2_is_subset (gunichar     c,
                const gchar *utf8,
                gsize        ulen)
{
    return (c <= 0xFFFF);
}

static gboolean
utf16_is_subset (gunichar     c,
                 const gchar *utf8,
                 gsize        ulen)
{
    return TRUE;
}

static gboolean
iso88591_is_subset (gunichar     c,
                    const gchar *utf8,
                    gsize        ulen)
{
    return (c <= 0xFF);
}

static gboolean
pccp437_is_subset (gunichar     c,
                   const gchar *utf8,
                   gsize        ulen)
{
    static const gunichar t[] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea,
        0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, 0x00c9, 0x00e6,
        0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc,
        0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, 0x00e1, 0x00ed, 0x00f3, 0x00fa,
        0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc,
        0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561,
        0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b,
        0x2510, 0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
        0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567, 0x2568,
        0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518,
        0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x03b1, 0x00df, 0x0393,
        0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4,
        0x221e, 0x03c6, 0x03b5, 0x2229, 0x2261, 0x00b1, 0x2265, 0x2264, 0x2320,
        0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2,
        0x25a0, 0x00a0
    };
    guint i;

    if (c <= 0x7F)
        return TRUE;
    for (i = 0; i < G_N_ELEMENTS (t); i++) {
        if (c == t[i])
            return TRUE;
    }
    return FALSE;
}

static gboolean
pcdn_is_subset (gunichar     c,
                const gchar *utf8,
                gsize        ulen)
{
    static const gunichar t[] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea,
        0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, 0x00c9, 0x00e6,
        0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc,
        0x00f8, 0x00a3, 0x00d8, 0x00d7, 0x0192, 0x00e1, 0x00ed, 0x00f3, 0x00fa,
        0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x00ae, 0x00ac, 0x00bd, 0x00bc,
        0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00c1,
        0x00c2, 0x00c0, 0x00a9, 0x2563, 0x2551, 0x2557, 0x255d, 0x00a2, 0x00a5,
        0x2510, 0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x00e3, 0x00c3,
        0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x00a4, 0x00f0,
        0x00d0, 0x00ca, 0x00cb, 0x00c8, 0x0131, 0x00cd, 0x00ce, 0x00cf, 0x2518,
        0x250c, 0x2588, 0x2584, 0x00a6, 0x00cc, 0x2580, 0x00d3, 0x00df, 0x00d4,
        0x00d2, 0x00f5, 0x00d5, 0x00b5, 0x00fe, 0x00de, 0x00da, 0x00db, 0x00d9,
        0x00fd, 0x00dd, 0x00af, 0x00b4, 0x00ad, 0x00b1, 0x2017, 0x00be, 0x00b6,
        0x00a7, 0x00f7, 0x00b8, 0x00b0, 0x00a8, 0x00b7, 0x00b9, 0x00b3, 0x00b2,
        0x25a0, 0x00a0
    };
    guint i;

    if (c <= 0x7F)
        return TRUE;
    for (i = 0; i < sizeof (t) / sizeof (t[0]); i++) {
        if (c == t[i])
            return TRUE;
    }
    return FALSE;
}

typedef struct {
    MMModemCharset cs;
    gboolean (*func) (gunichar     c,
                      const gchar *utf8,
                      gsize        ulen);
} SubsetEntry;

const SubsetEntry subset_table[] = {
    { MM_MODEM_CHARSET_GSM,     gsm_is_subset      },
    { MM_MODEM_CHARSET_IRA,     ira_is_subset      },
    { MM_MODEM_CHARSET_UCS2,    ucs2_is_subset     },
    { MM_MODEM_CHARSET_UTF16,   utf16_is_subset    },
    { MM_MODEM_CHARSET_8859_1,  iso88591_is_subset },
    { MM_MODEM_CHARSET_PCCP437, pccp437_is_subset  },
    { MM_MODEM_CHARSET_PCDN,    pcdn_is_subset     },
};

/**
 * mm_charset_can_covert_to:
 * @utf8: UTF-8 valid string.
 * @charset: the #MMModemCharset to validate the conversion from @utf8.
 *
 * Returns: %TRUE if the conversion is possible without errors, %FALSE otherwise.
 */
gboolean
mm_charset_can_convert_to (const gchar    *utf8,
                           MMModemCharset  charset)
{
    const gchar *p;
    guint        i;

    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, FALSE);
    g_return_val_if_fail (utf8 != NULL, FALSE);

    if (charset == MM_MODEM_CHARSET_UTF8)
        return TRUE;

    /* Find the charset in our subset table */
    for (i = 0; i < G_N_ELEMENTS (subset_table); i++) {
        if (subset_table[i].cs == charset)
            break;
    }
    g_return_val_if_fail (i < G_N_ELEMENTS (subset_table), FALSE);

    p = utf8;
    while (*p) {
        gunichar c;
        const char *end;

        c = g_utf8_get_char_validated (p, -1);
        g_return_val_if_fail (c != (gunichar) -1, 0);
        end = g_utf8_find_next_char (p, NULL);
        if (end == NULL) {
            /* Find the string terminating NULL */
            end = p;
            while (*++end);
        }

        if (!subset_table[i].func (c, p, (end - p)))
            return FALSE;

        p = end;
    }

    return TRUE;
}

guint8 *
mm_charset_gsm_unpack (const guint8 *gsm,
                       guint32       num_septets,
                       guint8        start_offset,  /* in _bits_ */
                       guint32      *out_unpacked_len)
{
    GByteArray *unpacked;
    guint i;

    unpacked = g_byte_array_sized_new (num_septets + 1);

    for (i = 0; i < num_septets; i++) {
        guint8 bits_here, bits_in_next, octet, offset, c;
        guint32 start_bit;

        start_bit = start_offset + (i * 7); /* Overall bit offset of char in buffer */
        offset = start_bit % 8;  /* Offset to start of char in this byte */
        bits_here = offset ? (8 - offset) : 7;
        bits_in_next = 7 - bits_here;

        /* Grab bits in the current byte */
        octet = gsm[start_bit / 8];
        c = (octet >> offset) & (0xFF >> (8 - bits_here));

        /* Grab any bits that spilled over to next byte */
        if (bits_in_next) {
            octet = gsm[(start_bit / 8) + 1];
            c |= (octet & (0xFF >> (8 - bits_in_next))) << bits_here;
        }
        g_byte_array_append (unpacked, &c, 1);
    }

    *out_unpacked_len = unpacked->len;
    return g_byte_array_free (unpacked, FALSE);
}

guint8 *
mm_charset_gsm_pack (const guint8 *src,
                     guint32       src_len,
                     guint8        start_offset,
                     guint32      *out_packed_len)
{
    guint8 *packed;
    guint octet = 0, lshift, plen;
    guint i = 0;

    g_return_val_if_fail (start_offset < 8, NULL);

    plen = (src_len * 7) + start_offset; /* total length in bits */
    if (plen % 8)
        plen += 8;
    plen /= 8;  /* now in bytes */

    packed = g_malloc0 (plen);

    for (i = 0, lshift = start_offset; i < src_len; i++) {
        packed[octet] |= (src[i] & 0x7F) << lshift;
        if (lshift > 1) {
            /* Grab the lost bits and add to next octet */
            g_assert (octet + 1 < plen);
            packed[octet + 1] = (src[i] & 0x7F) >> (8 - lshift);
        }
        if (lshift)
            octet++;
        lshift = lshift ? lshift - 1 : 7;
    }

    if (out_packed_len)
        *out_packed_len = plen;
    return packed;
}

/* We do all our best to get the given string, which is possibly given in the
 * specified charset, to UTF8. It may happen that the given string is really
 * the hex representation of the charset-encoded string, so we need to cope with
 * that case. */
gchar *
mm_charset_take_and_convert_to_utf8 (gchar          *str,
                                     MMModemCharset  charset)
{
    gchar *utf8 = NULL;

    if (!str)
        return NULL;

    switch (charset) {
    case MM_MODEM_CHARSET_UNKNOWN:
        g_warn_if_reached ();
        utf8 = str;
        break;

    case MM_MODEM_CHARSET_HEX:
        /* We'll assume that the HEX string is really valid ASCII at the end */
        utf8 = str;
        break;

    case MM_MODEM_CHARSET_GSM:
        utf8 = (gchar *) mm_charset_gsm_unpacked_to_utf8 ((const guint8 *) str, strlen (str));
        g_free (str);
        break;

    case MM_MODEM_CHARSET_8859_1:
    case MM_MODEM_CHARSET_PCCP437:
    case MM_MODEM_CHARSET_PCDN: {
        const gchar *iconv_from;
        GError *error = NULL;

        iconv_from = charset_iconv_from (charset);
        utf8 = g_convert (str, strlen (str),
                          "UTF-8//TRANSLIT", iconv_from,
                          NULL, NULL, &error);
        if (!utf8 || error) {
            g_clear_error (&error);
            utf8 = NULL;
        }

        g_free (str);
        break;
    }

    case MM_MODEM_CHARSET_UCS2:
    case MM_MODEM_CHARSET_UTF16: {
        gsize len;
        gboolean possibly_hex = TRUE;
        gsize bread = 0, bwritten = 0;

        /* If the string comes in hex-UCS-2, len needs to be a multiple of 4 */
        len = strlen (str);
        if ((len < 4) || ((len % 4) != 0))
            possibly_hex = FALSE;
        else {
            const gchar *p = str;

            /* All chars in the string must be hex */
            while (*p && possibly_hex)
                possibly_hex = isxdigit (*p++);
        }

        /* If hex, then we expect hex-encoded UCS-2 */
        if (possibly_hex) {
            utf8 = mm_modem_charset_hex_to_utf8 (str, charset);
            if (utf8) {
                g_free (str);
                break;
            }
        }

        /* If not hex, then it might be raw UCS-2 (very unlikely) or ASCII/UTF-8
         * (much more likely).  Try to convert to UTF-8 and if that fails, use
         * the partial conversion length to re-convert the part of the string
         * that is UTF-8, if any.
         */
        utf8 = g_convert (str, strlen (str),
                          "UTF-8//TRANSLIT", "UTF-8//TRANSLIT",
                          &bread, &bwritten, NULL);

        /* Valid conversion, or we didn't get enough valid UTF-8 */
        if (utf8 || (bwritten <= 2)) {
            g_free (str);
            break;
        }

        /* Last try; chop off the original string at the conversion failure
         * location and get what we can.
         */
        str[bread] = '\0';
        utf8 = g_convert (str, strlen (str),
                          "UTF-8//TRANSLIT", "UTF-8//TRANSLIT",
                          NULL, NULL, NULL);
        g_free (str);
        break;
    }

    /* If the given charset is ASCII or UTF8, we really expect the final string
     * already here */
    case MM_MODEM_CHARSET_IRA:
    case MM_MODEM_CHARSET_UTF8:
        utf8 = str;
        break;

    default:
        g_assert_not_reached ();
    }

    /* Validate UTF-8 always before returning. This result will be exposed in DBus
     * very likely... */
    if (utf8 && !g_utf8_validate (utf8, -1, NULL)) {
        /* Better return NULL than an invalid UTF-8 string */
        g_free (utf8);
        utf8 = NULL;
    }

    return utf8;
}

/* We do all our best to convert the given string, which comes in UTF-8, to the
 * specified charset. It may be that the output string needs to be the hex
 * representation of the charset-encoded string, so we need to cope with that
 * case. */
gchar *
mm_utf8_take_and_convert_to_charset (gchar          *str,
                                     MMModemCharset  charset)
{
    gchar *encoded = NULL;

    if (!str)
        return NULL;

    /* Validate UTF-8 always before converting */
    if (!g_utf8_validate (str, -1, NULL)) {
        /* Better return NULL than an invalid encoded string */
        g_free (str);
        return NULL;
    }

    switch (charset) {
    case MM_MODEM_CHARSET_UNKNOWN:
        g_warn_if_reached ();
        encoded = str;
        break;

    case MM_MODEM_CHARSET_HEX:
        encoded = str;
        break;

    case MM_MODEM_CHARSET_GSM:
        encoded = (gchar *) mm_charset_utf8_to_unpacked_gsm (str, NULL);
        g_free (str);
        break;

    case MM_MODEM_CHARSET_8859_1:
    case MM_MODEM_CHARSET_PCCP437:
    case MM_MODEM_CHARSET_PCDN: {
        const gchar *iconv_to;
        GError *error = NULL;

        iconv_to = charset_iconv_from (charset);
        encoded = g_convert (str, strlen (str),
                             iconv_to, "UTF-8",
                             NULL, NULL, &error);
        if (!encoded || error) {
            g_clear_error (&error);
            encoded = NULL;
        }

        g_free (str);
        break;
    }

    case MM_MODEM_CHARSET_UCS2:
    case MM_MODEM_CHARSET_UTF16: {
        const gchar *iconv_to;
        gsize encoded_len = 0;
        GError *error = NULL;
        gchar *hex;

        iconv_to = charset_iconv_from (charset);
        encoded = g_convert (str, strlen (str),
                             iconv_to, "UTF-8",
                             NULL, &encoded_len, &error);
        if (!encoded || error) {
            g_clear_error (&error);
            encoded = NULL;
        }

        /* Get hex representation of the string */
        hex = mm_utils_bin2hexstr ((guint8 *)encoded, encoded_len);
        g_free (encoded);
        encoded = hex;
        g_free (str);
        break;
    }

    /* If the given charset is ASCII or UTF8, we really expect the final string
     * already here. */
    case MM_MODEM_CHARSET_IRA:
    case MM_MODEM_CHARSET_UTF8:
        encoded = str;
        break;

    default:
        g_assert_not_reached ();
    }

    return encoded;
}
