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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef TEST_PORT_CONTEXT_H
#define TEST_PORT_CONTEXT_H

#include <glib.h>
#include <glib-object.h>

typedef struct _TestPortContext TestPortContext;

TestPortContext *test_port_context_new           (const gchar *name);
void             test_port_context_start         (TestPortContext *self);
void             test_port_context_stop          (TestPortContext *self);
void             test_port_context_free          (TestPortContext *self);

void             test_port_context_set_command   (TestPortContext *self,
                                                  const gchar *command,
                                                  const gchar *response);
void             test_port_context_load_commands (TestPortContext *self,
                                                  const gchar *commands_file);

#endif /* TEST_PORT_CONTEXT_H */
