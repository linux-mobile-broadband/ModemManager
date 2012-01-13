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

GType mm_pointer_array_get_type (void) G_GNUC_CONST;
#define MM_TYPE_POINTER_ARRAY (mm_pointer_array_get_type ())

G_END_DECLS

#endif /* __MM_PRIVATE_BOXED_TYPES_H__ */
