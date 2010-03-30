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

#ifndef LIBQCDM_RESULT_PRIVATE_H
#define LIBQCDM_RESULT_PRIVATE_H

#include <glib.h>
#include <glib-object.h>
#include "result.h"

QCDMResult *qcdm_result_new (void);

/* For these functions, 'key' *must* be a constant, not allocated and freed */

void qcdm_result_add_string (QCDMResult *result,
                             const char *key,
                             const char *str);

void qcdm_result_add_uint8  (QCDMResult *result,
                             const char *key,
                             guint8 num);

void qcdm_result_add_uint32 (QCDMResult *result,
                             const char *key,
                             guint32 num);

void qcdm_result_add_boxed  (QCDMResult *result,
                             const char *key,
                             GType btype,
                             gpointer boxed);

gboolean qcdm_result_get_boxed (QCDMResult *result,
                                const char *key,
                                gpointer *out_val);

#endif  /* LIBQCDM_RESULT_PRIVATE_H */

