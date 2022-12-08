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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_COMMON_ZTE_H
#define MM_COMMON_ZTE_H

#include "mm-broadband-modem.h"

typedef struct _MMCommonZteUnsolicitedSetup MMCommonZteUnsolicitedSetup;
MMCommonZteUnsolicitedSetup *mm_common_zte_unsolicited_setup_new  (void);
void                         mm_common_zte_unsolicited_setup_free (MMCommonZteUnsolicitedSetup *setup);

void mm_common_zte_set_unsolicited_events_handlers (MMBroadbandModem *self,
                                                    MMCommonZteUnsolicitedSetup *setup,
                                                    gboolean enable);

#endif /* MM_COMMON_ZTE_H */
