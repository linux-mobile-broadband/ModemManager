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
 * Copyright (C) 2016 Thomas Sailer <t.sailer@alumni.ethz.ch>
 *
 */
#ifndef MM_MODEM_HELPERS_THURAYA_H
#define MM_MODEM_HELPERS_THURAYA_H

#include <glib.h>

/* AT+CPMS=? (Preferred SMS storage) response parser */
gboolean mm_thuraya_3gpp_parse_cpms_test_response (const gchar  *reply,
                                                   GArray      **mem1,
                                                   GArray      **mem2,
                                                   GArray      **mem3,
                                                   GError      **error);

#endif  /* MM_MODEM_HELPERS_THURAYA_H */
