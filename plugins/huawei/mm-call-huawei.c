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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
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

#include "mm-log.h"
#include "mm-base-modem-at.h"
#include "mm-call-huawei.h"

G_DEFINE_TYPE (MMCallHuawei, mm_call_huawei, MM_TYPE_BASE_CALL)

/*****************************************************************************/

MMBaseCall *
mm_call_huawei_new (MMBaseModem *modem)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_CALL_HUAWEI,
                                       MM_BASE_CALL_MODEM, modem,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
}

static void
mm_call_huawei_init (MMCallHuawei *self)
{
}

static void
mm_call_huawei_class_init (MMCallHuaweiClass *klass)
{
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    base_call_class->setup_unsolicited_events   = NULL;
    base_call_class->cleanup_unsolicited_events = NULL;
}
