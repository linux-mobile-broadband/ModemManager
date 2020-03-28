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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_LOG_OBJECT_H
#define MM_LOG_OBJECT_H

#include <glib.h>
#include <glib-object.h>

#include "mm-log.h"

#define MM_TYPE_LOG_OBJECT mm_log_object_get_type ()
G_DECLARE_INTERFACE (MMLogObject, mm_log_object, MM, LOG_OBJECT, GObject)

struct _MMLogObjectInterface
{
  GTypeInterface g_iface;

  gchar * (* build_id) (MMLogObject *self);
};

const gchar *mm_log_object_get_id       (MMLogObject *self);
void         mm_log_object_set_owner_id (MMLogObject *self,
                                         const gchar *owner_id);

#endif /* MM_LOG_OBJECT_H */
