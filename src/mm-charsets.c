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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
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

/* Common fallback character when transliteration is enabled */
static const gchar *translit_fallback = "?";

/******************************************************************************/
/* Expected charset settings */

typedef struct {
    MMModemCharset  charset;
    const gchar    *gsm_name;
    const gchar    *other_name;
    const gchar    *iconv_name;
} CharsetSettings;

static const CharsetSettings charset_settings[] = {
    { MM_MODEM_CHARSET_UTF8,    "UTF-8",   "UTF8",   "UTF-8"     },
    { MM_MODEM_CHARSET_UCS2,    "UCS2",    NULL,     "UCS-2BE"   },
    { MM_MODEM_CHARSET_IRA,     "IRA",     "ASCII",  "ASCII"     },
    { MM_MODEM_CHARSET_GSM,     "GSM",     NULL,     NULL        },
    { MM_MODEM_CHARSET_8859_1,  "8859-1",  NULL,     "ISO8859-1" },
    { MM_MODEM_CHARSET_PCCP437, "PCCP437", "CP437",  "CP437"     },
    { MM_MODEM_CHARSET_PCDN,    "PCDN",    "CP850",  "CP850"     },
    { MM_MODEM_CHARSET_UTF16,   "UTF-16",  "UTF16",  "UTF-16BE"  },
};

MMModemCharset
mm_modem_charset_from_string (const gchar *string)
{
    guint i;

    g_return_val_if_fail (string != NULL, MM_MODEM_CHARSET_UNKNOWN);

    for (i = 0; i < G_N_ELEMENTS (charset_settings); i++) {
        if (strcasestr (string, charset_settings[i].gsm_name))
            return charset_settings[i].charset;
        if (charset_settings[i].other_name && strcasestr (string, charset_settings[i].other_name))
            return charset_settings[i].charset;
    }
    return MM_MODEM_CHARSET_UNKNOWN;
}

static const CharsetSettings *
lookup_charset_settings (MMModemCharset charset)
{
    guint i;

    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);
    for (i = 0; i < G_N_ELEMENTS (charset_settings); i++) {
        if (charset_settings[i].charset == charset)
            return &charset_settings[i];
    }
    g_warn_if_reached ();
    return NULL;
}

const gchar *
mm_modem_charset_to_string (MMModemCharset charset)
{
    const CharsetSettings *settings;

    settings = lookup_charset_settings (charset);
    return settings ? settings->gsm_name : NULL;
}

/******************************************************************************/
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

static gboolean
translit_gsm_nul_byte (GByteArray *gsm)
{
    guint i;
    guint n_replaces = 0;

    for (i = 0; i < gsm->len; i++) {
        if (gsm->data[i] == 0x00) {
            utf8_to_gsm_def_char (translit_fallback, strlen (translit_fallback), &gsm->data[i]);
            n_replaces++;
        }
    }

    return (n_replaces > 0);
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

static guint8 *
charset_gsm_unpacked_to_utf8 (const guint8  *gsm,
                              guint32        len,
                              gboolean       translit,
                              GError       **error)
{
    g_autoptr(GByteArray) utf8 = NULL;
    guint                 i;

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
        else if (translit)
            g_byte_array_append (utf8, (guint8 *) translit_fallback, strlen (translit_fallback));
        else {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                         "Invalid conversion from GSM7");
            return NULL;
        }
    }

    /* Always make sure returned string is NUL terminated */
    g_byte_array_append (utf8, (guint8 *) "\0", 1);
    return g_byte_array_free (g_steal_pointer (&utf8), FALSE);
}

static guint8 *
charset_utf8_to_unpacked_gsm (const gchar  *utf8,
                              gboolean      translit,
                              guint32      *out_len,
                              GError      **error)
{
    g_autoptr(GByteArray)  gsm = NULL;
    const gchar           *c;
    const gchar           *next;
    static const guint8    gesc = GSM_ESCAPE_CHAR;

    if (!utf8 || !g_utf8_validate (utf8, -1, NULL)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Couldn't convert UTF-8 to GSM: input UTF-8 validation failed");
        return NULL;
    }

    /* worst case initial length */
    gsm = g_byte_array_sized_new (g_utf8_strlen (utf8, -1) * 2 + 1);

    if (*utf8 == 0x00) {
        /* Zero-length string */
        g_byte_array_append (gsm, (guint8 *) "\0", 1);
        if (out_len)
            *out_len = 0;
        return g_byte_array_free (g_steal_pointer (&gsm), FALSE);
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
        } else if (utf8_to_gsm_def_char (c, next - c, &gch)) {
            g_byte_array_append (gsm, &gch, 1);
        } else if (translit) {
            /* add ? */
            g_byte_array_append (gsm, &gch, 1);
        } else {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                         "Couldn't convert UTF-8 char to GSM");
            return NULL;
        }

        c = next;
    }

    /* Output length doesn't consider terminating NUL byte */
    if (out_len)
        *out_len = gsm->len;

    /* Always make sure returned string is NUL terminated */
    g_byte_array_append (gsm, (guint8 *) "\0", 1);
    return g_byte_array_free (g_steal_pointer (&gsm), FALSE);
}

