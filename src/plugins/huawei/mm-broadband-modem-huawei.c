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
 * Copyright (C) 2011 - 2012 Google Inc.
 * Copyright (C) 2012 Huawei Technologies Co., Ltd
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
 * Copyright (C) 2012 - 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-huawei.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-time.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-signal.h"
#include "mm-iface-modem-voice.h"
#include "mm-broadband-modem-huawei.h"
#include "mm-broadband-bearer-huawei.h"
#include "mm-broadband-bearer.h"
#include "mm-bearer-list.h"
#include "mm-sim-huawei.h"
#include "mm-call-at.h"

static void iface_modem_init           (MMIfaceModemInterface         *iface);
static void iface_modem_3gpp_init      (MMIfaceModem3gppInterface     *iface);
static void iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssdInterface *iface);
static void iface_modem_location_init  (MMIfaceModemLocationInterface *iface);
static void iface_modem_cdma_init      (MMIfaceModemCdmaInterface     *iface);
static void iface_modem_time_init      (MMIfaceModemTimeInterface     *iface);
static void iface_modem_voice_init     (MMIfaceModemVoiceInterface    *iface);
static void iface_modem_signal_init    (MMIfaceModemSignalInterface   *iface);

static MMIfaceModemInterface         *iface_modem_parent;
static MMIfaceModem3gppInterface     *iface_modem_3gpp_parent;
static MMIfaceModemLocationInterface *iface_modem_location_parent;
static MMIfaceModemCdmaInterface     *iface_modem_cdma_parent;
static MMIfaceModemVoiceInterface    *iface_modem_voice_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemHuawei, mm_broadband_modem_huawei, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_VOICE, iface_modem_voice_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init))

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED
} FeatureSupport;

typedef struct {
    MMSignal *cdma;
    MMSignal *evdo;
    MMSignal *gsm;
    MMSignal *umts;
    MMSignal *lte;
    MMSignal *nr5g;
} DetailedSignal;

struct _MMBroadbandModemHuaweiPrivate {
    /* Regex for signal quality related notifications */
    GRegex *rssi_regex;
    GRegex *rssilvl_regex;
    GRegex *hrssilvl_regex;

    /* Regex for access-technology related notifications */
    GRegex *mode_regex;

    /* Regex for connection status related notifications */
    GRegex *dsflowrpt_regex;
    GRegex *ndisstat_regex;

    /* Regex for voice management notifications */
    GRegex *orig_regex;
    GRegex *conf_regex;
    GRegex *conn_regex;
    GRegex *cend_regex;
    GRegex *ddtmf_regex;

    /* Regex to ignore */
    GRegex *boot_regex;
    GRegex *connect_regex;
    GRegex *csnr_regex;
    GRegex *cusatp_regex;
    GRegex *cusatend_regex;
    GRegex *dsdormant_regex;
    GRegex *simst_regex;
    GRegex *srvst_regex;
    GRegex *stin_regex;
    GRegex *hcsq_regex;
    GRegex *pdpdeact_regex;
    GRegex *ndisend_regex;
    GRegex *rfswitch_regex;
    GRegex *position_regex;
    GRegex *posend_regex;
    GRegex *ecclist_regex;
    GRegex *ltersrp_regex;
    GRegex *cschannelinfo_regex;
    GRegex *ccallstate_regex;
    GRegex *eons_regex;
    GRegex *lwurc_regex;

    FeatureSupport ndisdup_support;
    FeatureSupport rfswitch_support;
    FeatureSupport sysinfoex_support;
    FeatureSupport syscfg_support;
    FeatureSupport syscfgex_support;
    FeatureSupport prefmode_support;
    FeatureSupport time_support;
    FeatureSupport nwtime_support;
    FeatureSupport cvoice_support;

    MMModemLocationSource enabled_sources;

    GArray *syscfg_supported_modes;
    GArray *syscfgex_supported_modes;
    GArray *prefmode_supported_modes;

    guint64 supported_gsm_umts_bands;
    guint64 supported_lte_bands;

    DetailedSignal detailed_signal;

    /* Voice call audio related properties */
    guint audio_hz;
    guint audio_bits;
};

/*****************************************************************************/

GList *
mm_broadband_modem_huawei_get_at_port_list (MMBroadbandModemHuawei *self)
{
    GList *out = NULL;
    MMPortSerialAt *port;
    GList *cdc_wdm_at_ports;

    /* Primary */
    port = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    if (port)
        out = g_list_append (out, port);

    /* Secondary */
    port = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));
    if (port)
        out = g_list_append (out, port);

    /* Additional cdc-wdm ports used for dialing */
    cdc_wdm_at_ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                                 MM_PORT_SUBSYS_USBMISC,
                                                 MM_PORT_TYPE_AT);

    return g_list_concat (out, cdc_wdm_at_ports);
}

/*****************************************************************************/

typedef struct {
    gboolean extended;
    guint srv_status;
    guint srv_domain;
    guint roam_status;
    guint sim_state;
    guint sys_mode;
    gboolean sys_submode_valid;
    guint sys_submode;
} SysinfoResult;

static gboolean
sysinfo_finish (MMBroadbandModemHuawei *self,
                GAsyncResult *res,
                gboolean *extended,
                guint *srv_status,
                guint *srv_domain,
                guint *roam_status,
                guint *sim_state,
                guint *sys_mode,
                gboolean *sys_submode_valid,
                guint *sys_submode,
                GError **error)
{
    SysinfoResult *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    if (extended)
        *extended = result->extended;
    if (srv_status)
        *srv_status = result->srv_status;
    if (srv_domain)
        *srv_domain = result->srv_domain;
    if (roam_status)
        *roam_status = result->roam_status;
    if (sim_state)
        *sim_state = result->sim_state;
    if (sys_mode)
        *sys_mode = result->sys_mode;
    if (sys_submode_valid)
        *sys_submode_valid = result->sys_submode_valid;
    if (sys_submode)
        *sys_submode = result->sys_submode;

    g_free (result);
    return TRUE;
}

static void
run_sysinfo_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GTask *task)
{
    GError *error = NULL;
    const gchar *response;
    SysinfoResult *result;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        mm_obj_dbg (self, "^SYSINFO failed: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    result = g_new0 (SysinfoResult, 1);
    result->extended = FALSE;
    if (!mm_huawei_parse_sysinfo_response (response,
                                           &result->srv_status,
                                           &result->srv_domain,
                                           &result->roam_status,
                                           &result->sys_mode,
                                           &result->sim_state,
                                           &result->sys_submode_valid,
                                           &result->sys_submode,
                                           &error)) {
        mm_obj_dbg (self, "^SYSINFO parsing failed: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        g_free (result);
        return;
    }

    g_task_return_pointer (task, result, g_free);
    g_object_unref (task);
}

static void
run_sysinfo (MMBroadbandModemHuawei *self,
             GTask *task)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^SYSINFO",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)run_sysinfo_ready,
                              task);
}

static void
run_sysinfoex_ready (MMBaseModem *_self,
                     GAsyncResult *res,
                     GTask *task)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GError *error = NULL;
    const gchar *response;
    SysinfoResult *result;

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (!response) {
        /* First time we try, we fallback to ^SYSINFO */
        if (self->priv->sysinfoex_support == FEATURE_SUPPORT_UNKNOWN) {
            self->priv->sysinfoex_support = FEATURE_NOT_SUPPORTED;
            mm_obj_dbg (self, "^SYSINFOEX failed: %s, assuming unsupported", error->message);
            g_error_free (error);
            run_sysinfo (self, task);
            return;
        }

        /* Otherwise, propagate error */
        mm_obj_dbg (self, "^SYSINFOEX failed: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (self->priv->sysinfoex_support == FEATURE_SUPPORT_UNKNOWN)
        self->priv->sysinfoex_support = FEATURE_SUPPORTED;

    result = g_new0 (SysinfoResult, 1);
    result->extended = TRUE;
    if (!mm_huawei_parse_sysinfoex_response (response,
                                             &result->srv_status,
                                             &result->srv_domain,
                                             &result->roam_status,
                                             &result->sim_state,
                                             &result->sys_mode,
                                             &result->sys_submode,
                                             &error)) {
        mm_obj_dbg (self, "^SYSINFOEX parsing failed: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        g_free (result);
        return;
    }

    /* Submode from SYSINFOEX always valid */
    result->sys_submode_valid = TRUE;
    g_task_return_pointer (task, result, g_free);
    g_object_unref (task);
}

static void
run_sysinfoex (MMBroadbandModemHuawei *self,
               GTask *task)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^SYSINFOEX",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)run_sysinfoex_ready,
                              task);
}

static void
sysinfo (MMBroadbandModemHuawei *self,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->sysinfoex_support == FEATURE_SUPPORT_UNKNOWN ||
        self->priv->sysinfoex_support == FEATURE_SUPPORTED)
        run_sysinfoex (self, task);
    else
        run_sysinfo (self, task);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
