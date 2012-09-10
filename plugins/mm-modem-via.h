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
 * Copyright (C) 2012 Red Hat, Inc.
 */

#ifndef MM_MODEM_VIA_H
#define MM_MODEM_VIA_H

#include "mm-generic-cdma.h"

#define MM_TYPE_MODEM_VIA            (mm_modem_via_get_type ())
#define MM_MODEM_VIA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_VIA, MMModemVia))
#define MM_MODEM_VIA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_VIA, MMModemViaClass))
#define MM_IS_MODEM_VIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_VIA))
#define MM_IS_MODEM_VIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_VIA))
#define MM_MODEM_VIA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_VIA, MMModemViaClass))

typedef struct {
    MMGenericCdma parent;
} MMModemVia;

typedef struct {
    MMGenericCdmaClass parent;
} MMModemViaClass;

GType mm_modem_via_get_type (void);

MMModem *mm_modem_via_new (const char *device,
                           const char *driver,
                           const char *plugin,
                           gboolean evdo_rev0,
                           gboolean evdo_revA,
                           guint32 vendor,
                           guint32 product);

#endif /* MM_MODEM_VIA_H */
