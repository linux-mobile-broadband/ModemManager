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

#ifndef LIBWMC_ERRORS_H
#define LIBWMC_ERRORS_H

#include <config.h>
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdio.h>

typedef u_int8_t wbool;
#ifndef TRUE
#define TRUE (u_int8_t) 1
#endif
#ifndef FALSE
#define FALSE (u_int8_t) 0
#endif

typedef struct {
    u_int32_t domain;
    u_int32_t code;
    char *message;
} WmcError;

WmcError *wmc_error_new (u_int32_t domain,
                         u_int32_t code,
                         const char *format,
                         ...) __attribute__((__format__ (__printf__, 3, 4)));

void wmc_error_set (WmcError **error,
                    u_int32_t domain,
                    u_int32_t code,
                    const char *format,
                    ...) __attribute__((__format__ (__printf__, 4, 5)));

void wmc_clear_error (WmcError **error);
void wmc_free_error (WmcError *error);

#define wmc_assert assert

#define wmc_return_if_fail(e) \
{ \
    if (!(e)) { \
        fprintf (stderr, "failed: ##e##\n"); \
        return; \
    } \
}

#define wmc_return_val_if_fail(e, v) \
{ \
    if (!(e)) { \
        fprintf (stderr, "failed: ##e##\n"); \
        return v; \
    } \
}

#endif  /* LIBWMC_COM_H */
