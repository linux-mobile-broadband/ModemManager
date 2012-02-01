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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_SMS_PART_H
#define MM_SMS_PART_H

#include <glib.h>

typedef struct _MMSmsPart MMSmsPart;

#define SMS_MAX_PDU_LEN 344

MMSmsPart *mm_sms_part_new  (guint index,
                             const gchar *hexpdu,
                             GError **error);
void       mm_sms_part_free (MMSmsPart *part);

guint             mm_sms_part_get_index              (MMSmsPart *part);
const gchar      *mm_sms_part_get_smsc               (MMSmsPart *part);
const gchar      *mm_sms_part_get_number             (MMSmsPart *part);
const gchar      *mm_sms_part_get_timestamp          (MMSmsPart *part);
const gchar      *mm_sms_part_get_text               (MMSmsPart *part);
const GByteArray *mm_sms_part_get_data               (MMSmsPart *part);
guint             mm_sms_part_get_data_coding_scheme (MMSmsPart *part);
guint             mm_sms_part_get_class              (MMSmsPart *part);

gboolean          mm_sms_part_should_concat          (MMSmsPart *part);
guint             mm_sms_part_get_concat_reference   (MMSmsPart *part);
guint             mm_sms_part_get_concat_max         (MMSmsPart *part);
guint             mm_sms_part_get_concat_sequence    (MMSmsPart *part);

#endif /* MM_SMS_PART_H */
