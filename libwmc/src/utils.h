/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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

#define DIAG_CONTROL_CHAR 0x7E
#define DIAG_TRAILER_LEN  3

/* Utility and testcase functions */

guint16 crc16 (const char *buffer, gsize len, guint16 seed);

gsize hdlc_escape (const char *inbuf,
                   gsize inbuf_len,
                   gboolean escape_all_ctrl,
                   char *outbuf,
                   gsize outbuf_len);

gsize hdlc_unescape (const char *inbuf,
                     gsize inbuf_len,
                     char *outbuf,
                     gsize outbuf_len,
                     gboolean *escaping);

gsize hdlc_encapsulate_buffer (char *inbuf,
                               gsize cmd_len,
                               gsize inbuf_len,
                               guint16 crc_seed,
                               gboolean add_trailer,
                               gboolean escape_all_ctrl,
                               char *outbuf,
                               gsize outbuf_len);

gboolean hdlc_decapsulate_buffer (const char *inbuf,
                                  gsize inbuf_len,
                                  gboolean check_known_crc,
                                  guint16 known_crc,
                                  char *outbuf,
                                  gsize outbuf_len,
                                  gsize *out_decap_len,
                                  gsize *out_used,
                                  gboolean *out_need_more);

/* Functions for actual communication */

gsize wmc_encapsulate (char *inbuf,
                       gsize cmd_len,
                       gsize inbuf_len,
                       char *outbuf,
                       gsize outbuf_len,
                       gboolean uml290);

gboolean wmc_decapsulate (const char *inbuf,
                          gsize inbuf_len,
                          char *outbuf,
                          gsize outbuf_len,
                          gsize *out_decap_len,
                          gsize *out_used,
                          gboolean *out_need_more,
                          gboolean uml290);

#endif  /* UTILS_H */

