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

#ifndef MM_UTILS_H
#define MM_UTILS_H

#include <glib.h>
#include <glib-object.h>

int utils_hex2byte (const char *hex);

char *utils_hexstr2bin (const char *hex, gsize *out_len);

char *utils_bin2hexstr (const guint8 *bin, gsize len);

gboolean utils_check_for_single_value (guint32 value);

/**********************************************/

GHashTable *value_hash_new (void);

/* Keys must be constant! */
void value_hash_add_uint (GHashTable *hash, const char *key, guint32 val);
void value_hash_add_string (GHashTable *hash, const char *key, const char *val);
void value_hash_add_byte_array (GHashTable *hash, const char *key, const GByteArray *val);
void value_hash_add_value (GHashTable *hash, const char *key, const GValue *val);

#endif  /* MM_UTILS_H */

