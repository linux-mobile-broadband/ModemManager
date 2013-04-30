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
#include <ModemManager-enums.h>

typedef enum {
    MM_SMS_ENCODING_UNKNOWN = 0x0,
    MM_SMS_ENCODING_GSM7,
    MM_SMS_ENCODING_8BIT,
    MM_SMS_ENCODING_UCS2
} MMSmsEncoding;

typedef struct _MMSmsPart MMSmsPart;

#define SMS_MAX_PDU_LEN 344
#define SMS_PART_INVALID_INDEX G_MAXUINT

MMSmsPart *mm_sms_part_new  (guint index,
                             MMSmsPduType type);
MMSmsPart *mm_sms_part_new_from_pdu  (guint index,
                                      const gchar *hexpdu,
                                      GError **error);
MMSmsPart *mm_sms_part_new_from_binary_pdu  (guint index,
                                             const guint8 *pdu,
                                             gsize pdu_len,
                                             GError **error);
void       mm_sms_part_free (MMSmsPart *part);

guint8    *mm_sms_part_get_submit_pdu (MMSmsPart *part,
                                       guint *out_pdulen,
                                       guint *out_msgstart,
                                       GError **error);

guint             mm_sms_part_get_index              (MMSmsPart *part);
void              mm_sms_part_set_index              (MMSmsPart *part,
                                                      guint index);

MMSmsPduType      mm_sms_part_get_pdu_type           (MMSmsPart *part);
void              mm_sms_part_set_pdu_type           (MMSmsPart *part,
                                                      MMSmsPduType type);

const gchar      *mm_sms_part_get_smsc               (MMSmsPart *part);
void              mm_sms_part_set_smsc               (MMSmsPart *part,
                                                      const gchar *smsc);
void              mm_sms_part_take_smsc              (MMSmsPart *part,
                                                      gchar *smsc);

const gchar      *mm_sms_part_get_number             (MMSmsPart *part);
void              mm_sms_part_set_number             (MMSmsPart *part,
                                                      const gchar *number);
void              mm_sms_part_take_number            (MMSmsPart *part,
                                                      gchar *number);

const gchar      *mm_sms_part_get_timestamp          (MMSmsPart *part);
void              mm_sms_part_set_timestamp          (MMSmsPart *part,
                                                      const gchar *timestamp);
void              mm_sms_part_take_timestamp         (MMSmsPart *part,
                                                      gchar *timestamp);

const gchar      *mm_sms_part_get_discharge_timestamp  (MMSmsPart *part);
void              mm_sms_part_set_discharge_timestamp  (MMSmsPart *part,
                                                        const gchar *timestamp);
void              mm_sms_part_take_discharge_timestamp (MMSmsPart *part,
                                                        gchar *timestamp);

const gchar      *mm_sms_part_get_text               (MMSmsPart *part);
void              mm_sms_part_set_text               (MMSmsPart *part,
                                                      const gchar *text);
void              mm_sms_part_take_text              (MMSmsPart *part,
                                                      gchar *text);

const GByteArray *mm_sms_part_get_data               (MMSmsPart *part);
void              mm_sms_part_set_data               (MMSmsPart *part,
                                                      GByteArray *data);
void              mm_sms_part_take_data              (MMSmsPart *part,
                                                      GByteArray *data);

MMSmsEncoding     mm_sms_part_get_encoding           (MMSmsPart *part);
void              mm_sms_part_set_encoding           (MMSmsPart *part,
                                                      MMSmsEncoding encoding);

gint              mm_sms_part_get_class              (MMSmsPart *part);
void              mm_sms_part_set_class              (MMSmsPart *part,
                                                      gint class);

guint             mm_sms_part_get_validity_relative  (MMSmsPart *part);
void              mm_sms_part_set_validity_relative  (MMSmsPart *part,
                                                      guint validity);

guint             mm_sms_part_get_delivery_state (MMSmsPart *part);
void              mm_sms_part_set_delivery_state (MMSmsPart *part,
                                                  guint delivery_state);

guint             mm_sms_part_get_message_reference  (MMSmsPart *part);
void              mm_sms_part_set_message_reference  (MMSmsPart *part,
                                                      guint message_reference);

gboolean          mm_sms_part_get_delivery_report_request (MMSmsPart *part);
void              mm_sms_part_set_delivery_report_request (MMSmsPart *part,
                                                           gboolean delivery_report_request);

guint             mm_sms_part_get_concat_reference   (MMSmsPart *part);
void              mm_sms_part_set_concat_reference   (MMSmsPart *part,
                                                      guint concat_reference);

guint             mm_sms_part_get_concat_max         (MMSmsPart *part);
void              mm_sms_part_set_concat_max         (MMSmsPart *part,
                                                      guint concat_max);
guint             mm_sms_part_get_concat_sequence    (MMSmsPart *part);
void              mm_sms_part_set_concat_sequence    (MMSmsPart *part,
                                                      guint concat_sequence);

gboolean          mm_sms_part_should_concat          (MMSmsPart *part);

/* For testcases only */
guint mm_sms_part_encode_address (const gchar *address,
                                  guint8 *buf,
                                  gsize buflen,
                                  gboolean is_smsc);

gchar **mm_sms_part_util_split_text (const gchar *text,
                                     MMSmsEncoding *encoding);

GByteArray **mm_sms_part_util_split_data (const guint8 *data,
                                          gsize data_len);

#endif /* MM_SMS_PART_H */
