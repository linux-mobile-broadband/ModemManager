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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_COMMON_NOVATEL_H
#define MM_COMMON_NOVATEL_H

#include "glib.h"
#include "mm-plugin.h"

void     mm_common_novatel_custom_init        (MMPortProbe *probe,
                                               MMPortSerialAt *port,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
gboolean mm_common_novatel_custom_init_finish (MMPortProbe *probe,
                                               GAsyncResult *result,
                                               GError **error);

#endif  /* MM_COMMON_NOVATEL_H */
