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

GError *mm_connection_error_for_code         (MMConnectionError       code,  gpointer log_object);
GError *mm_mobile_equipment_error_for_code   (MMMobileEquipmentError  code,  gpointer log_object);
GError *mm_mobile_equipment_error_for_string (const gchar            *str,   gpointer log_object);
GError *mm_message_error_for_code            (MMMessageError          code,  gpointer log_object);
GError *mm_message_error_for_string          (const gchar            *str,   gpointer log_object);
GError *mm_normalize_error                   (const GError           *error);
void    mm_register_error_mapping            (GQuark                  input_error_domain,
                                              gint                    input_error_code,
                                              GQuark                  output_error_domain,
                                              gint                    output_error_code);

/* Replacements for the dbus method invocation completions but with our error normalization
 * procedure in place, so that we only report back MM-specific errors. */
void mm_dbus_method_invocation_take_error           (GDBusMethodInvocation *invocation,
                                                     GError                *error);
void mm_dbus_method_invocation_return_error_literal (GDBusMethodInvocation *invocation,
                                                     GQuark                 domain,
                                                     gint                   code,
                                                     const gchar           *message);
void mm_dbus_method_invocation_return_error         (GDBusMethodInvocation *invocation,
                                                     GQuark                 domain,
                                                     gint                   code,
                                                     const gchar           *format,
                                                     ...) G_GNUC_PRINTF(4, 5);;
void mm_dbus_method_invocation_return_gerror        (GDBusMethodInvocation *invocation,
                                                     const GError          *error);

#endif /* MM_ERROR_HELPERS_H */
