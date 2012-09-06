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

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-sms-qmi.h"
#include "mm-base-modem.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMSmsQmi, mm_sms_qmi, MM_TYPE_SMS);

/*****************************************************************************/

MMSms *
mm_sms_qmi_new (MMBaseModem *modem)
{
    return MM_SMS (g_object_new (MM_TYPE_SMS_QMI,
                                 MM_SMS_MODEM, modem,
                                 NULL));
}

static void
mm_sms_qmi_init (MMSmsQmi *self)
{
}

static void
mm_sms_qmi_class_init (MMSmsQmiClass *klass)
{
}
