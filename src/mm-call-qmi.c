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
 * Copyright (C) 2021 Joel Selvaraj <jo@jsfamily.in>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem-qmi.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-voice.h"
#include "mm-call-qmi.h"
#include "mm-base-modem.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMCallQmi, mm_call_qmi, MM_TYPE_BASE_CALL)

MMBaseCall *
mm_call_qmi_new (MMBaseModem     *modem,
                 MMCallDirection  direction,
                 const gchar     *number)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_CALL_QMI,
                                       MM_BASE_CALL_MODEM, modem,
                                       "direction",        direction,
                                       "number",           number,
                                       MM_BASE_CALL_SKIP_INCOMING_TIMEOUT,       TRUE,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
}

static void
mm_call_qmi_init (MMCallQmi *self)
{
}

static void
mm_call_qmi_class_init (MMCallQmiClass *klass)
{
}
