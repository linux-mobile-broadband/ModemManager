/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MM_HELPERS_H_
#define _MM_HELPERS_H_

#define RETURN_NON_EMPTY_CONSTANT_STRING(input) do {    \
        const gchar *str;                               \
                                                        \
        str = (input);                                  \
        if (str && str[0])                              \
            return str;                                 \
    } while (0);                                        \
    return NULL

#define RETURN_NON_EMPTY_STRING(input) do {             \
        gchar *str;                                     \
                                                        \
        str = (input);                                  \
        if (str && str[0])                              \
            return str;                                 \
        g_free (str);                                   \
    } while (0);                                        \
    return NULL

#endif /* _MM_HELPERS_H_ */