reset_finish (MMIfaceModem *self,
              GAsyncResult *res,
              GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
reset (MMIfaceModem *self,
       GAsyncReadyCallback callback,
       gpointer user_data)
{
    const gchar *command;

    /* Unlike other Huawei modems that support AT^RESET for resetting the modem,
     * Huawei MU736 supports AT^RESET but does not reset the modem upon receiving
     * AT^RESET. It does, however, support resetting itself via AT+CFUN=16.
     */
    if (g_strcmp0 (mm_iface_modem_get_model (self), "MU736") == 0)
        command = "+CFUN=16";
    else
        command = "^RESET";

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static MMModemAccessTechnology
huawei_sysinfo_submode_to_act (guint submode)
{
    /* new more detailed system mode/access technology */
    switch (submode) {
    case 1:
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case 2:
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    case 3:
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    case 4:
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 5:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    case 6:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    case 7:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
    case 8: /* TD-SCDMA */
        break;
    case 9:  /* HSPA+ */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;
    case 10:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case 11:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
    case 12:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOB;
    case 13:  /* 1xRTT */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 16:  /* 3xRTT */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 17: /* HSPA+ (64QAM) */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;
    case 18: /* HSPA+ (MIMO) */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;
    default:
        break;
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static MMModemAccessTechnology
huawei_sysinfo_mode_to_act (guint mode)
{
    /* Older, less detailed system mode/access technology */
    switch (mode) {
    case 1:  /* AMPS */
        break;
    case 2:  /* CDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 3:  /* GSM/GPRS */
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    case 4:  /* HDR */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case 5:  /* WCDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 6:  /* GPS */
        break;
    case 7:  /* GSM/WCDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 8:  /* CDMA/HDR hybrid */
        return (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 | MM_MODEM_ACCESS_TECHNOLOGY_1XRTT);
    case 15: /* TD-SCDMA */
        break;
    default:
        break;
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static MMModemAccessTechnology
huawei_sysinfoex_submode_to_act (guint submode)
{
    switch (submode) {
    case 1: /* GSM */
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case 2: /* GPRS */
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    case 3: /* EDGE */
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;

    case 21: /* IS95A */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 22: /* IS95B */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 23: /* CDMA2000 1x */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 24: /* EVDO rel0 */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case 25: /* EVDO relA */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
    case 26: /* EVDO relB */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOB;
    case 27: /* Hybrid CDMA2000 1x */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 28: /* Hybrid EVDO rel0 */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case 29: /* Hybrid EVDO relA */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
    case 30: /* Hybrid EVDO relB */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOB;

    case 41: /* WCDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 42: /* HSDPA */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    case 43: /* HSUPA */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    case 44: /* HSPA */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
    case 45: /* HSPA+ */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;
    case 46: /* DC-HSPA+ */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;

    case 61: /* TD-SCDMA */
        break;

    case 81: /* 802.16e (WiMAX) */
        break;

    case 101: /* LTE */
        return MM_MODEM_ACCESS_TECHNOLOGY_LTE;

    default:
        break;
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static MMModemAccessTechnology
huawei_sysinfoex_mode_to_act (guint mode)
{
    /* Older, less detailed system mode/access technology */
    switch (mode) {
    case 1:  /* GSM */
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case 2:  /* CDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 3: /* WCDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 4: /* TD-SCDMA */
        break;
    case 5: /* WIMAX */
        break;
    case 6:  /* LTE */
        return MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    default:
        break;
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gboolean extended = FALSE;
    guint srv_status = 0;
    gboolean sys_submode_valid = FALSE;
    guint sys_submode = 0;
    guint sys_mode = 0;

    if (!sysinfo_finish (MM_BROADBAND_MODEM_HUAWEI (self),
                         res,
                         &extended,
                         &srv_status,
                         NULL, /* srv_domain */
                         NULL, /* roam_status */
                         NULL, /* sim_state */
                         &sys_mode,
                         &sys_submode_valid,
                         &sys_submode,
                         error))
        return FALSE;

    if (srv_status != 0) {
        /* Valid service */
        if (sys_submode_valid)
            act = (extended ?
                   huawei_sysinfoex_submode_to_act (sys_submode) :
                   huawei_sysinfo_submode_to_act (sys_submode));

        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            act = (extended ?
                   huawei_sysinfoex_mode_to_act (sys_mode) :
                   huawei_sysinfo_mode_to_act (sys_mode));
    }

    *access_technologies = act;
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    sysinfo (MM_BROADBAND_MODEM_HUAWEI (self), callback, user_data);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    g_autoptr(GRegex)      r = NULL;
    g_autoptr(GMatchInfo)  match_info = NULL;
    MMUnlockRetries       *unlock_retries;
    const gchar           *result;
    GError                *match_error = NULL;
    guint                  i;

    MMModemLock locks[4] = {
        MM_MODEM_LOCK_SIM_PUK,
        MM_MODEM_LOCK_SIM_PIN,
        MM_MODEM_LOCK_SIM_PUK2,
        MM_MODEM_LOCK_SIM_PIN2
    };

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    r = g_regex_new ("\\^CPIN:\\s*([^,]+),[^,]*,(\\d+),(\\d+),(\\d+),(\\d+)",
                     G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, result, strlen (result), 0, 0, &match_info, &match_error)) {
        if (match_error)
            g_propagate_error (error, match_error);
        else
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Could not parse ^CPIN results: Response didn't match (%s)",
                         result);
        return NULL;
    }

    unlock_retries = mm_unlock_retries_new ();
    for (i = 0; i <= 3; i++) {
        guint num;

        if (!mm_get_uint_from_match_info (match_info, i + 2, &num) ||
            num > 10) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Could not parse ^CPIN results: "
                         "Missing or invalid match info for lock '%s'",
                         mm_modem_lock_get_string (locks[i]));
            g_object_unref (unlock_retries);
            unlock_retries = NULL;
            break;
        }

        mm_unlock_retries_set (unlock_retries, locks[i], num);
    }

    return unlock_retries;
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^CPIN?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* After SIM unlock (Modem interface) */

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_sim_unlock_wait_cb (GTask *task)
{
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
modem_after_sim_unlock (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* A 3-second wait is necessary for SIM to become ready, or the firmware may
     * fail miserably and reboot itself */
    g_timeout_add_seconds (3, (GSourceFunc)after_sim_unlock_wait_cb, task);
}

/*****************************************************************************/
/* Common band/mode handling code */

typedef struct {
    MMModemBand mm;
    guint64 huawei;
} BandTable;

static BandTable gsm_umts_bands[] = {
    /* Sort 3G first since it's preferred */
    { MM_MODEM_BAND_UTRAN_1,  G_GUINT64_CONSTANT (0x0000000000400000) },
    { MM_MODEM_BAND_UTRAN_2,  G_GUINT64_CONSTANT (0x0000000000800000) },
    { MM_MODEM_BAND_UTRAN_3,  G_GUINT64_CONSTANT (0x0000000001000000) },
    { MM_MODEM_BAND_UTRAN_4,  G_GUINT64_CONSTANT (0x0000000002000000) },
    { MM_MODEM_BAND_UTRAN_5,  G_GUINT64_CONSTANT (0x0000000004000000) },
    { MM_MODEM_BAND_UTRAN_6,  G_GUINT64_CONSTANT (0x0000000008000000) },
    { MM_MODEM_BAND_UTRAN_8,  G_GUINT64_CONSTANT (0x0002000000000000) },
    { MM_MODEM_BAND_UTRAN_9,  G_GUINT64_CONSTANT (0x0004000000000000) },
    { MM_MODEM_BAND_UTRAN_19, G_GUINT64_CONSTANT (0x1000000000000000) },
    /* 2G second */
    { MM_MODEM_BAND_G850, G_GUINT64_CONSTANT (0x080000) },
    { MM_MODEM_BAND_DCS,  G_GUINT64_CONSTANT (0x000080) },
    { MM_MODEM_BAND_EGSM, G_GUINT64_CONSTANT (0x000100) },
    { MM_MODEM_BAND_PCS,  G_GUINT64_CONSTANT (0x200000) }
};

/* LTE band values reported by ^SYSCFGEX overlap with GSM/UMTS band values */
#define LTE_BAND_TO_HUAWEI(band) (G_GUINT64_CONSTANT(1) << ((band) - 1))

static BandTable lte_bands[] = {
    { MM_MODEM_BAND_EUTRAN_1, LTE_BAND_TO_HUAWEI(1) },
    { MM_MODEM_BAND_EUTRAN_2, LTE_BAND_TO_HUAWEI(2) },
    { MM_MODEM_BAND_EUTRAN_3, LTE_BAND_TO_HUAWEI(3) },
    { MM_MODEM_BAND_EUTRAN_4, LTE_BAND_TO_HUAWEI(4) },
    { MM_MODEM_BAND_EUTRAN_5, LTE_BAND_TO_HUAWEI(5) },
    { MM_MODEM_BAND_EUTRAN_6, LTE_BAND_TO_HUAWEI(6) },
    { MM_MODEM_BAND_EUTRAN_7, LTE_BAND_TO_HUAWEI(7) },
    { MM_MODEM_BAND_EUTRAN_8, LTE_BAND_TO_HUAWEI(8) },
    { MM_MODEM_BAND_EUTRAN_9, LTE_BAND_TO_HUAWEI(9) },
    { MM_MODEM_BAND_EUTRAN_10, LTE_BAND_TO_HUAWEI(10) },
    { MM_MODEM_BAND_EUTRAN_11, LTE_BAND_TO_HUAWEI(11) },
    { MM_MODEM_BAND_EUTRAN_12, LTE_BAND_TO_HUAWEI(12) },
    { MM_MODEM_BAND_EUTRAN_13, LTE_BAND_TO_HUAWEI(13) },
    { MM_MODEM_BAND_EUTRAN_14, LTE_BAND_TO_HUAWEI(14) },
    { MM_MODEM_BAND_EUTRAN_17, LTE_BAND_TO_HUAWEI(17) },
    { MM_MODEM_BAND_EUTRAN_18, LTE_BAND_TO_HUAWEI(18) },
    { MM_MODEM_BAND_EUTRAN_19, LTE_BAND_TO_HUAWEI(19) },
    { MM_MODEM_BAND_EUTRAN_20, LTE_BAND_TO_HUAWEI(20) },
    { MM_MODEM_BAND_EUTRAN_21, LTE_BAND_TO_HUAWEI(21) },
    { MM_MODEM_BAND_EUTRAN_25, LTE_BAND_TO_HUAWEI(25) },
    { MM_MODEM_BAND_EUTRAN_26, LTE_BAND_TO_HUAWEI(26) },
    { MM_MODEM_BAND_EUTRAN_28, LTE_BAND_TO_HUAWEI(28) },
    { MM_MODEM_BAND_EUTRAN_33, LTE_BAND_TO_HUAWEI(33) },
    { MM_MODEM_BAND_EUTRAN_34, LTE_BAND_TO_HUAWEI(34) },
    { MM_MODEM_BAND_EUTRAN_35, LTE_BAND_TO_HUAWEI(35) },
    { MM_MODEM_BAND_EUTRAN_36, LTE_BAND_TO_HUAWEI(36) },
    { MM_MODEM_BAND_EUTRAN_37, LTE_BAND_TO_HUAWEI(37) },
    { MM_MODEM_BAND_EUTRAN_38, LTE_BAND_TO_HUAWEI(38) },
    { MM_MODEM_BAND_EUTRAN_39, LTE_BAND_TO_HUAWEI(39) },
    { MM_MODEM_BAND_EUTRAN_40, LTE_BAND_TO_HUAWEI(40) },
    { MM_MODEM_BAND_EUTRAN_41, LTE_BAND_TO_HUAWEI(41) },
    { MM_MODEM_BAND_EUTRAN_42, LTE_BAND_TO_HUAWEI(42) },
    { MM_MODEM_BAND_EUTRAN_43, LTE_BAND_TO_HUAWEI(43) },
};

static gboolean
bands_array_to_huawei (GArray  *bands_array,
                       guint64 *out_gsm_umts_huawei,
                       guint64 *out_lte_huawei)
{
    guint i;
    MMModemBand mm_band;

    g_assert (out_gsm_umts_huawei);
    g_assert (out_lte_huawei);

    if (bands_array->len == 1 && g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        *out_gsm_umts_huawei = MM_HUAWEI_SYSCFG_BAND_ANY;
        *out_lte_huawei = MM_HUAWEI_SYSCFGEX_BAND_ANY_LTE;
        return TRUE;
    }

    *out_gsm_umts_huawei = 0;
    *out_lte_huawei = 0;

    for (i = 0; i < bands_array->len; i++) {
        guint j;

        mm_band = g_array_index (bands_array, MMModemBand, i);

        if (mm_band < MM_MODEM_BAND_EUTRAN_1) {
            for (j = 0; j < G_N_ELEMENTS (gsm_umts_bands); j++) {
                if (mm_band == gsm_umts_bands[j].mm)
                    *out_gsm_umts_huawei |= gsm_umts_bands[j].huawei;
            }
        } else {
            for (j = 0; j < G_N_ELEMENTS (lte_bands); j++) {
                if (mm_band == lte_bands[j].mm)
                    *out_lte_huawei |= lte_bands[j].huawei;
            }
        }
    }

    return (*out_gsm_umts_huawei + *out_lte_huawei) > 0;
}

static gboolean
huawei_to_bands_array (BandTable *band_table,
                       guint      band_table_len,
                       guint64    huawei,
                       GArray   **bands_array,
                       GError   **error)
{
    guint i;

    *bands_array = NULL;
    for (i = 0; i < band_table_len; i++) {
        if (huawei & band_table[i].huawei) {
            if (G_UNLIKELY (!*bands_array))
                *bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
            g_array_append_val (*bands_array, band_table[i].mm);
        }
    }

    if (!*bands_array) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't build bands array from '%" G_GUINT64_FORMAT "'",
                     huawei);
        return FALSE;
    }

    return TRUE;
}

static gboolean
gsm_umts_huawei_to_bands_array (guint64  huawei,
                                GArray **bands_array,
                                GError **error)
{
    return huawei_to_bands_array (gsm_umts_bands, G_N_ELEMENTS (gsm_umts_bands), huawei, bands_array, error);
}

static gboolean
lte_huawei_to_bands_array (guint64  huawei,
                           GArray **bands_array,
                           GError **error)
{
    return huawei_to_bands_array (lte_bands, G_N_ELEMENTS (lte_bands), huawei, bands_array, error);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
syscfg_load_current_bands_ready (MMBroadbandModemHuawei *self,
                                 GAsyncResult           *res,
                                 GTask                  *task)
{
    const gchar *response;
    GError      *error       = NULL;
    GArray      *bands_array = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (response) {
        guint64 huawei_bands;

        if (mm_huawei_parse_syscfg_response (response, NULL, &huawei_bands, &error)) {
            if (huawei_bands == MM_HUAWEI_SYSCFG_BAND_ANY)
                huawei_bands = self->priv->supported_gsm_umts_bands;

            gsm_umts_huawei_to_bands_array (huawei_bands, &bands_array, &error);
        }
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bands_array, (GDestroyNotify)g_array_unref);

    g_object_unref (task);
}

static void
syscfgex_load_current_bands_ready (MMBroadbandModemHuawei *self,
                                   GAsyncResult           *res,
                                   GTask                  *task)
{
    const gchar      *response;
    GError           *error = NULL;
    g_autoptr(GArray) bands_array = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (response) {
        guint64 huawei_gsm_umts_bands;
        guint64 huawei_lte_bands;
        GArray *bands_temp;

        if (!mm_huawei_parse_syscfgex_response (response, NULL, &huawei_gsm_umts_bands, &huawei_lte_bands, &error)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

        /* handle special "all bands" values */
        if (huawei_gsm_umts_bands == MM_HUAWEI_SYSCFG_BAND_ANY)
            huawei_gsm_umts_bands = self->priv->supported_gsm_umts_bands;

        if (huawei_lte_bands == MM_HUAWEI_SYSCFGEX_BAND_ANY_LTE)
            huawei_lte_bands = self->priv->supported_lte_bands;

        if (gsm_umts_huawei_to_bands_array (huawei_gsm_umts_bands, &bands_temp, &error)) {
            g_array_append_vals (bands_array, bands_temp->data, bands_temp->len);
            g_array_free (bands_temp, TRUE);

            if (lte_huawei_to_bands_array (huawei_lte_bands, &bands_temp, &error)) {
                g_array_append_vals (bands_array, bands_temp->data, bands_temp->len);
                g_array_free (bands_temp, TRUE);
            }
        }
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, g_steal_pointer (&bands_array), (GDestroyNotify)g_array_unref);

    g_object_unref (task);
}

static void
load_current_bands (MMIfaceModem *_self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->syscfgex_support == FEATURE_SUPPORTED) {
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^SYSCFGEX?",
            3,
            FALSE,
            (GAsyncReadyCallback)syscfgex_load_current_bands_ready,
            task);
        return;
    }

    /* fallback */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^SYSCFG?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)syscfg_load_current_bands_ready,
                              task);
}

/*****************************************************************************/
/* Set current bands (Modem interface) */

static gboolean
set_current_bands_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
syscfg_set_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical */
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
set_current_bands (MMIfaceModem        *self,
                   GArray              *bands_array,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask *task;
    g_autofree gchar *cmd = NULL;
    g_autofree gchar *bands_string = NULL;
    guint64 huawei_gsm_umts_bands;
    guint64 huawei_lte_bands;

    task = g_task_new (self, NULL, callback, user_data);

    bands_string = mm_common_build_bands_string ((MMModemBand *)(gpointer)bands_array->data,
                                                 bands_array->len);

    /* We need to split the bands array into two parts: gsm/umts and lte bands.
     * The encoded huawei values overlap and need to be passed as separate
     * AT command parameters to ^SYSCFG(EX) */
    if (!bands_array_to_huawei (bands_array, &huawei_gsm_umts_bands, &huawei_lte_bands)) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Invalid bands requested: '%s'",
                                 bands_string);
        g_object_unref (task);
        return;
    }

    /* Note: SYSCFG(EX) requires at least one band per enabled technology/mode.
     * E.g. if the user wants to set only LTE bands, we'd also need to switch
     * to LTE-only mode ("03"). "Automatic" mode ("00") won't work in this case.
     * "No Change" ("99" or 16) will work if the mode is already set accordingly.
     * So to keep things simple here, assume that the current mode is correct.
     */
    if (MM_BROADBAND_MODEM_HUAWEI (self)->priv->syscfgex_support == FEATURE_SUPPORTED) {
        cmd = g_strdup_printf ("^SYSCFGEX=\"99\",%" G_GINT64_MODIFIER "X,2,4,%" G_GINT64_MODIFIER "X,,",
                               huawei_gsm_umts_bands,
                               huawei_lte_bands);
    } else if (MM_BROADBAND_MODEM_HUAWEI (self)->priv->syscfg_support == FEATURE_SUPPORTED) {
        cmd = g_strdup_printf ("^SYSCFG=16,3,%" G_GINT64_MODIFIER "X,2,4",
                               huawei_gsm_umts_bands);
    } else {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Neither ^SYSCFG nor ^SYSCFGEX is supported to set bands");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)syscfg_set_ready,
                              task);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem  *_self,
                             GAsyncResult  *res,
                             GError       **error)
{
    MMBroadbandModemHuawei *self;
    self = MM_BROADBAND_MODEM_HUAWEI (_self);

    if (!g_task_propagate_boolean (G_TASK (res), error)) {
        return NULL;
    }

    /* check SYSCFGEX first */
    if (self->priv->syscfgex_supported_modes) {
        MMModemModeCombination mode;
        GArray                *supported_modes;
        GArray                *combinations;
        guint                  i;

        supported_modes = self->priv->syscfgex_supported_modes;

        /* Build list of combinations */
        combinations = g_array_sized_new (FALSE,
                                          FALSE,
                                          sizeof (MMModemModeCombination),
                                          supported_modes->len);
        for (i = 0; i < supported_modes->len; i++) {
            MMHuaweiSyscfgexCombination *huawei_mode;

            huawei_mode    = &g_array_index (supported_modes, MMHuaweiSyscfgexCombination, i);
            mode.allowed   = huawei_mode->allowed;
            mode.preferred = huawei_mode->preferred;
            g_array_append_val (combinations, mode);
        }

        return combinations;
    }

    if (self->priv->syscfg_supported_modes) {
        MMModemModeCombination mode;
        GArray                *supported_modes;
        GArray                *combinations;
        guint                  i;

        supported_modes = self->priv->syscfg_supported_modes;

        /* Build list of combinations */
        combinations = g_array_sized_new (FALSE,
                                          FALSE,
                                          sizeof (MMModemModeCombination),
                                          supported_modes->len);
        for (i = 0; i < supported_modes->len; i++) {
            MMHuaweiSyscfgCombination *huawei_mode;

            huawei_mode = &g_array_index (supported_modes,
                                          MMHuaweiSyscfgCombination,
                                          i);
            mode.allowed = huawei_mode->allowed;
            mode.preferred = huawei_mode->preferred;
            g_array_append_val (combinations, mode);
        }

        return combinations;
    }

    if (self->priv->prefmode_supported_modes) {
        return g_array_ref (self->priv->prefmode_supported_modes);
    }

    g_set_error_literal (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "No method found to retrieve supported modes");
    return NULL;
}

static void
syscfg_test_ready (MMBroadbandModemHuawei *self,
                   GAsyncResult           *res,
                   GTask                  *task)
{
    const gchar *response;
    GError      *error           = NULL;
    GArray      *supported_modes = NULL;
    guint64      supported_gsm_umts_bands = 0;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_obj_dbg (self, "error while checking ^SYSCFG format: %s", error->message);
        /* If SIM-PIN error, don't mark as feature unsupported; we'll retry later */
        if (!g_error_matches (error,
                              MM_MOBILE_EQUIPMENT_ERROR,
                              MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN))
            self->priv->syscfg_support = FEATURE_NOT_SUPPORTED;
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* There are 2G+3G Huawei modems out there which support mode switching with
     * AT^SYSCFG, but fail to provide a valid response for AT^SYSCFG=? (they just
     * return an empty string). So handle that case by providing a default response
     * string to get parsed. Ugly, ugly, blame Huawei.
     */
    if (!response[0])
        response = MM_HUAWEI_DEFAULT_SYSCFG_FMT;

    if (!mm_huawei_parse_syscfg_test (response, &supported_modes, &supported_gsm_umts_bands, &error)) {
        self->priv->syscfg_support = FEATURE_NOT_SUPPORTED;

        mm_obj_dbg (self, "failed to parse ^SYSCFG test response: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self->priv->syscfg_support = FEATURE_SUPPORTED;
    self->priv->syscfg_supported_modes = supported_modes;
    self->priv->supported_gsm_umts_bands = supported_gsm_umts_bands;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
syscfgex_test_ready (MMBroadbandModemHuawei *self,
                     GAsyncResult           *res,
                     GTask                  *task)
{
    const gchar *response;
    GError      *error           = NULL;
    GArray      *supported_modes = NULL;
    guint64      supported_gsm_umts_bands;
    guint64      supported_lte_bands;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (response) {
        if (!mm_huawei_parse_syscfgex_test (response,
                                            &supported_modes,
                                            &supported_gsm_umts_bands,
                                            &supported_lte_bands,
                                            &error)) {
            self->priv->syscfgex_support = FEATURE_NOT_SUPPORTED;

            mm_obj_dbg (self, "failed to parse ^SYSCFGEX test response: %s", error->message);
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        self->priv->syscfgex_support = FEATURE_SUPPORTED;
        self->priv->syscfgex_supported_modes = supported_modes;
        self->priv->supported_gsm_umts_bands = supported_gsm_umts_bands;
        self->priv->supported_lte_bands = supported_lte_bands;

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* If SIM-PIN error, don't mark as feature unsupported; we'll retry later */
    if (error) {
        mm_obj_dbg (self, "error while checking ^SYSCFGEX format: %s", error->message);
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        g_error_free (error);
    }

    self->priv->syscfgex_support = FEATURE_NOT_SUPPORTED;

    /* Try with SYSCFG */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^SYSCFG=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)syscfg_test_ready,
                              task);
}

static void
prefmode_test_ready (MMBroadbandModemHuawei *self,
                     GAsyncResult *res,
                     GTask *task)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (response)
        self->priv->prefmode_supported_modes = mm_huawei_parse_prefmode_test (response, self, &error);

    if (self->priv->prefmode_supported_modes) {
        MMModemModeCombination mode;
        guint i;
        GArray *combinations;

        /* Build list of combinations */
        combinations = g_array_sized_new (FALSE,
                                          FALSE,
                                          sizeof (MMModemModeCombination),
                                          self->priv->prefmode_supported_modes->len);
        for (i = 0; i < self->priv->prefmode_supported_modes->len; i++) {
            MMHuaweiPrefmodeCombination *huawei_mode;

            huawei_mode = &g_array_index (self->priv->prefmode_supported_modes,
                                          MMHuaweiPrefmodeCombination,
                                          i);
            mode.allowed = huawei_mode->allowed;
            mode.preferred = huawei_mode->preferred;
            g_array_append_val (combinations, mode);
        }

        self->priv->prefmode_support = FEATURE_SUPPORTED;
        g_task_return_pointer (task,
                               combinations,
                               (GDestroyNotify)g_array_unref);
    } else {
        mm_obj_dbg (self, "error while checking ^PREFMODE format: %s", error->message);
        /* If SIM-PIN error, don't mark as feature unsupported; we'll retry later */
        if (!g_error_matches (error,
                              MM_MOBILE_EQUIPMENT_ERROR,
                              MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN))
            self->priv->prefmode_support = FEATURE_NOT_SUPPORTED;
        g_task_return_error (task, error);
    }

    g_object_unref (task);
}

static void
syscfgex_load_supported_modes_bands (GTask *task)
{
    mm_base_modem_at_command (MM_BASE_MODEM (g_task_get_source_object (task)),
                              "^SYSCFGEX=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)syscfgex_test_ready,
                              task);
}

static void
load_supported_modes (MMIfaceModem *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (mm_iface_modem_is_cdma_only (_self)) {
        /* ^PREFMODE only in CDMA-only modems */
        self->priv->syscfg_support = FEATURE_NOT_SUPPORTED;
        self->priv->syscfgex_support = FEATURE_NOT_SUPPORTED;
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "^PREFMODE=?",
                                  3,
                                  TRUE,
                                  (GAsyncReadyCallback)prefmode_test_ready,
                                  task);
        return;
    }

    /* Check SYSCFGEX (with fallback to SYSCFG) */
    self->priv->prefmode_support = FEATURE_NOT_SUPPORTED;

    if (!self->priv->syscfgex_supported_modes) {
        syscfgex_load_supported_modes_bands (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static GArray *
load_supported_bands_finish (MMIfaceModem *_self,
                             GAsyncResult *res,
                             GError      **error)
{
    MMBroadbandModemHuawei *self;
    GArray *bands_array = NULL;
    GArray *bands_temp  = NULL;

    if (!g_task_propagate_boolean (G_TASK (res), error)) {
        return NULL;
    }

    self = MM_BROADBAND_MODEM_HUAWEI (_self);
    bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

    if (self->priv->supported_gsm_umts_bands &&
        gsm_umts_huawei_to_bands_array (self->priv->supported_gsm_umts_bands, &bands_temp, error)) {

        g_array_append_vals (bands_array, bands_temp->data, bands_temp->len);
        g_array_free (bands_temp, TRUE);
    }

    if (self->priv->supported_lte_bands &&
        lte_huawei_to_bands_array (self->priv->supported_lte_bands, &bands_temp, error)) {

        g_array_append_vals (bands_array, bands_temp->data, bands_temp->len);
        g_array_free (bands_temp, TRUE);
    }

    return bands_array;
}

static void
load_supported_bands (MMIfaceModem       *_self,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    MMBroadbandModemHuawei *self;
    GTask                  *task;

    self = MM_BROADBAND_MODEM_HUAWEI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    /* likely already fetched by load_supported_modes() */
    if (self->priv->supported_gsm_umts_bands == 0 && self->priv->supported_lte_bands == 0) {
        syscfgex_load_supported_modes_bands (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    MMModemModeCombination *out;

    out = g_task_propagate_pointer (G_TASK (res), error);
    if (!out)
        return FALSE;

    *allowed = out->allowed;
    *preferred = out->preferred;

    g_free (out);
    return TRUE;
}

static void
prefmode_load_current_modes_ready (MMBroadbandModemHuawei *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    const MMHuaweiPrefmodeCombination *current = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (response)
        current = mm_huawei_parse_prefmode_response (response,
                                                     self->priv->prefmode_supported_modes,
                                                     &error);

    if (error)
        g_task_return_error (task, error);
    else {
        MMModemModeCombination *out;

        out = g_new (MMModemModeCombination, 1);
        out->allowed = current->allowed;
        out->preferred = current->preferred;
        g_task_return_pointer (task, out, g_free);
    }
    g_object_unref (task);
}

static void
syscfg_load_current_modes_ready (MMBroadbandModemHuawei *self,
                                 GAsyncResult           *res,
                                 GTask                  *task)
{
    const gchar *response;
    GError      *error = NULL;

    g_autofree MMModemModeCombination *mode = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (response) {
        mode = g_new0 (MMModemModeCombination, 1);
        mm_huawei_parse_syscfg_response (response, mode, NULL, &error);
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, g_steal_pointer (&mode), g_free);

    g_object_unref (task);
}

static void
syscfgex_load_current_modes_ready (MMBroadbandModemHuawei *self,
                                   GAsyncResult           *res,
                                   GTask                  *task)
{
    const gchar *response;
    GError      *error = NULL;

    g_autofree MMModemModeCombination *mode = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (response) {
        mode = g_new0 (MMModemModeCombination, 1);

        if (mm_huawei_parse_syscfgex_response (response, mode, NULL, NULL, &error)) {
            if (mode->allowed == MM_MODEM_MODE_ANY) {
                guint         i;
                const GArray *supported_modes = self->priv->syscfgex_supported_modes;

                mode->allowed = MM_MODEM_MODE_NONE;
                for (i = 0; i < supported_modes->len; i++) {
                    mode->allowed |= g_array_index (supported_modes, MMHuaweiSyscfgexCombination, i).allowed;
                }
            }
        }
    }

    if (error)
        g_task_return_error (task, error);
    else {
        g_task_return_pointer (task, g_steal_pointer (&mode), g_free);
    }
    g_object_unref (task);
}

static void
load_current_modes (MMIfaceModem *_self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->syscfgex_support == FEATURE_SUPPORTED) {
        g_assert (self->priv->syscfgex_supported_modes != NULL);
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^SYSCFGEX?",
            3,
            FALSE,
            (GAsyncReadyCallback)syscfgex_load_current_modes_ready,
            task);
        return;
    }

    if (self->priv->syscfg_support == FEATURE_SUPPORTED) {
        g_assert (self->priv->syscfg_supported_modes != NULL);
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^SYSCFG?",
            3,
            FALSE,
            (GAsyncReadyCallback)syscfg_load_current_modes_ready,
            task);
        return;
    }

    if (self->priv->prefmode_support == FEATURE_SUPPORTED) {
        g_assert (self->priv->prefmode_supported_modes != NULL);
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^PREFMODE?",
            3,
            FALSE,
            (GAsyncReadyCallback)prefmode_load_current_modes_ready,
            task);
        return;
    }

    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Unable to load current modes");
    g_object_unref (task);
}

/*****************************************************************************/
/* Set current modes (Modem interface) */

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_current_modes_ready (MMBroadbandModemHuawei *self,
                         GAsyncResult *res,
                         GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
prefmode_set_current_modes (MMBroadbandModemHuawei *self,
                            MMModemMode allowed,
                            MMModemMode preferred,
                            GTask *task,
                            GError **error)
{
    guint i;
    MMHuaweiPrefmodeCombination *found = NULL;
    gchar *command;

    for (i = 0; i < self->priv->prefmode_supported_modes->len; i++) {
        MMHuaweiPrefmodeCombination *single;

        single = &g_array_index (self->priv->prefmode_supported_modes,
                                 MMHuaweiPrefmodeCombination,
                                 i);
        if (single->allowed == allowed && single->preferred == preferred) {
            found = single;
            break;
        }
    }

    if (!found) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Requested mode ^PREFMODE combination not found");
        return FALSE;
    }

    command = g_strdup_printf ("^PREFMODE=%u", found->prefmode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)set_current_modes_ready,
        task);
    g_free (command);
    return TRUE;
}

static gboolean
syscfg_set_current_modes (MMBroadbandModemHuawei *self,
                          MMModemMode allowed,
                          MMModemMode preferred,
                          GTask *task,
                          GError **error)
{
    guint i;
    MMHuaweiSyscfgCombination *found = NULL;
    gchar *command;

    for (i = 0; i < self->priv->syscfg_supported_modes->len; i++) {
        MMHuaweiSyscfgCombination *single;

        single = &g_array_index (self->priv->syscfg_supported_modes,
                                 MMHuaweiSyscfgCombination,
                                 i);
        if (single->allowed == allowed && single->preferred == preferred) {
            found = single;
            break;
        }
    }

    if (!found) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Requested mode ^SYSCFG combination not found");
        return FALSE;
    }

    command = g_strdup_printf ("^SYSCFG=%u,%u,%x,2,4",
                               found->mode,
                               found->acqorder,
                               MM_HUAWEI_SYSCFG_BAND_NO_CHANGE);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)set_current_modes_ready,
        task);
    g_free (command);
    return TRUE;
}

