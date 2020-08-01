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
 * Copyright (C) 2012 - Google, Inc.
 */

#ifndef __MM_PRIVATE_BOXED_TYPES_H__
#define __MM_PRIVATE_BOXED_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

GType mm_uint16_array_get_type (void) G_GNUC_CONST;
#define MM_TYPE_UINT16_ARRAY (mm_uint16_array_get_type ())

typedef struct { guint16 l; guint16 r; } mm_uint16_pair;
GType mm_uint16_pair_array_get_type (void) G_GNUC_CONST;
#define MM_TYPE_UINT16_PAIR_ARRAY (mm_uint16_pair_array_get_type ())

typedef struct { gchar *l; gchar *r; } mm_str_pair;
GType mm_str_pair_array_get_type (void) G_GNUC_CONST;
#define MM_TYPE_STR_PAIR_ARRAY (mm_str_pair_array_get_type ())

GType mm_pointer_array_get_type (void) G_GNUC_CONST;
#define MM_TYPE_POINTER_ARRAY (mm_pointer_array_get_type ())

GType mm_object_array_get_type (void) G_GNUC_CONST;
#define MM_TYPE_OBJECT_ARRAY (mm_object_array_get_type ())

typedef struct {
    GCallback async;
    GCallback finish;
} MMAsyncMethod;
GType mm_async_method_get_type (void) G_GNUC_CONST;
#define MM_TYPE_ASYNC_METHOD (mm_async_method_get_type ())

G_END_DECLS

#endif /* __MM_PRIVATE_BOXED_TYPES_H__ */
