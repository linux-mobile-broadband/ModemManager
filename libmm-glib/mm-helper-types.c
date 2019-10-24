/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include "mm-helper-types.h"

/**
 * mm_modem_port_info_array_free:
 * @array: an array of #MMModemPortInfo values.
 * @array_size: length of @array.
 *
 * Frees an array of #MMModemPortInfo values.
 *
 * Since: 1.0
 */
void
mm_modem_port_info_array_free (MMModemPortInfo *array,
                               guint array_size)
{
    guint i;

    for (i = 0; i < array_size; i++)
        g_free (array[i].name);
    g_free (array);
}
