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

#ifndef MM_MODEM_CDMA_H
#define MM_MODEM_CDMA_H

#include <mm-modem.h>

typedef enum {
    MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN =    0,
    MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED = 1,
    MM_MODEM_CDMA_REGISTRATION_STATE_HOME =       2,
    MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING =    3,

    MM_MODEM_CDMA_REGISTRATION_STATE_LAST = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING
} MMModemCdmaRegistrationState;

#define MM_TYPE_MODEM_CDMA               (mm_modem_cdma_get_type ())
#define MM_MODEM_CDMA(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_CDMA, MMModemCdma))
#define MM_IS_MODEM_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_CDMA))
#define MM_MODEM_CDMA_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_CDMA, MMModemCdma))

#define MM_MODEM_CDMA_REGISTRATION_STATE_CHANGED "registration-state-changed"

#define MM_MODEM_CDMA_MEID "meid"

typedef enum {
    MM_MODEM_CDMA_PROP_FIRST = 0x1200,

    MM_MODEM_CDMA_PROP_MEID = MM_MODEM_CDMA_PROP_FIRST,
} MMModemCdmaProp;

typedef struct _MMModemCdma MMModemCdma;

typedef void (*MMModemCdmaServingSystemFn) (MMModemCdma *modem,
                                            guint32 class,
                                            unsigned char band,
                                            guint32 sid,
                                            GError *error,
                                            gpointer user_data);

typedef void (*MMModemCdmaRegistrationStateFn) (MMModemCdma *modem,
                                                MMModemCdmaRegistrationState cdma_1x_reg_state,
                                                MMModemCdmaRegistrationState evdo_reg_state,
                                                GError *error,
                                                gpointer user_data);

struct _MMModemCdma {
    GTypeInterface g_iface;

    /* Methods */
    void (*get_signal_quality) (MMModemCdma *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

    void (*get_esn) (MMModemCdma *self,
                     MMModemStringFn callback,
                     gpointer user_data);

    void (*get_serving_system) (MMModemCdma *self,
                                MMModemCdmaServingSystemFn callback,
                                gpointer user_data);

    void (*get_registration_state) (MMModemCdma *self,
                                    MMModemCdmaRegistrationStateFn callback,
                                    gpointer user_data);

    void (*activate) (MMModemCdma *self,
                      MMModemUIntFn callback,
                      gpointer user_data);

    void (*activate_manual) (MMModemCdma *self,
                             MMModemUIntFn callback,
                             gpointer user_data);

    void (*activate_manual_debug) (MMModemCdma *self,
                                   MMModemUIntFn callback,
                                   gpointer user_data);

    /* Signals */
    void (*signal_quality) (MMModemCdma *self,
                            guint32 quality);

    void (*registration_state_changed) (MMModemCdma *self,
                                        MMModemCdmaRegistrationState cdma_1x_new_state,
                                        MMModemCdmaRegistrationState evdo_new_state);
};

GType mm_modem_cdma_get_type (void);

void mm_modem_cdma_get_signal_quality (MMModemCdma *self,
                                       MMModemUIntFn callback,
                                       gpointer user_data);

void mm_modem_cdma_get_esn (MMModemCdma *self,
                            MMModemStringFn callback,
                            gpointer user_data);

void mm_modem_cdma_get_serving_system (MMModemCdma *self,
                                       MMModemCdmaServingSystemFn callback,
                                       gpointer user_data);

void mm_modem_cdma_get_registration_state (MMModemCdma *self,
                                           MMModemCdmaRegistrationStateFn callback,
                                           gpointer user_data);

void mm_modem_cdma_activate (MMModemCdma *self, MMModemUIntFn callback,
                             gpointer user_data);

void mm_modem_cdma_activate_manual (MMModemCdma *self, MMModemUIntFn callback,
                                    gpointer user_data);

/* Protected */

void mm_modem_cdma_emit_signal_quality_changed (MMModemCdma *self, guint32 new_quality);

void mm_modem_cdma_emit_registration_state_changed (MMModemCdma *self,
                                                    MMModemCdmaRegistrationState cdma_1x_new_state,
                                                    MMModemCdmaRegistrationState evdo_new_state);

#endif  /* MM_MODEM_CDMA_H */
