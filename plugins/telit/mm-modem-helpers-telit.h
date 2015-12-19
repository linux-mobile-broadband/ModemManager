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
 * Copyright (C) 2015 Telit.
 *
 */
#ifndef MM_MODEM_HELPERS_TELIT_H
#define MM_MODEM_HELPERS_TELIT_H

#include <glib.h>

/* +CSIM response parser */
gint mm_telit_parse_csim_response (const guint step,
                                   const gchar *response,
                                   GError **error);

#endif  /* MM_MODEM_HELPERS_TELIT_H */
