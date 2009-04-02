/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

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
