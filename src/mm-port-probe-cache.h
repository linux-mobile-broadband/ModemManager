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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_PORT_PROBE_CACHE_H
#define MM_PORT_PROBE_CACHE_H

#include <glib.h>

#include "mm-port-probe.h"

MMPortProbe *mm_port_probe_cache_get    (GUdevDevice *port,
                                         const gchar *physdev_path,
                                         const gchar *driver);

void         mm_port_probe_cache_remove (GUdevDevice *port);

void         mm_port_probe_cache_clear  (void);

#endif /* MM_PORT_PROBE_CACHE_H */
