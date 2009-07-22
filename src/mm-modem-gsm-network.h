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
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_MODEM_GSM_NETWORK_H
#define MM_MODEM_GSM_NETWORK_H

#include <mm-modem.h>
#include <mm-modem-gsm.h>

#define MM_TYPE_MODEM_GSM_NETWORK      (mm_modem_gsm_network_get_type ())
#define MM_MODEM_GSM_NETWORK(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_GSM_NETWORK, MMModemGsmNetwork))
#define MM_IS_MODEM_GSM_NETWORK(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_GSM_NETWORK))
#define MM_MODEM_GSM_NETWORK_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_GSM_NETWORK, MMModemGsmNetwork))

typedef enum {
    MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE = 0,
    MM_MODEM_GSM_NETWORK_REG_STATUS_HOME = 1,
    MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING = 2,
    MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED = 3,
    MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN = 4,
    MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING = 5
} MMModemGsmNetworkRegStatus;

typedef struct _MMModemGsmNetwork MMModemGsmNetwork;

typedef void (*MMModemGsmNetworkScanFn) (MMModemGsmNetwork *self,
                                         GPtrArray *results,
                                         GError *error,
                                         gpointer user_data);

typedef void (*MMModemGsmNetworkRegInfoFn) (MMModemGsmNetwork *self,
                                            MMModemGsmNetworkRegStatus status,
                                            const char *oper_code,
                                            const char *oper_name,
                                            GError *error,
                                            gpointer user_data);

struct _MMModemGsmNetwork {
    GTypeInterface g_iface;

    /* Methods */
    /* 'register' is a reserved word */
    void (*do_register) (MMModemGsmNetwork *self,
                         const char *network_id,
                         MMModemFn callback,
                         gpointer user_data);

    void (*scan) (MMModemGsmNetwork *self,
                  MMModemGsmNetworkScanFn callback,
                  gpointer user_data);

    void (*set_apn) (MMModemGsmNetwork *self,
                     const char *apn,
                     MMModemFn callback,
                     gpointer user_data);

    void (*get_signal_quality) (MMModemGsmNetwork *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

    void (*set_band) (MMModemGsmNetwork *self,
                      MMModemGsmBand band,
                      MMModemFn callback,
                      gpointer user_data);

    void (*get_band) (MMModemGsmNetwork *self,
                      MMModemUIntFn callback,
                      gpointer user_data);

    void (*set_network_mode) (MMModemGsmNetwork *self,
                              MMModemGsmMode mode,
                              MMModemFn callback,
                              gpointer user_data);

    void (*get_network_mode) (MMModemGsmNetwork *self,
                              MMModemUIntFn callback,
                              gpointer user_data);

    void (*get_registration_info) (MMModemGsmNetwork *self,
                                   MMModemGsmNetworkRegInfoFn callback,
                                   gpointer user_data);

    /* Signals */
    void (*signal_quality) (MMModemGsmNetwork *self,
                            guint32 quality);

    void (*registration_info) (MMModemGsmNetwork *self,
                               MMModemGsmNetworkRegStatus status,
                               const char *open_code,
                               const char *oper_name);

    void (*network_mode) (MMModemGsmNetwork *self,
                          MMModemGsmMode mode);
};

GType mm_modem_gsm_network_get_type (void);

void mm_modem_gsm_network_register (MMModemGsmNetwork *self,
                                    const char *network_id,
                                    MMModemFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_scan (MMModemGsmNetwork *self,
                                MMModemGsmNetworkScanFn callback,
                                gpointer user_data);

void mm_modem_gsm_network_set_apn (MMModemGsmNetwork *self,
                                   const char *apn,
                                   MMModemFn callback,
                                   gpointer user_data);

void mm_modem_gsm_network_get_signal_quality (MMModemGsmNetwork *self,
                                              MMModemUIntFn callback,
                                              gpointer user_data);

void mm_modem_gsm_network_set_band (MMModemGsmNetwork *self,
                                    MMModemGsmBand band,
                                    MMModemFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_get_band (MMModemGsmNetwork *self,
                                    MMModemUIntFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_set_mode (MMModemGsmNetwork *self,
                                    MMModemGsmMode mode,
                                    MMModemFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_get_mode (MMModemGsmNetwork *self,
                                    MMModemUIntFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_get_registration_info (MMModemGsmNetwork *self,
                                                 MMModemGsmNetworkRegInfoFn callback,
                                                 gpointer user_data);

/* Protected */

void mm_modem_gsm_network_signal_quality (MMModemGsmNetwork *self,
                                          guint32 quality);

void mm_modem_gsm_network_registration_info (MMModemGsmNetwork *self,
                                             MMModemGsmNetworkRegStatus status,
                                             const char *oper_code,
                                             const char *oper_name);

void mm_modem_gsm_network_mode (MMModemGsmNetwork *self,
                                MMModemGsmMode mode);

#endif /* MM_MODEM_GSM_NETWORK_H */