static gboolean
syscfgex_set_current_modes (MMBroadbandModemHuawei *self,
                            MMModemMode allowed,
                            MMModemMode preferred,
                            GTask *task,
                            GError **error)
{
    guint i;
    MMHuaweiSyscfgexCombination *found = NULL;
    gchar *command;

    for (i = 0; i < self->priv->syscfgex_supported_modes->len; i++) {
        MMHuaweiSyscfgexCombination *single;

        single = &g_array_index (self->priv->syscfgex_supported_modes,
                                 MMHuaweiSyscfgexCombination,
                                 i);
        if (single->allowed == allowed && single->preferred == preferred) {
            found = single;
            break;
        }
    }

    if (!found) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Requested mode ^SYSCFGEX combination not found");
        return FALSE;
    }

    command = g_strdup_printf ("^SYSCFGEX=\"%s\",%x,2,4,%" G_GINT64_MODIFIER "x,,",
                               found->mode_str,
                               MM_HUAWEI_SYSCFG_BAND_ANY,
                               MM_HUAWEI_SYSCFGEX_BAND_ANY_LTE);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)set_current_modes_ready,
        task);
    g_free (command);
    return TRUE;
}

static void
set_current_modes (MMIfaceModem *_self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask *task;
    GError *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->syscfgex_support == FEATURE_SUPPORTED)
        syscfgex_set_current_modes (self, allowed, preferred, task, &error);
    else if (self->priv->syscfg_support == FEATURE_SUPPORTED)
        syscfg_set_current_modes (self, allowed, preferred, task, &error);
    else if (self->priv->prefmode_support == FEATURE_SUPPORTED)
        prefmode_set_current_modes (self, allowed, preferred, task, &error);
    else
        error = g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Setting current modes is not supported");

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
    }
}

/*****************************************************************************/
/* Supported IP families (Modem interface) */
static MMBearerIpFamily
load_supported_ip_families_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError      **error)
{
    GError *inner_error = NULL;
    gssize  value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_BEARER_IP_FAMILY_NONE;
    }
    return (MMBearerIpFamily)value;
}

