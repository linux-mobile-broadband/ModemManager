/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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

#include "result.h"

QcdmResult *qcdm_result_new (void);

void qcdm_result_add_string (QcdmResult *result,
                             const char *key,
                             const char *str);

void qcdm_result_add_u8     (QcdmResult *result,
                             const char *key,
                             uint8_t num);

void qcdm_result_add_u8_array (QcdmResult *result,
                               const char *key,
                               const uint8_t *array,
                               size_t array_len);

int qcdm_result_get_u8_array  (QcdmResult *result,
                               const char *key,
                               const uint8_t **out_val,
                               size_t *out_len);

void qcdm_result_add_u16_array (QcdmResult *result,
                                const char *key,
                                const uint16_t *array,
                                size_t array_len);

void qcdm_result_add_u32    (QcdmResult *result,
                             const char *key,
                             uint32_t num);

#endif  /* LIBQCDM_RESULT_PRIVATE_H */
