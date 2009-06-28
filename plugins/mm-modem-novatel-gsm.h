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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_MODEM_NOVATEL_GSM_H
#define MM_MODEM_NOVATEL_GSM_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_NOVATEL_GSM            (mm_modem_novatel_gsm_get_type ())
#define MM_MODEM_NOVATEL_GSM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_NOVATEL_GSM, MMModemNovatelGsm))
#define MM_MODEM_NOVATEL_GSM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_NOVATEL_GSM, MMModemNovatelGsmClass))
#define MM_IS_MODEM_NOVATEL_GSM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_NOVATEL_GSM))
#define MM_IS_MODEM_NOVATEL_GSM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_NOVATEL_GSM))
#define MM_MODEM_NOVATEL_GSM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_NOVATEL_GSM, MMModemNovatelGsmClass))

typedef struct {
    MMGenericGsm parent;
} MMModemNovatelGsm;

typedef struct {
    MMGenericGsmClass parent;
} MMModemNovatelGsmClass;

GType mm_modem_novatel_gsm_get_type (void);

MMModem *mm_modem_novatel_gsm_new (const char *device,
                                   const char *driver,
                                   const char *plugin_name);

#endif /* MM_MODEM_NOVATEL_GSM_H */
