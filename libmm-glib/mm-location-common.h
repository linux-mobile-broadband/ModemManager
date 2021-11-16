/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#ifndef MM_LOCATION_COMMON_H
#define MM_LOCATION_COMMON_H

#if !defined (__MODEM_MANAGER_H_INSIDE__)
#error "Only <ModemManager.h> can be included directly."
#endif

/**
 * MM_LOCATION_LONGITUDE_UNKNOWN:
 *
 * Identifier for an unknown longitude value.
 *
 * Proper longitude values fall in the [-180,180] range.
 *
 * Since: 1.0
 */
#define MM_LOCATION_LONGITUDE_UNKNOWN -G_MAXDOUBLE

/**
 * MM_LOCATION_LATITUDE_UNKNOWN:
 *
 * Identifier for an unknown latitude value.
 *
 * Proper latitude values fall in the [-90,90] range.
 *
 * Since: 1.0
 */
#define MM_LOCATION_LATITUDE_UNKNOWN  -G_MAXDOUBLE

/**
 * MM_LOCATION_ALTITUDE_UNKNOWN:
 *
 * Identifier for an unknown altitude value.
 *
 * Since: 1.0
 */
#define MM_LOCATION_ALTITUDE_UNKNOWN  -G_MAXDOUBLE

#endif /* MM_LOCATION_COMMON_H */
