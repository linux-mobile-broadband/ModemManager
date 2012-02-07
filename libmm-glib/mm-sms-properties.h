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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_SMS_PROPERTIES_H
#define MM_SMS_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

#include <libmm-common.h>

G_BEGIN_DECLS

typedef MMCommonSmsProperties     MMSmsProperties;
#define MM_TYPE_SMS_PROPERTIES(o) MM_TYPE_COMMON_SMS_PROPERTIES (o)
#define MM_SMS_PROPERTIES(o)      MM_COMMON_SMS_PROPERTIES(o)
#define MM_IS_SMS_PROPERTIES(o)   MM_IS_COMMON_SMS_PROPERTIES(o)

MMSmsProperties *mm_sms_properties_new (void);
MMSmsProperties *mm_sms_properties_new_from_string (
    const gchar *str,
    GError **error);

void mm_sms_properties_set_text (
    MMSmsProperties *properties,
    const gchar *text);
void mm_sms_properties_set_number (
    MMSmsProperties *properties,
    const gchar *number);
void mm_sms_properties_set_smsc (
    MMSmsProperties *properties,
    const gchar *smsc);
void mm_sms_properties_set_validity (
    MMSmsProperties *properties,
    guint validity);
void mm_sms_properties_set_class (
    MMSmsProperties *properties,
    guint class);

G_END_DECLS

#endif /* MM_SMS_PROPERTIES_H */
