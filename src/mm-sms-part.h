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
#include <ModemManager.h>

/* Despite 3GPP TS 23.038 specifies that Unicode SMS messages are
 * encoded in UCS-2, UTF-16 encoding is commonly used instead on many
 * modern platforms to allow encoding code points that fall outside the
 * Basic Multilingual Plane (BMP), such as Emoji. Most of the UCS-2
 * code points are identical to their equivalent UTF-16 code points.
 * In UTF-16, non-BMP code points are encoded in a pair of surrogate
 * code points (i.e. a high surrogate in 0xD800..0xDBFF, followed by a
 * low surrogate in 0xDC00..0xDFFF). An isolated surrogate code point
 * has no general interpretation in UTF-16, but could be a valid
 * (though unmapped) code point in UCS-2.
 *
 * The current implementation in ModemManager just assumes that whenever
 * possible (i.e. when parsing received PDUs or when creating submit
 * PDUs) UTF-16 will be used instead of plain UCS-2 (even if the PDUs
 * report the encoding as UCS-2).
 */
typedef enum { /*< underscore_name=mm_sms_encoding >*/
    MM_SMS_ENCODING_UNKNOWN = 0x0,
    MM_SMS_ENCODING_GSM7,
    MM_SMS_ENCODING_8BIT,
    MM_SMS_ENCODING_UCS2,
} MMSmsEncoding;

typedef struct _MMSmsPart MMSmsPart;

#define SMS_PART_INVALID_INDEX G_MAXUINT

#define MM_SMS_PART_IS_3GPP(part)                                       \
    (mm_sms_part_get_pdu_type (part) >= MM_SMS_PDU_TYPE_DELIVER &&      \
     mm_sms_part_get_pdu_type (part) <= MM_SMS_PDU_TYPE_STATUS_REPORT)

#define MM_SMS_PART_IS_CDMA(part)                                       \
    (mm_sms_part_get_pdu_type (part) >= MM_SMS_PDU_TYPE_CDMA_DELIVER && \
     mm_sms_part_get_pdu_type (part) <= MM_SMS_PDU_TYPE_CDMA_READ_ACKNOWLEDGEMENT)

MMSmsPart *mm_sms_part_new  (guint index,
                             MMSmsPduType type);
void       mm_sms_part_free (MMSmsPart *part);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSmsPart, mm_sms_part_free)

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

/* CDMA specific */
MMSmsCdmaTeleserviceId   mm_sms_part_get_cdma_teleservice_id   (MMSmsPart *part);
void                     mm_sms_part_set_cdma_teleservice_id   (MMSmsPart *part,
                                                                MMSmsCdmaTeleserviceId cdma_teleservice_id);
MMSmsCdmaServiceCategory mm_sms_part_get_cdma_service_category (MMSmsPart *part);
void                     mm_sms_part_set_cdma_service_category (MMSmsPart *part,
                                                                MMSmsCdmaServiceCategory cdma_service_category);

#endif /* MM_SMS_PART_H */
