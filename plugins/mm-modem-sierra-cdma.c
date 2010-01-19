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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-modem-sierra-cdma.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-port.h"
#include "mm-serial-parsers.h"

G_DEFINE_TYPE (MMModemSierraCdma, mm_modem_sierra_cdma, MM_TYPE_GENERIC_CDMA)

#define MM_MODEM_SIERRA_CDMA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_SIERRA_CDMA, MMModemSierraCdmaPrivate))

typedef enum {
    SYS_MODE_UNKNOWN,
    SYS_MODE_NO_SERVICE,
    SYS_MODE_CDMA_1X,
    SYS_MODE_EVDO_REV0,
    SYS_MODE_EVDO_REVA
} SysMode;

typedef struct {
    SysMode sys_mode;
} MMModemSierraCdmaPrivate;

MMModem *
mm_modem_sierra_cdma_new (const char *device,
                          const char *driver,
                          const char *plugin,
                          gboolean evdo_rev0,
                          gboolean evdo_revA)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_SIERRA_CDMA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_GENERIC_CDMA_EVDO_REV0, evdo_rev0,
                                   MM_GENERIC_CDMA_EVDO_REVA, evdo_revA,
                                   NULL));
}

/*****************************************************************************/

#define MODEM_REG_TAG "Modem has registered"
#define GENERIC_ROAM_TAG "Roaming:"
#define ROAM_1X_TAG "1xRoam:"
#define ROAM_EVDO_TAG "HDRRoam:"
#define SYS_MODE_TAG "Sys Mode:"
#define SYS_MODE_NO_SERVICE_TAG "NO SRV"
#define SYS_MODE_EVDO_TAG "HDR"
#define SYS_MODE_1X_TAG "1x"
#define EVDO_REV_TAG "HDR Revision:"
#define SID_TAG "SID:"

static gboolean
get_roam_value (const char *reply, const char *tag, gboolean *roaming)
{
    char *p;

    p = strstr (reply, tag);
    if (!p)
        return FALSE;

    p += strlen (tag);
    while (*p && isspace (*p))
        p++;
    if (*p == '1') {
        *roaming = TRUE;
        return TRUE;
    } else if (*p == '0') {
        *roaming = FALSE;
        return TRUE;
    }

    return FALSE;
}

static gboolean
sys_mode_has_service (SysMode mode)
{
    return (   mode == SYS_MODE_CDMA_1X
            || mode == SYS_MODE_EVDO_REV0
            || mode == SYS_MODE_EVDO_REVA);
}

