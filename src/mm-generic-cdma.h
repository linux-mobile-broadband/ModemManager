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
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_GENERIC_CDMA_H
#define MM_GENERIC_CDMA_H

#include "mm-modem.h"
#include "mm-modem-base.h"
#include "mm-modem-cdma.h"
#include "mm-serial-port.h"

#define MM_TYPE_GENERIC_CDMA            (mm_generic_cdma_get_type ())
#define MM_GENERIC_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GENERIC_CDMA, MMGenericCdma))
#define MM_GENERIC_CDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_GENERIC_CDMA, MMGenericCdmaClass))
#define MM_IS_GENERIC_CDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GENERIC_CDMA))
#define MM_IS_GENERIC_CDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_GENERIC_CDMA))
#define MM_GENERIC_CDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_GENERIC_CDMA, MMGenericCdmaClass))

typedef struct {
    MMModemBase parent;
} MMGenericCdma;

typedef struct {
    MMModemBaseClass parent;

    void (*query_registration_state) (MMGenericCdma *self,
                                      MMModemUIntFn callback,
                                      gpointer user_data);
} MMGenericCdmaClass;

GType mm_generic_cdma_get_type (void);

MMModem *mm_generic_cdma_new (const char *device,
                              const char *driver,
                              const char *plugin);

/* Private, for subclasses */

MMPort * mm_generic_cdma_grab_port (MMGenericCdma *self,
                                    const char *subsys,
                                    const char *name,
                                    MMPortType suggested_type,
                                    gpointer user_data,
                                    GError **error);

MMSerialPort *mm_generic_cdma_get_port (MMGenericCdma *modem, MMPortType ptype);

void mm_generic_cdma_set_registration_state (MMGenericCdma *self,
                                             MMModemCdmaRegistrationState new_state);

MMModemCdmaRegistrationState mm_generic_cdma_get_registration_state_sync (MMGenericCdma *self);

#endif /* MM_GENERIC_CDMA_H */
