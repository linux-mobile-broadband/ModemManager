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

#include <stdint.h>

typedef struct QcdmResult QcdmResult;

int qcdm_result_get_string (QcdmResult *r,
                            const char *key,
                            const char **out_val);

int qcdm_result_get_u8     (QcdmResult *r,
                            const char *key,
                            uint8_t *out_val);

int qcdm_result_get_u32    (QcdmResult *r,
                            const char *key,
                            uint32_t *out_val);

int qcdm_result_get_u16_array  (QcdmResult *result,
                                const char *key,
                                const uint16_t **out_val,
                                size_t *out_len);

QcdmResult *qcdm_result_ref    (QcdmResult *r);

void       qcdm_result_unref   (QcdmResult *r);

#endif  /* LIBQCDM_RESULT_H */
