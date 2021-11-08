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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_KERNEL_DEVICE_HELPERS_H
#define MM_KERNEL_DEVICE_HELPERS_H

#include <glib.h>

/* For virtual devices that keep a "lower" link to the parent physical device
 * they're based on, get the name of that parent physical device.
 * (e.g. lower_device_name(qmimux0) == wwan0) */
gchar *mm_kernel_device_get_lower_device_name (const gchar *sysfs_path);

/* Generic string matching logic */
gboolean mm_kernel_device_generic_string_match (const gchar *str,
                                                const gchar *pattern,
                                                gpointer     log_object);

#endif /* MM_KERNEL_DEVICE_HELPERS_H */
