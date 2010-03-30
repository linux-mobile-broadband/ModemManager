/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <glib.h>

#include "result.h"
#include "result-private.h"
#include "error.h"

struct QCDMResult {
    guint32 refcount;
    GHashTable *hash;
};


static void
gvalue_destroy (gpointer data)
{
    GValue *value = (GValue *) data;

    g_value_unset (value);
    g_slice_free (GValue, value);
}

QCDMResult *
qcdm_result_new (void)
{
    QCDMResult *result;

    g_type_init ();

    result = g_malloc0 (sizeof (QCDMResult));
    result->hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          NULL, gvalue_destroy);
    result->refcount = 1;
    return result;
}

QCDMResult *
qcdm_result_ref (QCDMResult *result)
{
    g_return_val_if_fail (result != NULL, NULL);
    g_return_val_if_fail (result->refcount > 0, NULL);

    result->refcount++;
    return result;
}

void
qcdm_result_unref (QCDMResult *result)
{
    g_return_if_fail (result != NULL);
    g_return_if_fail (result->refcount > 0);

    result->refcount--;
    if (result->refcount == 0) {
        g_hash_table_destroy (result->hash);
        memset (result, 0, sizeof (QCDMResult));
        g_free (result);
    }
}

void
qcdm_result_add_string (QCDMResult *result,
                        const char *key,
                        const char *str)
{
    GValue *val;

    g_return_if_fail (result != NULL);
    g_return_if_fail (result->refcount > 0);
    g_return_if_fail (key != NULL);
    g_return_if_fail (str != NULL);

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_STRING);
    g_value_set_string (val, str);

    g_hash_table_insert (result->hash, (gpointer) key, val);
}

gboolean
qcdm_result_get_string (QCDMResult *result,
                        const char *key,
                        const char **out_val)
{
    GValue *val;

    g_return_val_if_fail (result != NULL, FALSE);
    g_return_val_if_fail (result->refcount > 0, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);
    g_return_val_if_fail (out_val != NULL, FALSE);
    g_return_val_if_fail (*out_val == NULL, FALSE);

    val = g_hash_table_lookup (result->hash, key);
    if (!val)
        return FALSE;

    g_warn_if_fail (G_VALUE_HOLDS_STRING (val));
    if (!G_VALUE_HOLDS_STRING (val))
        return FALSE;

    *out_val = g_value_get_string (val);
    return TRUE;
}

void
qcdm_result_add_uint8 (QCDMResult *result,
                        const char *key,
                        guint8 num)
{
    GValue *val;

    g_return_if_fail (result != NULL);
    g_return_if_fail (result->refcount > 0);
    g_return_if_fail (key != NULL);

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_UCHAR);
    g_value_set_uchar (val, (unsigned char) num);

    g_hash_table_insert (result->hash, (gpointer) key, val);
}

gboolean
qcdm_result_get_uint8  (QCDMResult *result,
                        const char *key,
                        guint8 *out_val)
{
    GValue *val;

    g_return_val_if_fail (result != NULL, FALSE);
    g_return_val_if_fail (result->refcount > 0, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);
    g_return_val_if_fail (out_val != NULL, FALSE);

    val = g_hash_table_lookup (result->hash, key);
    if (!val)
        return FALSE;

    g_warn_if_fail (G_VALUE_HOLDS_UCHAR (val));
    if (!G_VALUE_HOLDS_UCHAR (val))
        return FALSE;

    *out_val = (guint8) g_value_get_uchar (val);
    return TRUE;
}

void
qcdm_result_add_uint32 (QCDMResult *result,
                        const char *key,
                        guint32 num)
{
    GValue *val;

    g_return_if_fail (result != NULL);
    g_return_if_fail (result->refcount > 0);
    g_return_if_fail (key != NULL);

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_UINT);
    g_value_set_uint (val, num);

    g_hash_table_insert (result->hash, (gpointer) key, val);
}

gboolean
qcdm_result_get_uint32 (QCDMResult *result,
                        const char *key,
                        guint32 *out_val)
{
    GValue *val;

    g_return_val_if_fail (result != NULL, FALSE);
    g_return_val_if_fail (result->refcount > 0, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);
    g_return_val_if_fail (out_val != NULL, FALSE);

    val = g_hash_table_lookup (result->hash, key);
    if (!val)
        return FALSE;

    g_warn_if_fail (G_VALUE_HOLDS_UINT (val));
    if (!G_VALUE_HOLDS_UINT (val))
        return FALSE;

    *out_val = (guint32) g_value_get_uint (val);
    return TRUE;
}

void
qcdm_result_add_boxed (QCDMResult *result,
                       const char *key,
                       GType btype,
                       gpointer boxed)
{
    GValue *val;

    g_return_if_fail (result != NULL);
    g_return_if_fail (result->refcount > 0);
    g_return_if_fail (key != NULL);

    val = g_slice_new0 (GValue);
    g_value_init (val, btype);
    g_value_set_static_boxed (val, boxed);

    g_hash_table_insert (result->hash, (gpointer) key, val);
}

gboolean
qcdm_result_get_boxed (QCDMResult *result,
                       const char *key,
                       gpointer *out_val)
{
    GValue *val;

    g_return_val_if_fail (result != NULL, FALSE);
    g_return_val_if_fail (result->refcount > 0, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);
    g_return_val_if_fail (out_val != NULL, FALSE);

    val = g_hash_table_lookup (result->hash, key);
    if (!val)
        return FALSE;

    g_warn_if_fail (G_VALUE_HOLDS_BOXED (val));
    if (!G_VALUE_HOLDS_BOXED (val))
        return FALSE;

    *out_val = g_value_get_boxed (val);
    return TRUE;
}