static void
parent_load_supported_ip_families_ready (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GTask *task)
{
    GError *error = NULL;
    MMBearerIpFamily families = MM_BEARER_IP_FAMILY_NONE;

    families = iface_modem_parent->load_supported_ip_families_finish (self, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_int (task, families);
    g_object_unref (task);
}

static void
load_supported_ip_families_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    const gchar     *response;
    MMBearerIpFamily families = MM_BEARER_IP_FAMILY_NONE;
    gboolean         ipv4_available;
    gboolean         ipv4_connected;
    gboolean         ipv6_available;
    gboolean         ipv6_connected;
    GError          *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_obj_dbg (self, "failed to fetch supported IP families using ^NDISSTATQRY: %s", error->message);
        /* fallback to generic detection with AT+CGDCONT=? */
        iface_modem_parent->load_supported_ip_families (
            self,
            (GAsyncReadyCallback)parent_load_supported_ip_families_ready,
            task);
        g_error_free (error);
        return;
    }

    if (mm_huawei_parse_ndisstatqry_response (response,
                                              &ipv4_available,
                                              &ipv4_connected,
                                              &ipv6_available,
                                              &ipv6_connected,
                                              &error)) {
        families |= ipv4_available ? MM_BEARER_IP_FAMILY_IPV4 : 0;
        families |= ipv6_available ? MM_BEARER_IP_FAMILY_IPV6 : 0;
        families |= (ipv4_available && ipv6_available) ? MM_BEARER_IP_FAMILY_IPV4V6 : 0;
    }

    if (error) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "failed to load supported IP families via ^NDISSTATQRY: %s", error->message);
        g_error_free (error);
    } else {
        g_task_return_int (task, (gssize)families);
    }

    g_object_unref (task);
}

static void
load_supported_ip_families (MMIfaceModem       *self,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^NDISSTATQRY?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)load_supported_ip_families_ready,
                              task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static void
huawei_signal_changed (MMPortSerialAt *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemHuawei *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    if (quality == 99) {
        /* 99 means unknown */
        quality = 0;
    } else {
        /* Normalize the quality */
        quality = MM_CLAMP_HIGH (quality, 31) * 100 / 31;
    }

    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
huawei_mode_changed (MMPortSerialAt *port,
                     GMatchInfo *match_info,
                     MMBroadbandModemHuawei *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gchar *str;
    gint a;
    guint32 mask = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    str = g_match_info_fetch (match_info, 1);
    a = atoi (str);
    g_free (str);

    /* CDMA/EVDO devices may not send this */
    str = g_match_info_fetch (match_info, 2);
    if (str[0])
        act = huawei_sysinfo_submode_to_act (atoi (str));
    g_free (str);

    switch (a) {
    case 3:
        /* GSM/GPRS mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            (act < MM_MODEM_ACCESS_TECHNOLOGY_GSM ||
             act > MM_MODEM_ACCESS_TECHNOLOGY_EDGE)) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_obj_warn (self, "unexpected access technology (%s) in GSM/GPRS mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 5:
        /* WCDMA mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            (act < MM_MODEM_ACCESS_TECHNOLOGY_UMTS ||
             act > MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS)) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_obj_warn (self, "unexpected access technology (%s) in WCDMA mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 2:
        /* CDMA mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            act != MM_MODEM_ACCESS_TECHNOLOGY_1XRTT) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_obj_warn (self, "unexpected access technology (%s) in CDMA mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            act = MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
        mask = MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 4:  /* HDR mode */
    case 8:  /* CDMA/HDR hybrid mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            (act < MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 ||
             act > MM_MODEM_ACCESS_TECHNOLOGY_EVDOB)) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_obj_warn (self, "unexpected access technology (%s) in EVDO mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            act = MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
        mask = MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 0:
        act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        break;

    default:
        mm_obj_warn (self, "unexpected mode change value reported: '%d'", a);
        return;
    }

    mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self), act, mask);
}

static void
huawei_status_changed (MMPortSerialAt *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemHuawei *self)
{
    gchar *str;
    gint n1, n2, n3, n4, n5, n6, n7;

    str = g_match_info_fetch (match_info, 1);
    if (sscanf (str, "%x,%x,%x,%x,%x,%x,%x", &n1, &n2, &n3, &n4, &n5, &n6, &n7))
        mm_obj_dbg (self, "duration: %d up: %d Kbps down: %d Kbps total: %d total: %d\n",
                    n1, n2 * 8 / 1000, n3  * 8 / 1000, n4 / 1024, n5 / 1024);
    g_free (str);
}

typedef struct {
    gboolean ipv4_available;
    gboolean ipv4_connected;
    gboolean ipv6_available;
    gboolean ipv6_connected;
} NdisstatResult;

static void
bearer_report_connection_status (MMBaseBearer *bearer,
                                 NdisstatResult *ndisstat_result)
{
    MMBearerConnectionStatus status;

    /* Note: unsolicited ^NDISSTAT messages can contain:
     *   a) only IPv4, b) both IPv4 and IPv6, c) only IPv6 connection status
     * A disconnect (^NDISDUP=1,0) seems to trigger two separate messages though
     */
    status = (ndisstat_result->ipv4_available && ndisstat_result->ipv4_connected) ||
             (ndisstat_result->ipv6_available && ndisstat_result->ipv6_connected) ?
             MM_BEARER_CONNECTION_STATUS_CONNECTED :
             MM_BEARER_CONNECTION_STATUS_DISCONNECTED;

    mm_base_bearer_report_connection_status (bearer, status);
}

static void
huawei_ndisstat_changed (MMPortSerialAt *port,
                         GMatchInfo *match_info,
                         MMBroadbandModemHuawei *self)
{
    gchar *str;
    NdisstatResult ndisstat_result;
    GError *error = NULL;
    MMBearerList *list = NULL;

    str = g_match_info_fetch (match_info, 1);
    if (!mm_huawei_parse_ndisstatqry_response (str,
                                               &ndisstat_result.ipv4_available,
                                               &ndisstat_result.ipv4_connected,
                                               &ndisstat_result.ipv6_available,
                                               &ndisstat_result.ipv6_connected,
                                               &error)) {
        mm_obj_dbg (self, "ignored invalid ^NDISSTAT unsolicited message '%s': %s",
                    str, error->message);
        g_error_free (error);
        g_free (str);
        return;
    }
    g_free (str);

    mm_obj_dbg (self, "NDIS status: IPv4 %s, IPv6 %s",
                ndisstat_result.ipv4_available ?
                (ndisstat_result.ipv4_connected ? "connected" : "disconnected") : "not available",
                ndisstat_result.ipv6_available ?
                (ndisstat_result.ipv6_connected ? "connected" : "disconnected") : "not available");

    /* If empty bearer list, nothing else to do */
    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);
    if (!list)
        return;

    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)bearer_report_connection_status,
                            &ndisstat_result);

    g_object_unref (list);
}

static void
detailed_signal_clear (DetailedSignal *signal)
{
    g_clear_object (&signal->cdma);
    g_clear_object (&signal->evdo);
    g_clear_object (&signal->gsm);
    g_clear_object (&signal->umts);
    g_clear_object (&signal->lte);
}

static gboolean
get_rssi_dbm (guint rssi, gdouble *out_val)
{
    if (rssi <= 96) {
        *out_val = (double) (-121.0 + rssi);
        return TRUE;
    }
    return FALSE;
}

static gboolean
get_ecio_db (guint ecio, gdouble *out_val)
{
    if (ecio <= 65) {
        *out_val = -32.5 + ((double) ecio / 2.0);
        return TRUE;
    }
    return FALSE;
}

static gboolean
get_rsrp_dbm (guint rsrp, gdouble *out_val)
{
    if (rsrp <= 97) {
        *out_val = (double) (-141.0 + rsrp);
        return TRUE;
    }
    return FALSE;
}

static gboolean
get_sinr_db (guint sinr, gdouble *out_val)
{
    if (sinr <= 251) {
        *out_val = -20.2 + (double) (sinr / 5.0);
        return TRUE;
    }
    return FALSE;
}

static gboolean
get_rsrq_db (guint rsrq, gdouble *out_val)
{
    if (rsrq <= 34) {
        *out_val = -20 + (double) (rsrq / 2.0);
        return TRUE;
    }
    return FALSE;
}

static void
huawei_hcsq_changed (MMPortSerialAt *port,
                     GMatchInfo *match_info,
                     MMBroadbandModemHuawei *self)
{
    gchar *str;
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    guint value1 = 0;
    guint value2 = 0;
    guint value3 = 0;
    guint value4 = 0;
    guint value5 = 0;
    gdouble v;
    GError *error = NULL;

    str = g_match_info_fetch (match_info, 1);
    if (!mm_huawei_parse_hcsq_response (str,
                                        &act,
                                        &value1,
                                        &value2,
                                        &value3,
                                        &value4,
                                        &value5,
                                        &error)) {
        mm_obj_dbg (self, "ignored invalid ^HCSQ message '%s': %s", str, error->message);
        g_error_free (error);
        g_free (str);
        return;
    }
    g_free (str);

    detailed_signal_clear (&self->priv->detailed_signal);

    /* 2G */
    if (act == MM_MODEM_ACCESS_TECHNOLOGY_GSM) {
        self->priv->detailed_signal.gsm = mm_signal_new ();
        /* value1: gsm_rssi */
        if (get_rssi_dbm (value1, &v))
            mm_signal_set_rssi (self->priv->detailed_signal.gsm, v);
        return;
    }

    /* 3G */
    if (act == MM_MODEM_ACCESS_TECHNOLOGY_UMTS) {
        self->priv->detailed_signal.umts = mm_signal_new ();
        /* value1: wcdma_rssi */
        if (get_rssi_dbm (value1, &v))
            mm_signal_set_rssi (self->priv->detailed_signal.umts, v);
        /* value2: wcdma_rscp; unused */
        /* value3: wcdma_ecio */
        if (get_ecio_db (value3, &v))
            mm_signal_set_ecio (self->priv->detailed_signal.umts, v);
        return;
    }

    /* 4G */
    if (act == MM_MODEM_ACCESS_TECHNOLOGY_LTE) {
        self->priv->detailed_signal.lte = mm_signal_new ();
        /* value1: lte_rssi */
        if (get_rssi_dbm (value1, &v))
            mm_signal_set_rssi (self->priv->detailed_signal.lte, v);
        /* value2: lte_rsrp */
        if (get_rsrp_dbm (value2, &v))
            mm_signal_set_rsrp (self->priv->detailed_signal.lte, v);
        /* value3: lte_sinr -> SNR? */
        if (get_sinr_db (value3, &v))
            mm_signal_set_snr (self->priv->detailed_signal.lte, v);
        /* value4: lte_rsrq */
        if (get_rsrq_db (value4, &v))
            mm_signal_set_rsrq (self->priv->detailed_signal.lte, v);
        return;
    }

    /* CDMA and EVDO not yet supported */
}

static void
set_3gpp_unsolicited_events_handlers (MMBroadbandModemHuawei *self,
                                      gboolean enable)
{
    GList *ports, *l;

    ports = mm_broadband_modem_huawei_get_at_port_list (self);

    /* Enable/disable unsolicited events in given port */
    for (l = ports; l; l = g_list_next (l)) {
        MMPortSerialAt *port = MM_PORT_SERIAL_AT (l->data);

        /* Signal quality related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->rssi_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_signal_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Access technology related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->mode_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_mode_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Connection status related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->dsflowrpt_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_status_changed : NULL,
            enable ? self : NULL,
            NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->ndisstat_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_ndisstat_changed : NULL,
            enable ? self : NULL,
            NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->hcsq_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_hcsq_changed : NULL,
            enable ? self : NULL,
            NULL);
    }

    g_list_free_full (ports, g_object_unref);
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_3gpp_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                            GAsyncResult *res,
                                            GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* Our own setup now */
        set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), TRUE);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_3gpp_setup_unsolicited_events_ready,
        task);
}

static void
parent_3gpp_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Our own cleanup first */
    set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_3gpp_cleanup_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
own_enable_unsolicited_events_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static const MMBaseModemAtCommand unsolicited_enable_sequence[] = {
    /* With ^PORTSEL we specify whether we want the PCUI port (0) or the
     * modem port (1) to receive the unsolicited messages */
    { "^PORTSEL=0", 5, FALSE, NULL },
    { "^CURC=1",    3, FALSE, NULL },
    { NULL }
};

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMPortSerialAt *primary;
    GError         *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't enable unsolicited events: no primary port");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        MM_IFACE_PORT_AT (primary),
        unsolicited_enable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
        task);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Disabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
own_disable_unsolicited_events_ready (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Next, chain up parent's disable */
    iface_modem_3gpp_parent->disable_unsolicited_events (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
        task);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GTask          *task;
    MMPortSerialAt *primary;

    task = g_task_new (self, NULL, callback, user_data);

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't disable unsolicited events: no primary port");
        g_object_unref (task);
        return;
    }

    /* Our own disable first */
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (self),
        MM_IFACE_PORT_AT (primary),
        "^CURC=0",
        5,
        FALSE, /* allow_cached */
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
huawei_modem_create_bearer_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_huawei_new_ready (GObject *source,
                                   GAsyncResult *res,
                                   GTask *task)
{
    MMBaseBearer *bearer;
    GError *error = NULL;

    bearer = mm_broadband_bearer_huawei_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GTask *task)
{
    MMBaseBearer *bearer;
    GError *error = NULL;

    bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

static void
create_bearer_for_net_port (GTask *task)
{
    MMBroadbandModemHuawei *self;
    MMBearerProperties *properties;

    self = g_task_get_source_object (task);
    properties = g_task_get_task_data (task);

    switch (self->priv->ndisdup_support) {
    case FEATURE_NOT_SUPPORTED:
        mm_obj_dbg (self, "^NDISDUP not supported, creating default bearer...");
        mm_broadband_bearer_new (MM_BROADBAND_MODEM (self),
                                 properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)broadband_bearer_new_ready,
                                 task);
        return;
    case FEATURE_SUPPORTED:
        mm_obj_dbg (self, "^NDISDUP supported, creating huawei bearer...");
        mm_broadband_bearer_huawei_new (MM_BROADBAND_MODEM_HUAWEI (self),
                                        properties,
                                        NULL, /* cancellable */
                                        (GAsyncReadyCallback)broadband_bearer_huawei_new_ready,
                                        task);
        return;
    case FEATURE_SUPPORT_UNKNOWN:
    default:
        g_assert_not_reached ();
    }
}

static MMPortSerialAt *
peek_port_at_for_data (MMBroadbandModemHuawei *self,
                       MMPort *port)
{
    GList *cdc_wdm_at_ports, *l;
    const gchar *net_port_parent_path;
    MMPortSerialAt *found = NULL;

    g_warn_if_fail (mm_port_get_subsys (port) == MM_PORT_SUBSYS_NET);
    net_port_parent_path = mm_kernel_device_get_interface_sysfs_path (mm_port_peek_kernel_device (port));
    if (!net_port_parent_path) {
        mm_obj_warn (self, "no parent path for net port %s", mm_port_get_device (port));
        return NULL;
    }

    /* Find the CDC-WDM port on the same USB interface as the given net port */
    cdc_wdm_at_ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                                 MM_PORT_SUBSYS_USBMISC,
                                                 MM_PORT_TYPE_AT);
    for (l = cdc_wdm_at_ports; l && !found; l = g_list_next (l)) {
        const gchar  *wdm_port_parent_path;

        g_assert (MM_IS_PORT_SERIAL_AT (l->data));
        wdm_port_parent_path = mm_kernel_device_get_interface_sysfs_path (mm_port_peek_kernel_device (MM_PORT (l->data)));
        if (wdm_port_parent_path && g_str_equal (wdm_port_parent_path, net_port_parent_path))
            found = MM_PORT_SERIAL_AT (l->data);
    }

    g_list_free_full (cdc_wdm_at_ports, g_object_unref);
    return found;
}


