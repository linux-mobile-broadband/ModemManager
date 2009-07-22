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

gpointer mm_serial_parser_v0_new     (void);
gboolean mm_serial_parser_v0_parse   (gpointer parser,
                                      GString *response,
                                      GError **error);

void     mm_serial_parser_v0_destroy (gpointer parser);


gpointer mm_serial_parser_v1_new     (void);
gboolean mm_serial_parser_v1_parse   (gpointer parser,
                                      GString *response,
                                      GError **error);

void     mm_serial_parser_v1_destroy (gpointer parser);


gpointer mm_serial_parser_v1_e1_new     (void);
gboolean mm_serial_parser_v1_e1_parse   (gpointer parser,
                                         GString *response,
                                         GError **error);

void     mm_serial_parser_v1_e1_destroy (gpointer parser);

#endif /* MM_SERIAL_PARSERS_H */
