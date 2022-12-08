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

#ifndef MM_COMMON_TELIT_H
#define MM_COMMON_TELIT_H

#include "glib.h"
#include "mm-plugin.h"

gboolean
telit_custom_init_finish (MMPortProbe *probe,
                          GAsyncResult *result,
                          GError **error);

void
telit_custom_init (MMPortProbe *probe,
                   MMPortSerialAt *port,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data);

gboolean
telit_grab_port (MMPlugin *self,
                 MMBaseModem *modem,
                 MMPortProbe *probe,
                 GError **error);

#endif  /* MM_COMMON_TELIT_H */
