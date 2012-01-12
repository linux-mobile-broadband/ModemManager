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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_MODEM_SIMPLE_STATUS_PROPERTIES_H
#define MM_MODEM_SIMPLE_STATUS_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

#include <libmm-common.h>

G_BEGIN_DECLS

typedef MMCommonSimpleProperties                  MMModemSimpleStatusProperties;
#define MM_TYPE_MODEM_SIMPLE_STATUS_PROPERTIES(o) MM_TYPE_COMMON_SIMPLE_PROPERTIES (o)
#define MM_MODEM_SIMPLE_STATUS_PROPERTIES(o)      MM_COMMON_SIMPLE_PROPERTIES(o)
#define MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES(o)   MM_IS_COMMON_SIMPLE_PROPERTIES(o)

MMModemState                  mm_modem_simple_status_properties_get_state               (MMModemSimpleStatusProperties *self);
guint32                       mm_modem_simple_status_properties_get_signal_quality      (MMModemSimpleStatusProperties *self,
                                                                                         gboolean *recent);
void                          mm_modem_simple_status_properties_get_bands               (MMModemSimpleStatusProperties *self,
                                                                                         const MMModemBand **bands,
                                                                                         guint *n_bands);
MMModemAccessTechnology       mm_modem_simple_status_properties_get_access_technologies (MMModemSimpleStatusProperties *self);

MMModem3gppRegistrationState  mm_modem_simple_status_properties_get_3gpp_registration_state (MMModemSimpleStatusProperties *self);
const gchar                  *mm_modem_simple_status_properties_get_3gpp_operator_code      (MMModemSimpleStatusProperties *self);
const gchar                  *mm_modem_simple_status_properties_get_3gpp_operator_name      (MMModemSimpleStatusProperties *self);

G_END_DECLS

#endif /* MM_MODEM_SIMPLE_STATUS_PROPERTIES_H */
