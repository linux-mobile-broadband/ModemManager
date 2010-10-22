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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#ifndef MM_MODEM_SIMTECH_GSM_H
#define MM_MODEM_SIMTECH_GSM_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_SIMTECH_GSM            (mm_modem_simtech_gsm_get_type ())
#define MM_MODEM_SIMTECH_GSM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_SIMTECH_GSM, MMModemSimtechGsm))
#define MM_MODEM_SIMTECH_GSM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_SIMTECH_GSM, MMModemSimtechGsmClass))
#define MM_IS_MODEM_SIMTECH_GSM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_SIMTECH_GSM))
#define MM_IS_MODEM_SIMTECH_GSM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_SIMTECH_GSM))
#define MM_MODEM_SIMTECH_GSM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_SIMTECH_GSM, MMModemSimtechGsmClass))

typedef struct {
    MMGenericGsm parent;
} MMModemSimtechGsm;

typedef struct {
    MMGenericGsmClass parent;
} MMModemSimtechGsmClass;

GType mm_modem_simtech_gsm_get_type (void);

MMModem *mm_modem_simtech_gsm_new (const char *device,
                                     const char *driver,
                                     const char *plugin,
                                     guint32 vendor,
                                     guint32 product);

#endif /* MM_MODEM_SIMTECH_H */
