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
 * Copyright (C) 2010 Red Hat, Inc.
 */

/******************************************
 * Generic utilities for Icera-based modems
 ******************************************/

#ifndef MM_ICERA_UTILS_H
#define MM_ICERA_UTILS_H

#include "mm-generic-gsm.h"

void mm_icera_utils_get_allowed_mode (MMGenericGsm *gsm,
                                      MMModemUIntFn callback,
                                      gpointer user_data);

void mm_icera_utils_set_allowed_mode (MMGenericGsm *gsm,
                                      MMModemGsmAllowedMode mode,
                                      MMModemFn callback,
                                      gpointer user_data);

void mm_icera_utils_register_unsolicted_handlers (MMGenericGsm *modem,
                                                  MMAtSerialPort *port);

void mm_icera_utils_change_unsolicited_messages (MMGenericGsm *modem,
                                                 gboolean enabled);

void mm_icera_utils_get_access_technology (MMGenericGsm *modem,
                                           MMModemUIntFn callback,
                                           gpointer user_data);

void mm_icera_utils_is_icera (MMGenericGsm *modem,
                              MMModemUIntFn callback,
                              gpointer user_data);

#endif  /* MM_ICERA_UTILS_H */

