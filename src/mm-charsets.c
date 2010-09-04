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
                                    const char *utf8,
                                    gboolean quoted,
                                    MMModemCharset charset)
{
    const char *iconv_to;
    char *converted;
    GError *error = NULL;
    gsize written = 0;

    g_return_val_if_fail (array != NULL, FALSE);
    g_return_val_if_fail (utf8 != NULL, FALSE);

    iconv_to = charset_iconv_to (charset);
    g_return_val_if_fail (iconv_to != NULL, FALSE);

    converted = g_convert (utf8, -1, iconv_to, "UTF-8", NULL, &written, &error);
    if (!converted) {
        if (error) {
            g_warning ("%s: failed to convert '%s' to %s character set: (%d) %s",
                       __func__, utf8, iconv_to,
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
    char *unconverted, *converted;
    const char *iconv_from;
    gsize unconverted_len = 0;
    GError *error = NULL;

    g_return_val_if_fail (src != NULL, NULL);
    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    iconv_from = charset_iconv_from (charset);
    g_return_val_if_fail (iconv_from != NULL, FALSE);

    unconverted = utils_hexstr2bin (src, &unconverted_len);
    g_return_val_if_fail (unconverted != NULL, NULL);

    if (charset == MM_MODEM_CHARSET_UTF8 || charset == MM_MODEM_CHARSET_IRA)
        return unconverted;

    converted = g_convert (unconverted, unconverted_len,
                           "UTF-8//TRANSLIT", iconv_from,
                           NULL, NULL, &error);
    if (!converted || error) {
        g_clear_error (&error);
        g_free (unconverted);
        converted = NULL;
    }

    return converted;
}


/* GSM 03.38 encoding conversion stuff */

#define GSM_DEF_ALPHABET_SIZE 128
#define GSM_EXT_ALPHABET_SIZE 10

typedef struct GsmUtf8Mapping {
    gchar chars[3];
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
gsm_def_char_to_utf8 (const guint8 gsm, guint8 out_utf8[2])
{
    g_return_val_if_fail (gsm < GSM_DEF_ALPHABET_SIZE, 0);
    memcpy (&out_utf8[0], &gsm_def_utf8_alphabet[gsm].chars[0], gsm_def_utf8_alphabet[gsm].len);
    return gsm_def_utf8_alphabet[gsm].len;
}

static gboolean
utf8_to_gsm_def_char (const char *utf8, guint32 len, guint8 *out_gsm)
{
    int i;

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
gsm_ext_char_to_utf8 (const guint8 gsm, guint8 out_utf8[3])
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
utf8_to_gsm_ext_char (const char *utf8, guint32 len, guint8 *out_gsm)
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
mm_charset_gsm_unpacked_to_utf8 (const guint8 *gsm, guint32 len)
{
    int i;
    GByteArray *utf8;

    g_return_val_if_fail (gsm != NULL, NULL);
    g_return_val_if_fail (len < 4096, NULL);

    /* worst case initial length */
    utf8 = g_byte_array_sized_new (len * 2 + 1);

    for (i = 0; i < len; i++) {
        guint8 uchars[4];
        guint8 ulen;

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

    g_byte_array_append (utf8, (guint8 *) "\0", 1);  /* NULL terminator */
    return g_byte_array_free (utf8, FALSE);
}

guint8 *
mm_charset_utf8_to_unpacked_gsm (const char *utf8, guint32 *out_len)
{
    GByteArray *gsm;
    const char *c = utf8, *next = c;
    static const guint8 gesc = GSM_ESCAPE_CHAR;
    int i = 0;

    g_return_val_if_fail (utf8 != NULL, NULL);
    g_return_val_if_fail (out_len != NULL, NULL);
    g_return_val_if_fail (g_utf8_validate (utf8, -1, NULL), NULL);

    /* worst case initial length */
    gsm = g_byte_array_sized_new (g_utf8_strlen (utf8, -1) * 2 + 1);

    if (*utf8 == 0x00) {
        /* Zero-length string */
        g_byte_array_append (gsm, (guint8 *) "\0", 1);
        *out_len = 0;
        return g_byte_array_free (gsm, FALSE);
    }

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
        i++;
    }

    *out_len = gsm->len;
    return g_byte_array_free (gsm, FALSE);
}

guint8 *
gsm_unpack (const guint8 *gsm,
            guint32 gsm_len,
            guint8 start_offset,  /* in _bits_ */
            guint32 *out_unpacked_len)
{
    GByteArray *unpacked;
    int i, nchars;

    nchars = ((gsm_len * 8) - start_offset) / 7;
    unpacked = g_byte_array_sized_new (nchars + 1);

    for (i = 0; i < nchars; i++) {
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
gsm_pack (const guint8 *src,
          guint32 src_len,
          guint8 start_offset,
          guint32 *out_packed_len)
{
    GByteArray *packed;
    guint8 c, add_last = 0;
    int i;

    packed = g_byte_array_sized_new (src_len);

    for (i = 0, c = 0; i < src_len; i++) {
        guint8 bits_here, offset;
        guint32 start_bit;

        start_bit = start_offset + (i * 7); /* Overall bit offset of char in buffer */
        offset = start_bit % 8; /* Offset to start of char in this byte */
        bits_here = offset ? (8 - offset) : 7;

        c |= (src[i] & 0x7F) << offset;
        if (offset) {
            /* Add this packed byte */
            g_byte_array_append (packed, &c, 1);
            c = add_last = 0;
        }

        /* Pack the rest of this char into the next byte */
        if (bits_here != 7) {
            c = (src[i] & 0x7F) >> bits_here;
            add_last = 1;
        }
    }
    if (add_last)
        g_byte_array_append (packed, &c, 1);

    *out_packed_len = packed->len;
    return g_byte_array_free (packed, FALSE);
}

