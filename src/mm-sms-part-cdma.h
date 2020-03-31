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
 * Copyright (C) 2013 Google, Inc.
 */

#ifndef MM_SMS_PART_CDMA_H
#define MM_SMS_PART_CDMA_H

#include <glib.h>
#include <ModemManager.h>

#include "mm-sms-part.h"

MMSmsPart *mm_sms_part_cdma_new_from_pdu        (guint         index,
                                                 const gchar   *hexpdu,
                                                 gpointer       log_object,
                                                 GError       **error);
MMSmsPart *mm_sms_part_cdma_new_from_binary_pdu (guint          index,
                                                 const guint8  *pdu,
                                                 gsize          pdu_len,
                                                 gpointer       log_object,
                                                 GError       **error);
guint8    *mm_sms_part_cdma_get_submit_pdu      (MMSmsPart     *part,
                                                 guint         *out_pdulen,
                                                 gpointer       log_object,
                                                 GError       **error);

#endif /* MM_SMS_PART_CDMA_H */
