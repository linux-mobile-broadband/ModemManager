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
#include <glib.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <dbus/dbus-glib.h>

#include "mm-utils.h"

/* From hostap, Copyright (c) 2002-2005, Jouni Malinen <jkmaline@cc.hut.fi> */

static int hex2num (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int utils_hex2byte (const char *hex)
{
	int a, b;
	a = hex2num(*hex++);
	if (a < 0)
		return -1;
	b = hex2num(*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}

char *
utils_hexstr2bin (const char *hex, gsize *out_len)
{
    size_t len = strlen (hex);
	size_t       i;
	int          a;
	const char * ipos = hex;
	char *       buf = NULL;
	char *       opos;

	/* Length must be a multiple of 2 */
    g_return_val_if_fail ((len % 2) == 0, NULL);

	opos = buf = g_malloc0 ((len / 2) + 1);
	for (i = 0; i < len; i += 2) {
		a = utils_hex2byte (ipos);
		if (a < 0) {
			g_free (buf);
			return NULL;
		}
		*opos++ = a;
		ipos += 2;
	}
    *out_len = len / 2;
	return buf;
}

/* End from hostap */

char *
utils_bin2hexstr (const guint8 *bin, gsize len)
{
    GString *ret;
    gsize i;

    g_return_val_if_fail (bin != NULL, NULL);

    ret = g_string_sized_new (len * 2 + 1);
    for (i = 0; i < len; i++)
        g_string_append_printf (ret, "%.2X", bin[i]);
    return g_string_free (ret, FALSE);
}

gboolean
utils_check_for_single_value (guint32 value)
{
    gboolean found = FALSE;
    guint32 i;

    for (i = 1; i <= 32; i++) {
        if (value & 0x1) {
            if (found)
                return FALSE;  /* More than one bit set */
            found = TRUE;
        }
        value >>= 1;
    }

    return TRUE;
}

/***************************************************************/

static void
vh_free_gvalue (gpointer data)
{
    g_value_unset ((GValue *) data);
    g_slice_free (GValue, data);
}

GHashTable *
value_hash_new (void)
{
    return g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) vh_free_gvalue);
}

void
value_hash_add_uint (GHashTable *hash, const char *key, guint32 val)
{
    GValue *v = g_slice_new0 (GValue);

    g_value_init (v, G_TYPE_UINT);
    g_value_set_uint (v, val);
    g_hash_table_insert (hash, (gpointer) key, v);
}

void
value_hash_add_string (GHashTable *hash, const char *key, const char *val)
{
    GValue *v = g_slice_new0 (GValue);

    g_value_init (v, G_TYPE_STRING);
    g_value_set_string (v, val);
    g_hash_table_insert (hash, (gpointer) key, v);
}

void
value_hash_add_byte_array (GHashTable *hash, const char *key, const GByteArray *val)
{
    GValue *v = g_slice_new0 (GValue);

    g_value_init (v, DBUS_TYPE_G_UCHAR_ARRAY);
    g_value_set_boxed (v, val);
    g_hash_table_insert (hash, (gpointer) key, v);
}

void
value_hash_add_value (GHashTable *hash, const char *key, const GValue *val)
{
    GValue *v = g_slice_new0 (GValue);

    g_value_init (v, G_VALUE_TYPE (val));
    g_value_copy (val, v);
    g_hash_table_insert (hash, (gpointer) key, v);
}

/***************************************************************/

