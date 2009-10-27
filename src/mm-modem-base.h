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

#ifndef MM_MODEM_BASE_H
#define MM_MODEM_BASE_H

#include <glib.h>
#include <glib/gtypes.h>
#include <glib-object.h>

#include "mm-port.h"

#define MM_TYPE_MODEM_BASE            (mm_modem_base_get_type ())
#define MM_MODEM_BASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_BASE, MMModemBase))
#define MM_MODEM_BASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_BASE, MMModemBaseClass))
#define MM_IS_MODEM_BASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_BASE))
#define MM_IS_MODEM_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_BASE))
#define MM_MODEM_BASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_BASE, MMModemBaseClass))

typedef struct _MMModemBase MMModemBase;
typedef struct _MMModemBaseClass MMModemBaseClass;

struct _MMModemBase {
    GObject parent;
};

struct _MMModemBaseClass {
    GObjectClass parent;
};

GType mm_modem_base_get_type (void);

MMPort *mm_modem_base_get_port     (MMModemBase *self,
                                    const char *subsys,
                                    const char *name);

MMPort *mm_modem_base_add_port     (MMModemBase *self,
                                    const char *subsys,
                                    const char *name,
                                    MMPortType ptype);

gboolean mm_modem_base_remove_port (MMModemBase *self,
                                    MMPort *port);

void mm_modem_base_set_valid (MMModemBase *self,
                              gboolean valid);

gboolean mm_modem_base_get_valid (MMModemBase *self);

#endif /* MM_MODEM_BASE_H */

