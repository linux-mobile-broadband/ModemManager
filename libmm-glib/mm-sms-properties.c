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

#include "mm-sms-properties.h"

void
mm_sms_properties_set_text (MMSmsProperties *self,
                            const gchar *text)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    mm_common_sms_properties_set_text (self, text);
}

void
mm_sms_properties_set_number (MMSmsProperties *self,
                                 const gchar *number)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    mm_common_sms_properties_set_number (self, number);
}

void
mm_sms_properties_set_smsc (MMSmsProperties *self,
                               const gchar *smsc)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    mm_common_sms_properties_set_smsc (self, smsc);
}

void
mm_sms_properties_set_validity (MMSmsProperties *self,
                                guint validity)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    mm_common_sms_properties_set_validity (self, validity);
}

void
mm_sms_properties_set_class (MMSmsProperties *self,
                                guint class)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    mm_common_sms_properties_set_class (self, class);
}

/*****************************************************************************/

MMSmsProperties *
mm_sms_properties_new_from_string (const gchar *str,
                                   GError **error)
{
    return mm_common_sms_properties_new_from_string (str, error);
}

MMSmsProperties *
mm_sms_properties_new (void)
{
    return mm_common_sms_properties_new ();
}
