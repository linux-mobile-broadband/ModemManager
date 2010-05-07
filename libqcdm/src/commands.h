/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBQCDM_COMMANDS_H
#define LIBQCDM_COMMANDS_H

#include <glib.h>

#include "result.h"

/**********************************************************************/

/* Generic enums */

enum {
    QCDM_CDMA_PREV_UNKNOWN       = 0,
    QCDM_CDMA_PREV_IS_95         = 1, /* and J_STD008 */
    QCDM_CDMA_PREV_IS_95A        = 2,
    QCDM_CDMA_PREV_IS_95A_TSB74  = 3,
    QCDM_CDMA_PREV_IS_95B_PHASE1 = 4,
    QCDM_CDMA_PREV_IS_95B_PHASE2 = 5,
    QCDM_CDMA_PREV_IS2000_REL0   = 6,
    QCDM_CDMA_PREV_IS2000_RELA   = 7
};

enum {
    QCDM_CDMA_BAND_CLASS_UNKNOWN          = 0,
    QCDM_CDMA_BAND_CLASS_0_CELLULAR_800   = 1,  /* US cellular 850MHz */
    QCDM_CDMA_BAND_CLASS_1_PCS            = 2,  /* US PCS 1900MHz */
    QCDM_CDMA_BAND_CLASS_2_TACS           = 3,
    QCDM_CDMA_BAND_CLASS_3_JTACS          = 4,  /* Japanese TACS */
    QCDM_CDMA_BAND_CLASS_4_KOREAN_PCS     = 5,
    QCDM_CDMA_BAND_CLASS_5_NMT450         = 6,
    QCDM_CDMA_BAND_CLASS_6_IMT2000        = 7,  /* 2100HMz */
    QCDM_CDMA_BAND_CLASS_7_CELLULAR_700   = 8,
    QCDM_CDMA_BAND_CLASS_8_1800           = 9,
    QCDM_CDMA_BAND_CLASS_9_900            = 10,
    QCDM_CDMA_BAND_CLASS_10_SECONDARY_800 = 11,
    QCDM_CDMA_BAND_CLASS_11_PAMR_400      = 12,
    QCDM_CDMA_BAND_CLASS_12_PAMR_800      = 13
};

enum {
    QCDM_HDR_REV_UNKNOWN = 0x00,
    QCDM_HDR_REV_0 = 0x01,
    QCDM_HDR_REV_A = 0x02
};

/**********************************************************************/

#define QCDM_CMD_VERSION_INFO_ITEM_COMP_DATE "comp-date"
#define QCDM_CMD_VERSION_INFO_ITEM_COMP_TIME "comp-time"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_DATE "release-date"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_TIME "release-time"
#define QCDM_CMD_VERSION_INFO_ITEM_MODEL "model"

gsize       qcdm_cmd_version_info_new    (char *buf,
                                          gsize len,
                                          GError **error);

QCDMResult *qcdm_cmd_version_info_result (const char *buf,
                                          gsize len,
                                          GError **error);

/**********************************************************************/

#define QCDM_CMD_ESN_ITEM_ESN "esn"

gsize       qcdm_cmd_esn_new    (char *buf,
                                 gsize len,
                                 GError **error);

QCDMResult *qcdm_cmd_esn_result (const char *buf,
                                 gsize len,
                                 GError **error);

/**********************************************************************/

/* Values for QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE */
enum {
    QCDM_CMD_CDMA_STATUS_RF_MODE_ANALOG = 0,
    QCDM_CMD_CDMA_STATUS_RF_MODE_CDMA_CELLULAR = 1,
    QCDM_CMD_CDMA_STATUS_RF_MODE_CDMA_PCS = 2,
    QCDM_CMD_CDMA_STATUS_RF_MODE_SLEEP = 3,
    QCDM_CMD_CDMA_STATUS_RF_MODE_GPS = 4,
    QCDM_CMD_CDMA_STATUS_RF_MODE_HDR = 5,
};

/* Values for QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE */
enum {
    QCDM_CMD_CDMA_STATUS_RX_STATE_ENTERING_CDMA = 0,
    QCDM_CMD_CDMA_STATUS_RX_STATE_SYNC_CHANNEL = 1,
    QCDM_CMD_CDMA_STATUS_RX_STATE_PAGING_CHANNEL = 2,
    QCDM_CMD_CDMA_STATUS_RX_STATE_TRAFFIC_CHANNEL_INIT = 3,
    QCDM_CMD_CDMA_STATUS_RX_STATE_TRAFFIC_CHANNEL = 4,
    QCDM_CMD_CDMA_STATUS_RX_STATE_EXITING_CDMA = 5,
};

