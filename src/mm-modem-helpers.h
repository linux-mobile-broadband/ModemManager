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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#ifndef MM_MODEM_HELPERS_H
#define MM_MODEM_HELPERS_H

#define MM_SCAN_TAG_STATUS "status"
#define MM_SCAN_TAG_OPER_LONG "operator-long"
#define MM_SCAN_TAG_OPER_SHORT "operator-short"
#define MM_SCAN_TAG_OPER_NUM "operator-num"
#define MM_SCAN_TAG_ACCESS_TECH "access-tech"

GPtrArray *mm_gsm_parse_scan_response (const char *reply, GError **error);

void mm_gsm_destroy_scan_data (gpointer data);

#endif  /* MM_MODEM_HELPERS_H */

