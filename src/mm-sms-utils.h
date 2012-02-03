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

guint8 *sms_create_submit_pdu (const char *number,
                               const char *text,
                               const char *smsc,
                               guint validity,
                               guint class,
                               guint *out_pdulen,
                               guint *out_msgstart,
                               GError **error);

GHashTable *sms_properties_hash_new (const char *smsc,
                                     const char *number,
                                     const char *timestamp,
                                     const char *text,
                                     const GByteArray *data,
                                     guint data_coding_scheme,
                                     guint *class);

/* For testcases only */
guint sms_encode_address (const char *address,
                          guint8 *buf,
                          size_t buflen,
                          gboolean is_smsc);


#endif /* MM_SMS_UTILS_H */
