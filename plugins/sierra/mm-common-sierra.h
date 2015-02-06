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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 */

#ifndef MM_COMMON_SIERRA_H
#define MM_COMMON_SIERRA_H

#include "mm-plugin.h"
#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-base-sim.h"

gboolean mm_common_sierra_grab_port (MMPlugin *self,
                                     MMBaseModem *modem,
                                     MMPortProbe *probe,
                                     GError **error);

gboolean mm_common_sierra_port_probe_list_is_icera (GList *probes);

void     mm_common_sierra_custom_init        (MMPortProbe *probe,
                                              MMPortSerialAt *port,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
gboolean mm_common_sierra_custom_init_finish (MMPortProbe *probe,
                                              GAsyncResult *result,
                                              GError **error);

void              mm_common_sierra_load_power_state        (MMIfaceModem *self,
                                                            GAsyncReadyCallback callback,
                                                            gpointer user_data);
MMModemPowerState mm_common_sierra_load_power_state_finish (MMIfaceModem *self,
                                                            GAsyncResult *res,
                                                            GError **error);

void     mm_common_sierra_modem_power_up        (MMIfaceModem *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mm_common_sierra_modem_power_up_finish (MMIfaceModem *self,
                                                 GAsyncResult *res,
                                                 GError **error);

void       mm_common_sierra_create_sim        (MMIfaceModem *self,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
MMBaseSim *mm_common_sierra_create_sim_finish (MMIfaceModem *self,
                                               GAsyncResult *res,
                                               GError **error);

void mm_common_sierra_setup_ports (MMBroadbandModem *self);

void mm_common_sierra_peek_parent_interfaces (MMIfaceModem *iface);

#endif /* MM_COMMON_SIERRA_H */
