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

#ifndef LIBWMC_RESULT_PRIVATE_H
#define LIBWMC_RESULT_PRIVATE_H

#include "result.h"

WmcResult *wmc_result_new (void);

void wmc_result_add_string (WmcResult *result,
                            const char *key,
                            const char *str);

void wmc_result_add_u8     (WmcResult *result,
                            const char *key,
                            u_int8_t num);

void wmc_result_add_u32    (WmcResult *result,
                            const char *key,
                            u_int32_t num);

#endif  /* LIBWMC_RESULT_PRIVATE_H */

