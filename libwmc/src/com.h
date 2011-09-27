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

#ifndef LIBWMC_COM_H
#define LIBWMC_COM_H

#include "errors.h"

enum {
    WMC_SERIAL_ERROR = 1000
};

enum {
    WMC_SERIAL_ERROR_CONFIG_FAILED = 1
};

wbool wmc_port_setup (int fd, WmcError **error);

#endif  /* LIBWMC_COM_H */
