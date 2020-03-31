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
 */

#ifndef MM_SERIAL_PARSERS_H
#define MM_SERIAL_PARSERS_H

#include <glib.h>

gpointer mm_serial_parser_v1_new                  (void);
void     mm_serial_parser_v1_set_custom_regex     (gpointer data,
                                                   GRegex *successful,
                                                   GRegex *error);
gboolean mm_serial_parser_v1_parse                (gpointer parser,
                                                   GString *response,
                                                   gpointer log_object,
                                                   GError **error);
void     mm_serial_parser_v1_destroy              (gpointer parser);
gboolean mm_serial_parser_v1_is_known_error       (const GError *error);

/* Parser filter: when FALSE returned, error should be set. This error will be
 * reported to the response listener right away. */
typedef gboolean (* mm_serial_parser_v1_filter_fn) (gpointer data,
                                                    gpointer user_data,
                                                    GString *response,
                                                    GError **error);
void     mm_serial_parser_v1_add_filter (gpointer data,
                                         mm_serial_parser_v1_filter_fn callback,
                                         gpointer user_data);

#endif /* MM_SERIAL_PARSERS_H */