MMPortSerialAt *
mm_broadband_modem_huawei_peek_port_at_for_data (MMBroadbandModemHuawei *self,
                                                 MMPort *port)
{
    MMPortSerialAt *found;

    g_assert (self->priv->ndisdup_support == FEATURE_SUPPORTED);

    found = peek_port_at_for_data (self, port);
    if (!found)
        mm_obj_dbg (self, "couldn't find associated cdc-wdm port for %s", mm_port_get_device (port));
    return found;
}

static void
ensure_ndisdup_support_checked (MMBroadbandModemHuawei *self,
                                MMPort *port)
{
    /* Check NDISDUP support the first time we need it */
    if (self->priv->ndisdup_support != FEATURE_SUPPORT_UNKNOWN)
        return;

    /* First, check for devices which support NDISDUP on any AT port. These
     * devices are tagged by udev */
    if (mm_kernel_device_get_global_property_as_boolean (mm_port_peek_kernel_device (port), "ID_MM_HUAWEI_NDISDUP_SUPPORTED")) {
        mm_obj_dbg (self, "^NDISDUP is supported");
        self->priv->ndisdup_support = FEATURE_SUPPORTED;
    }
    /* Then, look for devices which have both a net port and a cdc-wdm
     * AT-capable port. We assume that these devices allow NDISDUP only
     * when issued in the cdc-wdm port. */
    else if (peek_port_at_for_data (self, port)) {
        mm_obj_dbg (self, "^NDISDUP is supported on non-serial AT port");
        self->priv->ndisdup_support = FEATURE_SUPPORTED;
    }

    if (self->priv->ndisdup_support != FEATURE_SUPPORT_UNKNOWN)
        return;

    mm_obj_dbg (self, "^NDISDUP is not supported");
    self->priv->ndisdup_support = FEATURE_NOT_SUPPORTED;
}

static void
huawei_modem_create_bearer (MMIfaceModem *self,
                            MMBearerProperties *properties,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GTask *task;
    MMPort *port;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, g_object_ref (properties), g_object_unref);

    port = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (self), MM_PORT_TYPE_NET);
    if (port) {
        ensure_ndisdup_support_checked (MM_BROADBAND_MODEM_HUAWEI (self), port);
        create_bearer_for_net_port (task);
        return;
    }

    mm_obj_dbg (self, "creating default bearer...");
    mm_broadband_bearer_new (MM_BROADBAND_MODEM (self),
                             properties,
                             NULL, /* cancellable */
                             (GAsyncReadyCallback)broadband_bearer_new_ready,
                             task);
}

/*****************************************************************************/
/* USSD encode/decode (3GPP-USSD interface)
 *
 * Huawei devices don't use the current charset (as per AT+CSCS) in the CUSD
 * command, they instead expect data encoded in GSM-7 already, given as a
 * hex string.
 */

static gchar *
encode (MMIfaceModem3gppUssd *self,
        const gchar *command,
        guint *scheme,
        GError **error)
{
    g_autoptr(GByteArray)  gsm = NULL;
    g_autofree guint8     *packed = NULL;
    guint32                packed_len = 0;

    gsm = mm_modem_charset_bytearray_from_utf8 (command, MM_MODEM_CHARSET_GSM, FALSE, error);
    if (!gsm)
        return NULL;

    *scheme = MM_MODEM_GSM_USSD_SCHEME_7BIT;

    /* If command is a multiple of 7 characters long, Huawei firmwares
     * apparently want that padded.  Maybe all modems?
     */
    if (gsm->len % 7 == 0) {
        static const guint8 padding = 0x0d;

        g_byte_array_append (gsm, &padding, 1);
    }

    packed = mm_charset_gsm_pack (gsm->data, gsm->len, 0, &packed_len);
    return mm_utils_bin2hexstr (packed, packed_len);
}

static gchar *
decode (MMIfaceModem3gppUssd *self,
        const gchar *reply,
        GError **error)
{
    g_autofree guint8    *bin = NULL;
    gsize                 bin_len = 0;
    g_autofree guint8    *unpacked = NULL;
    guint32               unpacked_len;
    g_autoptr(GByteArray) unpacked_array = NULL;

    bin = mm_utils_hexstr2bin (reply, -1, &bin_len, error);
    if (!bin)
        return NULL;

    unpacked = mm_charset_gsm_unpack (bin, (bin_len * 8) / 7, 0, &unpacked_len);
    /* if the last character in a 7-byte block is padding, then drop it */
    if ((bin_len % 7 == 0) && (unpacked[unpacked_len - 1] == 0x0d))
        unpacked_len--;

    unpacked_array = g_byte_array_sized_new (unpacked_len);
    g_byte_array_append (unpacked_array, unpacked, unpacked_len);

    return mm_modem_charset_bytearray_to_utf8 (unpacked_array, MM_MODEM_CHARSET_GSM, FALSE, error);
}

/*****************************************************************************/

