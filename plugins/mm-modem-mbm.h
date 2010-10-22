/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2008 Ericsson AB
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *         Bjorn Runaker <bjorn.runaker@ericsson.com>
 *         Torgny Johansson <torgny.johansson@ericsson.com>
 *         Jonas Sjöquist <jonas.sjoquist@ericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef MM_MODEM_MBM_H
#define MM_MODEM_MBM_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_MBM              (mm_modem_mbm_get_type ())
#define MM_MODEM_MBM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_MBM, MMModemMbm))
#define MM_MODEM_MBM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_MBM, MMModemMbmClass))
#define MM_IS_MODEM_MBM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_MBM))
#define MM_IS_MODEM_MBM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_MBM))
#define MM_MODEM_MBM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_MBM, MMModemMbmClass))

typedef struct {
    MMGenericGsm parent;
} MMModemMbm;

typedef struct {
    MMGenericGsmClass parent;
} MMModemMbmClass;

GType mm_modem_mbm_get_type (void);

MMModem *mm_modem_mbm_new (const char *device,
                           const char *driver,
                           const char *plugin_name,
                           guint32 vendor,
                           guint32 product);

#endif /* MM_MODEM_MBM_H */
