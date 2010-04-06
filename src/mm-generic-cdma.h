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

#ifndef MM_GENERIC_CDMA_H
#define MM_GENERIC_CDMA_H

#include "mm-modem.h"
#include "mm-modem-base.h"
#include "mm-modem-cdma.h"
#include "mm-at-serial-port.h"
#include "mm-callback-info.h"

#define MM_TYPE_GENERIC_CDMA            (mm_generic_cdma_get_type ())
#define MM_GENERIC_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GENERIC_CDMA, MMGenericCdma))
#define MM_GENERIC_CDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_GENERIC_CDMA, MMGenericCdmaClass))
#define MM_IS_GENERIC_CDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GENERIC_CDMA))
#define MM_IS_GENERIC_CDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_GENERIC_CDMA))
#define MM_GENERIC_CDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_GENERIC_CDMA, MMGenericCdmaClass))

#define MM_GENERIC_CDMA_EVDO_REV0            "evdo-rev0"
#define MM_GENERIC_CDMA_EVDO_REVA            "evdo-revA"

#define MM_GENERIC_CDMA_REGISTRATION_TRY_CSS "registration-try-css"

typedef struct {
    MMModemBase parent;
} MMGenericCdma;

typedef struct {
    MMModemBaseClass parent;

    /* Subclasses should implement this function if they can more accurately
     * determine the registration state and/or roaming status than the base
     * class can (by using manufacturer custom AT commands or whatever).
     * The base class passes its detected registration state in the
     * cur_cdma_state and cur_evdo_state arguments, which the subclass should
     * override if necessary before passing to the callback.
     *
     * Subclasses can use the helper functions 
     * mm_generic_cdma_query_reg_state_callback_info_new(),
     * mm_generic_cdma_query_reg_state_set_callback_1x_state(), and
     * mm_generic_cdma_query_reg_state_set_callback_evdo_state() to create the
     * MMCallbackInfo object and to set the registration state which is passed
     * to the callback when the subclass' registration query completes.
     *
     * Subclasses should generally not return parsing or other non-critical
     * errors to the callback since that fails the entire registration check,
     * rendering the superclass' checks useless.
     */
    void (*query_registration_state) (MMGenericCdma *self,
                                      MMModemCdmaRegistrationState cur_cdma_state,
                                      MMModemCdmaRegistrationState cur_evdo_state,
                                      MMModemCdmaRegistrationStateFn callback,
                                      gpointer user_data);

    /* Called after generic enable operations, but before the modem has entered
     * the ENABLED state.
     */
    void (*post_enable) (MMGenericCdma *self,
                         MMModemFn callback,
                         gpointer user_data);

    /* Called after generic disable operations, but before the modem has entered
     * the DISABLED state.
     */
    void (*post_disable) (MMGenericCdma *self,
                          MMModemFn callback,
                          gpointer user_data);
} MMGenericCdmaClass;

GType mm_generic_cdma_get_type (void);

MMModem *mm_generic_cdma_new (const char *device,
                              const char *driver,
                              const char *plugin,
                              gboolean evdo_rev0,
                              gboolean evdo_revA);

/* Private, for subclasses */

MMPort * mm_generic_cdma_grab_port (MMGenericCdma *self,
                                    const char *subsys,
                                    const char *name,
                                    MMPortType suggested_type,
                                    gpointer user_data,
                                    GError **error);

MMAtSerialPort *mm_generic_cdma_get_at_port (MMGenericCdma *modem, MMPortType ptype);

MMAtSerialPort *mm_generic_cdma_get_best_at_port (MMGenericCdma *modem,
                                                  GError **error);

void mm_generic_cdma_update_cdma1x_quality (MMGenericCdma *self, guint32 quality);
void mm_generic_cdma_update_evdo_quality (MMGenericCdma *self, guint32 quality);

/* For unsolicited 1x registration state changes */
void mm_generic_cdma_set_1x_registration_state (MMGenericCdma *self,
                                                MMModemCdmaRegistrationState new_state);

/* For unsolicited EVDO registration state changes */
void mm_generic_cdma_set_evdo_registration_state (MMGenericCdma *self,
                                                  MMModemCdmaRegistrationState new_state);

MMModemCdmaRegistrationState mm_generic_cdma_1x_get_registration_state_sync (MMGenericCdma *self);

MMModemCdmaRegistrationState mm_generic_cdma_evdo_get_registration_state_sync (MMGenericCdma *self);

/* query_registration_state class function helpers */
MMCallbackInfo *mm_generic_cdma_query_reg_state_callback_info_new (MMGenericCdma *self,
                                                                   MMModemCdmaRegistrationState cur_cdma_state,
                                                                   MMModemCdmaRegistrationState cur_evdo_state,
                                                                   MMModemCdmaRegistrationStateFn callback,
                                                                   gpointer user_data);

MMModemCdmaRegistrationState mm_generic_cdma_query_reg_state_get_callback_1x_state (MMCallbackInfo *info);

void mm_generic_cdma_query_reg_state_set_callback_1x_state (MMCallbackInfo *info,
                                                            MMModemCdmaRegistrationState new_state);

MMModemCdmaRegistrationState mm_generic_cdma_query_reg_state_get_callback_evdo_state (MMCallbackInfo *info);

void mm_generic_cdma_query_reg_state_set_callback_evdo_state (MMCallbackInfo *info,
                                                              MMModemCdmaRegistrationState new_state);

#endif /* MM_GENERIC_CDMA_H */