static void
huawei_1x_signal_changed (MMPortSerialAt *port,
                          GMatchInfo *match_info,
                          MMBroadbandModemHuawei *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    quality = MM_CLAMP_HIGH (quality, 100);
    mm_obj_dbg (self, "1X signal quality: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
huawei_evdo_signal_changed (MMPortSerialAt *port,
                            GMatchInfo *match_info,
                            MMBroadbandModemHuawei *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    quality = MM_CLAMP_HIGH (quality, 100);
    mm_obj_dbg (self, "EVDO signal quality: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

/* Signal quality loading (Modem interface) */

static guint
modem_load_signal_quality_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return 0;
    }
    return (guint)value;
}

static void
parent_load_signal_quality_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    GError *error = NULL;
    guint signal_quality;

    signal_quality = iface_modem_parent->load_signal_quality_finish (self, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_int (task, signal_quality);
    g_object_unref (task);
}

static void
signal_ready (MMBaseModem *self,
              GAsyncResult *res,
              GTask *task)
{
    const gchar *response, *command;
    gchar buf[5];
    guint quality = 0, i = 0;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (!response) {
        /* Fallback to parent's method */
        iface_modem_parent->load_signal_quality (
            MM_IFACE_MODEM (self),
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            task);
        return;
    }

    command = g_task_get_task_data (task);
    g_assert (command);
    response = mm_strip_tag (response, command);
    /* 'command' won't include the trailing ':' in the response, so strip that */
    while ((*response == ':') || isspace (*response))
        response++;

    /* Sanitize response for mm_get_uint_from_str() which wants only digits */
    memset (buf, 0, sizeof (buf));
    while (i < (sizeof (buf) - 1) && isdigit (*response))
        buf[i++] = *response++;

    if (mm_get_uint_from_str (buf, &quality)) {
        quality = MM_CLAMP_HIGH (quality, 100);
        g_task_return_int (task, quality);
    } else {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't parse %s response: '%s'",
                                 command, response);
    }

    g_object_unref (task);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GTask *task;
    MMModemCdmaRegistrationState evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    const char *command = "^CSQLVL";

    task = g_task_new (self, NULL, callback, user_data);

    /* 3GPP modems can just run parent's signal quality loading */
    if (mm_iface_modem_is_3gpp (self)) {
        iface_modem_parent->load_signal_quality (
            self,
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            task);
        return;
    }

    /* CDMA modems need custom signal quality loading */

    g_object_get (G_OBJECT (self),
                  MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE, &evdo_state,
                  NULL);
    if (evdo_state > MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        command = "^HDRCSQLVL";

    g_task_set_task_data (task, g_strdup (command), g_free);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)signal_ready,
        task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (CDMA interface) */

static void
set_cdma_unsolicited_events_handlers (MMBroadbandModemHuawei *self,
                                      gboolean enable)
{
    GList *ports, *l;

    ports = mm_broadband_modem_huawei_get_at_port_list (self);

    /* Enable/disable unsolicited events in given port */
    for (l = ports; l; l = g_list_next (l)) {
        MMPortSerialAt *port = MM_PORT_SERIAL_AT (l->data);

        /* Signal quality related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->rssilvl_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_1x_signal_changed : NULL,
            enable ? self : NULL,
            NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->hrssilvl_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_evdo_signal_changed : NULL,
            enable ? self : NULL,
            NULL);
        /* Access technology related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->mode_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)huawei_mode_changed : NULL,
            enable ? self : NULL,
            NULL);
    }

    g_list_free_full (ports, g_object_unref);
}

static gboolean
modem_cdma_setup_cleanup_unsolicited_events_finish (MMIfaceModemCdma *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_cdma_setup_unsolicited_events_ready (MMIfaceModemCdma *self,
                                            GAsyncResult *res,
                                            GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_cdma_parent->setup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* Our own setup now */
        set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), TRUE);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
modem_cdma_setup_unsolicited_events (MMIfaceModemCdma *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's setup if needed */
    if (iface_modem_cdma_parent->setup_unsolicited_events &&
        iface_modem_cdma_parent->setup_unsolicited_events_finish) {
        iface_modem_cdma_parent->setup_unsolicited_events (
            self,
            (GAsyncReadyCallback)parent_cdma_setup_unsolicited_events_ready,
            task);
        return;
    }

    /* Otherwise just run our setup and complete */
    set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), TRUE);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_cdma_cleanup_unsolicited_events_ready (MMIfaceModemCdma *self,
                                              GAsyncResult *res,
                                              GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_cdma_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_cdma_cleanup_unsolicited_events (MMIfaceModemCdma *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Our own cleanup first */
    set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);

    /* Chain up parent's setup if needed */
    if (iface_modem_cdma_parent->cleanup_unsolicited_events &&
        iface_modem_cdma_parent->cleanup_unsolicited_events_finish) {
        iface_modem_cdma_parent->cleanup_unsolicited_events (
            self,
            (GAsyncReadyCallback)parent_cdma_cleanup_unsolicited_events_ready,
            task);
        return;
    }

    /* Otherwise we're done */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Setup registration checks (CDMA interface) */

typedef struct {
    gboolean skip_qcdm_call_manager_step;
    gboolean skip_qcdm_hdr_step;
    gboolean skip_at_cdma_service_status_step;
    gboolean skip_at_cdma1x_serving_system_step;
    gboolean skip_detailed_registration_state;
} SetupRegistrationChecksResults;

static gboolean
setup_registration_checks_finish (MMIfaceModemCdma *self,
                                  GAsyncResult *res,
                                  gboolean *skip_qcdm_call_manager_step,
                                  gboolean *skip_qcdm_hdr_step,
                                  gboolean *skip_at_cdma_service_status_step,
                                  gboolean *skip_at_cdma1x_serving_system_step,
                                  gboolean *skip_detailed_registration_state,
                                  GError **error)
{
    SetupRegistrationChecksResults *results;

    results = g_task_propagate_pointer (G_TASK (res), error);
    if (!results)
        return FALSE;

    *skip_qcdm_call_manager_step = results->skip_qcdm_call_manager_step;
    *skip_qcdm_hdr_step = results->skip_qcdm_hdr_step;
    *skip_at_cdma_service_status_step = results->skip_at_cdma_service_status_step;
    *skip_at_cdma1x_serving_system_step = results->skip_at_cdma1x_serving_system_step;
    *skip_detailed_registration_state = results->skip_detailed_registration_state;
    g_free (results);
    return TRUE;
}

static void
parent_setup_registration_checks_ready (MMIfaceModemCdma *self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    SetupRegistrationChecksResults *results;
    GError *error = NULL;

    results = g_new0 (SetupRegistrationChecksResults, 1);

    if (!iface_modem_cdma_parent->setup_registration_checks_finish (self,
                                                                    res,
                                                                    &results->skip_qcdm_call_manager_step,
                                                                    &results->skip_qcdm_hdr_step,
                                                                    &results->skip_at_cdma_service_status_step,
                                                                    &results->skip_at_cdma1x_serving_system_step,
                                                                    &results->skip_detailed_registration_state,
                                                                    &error)) {
        g_free (results);
        g_task_return_error (task, error);
    } else {
        gboolean evdo_supported = FALSE;

        g_object_get (self,
                      MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED, &evdo_supported,
                      NULL);

        /* Don't use AT+CSS on EVDO-capable hardware for determining registration
         * status, because often the device will have only an EVDO connection and
         * AT+CSS won't necessarily report EVDO registration status, only 1X.
         */
        if (evdo_supported)
            results->skip_at_cdma1x_serving_system_step = TRUE;

        /* Force to always use the detailed registration checks, as we have
         * ^SYSINFO for that */
        results->skip_detailed_registration_state = FALSE;

        g_task_return_pointer (task, results, g_free);
    }

    g_object_unref (task);
}

static void
setup_registration_checks (MMIfaceModemCdma *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Run parent's checks first */
    iface_modem_cdma_parent->setup_registration_checks (self,
                                                        (GAsyncReadyCallback)parent_setup_registration_checks_ready,
                                                        task);
}

/*****************************************************************************/
/* Detailed registration state (CDMA interface) */

typedef struct {
    MMModemCdmaRegistrationState detailed_cdma1x_state;
    MMModemCdmaRegistrationState detailed_evdo_state;
} DetailedRegistrationStateResults;

typedef struct {
    DetailedRegistrationStateResults state;
} DetailedRegistrationStateContext;

static gboolean
get_detailed_registration_state_finish (MMIfaceModemCdma *self,
                                        GAsyncResult *res,
                                        MMModemCdmaRegistrationState *detailed_cdma1x_state,
                                        MMModemCdmaRegistrationState *detailed_evdo_state,
                                        GError **error)
{
    DetailedRegistrationStateResults *results;

    results = g_task_propagate_pointer (G_TASK (res), error);
    if (!results)
        return FALSE;

    *detailed_cdma1x_state = results->detailed_cdma1x_state;
    *detailed_evdo_state = results->detailed_evdo_state;
    g_free (results);
    return TRUE;
}

static void
registration_state_sysinfo_ready (MMBroadbandModemHuawei *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DetailedRegistrationStateContext *ctx;
    gboolean extended = FALSE;
    guint srv_status = 0;
    guint sys_mode = 0;
    guint roam_status = 0;

    ctx = g_task_get_task_data (task);

    if (!sysinfo_finish (self,
                         res,
                         &extended,
                         &srv_status,
                         NULL, /* srv_domain */
                         &roam_status,
                         NULL, /* sim_state */
                         &sys_mode,
                         NULL, /* sys_submode_valid */
                         NULL, /* sys_submode */
                         NULL)) {
        /* If error, leave superclass' reg state alone if ^SYSINFO isn't supported. */
        g_task_return_pointer (task,
                               g_memdup (&ctx->state, sizeof (ctx->state)),
                               g_free);
        g_object_unref (task);
        return;
    }

    if (srv_status == 2) {
        MMModemCdmaRegistrationState reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        MMModemAccessTechnology act;
        gboolean cdma1x = FALSE;
        gboolean evdo = FALSE;

        /* Service available, check roaming state */
        if (roam_status == 0)
            reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
        else if (roam_status == 1)
            reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;

        /* Check service type */
        act = (extended ?
               huawei_sysinfoex_mode_to_act (sys_mode):
               huawei_sysinfo_mode_to_act (sys_mode));

        if (act & MM_MODEM_ACCESS_TECHNOLOGY_1XRTT) {
            cdma1x = TRUE;
            ctx->state.detailed_cdma1x_state = reg_state;
        }

        if (act & MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 ||
            act & MM_MODEM_ACCESS_TECHNOLOGY_EVDOA ||
            act & MM_MODEM_ACCESS_TECHNOLOGY_EVDOB) {
            evdo = TRUE;
            ctx->state.detailed_evdo_state = reg_state;
        }

        if (!cdma1x && !evdo) {
            /* Say we're registered to something even though sysmode parsing failed */
            mm_obj_dbg (self, "assuming registered at least in CDMA1x");
            ctx->state.detailed_cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        }
    }

    g_task_return_pointer (task,
                           g_memdup (&ctx->state, sizeof (ctx->state)),
                           g_free);
    g_object_unref (task);
}

static void
get_detailed_registration_state (MMIfaceModemCdma *self,
                                 MMModemCdmaRegistrationState cdma1x_state,
                                 MMModemCdmaRegistrationState evdo_state,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DetailedRegistrationStateContext *ctx;
    GTask *task;

    /* Setup context */
    ctx = g_new (DetailedRegistrationStateContext, 1);
    ctx->state.detailed_cdma1x_state = cdma1x_state;
    ctx->state.detailed_evdo_state = evdo_state;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    sysinfo (MM_BROADBAND_MODEM_HUAWEI (self),
             (GAsyncReadyCallback)registration_state_sysinfo_ready,
             task);
}

/*****************************************************************************/
/* Check if Voice supported (Voice interface) */

static gboolean
modem_voice_check_support_finish (MMIfaceModemVoice  *self,
                                  GAsyncResult       *res,
                                  GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
voice_parent_check_support_ready (MMIfaceModemVoice *self,
                                  GAsyncResult      *res,
                                  GTask             *task)
{
    gboolean parent_support;

    parent_support = iface_modem_voice_parent->check_support_finish (self, res, NULL);
    g_task_return_boolean (task, parent_support);
    g_object_unref (task);
}

static void
cvoice_check_ready (MMBaseModem  *_self,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GError                 *error = NULL;
    const gchar            *response;

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (!response ||
        !mm_huawei_parse_cvoice_response (response,
                                          &self->priv->audio_hz,
                                          &self->priv->audio_bits,
                                          &error)) {
        self->priv->cvoice_support = FEATURE_NOT_SUPPORTED;
        mm_obj_dbg (self, "CVOICE is unsupported: %s", error->message);
        g_clear_error (&error);

        /* Now check generic support */
        iface_modem_voice_parent->check_support (MM_IFACE_MODEM_VOICE (self),
                                                 (GAsyncReadyCallback)voice_parent_check_support_ready,
                                                 task);
        return;
    }

    mm_obj_dbg (self, "CVOICE is supported");
    self->priv->cvoice_support = FEATURE_SUPPORTED;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_voice_check_support (MMIfaceModemVoice   *self,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    GTask *task;

    /* Check for Huawei-specific ^CVOICE support */
    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^CVOICE?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cvoice_check_ready,
                              task);
}

/*****************************************************************************/
/* In-call audio channel setup/cleanup */

static gboolean
modem_voice_cleanup_in_call_audio_channel_finish (MMIfaceModemVoice  *self,
                                                  GAsyncResult       *res,
                                                  GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_voice_cleanup_in_call_audio_channel (MMIfaceModemVoice   *_self,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask                  *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If there is no CVOICE support, no custom audio setup required
     * (i.e. audio path is externally managed) */
    if (self->priv->cvoice_support == FEATURE_SUPPORTED) {
        MMPort *port;

        /* The QCDM port, if present, switches back from voice to QCDM after
         * the voice call is dropped. */
        port = MM_PORT (mm_base_modem_peek_port_qcdm (MM_BASE_MODEM (self)));
        if (port) {
            /* During a voice call, we'll set the QCDM port as connected, and that
             * will make us ignore all incoming data and avoid sending any outgoing
             * data. */
            mm_port_set_connected (port, FALSE);
        }
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
modem_voice_setup_in_call_audio_channel_finish (MMIfaceModemVoice  *_self,
                                                GAsyncResult       *res,
                                                MMPort            **audio_port,
                                                MMCallAudioFormat **audio_format,
                                                GError            **error)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    if (self->priv->cvoice_support == FEATURE_SUPPORTED) {
        MMPort *port;

        /* Setup audio format */
        if (audio_format) {
            gchar *resolution_str;

            resolution_str = g_strdup_printf ("s%ule", self->priv->audio_bits);
            *audio_format = mm_call_audio_format_new ();
            mm_call_audio_format_set_encoding   (*audio_format, "pcm");
            mm_call_audio_format_set_resolution (*audio_format, resolution_str);
            mm_call_audio_format_set_rate       (*audio_format, self->priv->audio_hz);
            g_free (resolution_str);
        }

        /* The QCDM port, if present, switches from QCDM to voice while
         * a voice call is active. */
        port = MM_PORT (mm_base_modem_peek_port_qcdm (MM_BASE_MODEM (self)));
        if (port) {
            /* During a voice call, we'll set the QCDM port as connected, and that
             * will make us ignore all incoming data and avoid sending any outgoing
             * data. */
            mm_port_set_connected (port, TRUE);
        }

        if (audio_port)
            *audio_port = (port ? g_object_ref (port) : NULL);;
    } else {
        if (audio_format)
            *audio_format =  NULL;
        if (audio_port)
            *audio_port =  NULL;
    }

    return TRUE;
}

static void
ddsetex_ready (MMBaseModem  *self,
               GAsyncResult *res,
               GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_voice_setup_in_call_audio_channel (MMIfaceModemVoice   *_self,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask                  *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If there is no CVOICE support, no custom audio setup required
     * (i.e. audio path is externally managed) */
    if (self->priv->cvoice_support != FEATURE_SUPPORTED) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Enable audio streaming on the audio port */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^DDSETEX=2",
                              5,
                              FALSE,
                              (GAsyncReadyCallback)ddsetex_ready,
                              task);
}

/*****************************************************************************/
/* Common setup/cleanup voice unsolicited events */

typedef enum {
    HUAWEI_CALL_TYPE_VOICE                  = 0,
    HUAWEI_CALL_TYPE_CS_DATA                = 1,
    HUAWEI_CALL_TYPE_PS_DATA                = 2,
    HUAWEI_CALL_TYPE_CDMA_SMS               = 3,
    HUAWEI_CALL_TYPE_OTA_STANDARD_OTASP     = 7,
    HUAWEI_CALL_TYPE_OTA_NON_STANDARD_OTASP = 8,
    HUAWEI_CALL_TYPE_EMERGENCY              = 9,
} HuaweiCallType;

static void
orig_received (MMPortSerialAt         *port,
               GMatchInfo             *match_info,
               MMBroadbandModemHuawei *self)
{
    MMCallInfo call_info = { 0 };
    guint      aux       = 0;

    if (!mm_get_uint_from_match_info (match_info, 2, &aux)) {
        mm_obj_warn (self, "couldn't parse call type from ^ORIG");
        return;
    }
    if (aux != HUAWEI_CALL_TYPE_VOICE && aux != HUAWEI_CALL_TYPE_EMERGENCY) {
        mm_obj_dbg (self, "ignored ^ORIG for non-voice call");
        return;
    }

    if (!mm_get_uint_from_match_info (match_info, 1, &aux)) {
        mm_obj_warn (self, "couldn't parse call index from ^ORIG");
        return;
    }
    call_info.index     = aux;
    call_info.state     = MM_CALL_STATE_DIALING;
    call_info.direction = MM_CALL_DIRECTION_OUTGOING;

    mm_obj_dbg (self, "call %u state updated: dialing", call_info.index);

    mm_iface_modem_voice_report_call (MM_IFACE_MODEM_VOICE (self), &call_info);
}

static void
conf_received (MMPortSerialAt         *port,
               GMatchInfo             *match_info,
               MMBroadbandModemHuawei *self)
{
    MMCallInfo call_info = { 0 };
    guint      aux       = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &aux)) {
        mm_obj_warn (self, "couldn't parse call index from ^CONF");
        return;
    }
    call_info.index     = aux;
    call_info.state     = MM_CALL_STATE_RINGING_OUT;
    call_info.direction = MM_CALL_DIRECTION_OUTGOING;

    mm_obj_dbg (self, "call %u state updated: ringing-out", call_info.index);

    mm_iface_modem_voice_report_call (MM_IFACE_MODEM_VOICE (self), &call_info);
}

static void
conn_received (MMPortSerialAt         *port,
               GMatchInfo             *match_info,
               MMBroadbandModemHuawei *self)
{
    MMCallInfo call_info = { 0 };
    guint      aux       = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &aux)) {
        mm_obj_warn (self, "couldn't parse call index from ^CONN");
        return;
    }
    call_info.index     = aux;
    call_info.state     = MM_CALL_STATE_ACTIVE;
    call_info.direction = MM_CALL_DIRECTION_UNKNOWN;

    mm_obj_dbg (self, "call %u state updated: active", aux);

    mm_iface_modem_voice_report_call (MM_IFACE_MODEM_VOICE (self), &call_info);
}

static void
cend_received (MMPortSerialAt         *port,
               GMatchInfo             *match_info,
               MMBroadbandModemHuawei *self)
{
    MMCallInfo call_info  = { 0 };
    guint      aux        = 0;

    /* only index is mandatory */
    if (!mm_get_uint_from_match_info (match_info, 1, &aux)) {
        mm_obj_warn (self, "couldn't parse call index from ^CEND");
        return;
    }
    call_info.index = aux;
    call_info.state = MM_CALL_STATE_TERMINATED;
    call_info.direction = MM_CALL_DIRECTION_UNKNOWN;

    mm_obj_dbg (self, "call %u state updated: terminated", call_info.index);
    if (mm_get_uint_from_match_info (match_info, 2, &aux))
        mm_obj_dbg (self, "  call duration: %u seconds", aux);
    if (mm_get_uint_from_match_info (match_info, 3, &aux))
        mm_obj_dbg (self, "  end status code: %u", aux);
    if (mm_get_uint_from_match_info (match_info, 4, &aux))
        mm_obj_dbg (self, "  call control cause: %u", aux);

    mm_iface_modem_voice_report_call (MM_IFACE_MODEM_VOICE (self), &call_info);
}

static void
ddtmf_received (MMPortSerialAt         *port,
                GMatchInfo             *match_info,
                MMBroadbandModemHuawei *self)
{
    gchar *dtmf;

    dtmf = g_match_info_fetch (match_info, 1);
    mm_obj_dbg (self, "received DTMF: %s", dtmf);
    /* call index unknown */
    mm_iface_modem_voice_received_dtmf (MM_IFACE_MODEM_VOICE (self), 0, dtmf);
    g_free (dtmf);
}

static void
common_voice_setup_cleanup_unsolicited_events (MMBroadbandModemHuawei *self,
                                               gboolean                enable)
{
    MMPortSerialAt *ports[2];
    guint           i;

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        mm_port_serial_at_add_unsolicited_msg_handler (ports[i],
                                                       self->priv->orig_regex,
                                                       enable ? (MMPortSerialAtUnsolicitedMsgFn)orig_received : NULL,
                                                       enable ? self : NULL,
                                                       NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (ports[i],
                                                       self->priv->conf_regex,
                                                       enable ? (MMPortSerialAtUnsolicitedMsgFn)conf_received : NULL,
                                                       enable ? self : NULL,
                                                       NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (ports[i],
                                                       self->priv->conn_regex,
                                                       enable ? (MMPortSerialAtUnsolicitedMsgFn)conn_received : NULL,
                                                       enable ? self : NULL,
                                                       NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (ports[i],
                                                       self->priv->cend_regex,
                                                       enable ? (MMPortSerialAtUnsolicitedMsgFn)cend_received : NULL,
                                                       enable ? self : NULL,
                                                       NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (ports[i],
                                                       self->priv->ddtmf_regex,
                                                       enable ? (MMPortSerialAtUnsolicitedMsgFn)ddtmf_received : NULL,
                                                       enable ? self : NULL,
                                                       NULL);
    }
}

/*****************************************************************************/
/* Setup unsolicited events (Voice interface) */

static gboolean
modem_voice_setup_unsolicited_events_finish (MMIfaceModemVoice  *self,
                                             GAsyncResult       *res,
                                             GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_voice_setup_unsolicited_events_ready (MMIfaceModemVoice *self,
                                              GAsyncResult      *res,
                                              GTask             *task)
{
    GError *error = NULL;

    if (!iface_modem_voice_parent->setup_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Our own setup now */
    common_voice_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_HUAWEI (self), TRUE);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_voice_setup_unsolicited_events (MMIfaceModemVoice   *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's setup */
    iface_modem_voice_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_voice_setup_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Cleanup unsolicited events (Voice interface) */

static gboolean
modem_voice_cleanup_unsolicited_events_finish (MMIfaceModemVoice  *self,
                                               GAsyncResult       *res,
                                               GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_voice_cleanup_unsolicited_events_ready (MMIfaceModemVoice *self,
                                               GAsyncResult      *res,
                                               GTask             *task)
{
    GError *error = NULL;

    if (!iface_modem_voice_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_voice_cleanup_unsolicited_events (MMIfaceModemVoice   *self,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* cleanup our own */
    common_voice_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);

    /* Chain up parent's cleanup */
    iface_modem_voice_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_voice_cleanup_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Enabling unsolicited events (Voice interface) */

static gboolean
modem_voice_enable_unsolicited_events_finish (MMIfaceModemVoice  *self,
                                              GAsyncResult       *res,
                                              GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
own_voice_enable_unsolicited_events_ready (MMBaseModem  *self,
                                           GAsyncResult *res,
                                           GTask        *task)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static const MMBaseModemAtCommand unsolicited_voice_enable_sequence[] = {
    /* With ^DDTMFCFG we active the DTMF Decoder */
    { "^DDTMFCFG=0,1", 3, FALSE, NULL },
    { NULL }
};

static void
parent_voice_enable_unsolicited_events_ready (MMIfaceModemVoice *self,
                                              GAsyncResult      *res,
                                              GTask             *task)
{
    MMPortSerialAt *primary;
    GError         *error = NULL;

    if (!iface_modem_voice_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't enable voice unsolicited events: no primary port");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        MM_IFACE_PORT_AT (primary),
        unsolicited_voice_enable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_voice_enable_unsolicited_events_ready,
        task);
}

static void
modem_voice_enable_unsolicited_events (MMIfaceModemVoice   *self,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's enable */
    iface_modem_voice_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_voice_enable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Disabling unsolicited events (Voice interface) */

static gboolean
modem_voice_disable_unsolicited_events_finish (MMIfaceModemVoice  *self,
                                               GAsyncResult       *res,
                                               GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
own_voice_disable_unsolicited_events_ready (MMBaseModem  *self,
                                            GAsyncResult *res,
                                            GTask        *task)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static const MMBaseModemAtCommand unsolicited_voice_disable_sequence[] = {
    /* With ^DDTMFCFG we deactivate the DTMF Decoder */
    { "^DDTMFCFG=1,0", 3, FALSE, NULL },
    { NULL }
};

static void
parent_voice_disable_unsolicited_events_ready (MMIfaceModemVoice *self,
                                               GAsyncResult      *res,
                                               GTask             *task)
{
    MMPortSerialAt *primary;
    GError         *error = NULL;

    if (!iface_modem_voice_parent->disable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't disable voice unsolicited events: no primary port");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        MM_IFACE_PORT_AT (primary),
        unsolicited_voice_disable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_voice_disable_unsolicited_events_ready,
        task);
}

static void
modem_voice_disable_unsolicited_events (MMIfaceModemVoice   *self,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's disable */
    iface_modem_voice_parent->disable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_voice_disable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Create call (Voice interface) */

static MMBaseCall *
create_call (MMIfaceModemVoice *self,
             MMCallDirection    direction,
             const gchar       *number,
             const guint        dtmf_tone_duration)
{
    return mm_call_at_new (MM_BASE_MODEM (self),
                           G_OBJECT (self),
                           direction,
                           number,
                           dtmf_tone_duration,
                           TRUE,  /* skip_incoming_timeout */
                           TRUE,  /* supports_dialing_to_ringing */
                           TRUE); /* supports_ringing_to_active) */
}

/*****************************************************************************/
/* Load network time (Time interface) */

static MMNetworkTimezone *
modem_time_load_network_timezone_finish (MMIfaceModemTime *_self,
                                         GAsyncResult *res,
                                         GError **error)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    MMNetworkTimezone *tz = NULL;
    const gchar *response;

    g_assert (self->priv->nwtime_support == FEATURE_SUPPORTED ||
              self->priv->time_support == FEATURE_SUPPORTED);

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (_self), res, error);
    if (!response)
        return NULL;

    if (self->priv->nwtime_support == FEATURE_SUPPORTED)
        mm_huawei_parse_nwtime_response (response, NULL, &tz, error);
    else if (self->priv->time_support == FEATURE_SUPPORTED)
        mm_huawei_parse_time_response (response, NULL, &tz, error);
    return tz;
}


static gchar *
modem_time_load_network_time_finish (MMIfaceModemTime *_self,
                                     GAsyncResult *res,
                                     GError **error)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    const gchar *response;
    gchar *iso8601 = NULL;

    g_assert (self->priv->nwtime_support == FEATURE_SUPPORTED ||
              self->priv->time_support == FEATURE_SUPPORTED);

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (_self), res, error);
    if (!response)
        return NULL;

    if (self->priv->nwtime_support == FEATURE_SUPPORTED)
        mm_huawei_parse_nwtime_response (response, &iso8601, NULL, error);
    else if (self->priv->time_support == FEATURE_SUPPORTED)
        mm_huawei_parse_time_response (response, &iso8601, NULL, error);
    return iso8601;
}

static void
modem_time_load_network_time_or_zone (MMIfaceModemTime *_self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    const char *command = NULL;
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);

    if (self->priv->nwtime_support == FEATURE_SUPPORTED)
        command = "^NWTIME?";
    else if (self->priv->time_support == FEATURE_SUPPORTED)
        command = "^TIME";

    g_assert (command != NULL);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Power state loading (Modem interface) */

static void
enable_disable_unsolicited_rfswitch_event_handler (MMBroadbandModemHuawei *self,
                                                   gboolean enable)
{
    GList *ports, *l;

    ports = mm_broadband_modem_huawei_get_at_port_list (self);

    mm_obj_dbg (self, "%s ^RFSWITCH unsolicited event handler",
                enable ? "enable" : "disable");

    for (l = ports; l; l = g_list_next (l)) {
        MMPortSerialAt *port = MM_PORT_SERIAL_AT (l->data);

        mm_port_serial_at_enable_unsolicited_msg_handler (
            port,
            self->priv->rfswitch_regex,
            enable);
    }

    g_list_free_full (ports, g_object_unref);
}

static void
parent_load_power_state_ready (MMIfaceModem *self,
                               GAsyncResult *res,
                               GTask *task)
{
    GError *error = NULL;
    MMModemPowerState power_state;

    power_state = iface_modem_parent->load_power_state_finish (self, res, &error);
    if (error)
        g_task_return_error (task, error);
    else {
        /* As modem_power_down uses +CFUN=0 to put the modem in low state, we treat
         * CFUN 0 as 'LOW' power state instead of 'OFF'. Otherwise, MMIfaceModem
         * would prevent the modem from transitioning back to the 'ON' power state. */
        if (power_state == MM_MODEM_POWER_STATE_OFF)
            power_state = MM_MODEM_POWER_STATE_LOW;

        g_task_return_int (task, power_state);
    }

    g_object_unref (task);
}

static void
huawei_rfswitch_check_ready (MMBaseModem *_self,
                             GAsyncResult *res,
                             GTask *task)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GError *error = NULL;
    const gchar *response;
    gint sw_state;

    enable_disable_unsolicited_rfswitch_event_handler (MM_BROADBAND_MODEM_HUAWEI (self),
                                                       TRUE /* enable */);

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (response) {
        response = mm_strip_tag (response, "^RFSWITCH:");
        if (sscanf (response, "%d", &sw_state) != 1 ||
            (sw_state != 0 && sw_state != 1)) {
            mm_obj_warn (self, "couldn't parse ^RFSWITCH response '%s'", response);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't parse ^RFSWITCH response '%s'",
                                 response);
        }
    }

    if (self->priv->rfswitch_support == FEATURE_SUPPORT_UNKNOWN) {
        if (error) {
            mm_obj_dbg (self, "^RFSWITCH is not supported");
            self->priv->rfswitch_support = FEATURE_NOT_SUPPORTED;
            g_error_free (error);
            /* Fall back to parent's load_power_state */
            iface_modem_parent->load_power_state (MM_IFACE_MODEM (self),
                                                  (GAsyncReadyCallback)parent_load_power_state_ready,
                                                  task);
            return;
        }

        mm_obj_dbg (self, "^RFSWITCH is supported");
        self->priv->rfswitch_support = FEATURE_SUPPORTED;
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_int (task,
                           sw_state ? MM_MODEM_POWER_STATE_ON : MM_MODEM_POWER_STATE_LOW);

    g_object_unref (task);
}

static MMModemPowerState
load_power_state_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_POWER_STATE_UNKNOWN;
    }
    return (MMModemPowerState)value;
}

static void
load_power_state (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    switch (MM_BROADBAND_MODEM_HUAWEI (self)->priv->rfswitch_support) {
    case FEATURE_SUPPORT_UNKNOWN:
    case FEATURE_SUPPORTED: {
        /* Temporarily disable the unsolicited ^RFSWITCH event handler in order to
         * prevent it from discarding the response to the ^RFSWITCH? command.
         * It will be re-enabled in huawei_rfswitch_check_ready.
         */
        enable_disable_unsolicited_rfswitch_event_handler (MM_BROADBAND_MODEM_HUAWEI (self),
                                                           FALSE /* enable */);
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "^RFSWITCH?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)huawei_rfswitch_check_ready,
                                  task);
        break;
    }
    case FEATURE_NOT_SUPPORTED:
      /* Run parent's load_power_state */
      iface_modem_parent->load_power_state (self,
                                            (GAsyncReadyCallback)parent_load_power_state_ready,
                                            task);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

/*****************************************************************************/
/* Modem power up (Modem interface) */

static gboolean
huawei_modem_power_up_finish (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
huawei_modem_power_up (MMIfaceModem *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    switch (MM_BROADBAND_MODEM_HUAWEI (self)->priv->rfswitch_support) {
    case FEATURE_NOT_SUPPORTED:
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+CFUN=1",
                                  30,
                                  FALSE,
                                  callback,
                                  user_data);
        break;
    case FEATURE_SUPPORTED:
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "^RFSWITCH=1",
                                  30,
                                  FALSE,
                                  callback,
                                  user_data);
        break;
    case FEATURE_SUPPORT_UNKNOWN:
    default:
        g_assert_not_reached ();
        break;
    }
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
huawei_modem_power_down_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
huawei_modem_power_down (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    switch (MM_BROADBAND_MODEM_HUAWEI (self)->priv->rfswitch_support) {
    case FEATURE_NOT_SUPPORTED:
        /* +CFUN=0 is supported on all Huawei modems but +CFUN=4 isn't,
         * thus we use +CFUN=0 to put the modem in low power state. */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+CFUN=0",
                                  30,
                                  FALSE,
                                  callback,
                                  user_data);
        break;
    case FEATURE_SUPPORTED:
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "^RFSWITCH=0",
                                  30,
                                  FALSE,
                                  callback,
                                  user_data);
        break;
    case FEATURE_SUPPORT_UNKNOWN:
    default:
        g_assert_not_reached ();
        break;
    }
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
huawei_modem_create_sim_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return mm_sim_huawei_new_finish (res, error);
}

static void
huawei_modem_create_sim (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    /* New Sierra SIM */
    mm_sim_huawei_new (MM_BASE_MODEM (self),
                       NULL, /* cancellable */
                       callback,
                       user_data);
}


/*****************************************************************************/
/* Location capabilities loading (Location interface) */

static MMModemLocationSource
location_load_capabilities_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_SOURCE_NONE;
    }
    return (MMModemLocationSource)value;
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult *res,
                                GTask *task)
{
    MMModemLocationSource sources;
    GError *error = NULL;

    sources = iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* not sure how to check if GPS is supported, just allow it */
    if (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED);

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

static void
location_load_capabilities (MMIfaceModemLocation *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's setup */
    iface_modem_location_parent->load_capabilities (self,
                                                    (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                    task);
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

static gboolean
disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                   GAsyncResult          *res,
                                   GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gps_disabled_ready (MMBaseModem  *self,
                    GAsyncResult *res,
                    GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disable_location_gathering (MMIfaceModemLocation  *_self,
                            MMModemLocationSource  source,
                            GAsyncReadyCallback    callback,
                            gpointer               user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask                  *task;

    /* NOTE: no parent disable_location_gathering() implementation */

    task = g_task_new (self, NULL, callback, user_data);

    self->priv->enabled_sources &= ~source;

    /* Only stop GPS engine if no GPS-related sources enabled */
    if ((source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                   MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                   MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) &&
        !(self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                         MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                         MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))) {
        MMPortSerialGps *gps_port;

        /* Close the data port if we don't need it anymore */
        if (source & (MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA)) {
            gps_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
            if (gps_port)
                mm_port_serial_close (MM_PORT_SERIAL (gps_port));
        }

        mm_base_modem_at_command (MM_BASE_MODEM (_self),
                                  "^WPEND",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)gps_disabled_ready,
                                  task);
        return;
    }

    /* For any other location (e.g. 3GPP), or if still some GPS needed, just return */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

static const MMBaseModemAtCommand gps_startup[] = {
    { "^WPDOM=0",        3, FALSE, mm_base_modem_response_processor_no_result_continue },
    { "^WPDST=1",        3, FALSE, mm_base_modem_response_processor_no_result_continue },
    { "^WPDFR=65535,30", 3, FALSE, mm_base_modem_response_processor_no_result_continue },
    { "^WPDGP",          3, FALSE, mm_base_modem_response_processor_no_result_continue },
    { NULL }
};

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                  GAsyncResult          *res,
                                  GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gps_startup_ready (MMBaseModem  *_self,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    MMModemLocationSource   source;
    GError                 *error = NULL;

    mm_base_modem_at_sequence_finish (_self, res, NULL, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* Only open the GPS port in NMEA/RAW setups */
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMPortSerialGps *gps_port;

        gps_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
        if (!gps_port || !mm_port_serial_open (MM_PORT_SERIAL (gps_port), &error)) {
            if (error)
                g_task_return_error (task, error);
            else
                g_task_return_new_error (task,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't open raw GPS serial port");
        } else {
            /* GPS port was successfully opened */
            self->priv->enabled_sources |= source;
            g_task_return_boolean (task, TRUE);
        }
    } else {
        /* No need to open GPS port */
        self->priv->enabled_sources |= source;
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GError                 *error = NULL;
    MMModemLocationSource   source;
    gboolean                start_gps = FALSE;

    if (!iface_modem_location_parent->enable_location_gathering_finish (_self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own enabling */

    source = GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* Only start GPS engine if not done already */
    start_gps = ((source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                            MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                            MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) &&
                 !(self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                                  MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                                  MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)));

    if (start_gps) {
        mm_base_modem_at_sequence (
            MM_BASE_MODEM (self),
            gps_startup,
            NULL, /* response_processor_context */
            NULL, /* response_processor_context_free */
            (GAsyncReadyCallback)gps_startup_ready,
            task);
        return;
    }

    /* For any other location (e.g. 3GPP), or if GPS already running just return */
    self->priv->enabled_sources |= source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
enable_location_gathering (MMIfaceModemLocation  *self,
                           MMModemLocationSource  source,
                           GAsyncReadyCallback    callback,
                           gpointer               user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (self,
                                                            source,
                                                            (GAsyncReadyCallback)parent_enable_location_gathering_ready,
                                                            task);
}

/*****************************************************************************/
/* Check support (Time interface) */

static gboolean
modem_time_check_support_finish (MMIfaceModemTime *_self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_time_check_ready (MMBaseModem *_self,
                        GAsyncResult *res,
                        GTask *task)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);

    /* Responses are checked in the sequence parser, ignore overall result */
    mm_base_modem_at_sequence_finish (_self, res, NULL, NULL);

    g_task_return_boolean (task,
                           (self->priv->nwtime_support == FEATURE_SUPPORTED ||
                            self->priv->time_support == FEATURE_SUPPORTED));
    g_object_unref (task);
}

static MMBaseModemAtResponseProcessorResult
modem_check_time_reply (MMBaseModem   *_self,
                        gpointer       none,
                        const gchar   *command,
                        const gchar   *response,
                        gboolean       last_command,
                        const GError  *error,
                        GVariant     **result,
                        GError       **result_error)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);

    if (!error) {
        if (strstr (response, "^NWTIME"))
            self->priv->nwtime_support = FEATURE_SUPPORTED;
        else if (strstr (response, "^TIME"))
            self->priv->time_support = FEATURE_SUPPORTED;
    } else {
        if (strstr (command, "^NWTIME"))
            self->priv->nwtime_support = FEATURE_NOT_SUPPORTED;
        else if (strstr (command, "^TIME"))
            self->priv->time_support = FEATURE_NOT_SUPPORTED;
    }

    *result = NULL;
    *result_error = NULL;
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
}

static const MMBaseModemAtCommand time_cmd_sequence[] = {
    { "^NWTIME?", 3, FALSE, modem_check_time_reply }, /* 3GPP/LTE */
    { "^TIME",  3, FALSE, modem_check_time_reply }, /* CDMA */
    { NULL }
};

static void
modem_time_check_support (MMIfaceModemTime *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               time_cmd_sequence,
                               NULL, /* response_processor_context */
                               NULL, /* response_processor_context_free */
                               (GAsyncReadyCallback)modem_time_check_ready,
                               task);
}

/*****************************************************************************/
/* Check support (Signal interface) */

static void
hcsq_check_ready (MMBaseModem *_self,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (response)
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static gboolean
signal_check_support_finish (MMIfaceModemSignal *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
signal_check_support (MMIfaceModemSignal *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^HCSQ?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)hcsq_check_ready,
                              task);
}

/*****************************************************************************/
/* Load extended signal information */

static void
detailed_signal_free (DetailedSignal *signal)
{
    detailed_signal_clear (signal);
    g_slice_free (DetailedSignal, signal);
}

static gboolean
signal_load_values_finish (MMIfaceModemSignal *self,
                           GAsyncResult *res,
                           MMSignal **cdma,
                           MMSignal **evdo,
                           MMSignal **gsm,
                           MMSignal **umts,
                           MMSignal **lte,
                           MMSignal **nr5g,
                           GError **error)
{
    DetailedSignal *signals;

    signals = g_task_propagate_pointer (G_TASK (res), error);
    if (!signals)
        return FALSE;

    *cdma = signals->cdma ? g_object_ref (signals->cdma) : NULL;
    *evdo = signals->evdo ? g_object_ref (signals->evdo) : NULL;
    *gsm  = signals->gsm ? g_object_ref (signals->gsm) : NULL;
    *umts = signals->umts ? g_object_ref (signals->umts) : NULL;
    *lte  = signals->lte ? g_object_ref (signals->lte) : NULL;
    *nr5g = signals->nr5g ? g_object_ref (signals->nr5g) : NULL;

    detailed_signal_free (signals);
    return TRUE;
}

static void
hcsq_get_ready (MMBaseModem *_self,
                GAsyncResult *res,
                GTask *task)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    DetailedSignal *signals;
    GError *error = NULL;

    /* Don't care about the response; it will have been parsed by the HCSQ
     * unsolicited event handler and self->priv->detailed_signal will already
     * be updated.
     */
    if (!mm_base_modem_at_command_finish (_self, res, &error)) {
        mm_obj_dbg (self, "^HCSQ failed: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    signals = g_slice_new0 (DetailedSignal);
    signals->cdma = self->priv->detailed_signal.cdma ? g_object_ref (self->priv->detailed_signal.cdma) : NULL;
    signals->evdo = self->priv->detailed_signal.evdo ? g_object_ref (self->priv->detailed_signal.evdo) : NULL;
    signals->gsm = self->priv->detailed_signal.gsm ? g_object_ref (self->priv->detailed_signal.gsm) : NULL;
    signals->umts = self->priv->detailed_signal.umts ? g_object_ref (self->priv->detailed_signal.umts) : NULL;
    signals->lte = self->priv->detailed_signal.lte ? g_object_ref (self->priv->detailed_signal.lte) : NULL;

    g_task_return_pointer (task, signals, (GDestroyNotify)detailed_signal_free);
    g_object_unref (task);
}

static void
signal_load_values (MMIfaceModemSignal *_self,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (_self);
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    /* Clear any previous detailed signal values to get new ones */
    detailed_signal_clear (&self->priv->detailed_signal);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^HCSQ?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)hcsq_get_ready,
                              task);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
set_ignored_unsolicited_events_handlers (MMBroadbandModemHuawei *self)
{
    GList *ports, *l;

    ports = mm_broadband_modem_huawei_get_at_port_list (self);

    /* Enable/disable unsolicited events in given port */
    for (l = ports; l; l = g_list_next (l)) {
        MMPortSerialAt *port = MM_PORT_SERIAL_AT (l->data);

        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->boot_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->connect_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->csnr_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->cusatp_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->cusatend_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->dsdormant_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->simst_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->srvst_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->stin_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->pdpdeact_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->ndisend_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->rfswitch_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->position_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->posend_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->ecclist_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->ltersrp_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->cschannelinfo_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->ccallstate_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->eons_regex,
            NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->lwurc_regex,
            NULL, NULL, NULL);
    }

    g_list_free_full (ports, g_object_unref);
}

static void
gps_trace_received (MMPortSerialGps *port,
                    const gchar *trace,
                    MMIfaceModemLocation *self)
{
    mm_iface_modem_location_gps_update (self, trace);
}

static void
setup_ports (MMBroadbandModem *self)
{
    MMPortSerialGps *gps_data_port;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_huawei_parent_class)->setup_ports (self);

    /* Unsolicited messages to always ignore */
    set_ignored_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self));

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);
    set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);

    /* NMEA GPS monitoring */
    gps_data_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
    if (gps_data_port) {
        /* make sure GPS is stopped incase it was left enabled */
        mm_base_modem_at_command (MM_BASE_MODEM (self), "^WPEND", 3, FALSE, NULL, NULL);
        /* Add handler for the NMEA traces */
        mm_port_serial_gps_add_trace_handler (gps_data_port,
                                              (MMPortSerialGpsTraceFn)gps_trace_received,
                                              self, NULL);
    }
}

/*****************************************************************************/

MMBroadbandModemHuawei *
mm_broadband_modem_huawei_new (const gchar *device,
                               const gchar *physdev,
                               const gchar **drivers,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_HUAWEI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Generic bearer (TTY) or Huawei bearer (NET) supported */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_huawei_init (MMBroadbandModemHuawei *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_HUAWEI,
                                              MMBroadbandModemHuaweiPrivate);
    /* Prepare regular expressions to setup */
    self->priv->rssi_regex = g_regex_new ("\\r\\n\\^RSSI:\\s*(\\d+)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->rssilvl_regex = g_regex_new ("\\r\\n\\^RSSILVL:\\s*(\\d+)\\r+\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->hrssilvl_regex = g_regex_new ("\\r\\n\\^HRSSILVL:\\s*(\\d+)\\r+\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    /* 3GPP: <cr><lf>^MODE:5<cr><lf>
     * CDMA: <cr><lf>^MODE: 2<cr><cr><lf>
     */
    self->priv->mode_regex = g_regex_new ("\\r\\n\\^MODE:\\s*(\\d*),?(\\d*)\\r+\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->dsflowrpt_regex = g_regex_new ("\\r\\n\\^DSFLOWRPT:(.+)\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ndisstat_regex = g_regex_new ("\\r\\n(\\^NDISSTAT:.+)\\r+\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->orig_regex = g_regex_new ("\\r\\n\\^ORIG:\\s*(\\d+),\\s*(\\d+)\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->conf_regex = g_regex_new ("\\r\\n\\^CONF:\\s*(\\d+)\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->conn_regex = g_regex_new ("\\r\\n\\^CONN:\\s*(\\d+),\\s*(\\d+)\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->cend_regex = g_regex_new ("\\r\\n\\^CEND:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)(?:,\\s*(\\d*))?\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ddtmf_regex = g_regex_new ("\\r\\n\\^DDTMF:\\s*([0-9A-D\\*\\#])\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->boot_regex = g_regex_new ("\\r\\n\\^BOOT:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->connect_regex = g_regex_new ("\\r\\n\\^CONNECT .+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->csnr_regex = g_regex_new ("\\r\\n\\^CSNR:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->cusatp_regex = g_regex_new ("\\r\\n\\+CUSATP:.+\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->cusatend_regex = g_regex_new ("\\r\\n\\+CUSATEND\\r\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->dsdormant_regex = g_regex_new ("\\r\\n\\^DSDORMANT:.+\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->simst_regex = g_regex_new ("\\r\\n\\^SIMST:.+\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->srvst_regex = g_regex_new ("\\r\\n\\^SRVST:.+\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->stin_regex = g_regex_new ("\\r\\n\\^STIN:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->hcsq_regex = g_regex_new ("\\r\\n(\\^HCSQ:.+)\\r+\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->pdpdeact_regex = g_regex_new ("\\r\\n\\^PDPDEACT:.+\\r+\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ndisend_regex = g_regex_new ("\\r\\n\\^NDISEND:.+\\r+\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->rfswitch_regex = g_regex_new ("\\r\\n\\^RFSWITCH:.+\\r\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->position_regex = g_regex_new ("\\r\\n\\^POSITION:.+\\r\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->posend_regex = g_regex_new ("\\r\\n\\^POSEND:.+\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ecclist_regex = g_regex_new ("\\r\\n\\^ECCLIST:.+\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ltersrp_regex = g_regex_new ("\\r\\n\\^LTERSRP:.+\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->cschannelinfo_regex = g_regex_new ("\\r\\n\\^CSCHANNELINFO:.+\\r\\n",
                                                    G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ccallstate_regex = g_regex_new ("\\r\\n\\^CCALLSTATE:.+\\r\\n",
                                                G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->eons_regex = g_regex_new ("\\r\\n\\^EONS:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->lwurc_regex = g_regex_new ("\\r\\n\\^LWURC:.+\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->ndisdup_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->rfswitch_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->sysinfoex_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->syscfg_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->syscfgex_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->prefmode_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->nwtime_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->time_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->cvoice_support = FEATURE_SUPPORT_UNKNOWN;
}

static void
dispose (GObject *object)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (object);

    detailed_signal_clear (&self->priv->detailed_signal);

    G_OBJECT_CLASS (mm_broadband_modem_huawei_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (object);

    g_regex_unref (self->priv->rssi_regex);
    g_regex_unref (self->priv->rssilvl_regex);
    g_regex_unref (self->priv->hrssilvl_regex);
    g_regex_unref (self->priv->mode_regex);
    g_regex_unref (self->priv->dsflowrpt_regex);
    g_regex_unref (self->priv->ndisstat_regex);
    g_regex_unref (self->priv->orig_regex);
    g_regex_unref (self->priv->conf_regex);
    g_regex_unref (self->priv->conn_regex);
    g_regex_unref (self->priv->cend_regex);
    g_regex_unref (self->priv->ddtmf_regex);

    g_regex_unref (self->priv->boot_regex);
    g_regex_unref (self->priv->connect_regex);
    g_regex_unref (self->priv->csnr_regex);
    g_regex_unref (self->priv->cusatp_regex);
    g_regex_unref (self->priv->cusatend_regex);
    g_regex_unref (self->priv->dsdormant_regex);
    g_regex_unref (self->priv->simst_regex);
    g_regex_unref (self->priv->srvst_regex);
    g_regex_unref (self->priv->stin_regex);
    g_regex_unref (self->priv->hcsq_regex);
    g_regex_unref (self->priv->pdpdeact_regex);
    g_regex_unref (self->priv->ndisend_regex);
    g_regex_unref (self->priv->rfswitch_regex);
    g_regex_unref (self->priv->position_regex);
    g_regex_unref (self->priv->posend_regex);
    g_regex_unref (self->priv->ecclist_regex);
    g_regex_unref (self->priv->ltersrp_regex);
    g_regex_unref (self->priv->cschannelinfo_regex);
    g_regex_unref (self->priv->ccallstate_regex);
    g_regex_unref (self->priv->eons_regex);
    g_regex_unref (self->priv->lwurc_regex);

    if (self->priv->syscfg_supported_modes)
        g_array_unref (self->priv->syscfg_supported_modes);
    if (self->priv->syscfgex_supported_modes)
        g_array_unref (self->priv->syscfgex_supported_modes);
    if (self->priv->prefmode_supported_modes)
        g_array_unref (self->priv->prefmode_supported_modes);

    G_OBJECT_CLASS (mm_broadband_modem_huawei_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->reset = reset;
    iface->reset_finish = reset_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_current_bands = set_current_bands;
    iface->set_current_bands_finish = set_current_bands_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->load_supported_ip_families = load_supported_ip_families;
    iface->load_supported_ip_families_finish = load_supported_ip_families_finish;
    iface->load_signal_quality = modem_load_signal_quality;
    iface->load_signal_quality_finish = modem_load_signal_quality_finish;
    iface->create_bearer = huawei_modem_create_bearer;
    iface->create_bearer_finish = huawei_modem_create_bearer_finish;
    iface->load_power_state = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;
    iface->modem_power_up = huawei_modem_power_up;
    iface->modem_power_up_finish = huawei_modem_power_up_finish;
    iface->modem_power_down = huawei_modem_power_down;
    iface->modem_power_down_finish = huawei_modem_power_down_finish;
    iface->create_sim = huawei_modem_create_sim;
    iface->create_sim_finish = huawei_modem_create_sim_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gppInterface *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_disable_unsolicited_events_finish;
}

static void
iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssdInterface *iface)
{
    iface->encode = encode;
    iface->decode = decode;
}

static void
iface_modem_cdma_init (MMIfaceModemCdmaInterface *iface)
{
    iface_modem_cdma_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_cdma_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_cdma_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_cdma_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_cdma_setup_cleanup_unsolicited_events_finish;
    iface->setup_registration_checks = setup_registration_checks;
    iface->setup_registration_checks_finish = setup_registration_checks_finish;
    iface->get_detailed_registration_state = get_detailed_registration_state;
    iface->get_detailed_registration_state_finish = get_detailed_registration_state_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocationInterface *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities = location_load_capabilities;
    iface->load_capabilities_finish = location_load_capabilities_finish;
    iface->enable_location_gathering = enable_location_gathering;
    iface->enable_location_gathering_finish = enable_location_gathering_finish;
    iface->disable_location_gathering = disable_location_gathering;
    iface->disable_location_gathering_finish = disable_location_gathering_finish;
}

static void
iface_modem_time_init (MMIfaceModemTimeInterface *iface)
{
    iface->check_support = modem_time_check_support;
    iface->check_support_finish = modem_time_check_support_finish;
    iface->load_network_time = modem_time_load_network_time_or_zone;
    iface->load_network_time_finish = modem_time_load_network_time_finish;
    iface->load_network_timezone = modem_time_load_network_time_or_zone;
    iface->load_network_timezone_finish = modem_time_load_network_timezone_finish;
}

static void
iface_modem_voice_init (MMIfaceModemVoiceInterface *iface)
{
    iface_modem_voice_parent = g_type_interface_peek_parent (iface);

    iface->check_support = modem_voice_check_support;
    iface->check_support_finish = modem_voice_check_support_finish;
    iface->setup_unsolicited_events = modem_voice_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_voice_setup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_voice_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_voice_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_voice_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_voice_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_voice_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_voice_disable_unsolicited_events_finish;
    iface->setup_in_call_audio_channel = modem_voice_setup_in_call_audio_channel;
    iface->setup_in_call_audio_channel_finish = modem_voice_setup_in_call_audio_channel_finish;
    iface->cleanup_in_call_audio_channel = modem_voice_cleanup_in_call_audio_channel;
    iface->cleanup_in_call_audio_channel_finish = modem_voice_cleanup_in_call_audio_channel_finish;

    iface->create_call = create_call;
}

static void
iface_modem_signal_init (MMIfaceModemSignalInterface *iface)
{
    iface->check_support = signal_check_support;
    iface->check_support_finish = signal_check_support_finish;
    iface->load_values = signal_load_values;
    iface->load_values_finish = signal_load_values_finish;
}

static void
mm_broadband_modem_huawei_class_init (MMBroadbandModemHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemHuaweiPrivate));

    object_class->dispose = dispose;
    object_class->finalize = finalize;

    broadband_modem_class->setup_ports = setup_ports;
}
