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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#ifndef MM_ERROR_HELPERS_H
#define MM_ERROR_HELPERS_H

#include <glib-object.h>

#include <ModemManager.h>
#include <libmm-glib.h>

GError *mm_connection_error_for_code         (MMConnectionError       code, gpointer log_object);
GError *mm_mobile_equipment_error_for_code   (MMMobileEquipmentError  code, gpointer log_object);
GError *mm_mobile_equipment_error_for_string (const gchar            *str,  gpointer log_object);
GError *mm_message_error_for_code            (MMMessageError          code, gpointer log_object);
GError *mm_message_error_for_string          (const gchar            *str,  gpointer log_object);

#endif /* MM_ERROR_HELPERS_H */
