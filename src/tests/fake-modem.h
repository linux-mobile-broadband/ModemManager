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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#ifndef MM_FAKE_MODEM_H
#define MM_FAKE_MODEM_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-modem.h"
#include "mm-call-list.h"

#define MM_TYPE_FAKE_MODEM            (mm_fake_modem_get_type ())
#define MM_FAKE_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_FAKE_MODEM, MMFakeModem))
#define MM_FAKE_MODEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_FAKE_MODEM, MMFakeModemClass))
#define MM_IS_FAKE_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_FAKE_MODEM))
#define MM_IS_FAKE_MODEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_FAKE_MODEM))
#define MM_FAKE_MODEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_FAKE_MODEM, MMFakeModemClass))

typedef struct _MMFakeModem MMFakeModem;
typedef struct _MMFakeModemClass MMFakeModemClass;
typedef struct _MMFakeModemPrivate MMFakeModemPrivate;

struct _MMFakeModem {
    MMBaseModem parent;
    MMFakeModemPrivate *priv;
};

struct _MMFakeModemClass {
    MMBaseModemClass parent;
};

GType mm_fake_modem_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMFakeModem, g_object_unref)

MMFakeModem *mm_fake_modem_new (GDBusConnection *connection);

const gchar *mm_fake_modem_get_path (MMFakeModem *self);

gboolean mm_fake_modem_export_interfaces (MMFakeModem  *self,
                                          GError      **error);

MMCallList *mm_fake_modem_get_call_list (MMFakeModem *self);

#endif /* MM_FAKE_MODEM_H */
