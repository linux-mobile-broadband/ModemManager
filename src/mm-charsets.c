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

typedef struct {
    const char *name;
    MMModemCharset charset;
} CharsetEntry;

static CharsetEntry charset_map[] = {
    { "UTF-8",   MM_MODEM_CHARSET_UTF8 },
    { "UCS2",    MM_MODEM_CHARSET_UCS2 },
    { "IRA",     MM_MODEM_CHARSET_IRA },
    { "GSM",     MM_MODEM_CHARSET_GSM },
    { "8859-1",  MM_MODEM_CHARSET_8859_1 },
    { "PCCP437", MM_MODEM_CHARSET_PCCP437 },
    { "PCDN",    MM_MODEM_CHARSET_PCDN },
    { "HEX",     MM_MODEM_CHARSET_HEX },
    { NULL,      MM_MODEM_CHARSET_UNKNOWN }
};

const char *
mm_modem_charset_to_string (MMModemCharset charset)
{
    CharsetEntry *iter = &charset_map[0];

    g_return_val_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN, NULL);

    while (iter->name) {
        if (iter->charset == charset)
            return iter->name;
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

    while (iter->name) {
        if (strcasestr (string, iter->name))
            return iter->charset;
        iter++;
    }
    return MM_MODEM_CHARSET_UNKNOWN;
}

