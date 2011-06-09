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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#ifndef MM_GENERIC_GSM_H
#define MM_GENERIC_GSM_H

#include <config.h>

#include "mm-modem-gsm.h"
#include "mm-modem-gsm-network.h"
#include "mm-modem-base.h"
#include "mm-at-serial-port.h"
#include "mm-callback-info.h"
#include "mm-charsets.h"

#define MM_TYPE_GENERIC_GSM            (mm_generic_gsm_get_type ())
#define MM_GENERIC_GSM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GENERIC_GSM, MMGenericGsm))
#define MM_GENERIC_GSM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_GENERIC_GSM, MMGenericGsmClass))
#define MM_IS_GENERIC_GSM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GENERIC_GSM))
#define MM_IS_GENERIC_GSM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_GENERIC_GSM))
#define MM_GENERIC_GSM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_GENERIC_GSM, MMGenericGsmClass))

#define MM_GENERIC_GSM_POWER_UP_CMD       "power-up-cmd"
#define MM_GENERIC_GSM_POWER_DOWN_CMD     "power-down-cmd"
#define MM_GENERIC_GSM_INIT_CMD           "init-cmd"
#define MM_GENERIC_GSM_INIT_CMD_OPTIONAL  "init-cmd-optional"
#define MM_GENERIC_GSM_FLOW_CONTROL_CMD   "flow-control-cmd"

typedef enum {
    MM_GENERIC_GSM_PROP_FIRST = 0x2000,

    MM_GENERIC_GSM_PROP_POWER_UP_CMD,
    MM_GENERIC_GSM_PROP_POWER_DOWN_CMD,
    MM_GENERIC_GSM_PROP_INIT_CMD,
    MM_GENERIC_GSM_PROP_SUPPORTED_BANDS,
    MM_GENERIC_GSM_PROP_SUPPORTED_MODES,
    MM_GENERIC_GSM_PROP_INIT_CMD_OPTIONAL,
    MM_GENERIC_GSM_PROP_ALLOWED_MODE,
    MM_GENERIC_GSM_PROP_ACCESS_TECHNOLOGY,
    MM_GENERIC_GSM_PROP_LOC_CAPABILITIES,
    MM_GENERIC_GSM_PROP_LOC_ENABLED,
    MM_GENERIC_GSM_PROP_LOC_SIGNAL,
    MM_GENERIC_GSM_PROP_LOC_LOCATION,
    MM_GENERIC_GSM_PROP_SIM_IDENTIFIER,
    MM_GENERIC_GSM_PROP_USSD_STATE,
    MM_GENERIC_GSM_PROP_USSD_NETWORK_REQUEST,
    MM_GENERIC_GSM_PROP_USSD_NETWORK_NOTIFICATION,
    MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD
} MMGenericGsmProp;

typedef enum {
    MM_GENERIC_GSM_REG_TYPE_UNKNOWN = 0,
    MM_GENERIC_GSM_REG_TYPE_CS = 1,
    MM_GENERIC_GSM_REG_TYPE_PS = 2
} MMGenericGsmRegType;

typedef struct {
    MMModemBase parent;
} MMGenericGsm;

typedef struct {
    MMModemBaseClass parent;

    /* Called after opening the primary serial port and updating the modem's
     * state to ENABLING, but before sending any commands to the device.  Modems
     * that need to perform custom initialization sequences or other setup should
     * generally override this method instead of the MMModem interface's enable()
     * method, unless the customization must happen *after* the generic init
     * sequence has completed.  When the subclass' enable attempt is complete
     * the subclass should call mm_generic_gsm_enable_complete() with any error
     * encountered during the process and the MMCallbackInfo created from the
     * callback and user_data passed in here.
     */
    void (*do_enable) (MMGenericGsm *self, MMModemFn callback, gpointer user_data);

    /* Called after the generic class has attempted to power up the modem.
     * Subclasses can handle errors here if they know the device supports their
     * power up command.  Will only be called if the device does *not* override
     * the MMModem enable() command or allows the generic class' do_enable()
     * handler to execute.
     */
    void (*do_enable_power_up_done) (MMGenericGsm *self,
                                     GString *response,
                                     GError *error,
                                     MMCallbackInfo *info);

    /* Called to terminate the active data call and deactivate the given PDP
     * context.
     */
    void (*do_disconnect) (MMGenericGsm *self,
                           gint cid,
                           MMModemFn callback,
                           gpointer user_data);

    /* Called by the generic class to set the allowed operating mode of the device */
    void (*set_allowed_mode) (MMGenericGsm *self,
                               MMModemGsmAllowedMode mode,
                               MMModemFn callback,
                               gpointer user_data);

    /* Called by the generic class to get the allowed operating mode of the device */
    void (*get_allowed_mode) (MMGenericGsm *self,
                               MMModemUIntFn callback,
                               gpointer user_data);

    /* Called by the generic class to the current radio access technology the
     * device is using while communicating with the base station.
     */
    void (*get_access_technology) (MMGenericGsm *self,
                                   MMModemUIntFn callback,
                                   gpointer user_data);

    /* Called by the generic class to get additional Location capabilities that
     * subclasses may implement.  The MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI
     * capabilities is automatically provided by the generic class, and
     * subclasses should return a bitfield of additional location capabilities
     * they support in the callback here.
     */
    void (*loc_get_capabilities) (MMGenericGsm *self,
                                  MMModemUIntFn callback,
                                  gpointer user_data);

    /* Called by the generic class to retrieve the SIM's ICCID */
    void (*get_sim_iccid) (MMGenericGsm *self,
                           MMModemStringFn callback,
                           gpointer user_data);
} MMGenericGsmClass;