/******************************************************************************/
/* Checks to see whether conversion to a target charset may be done without
 * any loss. */

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

/******************************************************************************/
/* GSM-7 pack/unpack operations */

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

/*****************************************************************************/
/* Main conversion functions */

static guint8 *
charset_iconv_from_utf8 (const gchar            *utf8,
                         const CharsetSettings  *settings,
                         gboolean                translit,
                         guint                  *out_size,
                         GError                **error)
{
    g_autoptr(GError)      inner_error = NULL;
    gsize                  bytes_written = 0;
    g_autofree guint8     *encoded = NULL;

    encoded = (guint8 *) g_convert (utf8, -1,
                                    settings->iconv_name, "UTF-8",
                                    NULL, &bytes_written, &inner_error);
    if (encoded) {
        if (out_size)
            *out_size = (guint) bytes_written;
        return g_steal_pointer (&encoded);
    }

    if (!translit) {
        g_propagate_error (error, g_steal_pointer (&inner_error));
        g_prefix_error (error, "Couldn't convert from UTF-8 to %s: ", settings->gsm_name);
        return NULL;
    }

    encoded = (guint8 *) g_convert_with_fallback (utf8, -1,
                                                  settings->iconv_name, "UTF-8", translit_fallback,
                                                  NULL, &bytes_written, error);
    if (encoded) {
        if (out_size)
            *out_size = (guint) bytes_written;
        return g_steal_pointer (&encoded);
    }

    g_prefix_error (error, "Couldn't convert from UTF-8 to %s with translit: ", settings->gsm_name);
    return NULL;
}

GByteArray *
mm_modem_charset_bytearray_from_utf8 (const gchar     *utf8,
                                      MMModemCharset   charset,
                                      gboolean         translit,
                                      GError         **error)
{
    const CharsetSettings *settings;
    guint8                *encoded = NULL;
    guint                  encoded_size = 0;

    settings = lookup_charset_settings (charset);

    if (charset == MM_MODEM_CHARSET_UNKNOWN) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot convert from UTF-8: unknown target charset");
        return NULL;
    }

    switch (charset) {
        case MM_MODEM_CHARSET_GSM:
            encoded = charset_utf8_to_unpacked_gsm (utf8, translit, &encoded_size, error);
            break;
        case MM_MODEM_CHARSET_IRA:
        case MM_MODEM_CHARSET_8859_1:
        case MM_MODEM_CHARSET_UTF8:
        case MM_MODEM_CHARSET_UCS2:
        case MM_MODEM_CHARSET_PCCP437:
        case MM_MODEM_CHARSET_PCDN:
        case MM_MODEM_CHARSET_UTF16:
            encoded = charset_iconv_from_utf8 (utf8, settings, translit, &encoded_size, error);
            break;
        case MM_MODEM_CHARSET_UNKNOWN:
        default:
            g_assert_not_reached ();
    }

    return g_byte_array_new_take (encoded, encoded_size);
}

gchar *
mm_modem_charset_str_from_utf8 (const gchar     *utf8,
                                MMModemCharset   charset,
                                gboolean         translit,
                                GError         **error)
{
    g_autoptr(GByteArray) bytearray = NULL;

    if (charset == MM_MODEM_CHARSET_UNKNOWN) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot convert from UTF-8: unknown target charset");
        return NULL;
    }

    bytearray = mm_modem_charset_bytearray_from_utf8 (utf8, charset, translit, error);
    if (!bytearray)
        return NULL;

    switch (charset) {
        case MM_MODEM_CHARSET_GSM:
            /* Note: strings encoded in unpacked GSM-7 can be used as plain
             * strings as long as the string doesn't contain character '@', which
             * is the one encoded as 0x00. At this point, we perform transliteration
             * of the NUL bytes in the GSM-7 bytearray, and we fail the operation
             * if one or more replacements were done and transliteration wasn't
             * requested */
            if (translit_gsm_nul_byte (bytearray) && !translit) {
                g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                             "Cannot convert to GSM-7 string: transliteration required for embedded '@'");
                return NULL;
            }
            /* fall through */
        case MM_MODEM_CHARSET_IRA:
        case MM_MODEM_CHARSET_8859_1:
        case MM_MODEM_CHARSET_UTF8:
        case MM_MODEM_CHARSET_PCCP437:
        case MM_MODEM_CHARSET_PCDN:
            return (gchar *) g_byte_array_free (g_steal_pointer (&bytearray), FALSE);
        case MM_MODEM_CHARSET_UCS2:
        case MM_MODEM_CHARSET_UTF16:
            return mm_utils_bin2hexstr (bytearray->data, bytearray->len);
        default:
        case MM_MODEM_CHARSET_UNKNOWN:
            g_assert_not_reached ();
    }
}

