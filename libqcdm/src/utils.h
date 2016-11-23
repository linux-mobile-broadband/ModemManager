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

#ifndef LIBQCDM_UTILS_H
#define LIBQCDM_UTILS_H

#include <config.h>
#include <stdint.h>

typedef uint8_t qcdmbool;
#ifndef TRUE
#define TRUE ((uint8_t) 1)
#endif
#ifndef FALSE
#define FALSE ((uint8_t) 0)
#endif

#define DIAG_CONTROL_CHAR 0x7E
#define DIAG_TRAILER_LEN  3

uint16_t dm_crc16 (const char *buffer, size_t len);

size_t dm_escape (const char *inbuf,
                  size_t inbuf_len,
                  char *outbuf,
                  size_t outbuf_len);

size_t dm_unescape (const char *inbuf,
                    size_t inbuf_len,
                    char *outbuf,
                    size_t outbuf_len,
                    qcdmbool *escaping);

size_t dm_encapsulate_buffer (char *inbuf,
                              size_t cmd_len,
                              size_t inbuf_len,
                              char *outbuf,
                              size_t outbuf_len);

qcdmbool dm_decapsulate_buffer (const char *inbuf,
                                size_t inbuf_len,
                                char *outbuf,
                                size_t outbuf_len,
                                size_t *out_decap_len,
                                size_t *out_used,
                                qcdmbool *out_need_more);

#endif  /* LIBQCDM_UTILS_H */
