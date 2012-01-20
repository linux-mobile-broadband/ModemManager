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

enum {
	LOGL_ERR   = 0x00000001,
	LOGL_WARN  = 0x00000002,
	LOGL_INFO  = 0x00000004,
	LOGL_DEBUG = 0x00000008
};

enum {
    WMC_SUCCESS = 0,
    WMC_ERROR_INVALID_ARGUMENTS = 1,
    WMC_ERROR_SERIAL_CONFIG_FAILED = 2,
    WMC_ERROR_VALUE_NOT_FOUND = 3,
    WMC_ERROR_RESPONSE_UNEXPECTED = 4,
    WMC_ERROR_RESPONSE_BAD_LENGTH = 5,
};

#define wmc_assert assert

#define wmc_return_if_fail(e) \
{ \
    if (!(e)) { \
        wmc_warn (0, "failed: " #e "\n"); \
        return; \
    } \
}

#define wmc_return_val_if_fail(e, v) \
{ \
    if (!(e)) { \
        wmc_warn (0, "failed: " #e "\n"); \
        return v; \
    } \
}

void _wmc_log (const char *file,
               int line,
               const char *func,
               int domain,
               int level,
               const char *format,
               ...) __attribute__((__format__ (__printf__, 6, 7)));

#define wmc_dbg(domain, ...) \
	_wmc_log (__FILE__, __LINE__, __func__, domain, LOGL_DEBUG, ## __VA_ARGS__ )

#define wmc_warn(domain, ...) \
	_wmc_log (__FILE__, __LINE__, __func__, domain, LOGL_WARN, ## __VA_ARGS__ )

#define wmc_err(domain, ...) \
	_wmc_log (__FILE__, __LINE__, __func__, domain, LOGL_ERR, ## __VA_ARGS__ )

#endif  /* LIBWMC_ERRORS_H */
