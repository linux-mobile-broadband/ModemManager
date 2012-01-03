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
    VAL_TYPE_U32 = 3
} ValType;

struct Val {
    char *key;
    ValType type;
    union {
        char *s;
        u_int8_t u8;
        u_int32_t u32;
    } u;
    Val *next;
};

static void
val_free (Val *v)
{
    if (v->type == VAL_TYPE_STRING) {
        if (v->u.s)
            free (v->u.s);
    }
    free (v->key);
    memset (v, 0, sizeof (*v));
    free (v);
}

static Val *
val_new_string (const char *key, const char *value)
{
    Val *v;

    wmc_return_val_if_fail (key != NULL, NULL);
    wmc_return_val_if_fail (key[0] != '\0', NULL);
    wmc_return_val_if_fail (value != NULL, NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_STRING;
    v->u.s = strdup (value);
    return v;
}

static Val *
val_new_u8 (const char *key, u_int8_t u)
{
    Val *v;

    wmc_return_val_if_fail (key != NULL, NULL);
    wmc_return_val_if_fail (key[0] != '\0', NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_U8;
    v->u.u8 = u;
    return v;
}

static Val *
val_new_u32 (const char *key, u_int32_t u)
{
    Val *v;

    wmc_return_val_if_fail (key != NULL, NULL);
    wmc_return_val_if_fail (key[0] != '\0', NULL);

    v = calloc (sizeof (Val), 1);
    if (v == NULL)
        return NULL;

    v->key = strdup (key);
    v->type = VAL_TYPE_U32;
    v->u.u32 = u;
    return v;
}

/*********************************************************/

struct WmcResult {
    u_int32_t refcount;
    Val *first;
};

WmcResult *
wmc_result_new (void)
{
    WmcResult *r;

    r = calloc (sizeof (WmcResult), 1);
    if (r)
        r->refcount = 1;
    return r;
}

WmcResult *
wmc_result_ref (WmcResult *r)
{
    wmc_return_val_if_fail (r != NULL, NULL);
    wmc_return_val_if_fail (r->refcount > 0, NULL);

    r->refcount++;
    return r;
}

static void
wmc_result_free (WmcResult *r)
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
wmc_result_unref (WmcResult *r)
{
    wmc_return_if_fail (r != NULL);
    wmc_return_if_fail (r->refcount > 0);

    r->refcount--;
    if (r->refcount == 0)
        wmc_result_free (r);
}

static Val *
find_val (WmcResult *r, const char *key, ValType expected_type)
{
    Val *v, *n;

    v = r->first;
    while (v) {
        n = v->next;
        if (strcmp (v->key, key) == 0) {
            /* Check type */
            wmc_return_val_if_fail (v->type == expected_type, NULL);
            return v;
        }
        v = n;
    }
    return NULL;
}

void
wmc_result_add_string (WmcResult *r,
                       const char *key,
                       const char *str)
{
    Val *v;

    wmc_return_if_fail (r != NULL);
    wmc_return_if_fail (r->refcount > 0);
    wmc_return_if_fail (key != NULL);
    wmc_return_if_fail (str != NULL);

    v = val_new_string (key, str);
    wmc_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
wmc_result_get_string (WmcResult *r,
                       const char *key,
                       const char **out_val)
{
    Val *v;

    wmc_return_val_if_fail (r != NULL, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (r->refcount > 0, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (key != NULL, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (out_val != NULL, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (*out_val == NULL, -WMC_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_STRING);
    if (v == NULL)
        return -WMC_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.s;
    return 0;
}

void
wmc_result_add_u8 (WmcResult *r,
                   const char *key,
                   u_int8_t num)
{
    Val *v;

    wmc_return_if_fail (r != NULL);
    wmc_return_if_fail (r->refcount > 0);
    wmc_return_if_fail (key != NULL);

    v = val_new_u8 (key, num);
    wmc_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
wmc_result_get_u8  (WmcResult *r,
                    const char *key,
                    u_int8_t *out_val)
{
    Val *v;

    wmc_return_val_if_fail (r != NULL, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (r->refcount > 0, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (key != NULL, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (out_val != NULL, -WMC_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_U8);
    if (v == NULL)
        return -WMC_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.u8;
    return 0;
}

void
wmc_result_add_u32 (WmcResult *r,
                    const char *key,
                    u_int32_t num)
{
    Val *v;

    wmc_return_if_fail (r != NULL);
    wmc_return_if_fail (r->refcount > 0);
    wmc_return_if_fail (key != NULL);

    v = val_new_u32 (key, num);
    wmc_return_if_fail (v != NULL);
    v->next = r->first;
    r->first = v;
}

int
wmc_result_get_u32 (WmcResult *r,
                    const char *key,
                    u_int32_t *out_val)
{
    Val *v;

    wmc_return_val_if_fail (r != NULL, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (r->refcount > 0, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (key != NULL, -WMC_ERROR_INVALID_ARGUMENTS);
    wmc_return_val_if_fail (out_val != NULL, -WMC_ERROR_INVALID_ARGUMENTS);

    v = find_val (r, key, VAL_TYPE_U32);
    if (v == NULL)
        return -WMC_ERROR_VALUE_NOT_FOUND;

    *out_val = v->u.u32;
    return 0;
}

