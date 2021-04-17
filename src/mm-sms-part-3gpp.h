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
 * Copyright (C) 2011 - 2012 Red Hat, Inc.
 * Copyright (C) 2013 Google, Inc.
 */

#ifndef MM_SMS_PART_3GPP_H
#define MM_SMS_PART_3GPP_H

#include <glib.h>
#include <ModemManager.h>

#include "mm-sms-part.h"

MMSmsPart *mm_sms_part_3gpp_new_from_pdu        (guint          index,
                                                 const gchar   *hexpdu,
                                                 gpointer       log_object,
                                                 GError       **error);
MMSmsPart *mm_sms_part_3gpp_new_from_binary_pdu (guint          index,
                                                 const guint8  *pdu,
                                                 gsize          pdu_len,
                                                 gpointer       log_object,
                                                 gboolean	transfer_route,
                                                 GError       **error);
guint8    *mm_sms_part_3gpp_get_submit_pdu      (MMSmsPart     *part,
                                                 guint         *out_pdulen,
                                                 guint         *out_msgstart,
                                                 gpointer       log_object,
                                                 GError       **error);

/* For testcases only */

guint       mm_sms_part_3gpp_encode_address   (const gchar   *address,
                                               guint8        *buf,
                                               gsize          buflen,
                                               gboolean       is_smsc);
gchar      **mm_sms_part_3gpp_util_split_text (const gchar   *text,
                                               MMSmsEncoding *encoding,
                                               gpointer       log_object);
GByteArray **mm_sms_part_3gpp_util_split_data (const guint8  *data,
                                               gsize          data_len);

#endif /* MM_SMS_PART_3GPP_H */