GType mm_generic_gsm_get_type (void);

MMModem *mm_generic_gsm_new (const char *device,
                             const char *driver,
                             const char *plugin,
                             guint vendor,
                             guint product);

/* Private, for subclasses */

#define MM_GENERIC_GSM_PREV_STATE_TAG "prev-state"

void mm_generic_gsm_pending_registration_stop (MMGenericGsm *modem);

void mm_generic_gsm_ussd_cleanup (MMGenericGsm *modem);

gint mm_generic_gsm_get_cid (MMGenericGsm *modem);

void mm_generic_gsm_set_reg_status (MMGenericGsm *modem,
                                    MMGenericGsmRegType reg_type,
                                    MMModemGsmNetworkRegStatus status);

MMModemCharset mm_generic_gsm_get_charset (MMGenericGsm *modem);

/* Called to asynchronously update the current allowed operating mode that the
 * device is allowed to use when connecting to a network.  This isn't the
 * specific access technology the device is currently using (see 
 * mm_generic_gsm_set_access_technology() for that) but the mode the device is
 * allowed to choose from when connecting.
 */
void mm_generic_gsm_update_allowed_mode (MMGenericGsm *modem,
                                         MMModemGsmAllowedMode mode);

/* Called to asynchronously update the current access technology of the device;
 * this is NOT the 2G/3G mode preference, but the current radio access
 * technology being used to communicate with the base station.
 */
void mm_generic_gsm_update_access_technology (MMGenericGsm *modem,
                                              MMModemGsmAccessTech act);

/* Called to asynchronously update the current signal quality of the device;
 * 'quality' is a 0 - 100% quality.
 */
void mm_generic_gsm_update_signal_quality (MMGenericGsm *modem, guint32 quality);

MMAtSerialPort *mm_generic_gsm_get_at_port (MMGenericGsm *modem,
                                            MMPortType ptype);

MMAtSerialPort *mm_generic_gsm_get_best_at_port (MMGenericGsm *modem,
                                                 GError **error);

MMPort *mm_generic_gsm_grab_port (MMGenericGsm *modem,
                                  const char *subsys,
                                  const char *name,
                                  MMPortType ptype,
                                  GError **error);

/* stay_connected should be TRUE for unsolicited registration updates, otherwise
 * the registration update will clear connected/connecting/disconnecting state
 * which we don't want.  stay_connected should be FALSE for other cases like
 * updating the state after disconnecting, or after a connect error occurs.
 */
void mm_generic_gsm_update_enabled_state (MMGenericGsm *modem,
                                          gboolean stay_connected,
                                          MMModemStateReason reason);

/* Called to complete the enable operation for custom enable() handling; if an
 * error is passed in, it copies the error to the callback info.  This function
 * always schedules the callback info.  It will also update the modem with the
 * correct state for both failure and success of the enable operation.
 */
void mm_generic_gsm_enable_complete (MMGenericGsm *modem,
                                     GError *error,
                                     MMCallbackInfo *info);

/* Called to complete the enable operation for custom connect() handling; if an
 * error is passed in, it copies the error to the callback info.  This function
 * always schedules the callback info.  It will also update the modem with the
 * correct state for both failure and success of the connect operation.
 */
void mm_generic_gsm_connect_complete (MMGenericGsm *modem,
                                      GError *error,
                                      MMCallbackInfo *info);

#endif /* MM_GENERIC_GSM_H */
