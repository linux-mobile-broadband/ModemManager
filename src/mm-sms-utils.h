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
 * Copyright (C) 2010 Red Hat, Inc.
 */

#ifndef MM_SMS_UTILS_H
#define MM_SMS_UTILS_H

#include <glib.h>

#define SMS_MAX_PDU_LEN 344

GHashTable *sms_parse_pdu (const char *hexpdu, GError **error);

#endif /* MM_SMS_UTILS_H */
