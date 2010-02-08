/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef UTILS_H
#define UTILS_H

#include <glib.h>

guint16 crc16 (const char *buffer, gsize len);

gsize dm_escape (const char *inbuf,
                 gsize inbuf_len,
                 char *outbuf,
                 gsize outbuf_len);

gsize dm_unescape (const char *inbuf,
                   gsize inbuf_len,
                   char *outbuf,
                   gsize outbuf_len,
                   gboolean *escaping);

#endif  /* UTILS_H */