#define QCDM_CMD_CDMA_STATUS_ITEM_ESN             "esn"
#define QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE         "rf-mode"
#define QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE        "rx-state"
#define QCDM_CMD_CDMA_STATUS_ITEM_ENTRY_REASON    "entry-reason"
#define QCDM_CMD_CDMA_STATUS_ITEM_CURRENT_CHANNEL "current-channel"
#define QCDM_CMD_CDMA_STATUS_ITEM_CODE_CHANNEL    "code-channel"
#define QCDM_CMD_CDMA_STATUS_ITEM_PILOT_BASE      "pilot-base"
#define QCDM_CMD_CDMA_STATUS_ITEM_SID             "sid"
#define QCDM_CMD_CDMA_STATUS_ITEM_NID             "nid"

gsize       qcdm_cmd_cdma_status_new    (char *buf,
                                         gsize len,
                                         GError **error);

QCDMResult *qcdm_cmd_cdma_status_result (const char *buf,
                                         gsize len,
                                         GError **error);

/**********************************************************************/

/* NOTE: this command does not appear to be implemented in recent
 * devices and probably returns (QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_COMMAND).
 */

#define QCDM_CMD_SW_VERSION_ITEM_VERSION   "version"
#define QCDM_CMD_SW_VERSION_ITEM_COMP_DATE "comp-date"
#define QCDM_CMD_SW_VERSION_ITEM_COMP_TIME "comp-time"

gsize       qcdm_cmd_sw_version_new    (char *buf,
                                        gsize len,
                                        GError **error);

QCDMResult *qcdm_cmd_sw_version_result (const char *buf,
                                        gsize len,
                                        GError **error);

/**********************************************************************/

enum {
    QCDM_CMD_PILOT_SETS_TYPE_UNKNOWN = 0,
    QCDM_CMD_PILOT_SETS_TYPE_ACTIVE = 1,
    QCDM_CMD_PILOT_SETS_TYPE_CANDIDATE = 2,
    QCDM_CMD_PILOT_SETS_TYPE_NEIGHBOR = 3,
};

gsize       qcdm_cmd_pilot_sets_new    (char *buf,
                                        gsize len,
                                        GError **error);

QCDMResult *qcdm_cmd_pilot_sets_result (const char *buf,
                                        gsize len,
                                        GError **error);

gboolean    qcdm_cmd_pilot_sets_result_get_num   (QCDMResult *result,
                                                  guint32 set_type,
                                                  guint32 *out_num);

gboolean    qcdm_cmd_pilot_sets_result_get_pilot (QCDMResult *result,
                                                  guint32 set_type,
                                                  guint32 num,
                                                  guint32 *out_pn_offset,
                                                  guint32 *out_ecio,
                                                  float *out_db);

/**********************************************************************/

#define QCDM_CMD_NV_GET_MDN_ITEM_PROFILE "profile"
#define QCDM_CMD_NV_GET_MDN_ITEM_MDN "mdn"

gsize       qcdm_cmd_nv_get_mdn_new    (char *buf,
                                        gsize len,
                                        guint8 profile,
                                        GError **error);

QCDMResult *qcdm_cmd_nv_get_mdn_result (const char *buf,
                                        gsize len,
                                        GError **error);

/**********************************************************************/

/* Values for QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF */
enum {
    QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_HOME_ONLY = 0x01,
    QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_ROAM_ONLY = 0x06,
    QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_AUTO = 0xFF,
};

#define QCDM_CMD_NV_GET_ROAM_PREF_ITEM_PROFILE   "profile"
#define QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF "roam-pref"

gsize       qcdm_cmd_nv_get_roam_pref_new    (char *buf,
                                              gsize len,
                                              guint8 profile,
                                              GError **error);

QCDMResult *qcdm_cmd_nv_get_roam_pref_result (const char *buf,
                                              gsize len,
                                              GError **error);

gsize       qcdm_cmd_nv_set_roam_pref_new    (char *buf,
                                              gsize len,
                                              guint8 profile,
                                              guint8 roam_pref,
                                              GError **error);

QCDMResult *qcdm_cmd_nv_set_roam_pref_result (const char *buf,
                                              gsize len,
                                              GError **error);

/**********************************************************************/

/* Values for QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF */
enum {
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO = 0x04,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_ONLY = 0x09,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_HDR_ONLY = 0x0A,
};

#define QCDM_CMD_NV_GET_MODE_PREF_ITEM_PROFILE   "profile"
#define QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF "mode-pref"

gsize       qcdm_cmd_nv_get_mode_pref_new    (char *buf,
                                              gsize len,
                                              guint8 profile,
                                              GError **error);

QCDMResult *qcdm_cmd_nv_get_mode_pref_result (const char *buf,
                                              gsize len,
                                              GError **error);

gsize       qcdm_cmd_nv_set_mode_pref_new    (char *buf,
                                              gsize len,
                                              guint8 profile,
                                              guint8 mode_pref,
                                              GError **error);

QCDMResult *qcdm_cmd_nv_set_mode_pref_result (const char *buf,
                                              gsize len,
                                              GError **error);

