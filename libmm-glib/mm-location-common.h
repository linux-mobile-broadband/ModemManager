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
 */
#define MM_LOCATION_LONGITUDE_UNKNOWN G_MINDOUBLE

/**
 * MM_LOCATION_LATITUDE_UNKNOWN:
 *
 * Identifier for an unknown latitude value.
 *
 * Proper latitude values fall in the [-90,90] range.
 */
#define MM_LOCATION_LATITUDE_UNKNOWN  G_MINDOUBLE

/**
 * MM_LOCATION_ALTITUDE_UNKNOWN:
 *
 * Identifier for an unknown altitude value.
 */
#define MM_LOCATION_ALTITUDE_UNKNOWN  G_MINDOUBLE

#endif /* MM_LOCATION_COMMON_H */
