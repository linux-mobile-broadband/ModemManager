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

#ifndef MM_CHILD_H
#define MM_CHILD_H

#include <glib.h>
#include <glib-object.h>

/* Properties */
#define MM_BIND_TO "bind-to"

/* Property name used by an object that wants to allow others
 * to bind its connection property.
 */
#define MM_BINDABLE_CONNECTION "bind-connection"

#define MM_TYPE_BIND mm_bind_get_type ()
G_DECLARE_INTERFACE (MMBind, mm_bind, MM, BIND, MMBind)

struct _MMBindInterface
{
  GTypeInterface g_iface;
};

gboolean mm_bind_to (MMBind      *self,
                     const gchar *local_propname,
                     GObject     *other_object);

#endif /* MM_LOG_OBJECT_H */
