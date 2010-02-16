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

#ifndef LIBQCDM_RESULT_H
#define LIBQCDM_RESULT_H

#include <glib.h>

typedef struct QCDMResult QCDMResult;

gboolean qcdm_result_get_string (QCDMResult *result,
                                 const char *key,
                                 const char **out_val);

gboolean qcdm_result_get_uint8  (QCDMResult *result,
                                 const char *key,
                                 guint8 *out_val);

gboolean qcdm_result_get_uint32 (QCDMResult *result,
                                 const char *key,
                                 guint32 *out_val);

QCDMResult *qcdm_result_ref     (QCDMResult *result);

void        qcdm_result_unref   (QCDMResult *result);

#endif  /* LIBQCDM_RESULT_H */

