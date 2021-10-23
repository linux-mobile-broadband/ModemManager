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
 * Copyright (C) 2012 Google, Inc.
 */

#include <ctype.h>
#include <string.h>

#include <glib.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-sms-part.h"
#include "mm-charsets.h"
#include "mm-log.h"

struct _MMSmsPart {
    guint index;
    MMSmsPduType pdu_type;
    gchar *smsc;
    gchar *timestamp;
    gchar *discharge_timestamp;
    gchar *number;
    gchar *text;
    MMSmsEncoding encoding;
    GByteArray *data;
    gint  class;
    guint validity_relative;
    gboolean delivery_report_request;
    guint message_reference;
    guint message_id;
    /* NOT a MMSmsDeliveryState, which just includes the known values */
    guint delivery_state;

    gboolean should_concat;
    guint concat_reference;
    guint concat_max;
    guint concat_sequence;

    /* CDMA specific */
    MMSmsCdmaTeleserviceId cdma_teleservice_id;
    MMSmsCdmaServiceCategory cdma_service_category;
};

void
mm_sms_part_free (MMSmsPart *self)
{
    g_free (self->discharge_timestamp);
    g_free (self->timestamp);
    g_free (self->smsc);
    g_free (self->number);
    g_free (self->text);
    if (self->data)
        g_byte_array_unref (self->data);
    g_slice_free (MMSmsPart, self);
}

#define PART_GET_FUNC(type, name)             \
    type                                      \
    mm_sms_part_get_##name (MMSmsPart *self)  \
    {                                         \
        return self->name;                    \
    }

#define PART_SET_FUNC(type, name)             \
    void                                      \
    mm_sms_part_set_##name (MMSmsPart *self,  \
                            type value)       \
    {                                         \
        self->name = value;                   \
    }

#define PART_SET_TAKE_STR_FUNC(name)             \
    void                                         \
    mm_sms_part_set_##name (MMSmsPart *self,     \
                            const gchar *value)  \
    {                                            \
        g_free (self->name);                     \
        self->name = g_strdup (value);           \
    }                                            \
                                                 \
    void                                         \
    mm_sms_part_take_##name (MMSmsPart *self,    \
                             gchar *value)       \
    {                                            \
        g_free (self->name);                     \
        self->name = value;                      \
    }

PART_GET_FUNC (guint, index)
PART_SET_FUNC (guint, index)
PART_GET_FUNC (MMSmsPduType, pdu_type)
PART_SET_FUNC (MMSmsPduType, pdu_type)
PART_GET_FUNC (const gchar *, smsc)
PART_SET_TAKE_STR_FUNC (smsc)
PART_GET_FUNC (const gchar *, number)
PART_SET_TAKE_STR_FUNC (number)
PART_GET_FUNC (const gchar *, timestamp)
PART_SET_TAKE_STR_FUNC (timestamp)
PART_GET_FUNC (const gchar *, discharge_timestamp)
PART_SET_TAKE_STR_FUNC (discharge_timestamp)
PART_GET_FUNC (guint, concat_max)
PART_SET_FUNC (guint, concat_max)
PART_GET_FUNC (guint, concat_sequence)
PART_SET_FUNC (guint, concat_sequence)
PART_GET_FUNC (const gchar *, text)
PART_SET_TAKE_STR_FUNC (text)
PART_GET_FUNC (MMSmsEncoding, encoding)
PART_SET_FUNC (MMSmsEncoding, encoding)
PART_GET_FUNC (gint,  class)
PART_SET_FUNC (gint,  class)
PART_GET_FUNC (guint, validity_relative)
PART_SET_FUNC (guint, validity_relative)
PART_GET_FUNC (gboolean, delivery_report_request)
PART_SET_FUNC (gboolean, delivery_report_request)
PART_GET_FUNC (guint, message_id)
PART_SET_FUNC (guint, message_id)
PART_GET_FUNC (guint, message_reference)
PART_SET_FUNC (guint, message_reference)
PART_GET_FUNC (guint, delivery_state)
PART_SET_FUNC (guint, delivery_state)

PART_GET_FUNC (guint, concat_reference)

void
mm_sms_part_set_concat_reference (MMSmsPart *self,
                                  guint value)
{
    self->should_concat = TRUE;
    self->concat_reference = value;
}

PART_GET_FUNC (const GByteArray *, data)

void
mm_sms_part_set_data (MMSmsPart *self,
                      GByteArray *value)
{
    if (self->data)
        g_byte_array_unref (self->data);
    self->data = (value ? g_byte_array_ref (value) : NULL);
}

void
mm_sms_part_take_data (MMSmsPart *self,
                       GByteArray *value)
{
    if (self->data)
        g_byte_array_unref (self->data);
    self->data = value;
}

gboolean
mm_sms_part_should_concat (MMSmsPart *self)
{
    return self->should_concat;
}

PART_GET_FUNC (MMSmsCdmaTeleserviceId, cdma_teleservice_id)
PART_SET_FUNC (MMSmsCdmaTeleserviceId, cdma_teleservice_id)
PART_GET_FUNC (MMSmsCdmaServiceCategory, cdma_service_category)
PART_SET_FUNC (MMSmsCdmaServiceCategory, cdma_service_category)

MMSmsPart *
mm_sms_part_new (guint index,
                 MMSmsPduType pdu_type)
{
    MMSmsPart *sms_part;

    sms_part = g_slice_new0 (MMSmsPart);
    sms_part->index = index;
    sms_part->pdu_type = pdu_type;
    sms_part->encoding = MM_SMS_ENCODING_UNKNOWN;
    sms_part->delivery_state = MM_SMS_DELIVERY_STATE_UNKNOWN;
    sms_part->cdma_teleservice_id = MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN;
    sms_part->cdma_service_category = MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN;
    sms_part->class = -1;

    return sms_part;
}
