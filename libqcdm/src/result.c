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
#include <stdlib.h>

#include "result.h"
#include "result-private.h"
#include "errors.h"

/*********************************************************/

typedef struct Val Val;

typedef enum {
    VAL_TYPE_NONE = 0,
    VAL_TYPE_STRING = 1,
    VAL_TYPE_U8 = 2,
    VAL_TYPE_U32 = 3,
    VAL_TYPE_U8_ARRAY = 4,
    VAL_TYPE_U16_ARRAY = 5,
} ValType;

struct Val {
    char *key;
    uint8_t type;
    union {
        char *s;
        uint8_t u8;
        uint32_t u32;
        uint8_t *u8_array;
        uint16_t *u16_array;
    } u;
    uint32_t array_len;
    Val *next;
};

static void
val_free (Val *v)
{
    if (v->type == VAL_TYPE_STRING) {
        free (v->u.s);
    } else if (v->type == VAL_TYPE_U8_ARRAY) {
        free (v->u.u8_array);
    } else if (v->type == VAL_TYPE_U16_ARRAY) {
        free (v->u.u16_array);
    }
    free (v->key);
    memset (v, 0, sizeof (*v));
    free (v);
}

static Val *
val_new_string (const char *key, const char *value)
{
    Val *v;

    qcdm_return_val_if_fail (key != NULL, NULL);
    qcdm_return_val_if_fail (key[0] != '\0', NULL);
    qcdm_return_val_if_fail (value != NULL, NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_STRING;
    v->u.s = strdup (value);
    return v;
}

static Val *
val_new_u8 (const char *key, uint8_t u)
{
    Val *v;

    qcdm_return_val_if_fail (key != NULL, NULL);
    qcdm_return_val_if_fail (key[0] != '\0', NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_U8;
    v->u.u8 = u;
    return v;
}

static Val *
val_new_u8_array (const char *key, const uint8_t *array, size_t array_len)
{
    Val *v;

    qcdm_return_val_if_fail (key != NULL, NULL);
    qcdm_return_val_if_fail (key[0] != '\0', NULL);
    qcdm_return_val_if_fail (array != NULL, NULL);
    qcdm_return_val_if_fail (array_len > 0, NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_U8_ARRAY;
    v->u.u8_array = malloc (array_len);
    if (v->u.u8_array == NULL) {
        val_free (v);
        return NULL;
    }
    memcpy (v->u.u8_array, array, array_len);
    v->array_len = array_len;

    return v;
}

static Val *
val_new_u32 (const char *key, uint32_t u)
{
    Val *v;

    qcdm_return_val_if_fail (key != NULL, NULL);
    qcdm_return_val_if_fail (key[0] != '\0', NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_U32;
    v->u.u32 = u;
    return v;
}

static Val *
val_new_u16_array (const char *key, const uint16_t *array, size_t array_len)
{
    Val *v;
    size_t sz;

    qcdm_return_val_if_fail (key != NULL, NULL);
    qcdm_return_val_if_fail (key[0] != '\0', NULL);
    qcdm_return_val_if_fail (array != NULL, NULL);
    qcdm_return_val_if_fail (array_len > 0, NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_U16_ARRAY;
    sz = sizeof (uint16_t) * array_len;
    v->u.u16_array = malloc (sz);
    if (v->u.u16_array == NULL) {
        val_free (v);
        return NULL;
    }
    memcpy (v->u.u16_array, array, sz);
    v->array_len = array_len;

    return v;
}

/*********************************************************/

struct QcdmResult {
    uint32_t refcount;
    Val *first;
};

QcdmResult *
qcdm_result_new (void)
{
    QcdmResult *r;

    r = calloc (sizeof (QcdmResult), 1);
    if (r)
        r->refcount = 1;
    return r;
}

QcdmResult *
qcdm_result_ref (QcdmResult *r)
{
    qcdm_return_val_if_fail (r != NULL, NULL);
    qcdm_return_val_if_fail (r->refcount > 0, NULL);

    r->refcount++;
    return r;
}

static void
qcdm_result_free (QcdmResult *r)
{
    Val *v, *n;

    v = r->first;
    while (v) {
        n = v->next;
        val_free (v);
        v = n;
    }
    memset (r, 0, sizeof (*r));
    free (r);
}

void
qcdm_result_unref (QcdmResult *r)
{
    qcdm_return_if_fail (r != NULL);
    qcdm_return_if_fail (r->refcount > 0);

    r->refcount--;
    if (r->refcount == 0)
        qcdm_result_free (r);
}

static Val *
find_val (QcdmResult *r, const char *key, ValType expected_type)
{
    Val *v, *n;

    v = r->first;
    while (v) {
        n = v->next;
        if (strcmp (v->key, key) == 0) {
            /* Check type */
            qcdm_return_val_if_fail (v->type == expected_type, NULL);
            return v;
        }
        v = n;
    }
    return NULL;
}

void
qcdm_result_add_string (QcdmResult *r,
                       const char *key,
                       const char *str)
{
    Val *v;

    qcdm_return_if_fail (r != NULL);
    qcdm_return_if_fail (r->refcount > 0);
    qcdm_return_if_fail (key != NULL);
    qcdm_return_if_fail (str != NULL);

    v = val_new_string (key, str);
    qcdm_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
qcdm_result_get_string (QcdmResult *r,
                       const char *key,
                       const char **out_val)
{
    Val *v;

    qcdm_return_val_if_fail (r != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (r->refcount > 0, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (key != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (out_val != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (*out_val == NULL, -QCDM_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_STRING);
    if (v == NULL)
        return -QCDM_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.s;
    return 0;
}

void
qcdm_result_add_u8 (QcdmResult *r,
                   const char *key,
                   uint8_t num)
{
    Val *v;

    qcdm_return_if_fail (r != NULL);
    qcdm_return_if_fail (r->refcount > 0);
    qcdm_return_if_fail (key != NULL);

    v = val_new_u8 (key, num);
    qcdm_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
qcdm_result_get_u8  (QcdmResult *r,
                    const char *key,
                    uint8_t *out_val)
{
    Val *v;

    qcdm_return_val_if_fail (r != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (r->refcount > 0, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (key != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (out_val != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_U8);
    if (v == NULL)
        return -QCDM_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.u8;
    return 0;
}

void
qcdm_result_add_u8_array (QcdmResult *r,
                          const char *key,
                          const uint8_t *array,
                          size_t array_len)
{
    Val *v;

    qcdm_return_if_fail (r != NULL);
    qcdm_return_if_fail (r->refcount > 0);
    qcdm_return_if_fail (key != NULL);
    qcdm_return_if_fail (array != NULL);

    v = val_new_u8_array (key, array, array_len);
    qcdm_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
qcdm_result_get_u8_array (QcdmResult *r,
                          const char *key,
                          const uint8_t **out_val,
                          size_t *out_len)
{
    Val *v;

    qcdm_return_val_if_fail (r != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (r->refcount > 0, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (key != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (out_val != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (out_len != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_U8_ARRAY);
    if (v == NULL)
        return -QCDM_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.u8_array;
    *out_len = v->array_len;
    return 0;
}

void
qcdm_result_add_u32 (QcdmResult *r,
                    const char *key,
                    uint32_t num)
{
    Val *v;

    qcdm_return_if_fail (r != NULL);
    qcdm_return_if_fail (r->refcount > 0);
    qcdm_return_if_fail (key != NULL);

    v = val_new_u32 (key, num);
    qcdm_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
qcdm_result_get_u32 (QcdmResult *r,
                    const char *key,
                    uint32_t *out_val)
{
    Val *v;

    qcdm_return_val_if_fail (r != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (r->refcount > 0, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (key != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (out_val != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_U32);
    if (v == NULL)
        return -QCDM_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.u32;
    return 0;
}

void
qcdm_result_add_u16_array (QcdmResult *r,
                           const char *key,
                           const uint16_t *array,
                           size_t array_len)
{
    Val *v;

    qcdm_return_if_fail (r != NULL);
    qcdm_return_if_fail (r->refcount > 0);
    qcdm_return_if_fail (key != NULL);
    qcdm_return_if_fail (array != NULL);

    v = val_new_u16_array (key, array, array_len);
    qcdm_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
qcdm_result_get_u16_array (QcdmResult *r,
                           const char *key,
                           const uint16_t **out_val,
                           size_t *out_len)
{
    Val *v;

    qcdm_return_val_if_fail (r != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (r->refcount > 0, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (key != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (out_val != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);
    qcdm_return_val_if_fail (out_len != NULL, -QCDM_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_U16_ARRAY);
    if (v == NULL)
        return -QCDM_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.u16_array;
    *out_len = v->array_len;
    return 0;
}