static gchar *
charset_iconv_to_utf8 (const guint8           *data,
                       guint32                 len,
                       const CharsetSettings  *settings,
                       gboolean                translit,
                       GError                **error)
{
    g_autoptr(GError)  inner_error = NULL;
    g_autofree gchar  *utf8 = NULL;

    utf8 = g_convert ((const gchar *) data, len,
                      "UTF-8",
                      settings->iconv_name,
                      NULL, NULL, &inner_error);
    if (utf8)
        return g_steal_pointer (&utf8);

    if (!translit) {
        g_propagate_error (error, g_steal_pointer (&inner_error));
        g_prefix_error (error, "Couldn't convert from %s to UTF-8: ", settings->gsm_name);
        return NULL;
    }

    utf8 = g_convert_with_fallback ((const gchar *) data, len,
                                    "UTF-8", settings->iconv_name, translit_fallback,
                                    NULL, NULL, error);
    if (utf8)
        return g_steal_pointer (&utf8);

    g_prefix_error (error, "Couldn't convert from %s to UTF-8 with translit: ", settings->gsm_name);
    return NULL;
}

gchar *
mm_modem_charset_bytearray_to_utf8 (GByteArray      *bytearray,
                                    MMModemCharset   charset,
                                    gboolean         translit,
                                    GError         **error)
{
    const CharsetSettings *settings;
    g_autofree gchar      *utf8 = NULL;

    if (charset == MM_MODEM_CHARSET_UNKNOWN) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot convert from UTF-8: unknown target charset");
        return NULL;
    }

    settings = lookup_charset_settings (charset);

    switch (charset) {
        case MM_MODEM_CHARSET_GSM:
            utf8 = (gchar *) charset_gsm_unpacked_to_utf8 (bytearray->data,
                                                           bytearray->len,
                                                           translit,
                                                           error);
            break;
        case MM_MODEM_CHARSET_IRA:
        case MM_MODEM_CHARSET_UTF8:
        case MM_MODEM_CHARSET_8859_1:
        case MM_MODEM_CHARSET_PCCP437:
        case MM_MODEM_CHARSET_PCDN:
        case MM_MODEM_CHARSET_UCS2:
        case MM_MODEM_CHARSET_UTF16:
            utf8 = charset_iconv_to_utf8 (bytearray->data,
                                          bytearray->len,
                                          settings,
                                          translit,
                                          error);
            break;
        case MM_MODEM_CHARSET_UNKNOWN:
        default:
            g_assert_not_reached ();
    }

    if (utf8 && g_utf8_validate (utf8, -1, NULL))
        return g_steal_pointer (&utf8);

    g_prefix_error (error, "Invalid conversion from %s to UTF-8: ", settings->gsm_name);
    return NULL;
}

gchar *
mm_modem_charset_str_to_utf8 (const gchar     *str,
                              gssize           len,
                              MMModemCharset   charset,
                              gboolean         translit,
                              GError         **error)
{
    g_autoptr(GByteArray) bytearray = NULL;

    if (charset == MM_MODEM_CHARSET_UNKNOWN) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot convert from UTF-8: unknown target charset");
        return NULL;
    }

    /* Note: if the input string is GSM-7 encoded and it contains the '@'
     * character, using -1 to indicate string length won't work properly,
     * as '@' is encoded as 0x00. Whenever possible, if using GSM-7,
     * give a proper len value or otherwise use the bytearray_to_utf8()
     * method instead. */
    if (len < 0)
        len = strlen (str);

    switch (charset) {
        case MM_MODEM_CHARSET_GSM:
        case MM_MODEM_CHARSET_IRA:
        case MM_MODEM_CHARSET_8859_1:
        case MM_MODEM_CHARSET_UTF8:
        case MM_MODEM_CHARSET_PCCP437:
        case MM_MODEM_CHARSET_PCDN:
            bytearray = g_byte_array_sized_new (len);
            g_byte_array_append (bytearray, (const guint8 *)str, len);
            break;
        case MM_MODEM_CHARSET_UCS2:
        case MM_MODEM_CHARSET_UTF16: {
            guint8 *bin = NULL;
            gsize   bin_len;

            bin = (guint8 *) mm_utils_hexstr2bin (str, len, &bin_len, error);
            if (!bin)
                return NULL;

            bytearray = g_byte_array_new_take (bin, bin_len);
            break;
        }
        case MM_MODEM_CHARSET_UNKNOWN:
        default:
            g_assert_not_reached ();
    }

    return mm_modem_charset_bytearray_to_utf8 (bytearray, charset, translit, error);
}