/**********************************************************************/

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE */
enum {
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_ONLINE = 5
};

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE */
enum {
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_NO_SERVICE = 0,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_AMPS = 1,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_CDMA = 2,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GSM = 3,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_HDR = 4,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_WCDMA = 5,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GPS = 6
};

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF */
enum {
    QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_HOME_ONLY = 0x01,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_ROAM_ONLY = 0x06,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_AUTO = 0xFF,
};

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF */
enum {
    /* Note: not the same values as QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF has;
     * AUTO really is 0x02 here, not 0x04 like the NV item value for AUTO.
     */
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_DIGITAL_ONLY = 0x01,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_AUTO = 0x02,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_1X_ONLY = 0x09,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_HDR_ONLY = 0x0A,
};

#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_CALL_STATE             "call-state"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE         "operating-mode"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE            "system-mode"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF              "mode-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_BAND_PREF              "band-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF              "roam-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SERVICE_DOMAIN_PREF    "service-domain-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ACQ_ORDER_PREF         "acq-order-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_HYBRID_PREF            "hybrid-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_NETWORK_SELECTION_PREF "network-selection-pref"

gsize       qcdm_cmd_cm_subsys_state_info_new    (char *buf,
                                                  gsize len,
                                                  GError **error);

QCDMResult *qcdm_cmd_cm_subsys_state_info_result (const char *buf,
                                                  gsize len,
                                                  GError **error);

/**********************************************************************/

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_INACTIVE = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_ACQUISITION = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_SYNC = 2,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_IDLE = 3,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_ACCESS = 4,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_CONNECTED = 5
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSED = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_SETUP = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_AT_INIT = 2,  /* initiated by Access Terminal */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_AN_INIT = 3,  /* initiated by Access Node */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_OPEN = 4,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSING = 5
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE (TIA-856-A section 9.2.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INACTIVE = 0,  /* initial state */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INIT = 1,  /* terminal has yet to acquire network */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_IDLE = 2,  /* network acquired but no connection */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_CONNECTED = 3,  /* open connection to the network */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE (TIA-856-A section 9.3.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_INACTIVE = 0,  /* protocol waiting for ACTIVATE command */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_NET_DETERMINE = 1,  /* choosing a network to operate on */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_ACQUISITION = 2,  /* acquiring Forward Pilot Channel */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_SYNC = 3,  /* synchronizing to Control Channel */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE (TIA-856-A section 9.4.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_INACTIVE = 0,  /* protocol waiting for ACTIVATE command */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_SLEEP = 1,  /* sleeping */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_MONITOR = 2,  /* monitoring the Control Channel */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_SETUP = 3,  /* setting up a connection */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE (TIA-856-A section 9.6.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_INACTIVE = 0,  /* protocol waiting for ACTIVATE command */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_OPEN = 1,  /* connection is open */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_CLOSING = 2,  /* connection is closed */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE (TIA-856-A section 9.7.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_INACTIVE = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_IDLE = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_CONNECTED = 2,
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG (TIA-856-A section 9.9.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_INIT = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_INACTIVE = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_ACTIVE = 2,
};

#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE           "at-state"  /* State of Access Terminal */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE      "session-state"  /* Current session state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE         "almp-state"  /* Air Link Management Protocol (ALMP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE         "init-state"  /* Initialization State Protocol (ISP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE         "idle-state"  /* Idle State Protocol (IDP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE    "connected-state"  /* Connected State Protocol (CSP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE_STATE "route-update-state"  /* Route Update Protocol (RUP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG_STATE "overhead-msg-state"
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_HDR_HYBRID_MODE    "hdr-hybrid-mode"

gsize       qcdm_cmd_hdr_subsys_state_info_new    (char *buf,
                                                   gsize len,
                                                   GError **error);

QCDMResult *qcdm_cmd_hdr_subsys_state_info_result (const char *buf,
                                                   gsize len,
                                                   GError **error);

/**********************************************************************/

#define QCDM_CMD_ZTE_SUBSYS_STATUS_ITEM_SIGNAL_INDICATOR    "signal-indicator"

gsize       qcdm_cmd_zte_subsys_status_new    (char *buf,
                                               gsize len,
                                               GError **error);

QCDMResult *qcdm_cmd_zte_subsys_status_result (const char *buf,
                                               gsize len,
                                               GError **error);

/**********************************************************************/

#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_RSSI       "rssi"

/* One of QCDM_CDMA_PREV_* */
#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_PREV       "prev"

/* One of QCDM_CDMA_BAND_CLASS_* */
#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_BAND_CLASS "band-class"

#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_ERI        "eri"

/* One of QCDM_HDR_REV_* */
#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV    "hdr-rev"

enum {
    QCDM_NW_CHIPSET_UNKNOWN = 0,
    QCDM_NW_CHIPSET_6500 = 1,
    QCDM_NW_CHIPSET_6800 = 2,
};

gsize       qcdm_cmd_nw_subsys_modem_snapshot_cdma_new    (char *buf,
                                                           gsize len,
                                                           guint8 chipset,
                                                           GError **error);

QCDMResult *qcdm_cmd_nw_subsys_modem_snapshot_cdma_result (const char *buf,
                                                           gsize len,
                                                           GError **error);

/**********************************************************************/

#endif  /* LIBQCDM_COMMANDS_H */
