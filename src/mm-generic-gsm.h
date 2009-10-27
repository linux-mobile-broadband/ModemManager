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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_GENERIC_GSM_H
#define MM_GENERIC_GSM_H

#include "mm-modem-gsm.h"
#include "mm-modem-gsm-network.h"
#include "mm-modem-base.h"
#include "mm-serial-port.h"

#define MM_TYPE_GENERIC_GSM            (mm_generic_gsm_get_type ())
#define MM_GENERIC_GSM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GENERIC_GSM, MMGenericGsm))
#define MM_GENERIC_GSM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_GENERIC_GSM, MMGenericGsmClass))
#define MM_IS_GENERIC_GSM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GENERIC_GSM))
#define MM_IS_GENERIC_GSM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_GENERIC_GSM))
#define MM_GENERIC_GSM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_GENERIC_GSM, MMGenericGsmClass))

#define MM_GENERIC_GSM_POWER_UP_CMD   "power-up-cmd"
#define MM_GENERIC_GSM_POWER_DOWN_CMD "power-down-cmd"
#define MM_GENERIC_GSM_INIT_CMD       "init-cmd"

typedef enum {
    MM_GENERIC_GSM_PROP_FIRST = 0x2000,

    MM_GENERIC_GSM_PROP_POWER_UP_CMD,
    MM_GENERIC_GSM_PROP_POWER_DOWN_CMD,
    MM_GENERIC_GSM_PROP_INIT_CMD,
    MM_GENERIC_GSM_PROP_SUPPORTED_BANDS,
    MM_GENERIC_GSM_PROP_SUPPORTED_MODES,

    MM_GENERIC_GSM_LAST_PROP = MM_GENERIC_GSM_PROP_INIT_CMD
} MMGenericGsmProp;


typedef struct {
    MMModemBase parent;
} MMGenericGsm;

typedef struct {
    MMModemBaseClass parent;
} MMGenericGsmClass;

GType mm_generic_gsm_get_type (void);

MMModem *mm_generic_gsm_new (const char *device,
                             const char *driver,
                             const char *plugin);

void mm_generic_gsm_set_unsolicited_registration (MMGenericGsm *modem,
                                                  gboolean enabled);

void mm_generic_gsm_pending_registration_stop    (MMGenericGsm *modem);

void mm_generic_gsm_set_cid (MMGenericGsm *modem,
                             guint32 cid);

guint32 mm_generic_gsm_get_cid (MMGenericGsm *modem);
void mm_generic_gsm_set_reg_status (MMGenericGsm *modem,
                                    MMModemGsmNetworkRegStatus status);

void mm_generic_gsm_check_pin (MMGenericGsm *modem,
                               MMModemFn callback,
                               gpointer user_data);

MMSerialPort *mm_generic_gsm_get_port (MMGenericGsm *modem,
                                       MMPortType ptype);

MMPort *mm_generic_gsm_grab_port (MMGenericGsm *modem,
                                  const char *subsys,
                                  const char *name,
                                  MMPortType ptype,
                                  GError **error);

void mm_generic_gsm_update_enabled_state (MMGenericGsm *modem,
                                          MMModemStateReason reason);

#endif /* MM_GENERIC_GSM_H */
