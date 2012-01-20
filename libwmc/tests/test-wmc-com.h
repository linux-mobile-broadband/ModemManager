/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TEST_WMC_COM_H
#define TEST_WMC_COM_H

#include "utils.h"

gpointer test_com_setup (const char *port, wmcbool uml290, wmcbool debug);
void test_com_teardown (gpointer d);

void test_com_port_init (void *f, void *data);

void test_com_init (void *f, void *data);

void test_com_device_info (void *f, void *data);

void test_com_network_info (void *f, void *data);

void test_com_get_global_mode (void *f, void *data);

#endif  /* TEST_WMC_COM_H */

