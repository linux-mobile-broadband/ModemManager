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

#ifndef LIBWMC_UTILS_H
#define LIBWMC_UTILS_H

#include <sys/types.h>

typedef u_int8_t wmcbool;
#ifndef TRUE
#define TRUE ((u_int8_t) 1)
#endif
#ifndef FALSE
#define FALSE ((u_int8_t) 0)
#endif

#define DIAG_CONTROL_CHAR 0x7E
#define DIAG_TRAILER_LEN  3

/* Utility and testcase functions */

u_int16_t wmc_crc16 (const char *buffer, size_t len, u_int16_t seed);

size_t hdlc_escape (const char *inbuf,
                    size_t inbuf_len,
                    wmcbool escape_all_ctrl,
                    char *outbuf,
                    size_t outbuf_len);

size_t hdlc_unescape (const char *inbuf,
                      size_t inbuf_len,
                      char *outbuf,
                      size_t outbuf_len,
                      wmcbool *escaping);

size_t hdlc_encapsulate_buffer (char *inbuf,
                                size_t cmd_len,
                                size_t inbuf_len,
                                u_int16_t crc_seed,
                                wmcbool add_trailer,
                                wmcbool escape_all_ctrl,
                                char *outbuf,
                                size_t outbuf_len);

wmcbool hdlc_decapsulate_buffer (const char *inbuf,
                                 size_t inbuf_len,
                                 wmcbool check_known_crc,
                                 u_int16_t known_crc,
                                 char *outbuf,
                                 size_t outbuf_len,
                                 size_t *out_decap_len,
                                 size_t *out_used,
                                 wmcbool *out_need_more);

/* Functions for actual communication */

size_t wmc_encapsulate (char *inbuf,
                        size_t cmd_len,
                        size_t inbuf_len,
                        char *outbuf,
                        size_t outbuf_len,
                        wmcbool uml290);

wmcbool wmc_decapsulate (const char *inbuf,
                         size_t inbuf_len,
                         char *outbuf,
                         size_t outbuf_len,
                         size_t *out_decap_len,
                         size_t *out_used,
                         wmcbool *out_need_more,
                         wmcbool uml290);

#endif  /* LIBWMC_UTILS_H */

