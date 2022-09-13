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
 * Copyright (C) 2022 Google, Inc.
 */

#ifndef MM_LOG_HELPERS_H
#define MM_LOG_HELPERS_H

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"

void mm_log_bearer_properties (gpointer            log_object,
                               MMLogLevel          level,
                               const gchar        *prefix,
                               MMBearerProperties *properties);

#endif /* MM_LOG_HELPERS_H */