static void
status_done (MMSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSierraCdmaPrivate *priv = MM_MODEM_SIERRA_CDMA_GET_PRIVATE (info->modem);
    char **lines, **iter;
    gboolean registered = FALSE;
    gboolean have_sid = FALSE;
    SysMode evdo_mode = SYS_MODE_UNKNOWN;
    SysMode sys_mode = SYS_MODE_UNKNOWN;
    gboolean cdma_1x_set = FALSE, evdo_set = FALSE;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    lines = g_strsplit_set (response->str, "\n\r", 0);
    if (!lines) {
        /* Whatever, just use default registration state */
        goto done;
    }

    /* Sierra CDMA parts have two general formats depending on whether they 
     * support EVDO or not.  EVDO parts report both 1x and EVDO roaming status
     * while of course 1x parts only report 1x status.  Some modems also do not
     * report the Roaming information (MP 555 GPS).
     * 
     * AT!STATUS responses:
     *
     * Unregistered MC5725:
     * -----------------------
     * Current band: PCS CDMA
     * Current channel: 350
     * SID: 0  NID: 0  1xRoam: 0 HDRRoam: 0
     * Temp: 33  State: 100  Sys Mode: NO SRV
     * Pilot NOT acquired
     * Modem has NOT registered
     * 
     * Registered MC5725:
     * -----------------------
     * Current band: Cellular Sleep
     * Current channel: 775
     * SID: 30  NID: 2  1xRoam: 0 HDRRoam: 0
     * Temp: 29  State: 200  Sys Mode: HDR
     * Pilot acquired
     * Modem has registered
     * HDR Revision: A
     *
     * Unregistered AC580:
     * -----------------------
     * Current band: PCS CDMA
     * Current channel: 350
     * SID: 0 NID: 0  Roaming: 0
     * Temp: 39  State: 100  Scan Mode: 0
     * Pilot NOT acquired
     * Modem has NOT registered
     *
     * Registered AC580:
     * -----------------------
     * Current band: Cellular Sleep
     * Current channel: 548
     * SID: 26  NID: 1  Roaming: 1
     * Temp: 39  State: 200  Scan Mode: 0
     * Pilot Acquired
     * Modem has registered
     */

    /* We have to handle the two formats slightly differently; for newer formats
     * with "Sys Mode", we consider the modem registered if the Sys Mode is not
     * "NO SRV".  The explicit registration status is just icing on the cake.
     * For older formats (no "Sys Mode") we treat the modem as registered if
     * the SID is non-zero.
     */

    for (iter = lines; iter && *iter; iter++) {
        gboolean bool_val = FALSE;
        char *p;

        if (!strncmp (*iter, MODEM_REG_TAG, strlen (MODEM_REG_TAG))) {
            registered = TRUE;
            continue;
        }

        /* Roaming */
        if (get_roam_value (*iter, ROAM_1X_TAG, &bool_val)) {
            mm_generic_cdma_query_reg_state_set_callback_1x_state (info,
                        bool_val ? MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING :
                                   MM_MODEM_CDMA_REGISTRATION_STATE_HOME);
            cdma_1x_set = TRUE;
        }
        if (get_roam_value (*iter, ROAM_EVDO_TAG, &bool_val)) {
            mm_generic_cdma_query_reg_state_set_callback_evdo_state (info,
                        bool_val ? MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING :
                                   MM_MODEM_CDMA_REGISTRATION_STATE_HOME);
            evdo_set = TRUE;
        }
        if (get_roam_value (*iter, GENERIC_ROAM_TAG, &bool_val)) {
            MMModemCdmaRegistrationState reg_state;

            reg_state = bool_val ? MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING :
                                   MM_MODEM_CDMA_REGISTRATION_STATE_HOME;

            mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);
            mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, reg_state);
            cdma_1x_set = TRUE;
            evdo_set = TRUE;
        }

        /* Current system mode */
        p = strstr (*iter, SYS_MODE_TAG);
        if (p) {
            p += strlen (SYS_MODE_TAG);
            while (*p && isspace (*p))
                p++;
            if (!strncmp (p, SYS_MODE_NO_SERVICE_TAG, strlen (SYS_MODE_NO_SERVICE_TAG)))
                sys_mode = SYS_MODE_NO_SERVICE;
            else if (!strncmp (p, SYS_MODE_EVDO_TAG, strlen (SYS_MODE_EVDO_TAG)))
                sys_mode = SYS_MODE_EVDO_REV0;
            else if (!strncmp (p, SYS_MODE_1X_TAG, strlen (SYS_MODE_1X_TAG)))
                sys_mode = SYS_MODE_CDMA_1X;
        }

        /* Current EVDO revision if system mode is EVDO */
        p = strstr (*iter, EVDO_REV_TAG);
        if (p) {
            p += strlen (EVDO_REV_TAG);
            while (*p && isspace (*p))
                p++;
            if (*p == 'A')
                evdo_mode = SYS_MODE_EVDO_REVA;
            else if (*p == '0')
                evdo_mode = SYS_MODE_EVDO_REV0;
        }

        /* SID */
        p = strstr (*iter, SID_TAG);
        if (p) {
            p += strlen (SID_TAG);
            while (*p && isspace (*p))
                p++;
            if (isdigit (*p) && (*p != '0'))
                have_sid = TRUE;
        }
    }

    /* Update current system mode */
    if (sys_mode == SYS_MODE_EVDO_REV0 || sys_mode == SYS_MODE_EVDO_REVA) {
        /* Prefer the explicit EVDO mode from EVDO_REV_TAG */
        if (evdo_mode != SYS_MODE_UNKNOWN)
            sys_mode = evdo_mode;
    }
    priv->sys_mode = sys_mode;

    if (registered || have_sid || sys_mode_has_service (sys_mode)) {
        /* As a backup, if for some reason the registration states didn't get
         * figured out by parsing the status info, set some generic registration
         * states here.
         */
        if (!cdma_1x_set)
            mm_generic_cdma_query_reg_state_set_callback_1x_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED);

        /* Ensure EVDO registration mode is set if we're at least in EVDO mode */
        if (!evdo_set && (sys_mode == SYS_MODE_EVDO_REV0 || sys_mode == SYS_MODE_EVDO_REVA))
            mm_generic_cdma_query_reg_state_set_callback_1x_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED);
    } else {
        /* Not registered */
        mm_generic_cdma_query_reg_state_set_callback_1x_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
    }

done:
    mm_callback_info_schedule (info);
}

static void
query_registration_state (MMGenericCdma *cdma,
                          MMModemCdmaRegistrationStateFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary, *secondary;
    MMSerialPort *port;

    port = primary = mm_generic_cdma_get_port (cdma, MM_PORT_TYPE_PRIMARY);
    secondary = mm_generic_cdma_get_port (cdma, MM_PORT_TYPE_SECONDARY);

    info = mm_generic_cdma_query_reg_state_callback_info_new (cdma, callback, user_data);

    if (mm_port_get_connected (MM_PORT (primary))) {
        if (!secondary) {
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                                               "Cannot get query registration state while connected");
            mm_callback_info_schedule (info);
            return;
        }

        /* Use secondary port if primary is connected */
        port = secondary;
    }

    mm_serial_port_queue_command (port, "!STATUS", 3, status_done, info);
}

static void
pcstate_done (MMSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    /* Ignore errors for now; we're not sure if all Sierra CDMA devices support
     * at!pcstate.
     */
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
}

static void
post_enable (MMGenericCdma *cdma,
             MMModemFn callback,
             gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (cdma), callback, user_data);

    primary = mm_generic_cdma_get_port (cdma, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_serial_port_queue_command (primary, "!pcstate=1", 5, pcstate_done, info);
}

static void
post_disable (MMGenericCdma *cdma,
              MMModemFn callback,
              gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (cdma), callback, user_data);

    primary = mm_generic_cdma_get_port (cdma, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_serial_port_queue_command (primary, "!pcstate=0", 5, pcstate_done, info);
}

/*****************************************************************************/

static void
mm_modem_sierra_cdma_init (MMModemSierraCdma *self)
{
}

static void
mm_modem_sierra_cdma_class_init (MMModemSierraCdmaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericCdmaClass *cdma_class = MM_GENERIC_CDMA_CLASS (klass);

    mm_modem_sierra_cdma_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemSierraCdmaPrivate));

    cdma_class->query_registration_state = query_registration_state;
    cdma_class->post_enable = post_enable;
    cdma_class->post_disable = post_disable;
}

