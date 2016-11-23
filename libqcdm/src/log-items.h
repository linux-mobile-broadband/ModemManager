/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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

#ifndef LIBQCDM_LOG_ITEMS_H
#define LIBQCDM_LOG_ITEMS_H

#include <stdint.h>

enum {
    /* CDMA and EVDO items */
    DM_LOG_ITEM_CDMA_ACCESS_CHANNEL_MSG         = 0x1004,
    DM_LOG_ITEM_CDMA_REV_CHANNEL_TRAFFIC_MSG    = 0x1005,
    DM_LOG_ITEM_CDMA_SYNC_CHANNEL_MSG           = 0x1006,
    DM_LOG_ITEM_CDMA_PAGING_CHANNEL_MSG         = 0x1007,
    DM_LOG_ITEM_CDMA_FWD_CHANNEL_TRAFFIC_MSG    = 0x1008,
    DM_LOG_ITEM_CDMA_FWD_LINK_VOCODER_PACKET    = 0x1009,
    DM_LOG_ITEM_CDMA_REV_LINK_VOCODER_PACKET    = 0x100A,
    DM_LOG_ITEM_CDMA_MARKOV_STATS               = 0x100E,
    DM_LOG_ITEM_CDMA_REVERSE_POWER_CONTROL      = 0x102C,
    DM_LOG_ITEM_CDMA_SERVICE_CONFIG             = 0x102E,
    DM_LOG_ITEM_EVDO_HANDOFF_STATE              = 0x105E,
    DM_LOG_ITEM_EVDO_ACTIVE_PILOT_SET           = 0x105F,
    DM_LOG_ITEM_EVDO_REV_LINK_PACKET_SUMMARY    = 0x1060,
    DM_LOG_ITEM_EVDO_REV_TRAFFIC_RATE_COUNT     = 0x1062,
    DM_LOG_ITEM_EVDO_REV_POWER_CONTROL          = 0x1063,
    DM_LOG_ITEM_EVDO_ARQ_EFFECTIVE_RECEIVE_RATE = 0x1066,
    DM_LOG_ITEM_EVDO_AIR_LINK_SUMMARY           = 0x1068,
    DM_LOG_ITEM_EVDO_POWER                      = 0x1069,
    DM_LOG_ITEM_EVDO_FWD_LINK_PACKET_SNAPSHOT   = 0x106A,
    DM_LOG_ITEM_EVDO_ACCESS_ATTEMPT             = 0x106C,
    DM_LOG_ITEM_EVDO_REV_ACTIVITY_BITS_BUFFER   = 0x106D,
    DM_LOG_ITEM_EVDO_PILOT_SETS                 = 0x107A,
    DM_LOG_ITEM_EVDO_STATE_INFO                 = 0x107E,
    DM_LOG_ITEM_EVDO_SECTOR_INFO                = 0x1080,
    DM_LOG_ITEM_EVDO_PILOT_SETS_V2              = 0x108B,

    /* WCDMA items */
    DM_LOG_ITEM_WCDMA_TA_FINGER_INFO       = 0x4003,
    DM_LOG_ITEM_WCDMA_AGC_INFO             = 0x4105,
    DM_LOG_ITEM_WCDMA_RRC_STATE            = 0x4125,
    DM_LOG_ITEM_WCDMA_CELL_ID              = 0x4127,

    /* GSM items */
    DM_LOG_ITEM_GSM_BURST_METRICS          = 0x506c,
    DM_LOG_ITEM_GSM_BCCH_MESSAGE           = 0x5134,
};


/* DM_LOG_ITEM_CDMA_PAGING_CHANNEL_MSG */
struct DMLogItemPagingChannelMsg {
    uint8_t msg_len;  /* size of entire struct including this field */
    uint8_t msg_type; /* MSG_TYPE as in 3GPP2 C.S0004 Table 3.1.2.3.1.1.2 */
    uint8_t data[0];  /* Packed message as in 3GPP2 C.S0005 3.7.2.3.2.x */
} __attribute ((packed));
typedef struct DMLogItemPagingChannelMsg DMLogItemPagingChannelMsg;


/* DM_LOG_ITEM_CDMA_REVERSE_POWER_CONTROL */
struct DMLogItemRPCItem {
    uint8_t channel_set_mask;
    uint16_t frame_count;
    uint8_t len_per_frame;
    uint16_t dec_history;
    uint8_t rx_agc_vals;
    uint8_t tx_power_vals;
    uint8_t tx_gain_adjust;
} __attribute__ ((packed));
typedef struct DMLogItemRPCItem DMLogItemRPCItem;

struct DMLogItemCdmaReversePowerControl {
    uint8_t frame_offset;
    uint8_t band_class;
    uint16_t rev_chan_rc;
    uint8_t pilot_gating_rate;
    uint8_t step_size;
    uint8_t num_records;
    DMLogItemRPCItem records[];
} __attribute__ ((packed));
typedef struct DMLogItemCdmaReversePowerControl DMLogItemCdmaReversePowerControl;

/* DM_LOG_ITEM_EVDO_PILOT_SETS_V2 */
struct DMLogItemEvdoPilotSetsV2Pilot {
    uint16_t pilot_pn;
    /* HDR pilot energy doesn't appear to be in the same units as 1x pilot
     * energy (eg, -0.5 dBm increments).  Instead it appears roughly correlated
     * to RSSI dBm by using this formula empirically derived from simultaneous
     * AT!RSSI and HDR Pilot Sets V2 results from a Sierra modem:
     *
     * RSSI dBm = -110 + (MAX(pilot_energy - 50, 0) / 14)
     */
    uint16_t pilot_energy;
    union {
        struct {
            uint16_t mac_index;
            uint8_t unknown1;
            uint8_t unknown2;
            uint16_t window_center;
        } Active;
        struct {
            uint16_t channel_number;
            uint8_t unknown1;
            uint8_t unknown2;
            uint16_t window_center;
        } Candidate;
        struct {
            uint16_t channel_number;
            uint16_t window_center;
            uint8_t unknown1; // Offset?
            uint8_t unknown2; // Age?
        } Remaining;
    };
} __attribute__ ((packed));
typedef struct DMLogItemEvdoPilotSetsV2Pilot DMLogItemEvdoPilotSetsV2Pilot;

/* DM_LOG_ITEM_EVDO_PILOT_SETS_V2 */
struct DMLogItemEvdoPilotSetsV2 {
    uint8_t pn_offset;
    uint8_t active_count;
    uint8_t active_window;
    uint16_t active_channel;
    uint8_t unknown1;
    uint8_t candidate_count;
    uint8_t candidate_window;
    uint8_t remaining_count;
    uint8_t remaining_window;
    uint8_t unknown2;

    DMLogItemEvdoPilotSetsV2Pilot sets[];
} __attribute__ ((packed));
typedef struct DMLogItemEvdoPilotSetsV2 DMLogItemEvdoPilotSetsV2;

/* DM_LOG_ITEM_WCDMA_TA_FINGER_INFO */
struct DMLogItemWcdmaTaFingerInfo {
    int32_t tx_pos;
    int16_t coherent_interval_len;
    uint8_t non_coherent_interval_len;
    uint8_t num_paths;
    uint32_t path_enr;
    int32_t pn_pos_path;
    int16_t pri_cpich_psc;
    uint8_t unknown1;
    uint8_t sec_cpich_ssc;
    uint8_t finger_channel_code_index;
    uint8_t finger_index;
} __attribute__ ((packed));
typedef struct DMLogItemWcdmaTaFingerInfo DMLogItemWcdmaTaFingerInfo;


/* DM_LOG_ITEM_WCDMA_AGC_INFO */
struct DMLogItemWcdmaAgcInfo {
    uint8_t num_samples;
    uint16_t rx_agc;
    uint16_t tx_agc;
    uint16_t rx_agc_adj_pdm;
    uint16_t tx_agc_adj_pdm;
    uint16_t max_tx;
    /* Bit 4 means tx_agc is valid */
    uint8_t agc_info;
} __attribute__ ((packed));
typedef struct DMLogItemWcdmaAgcInfo DMLogItemWcdmaAgcInfo;


/* DM_LOG_ITEM_WCDMA_RRC_STATE */
enum {
    DM_LOG_ITEM_WCDMA_RRC_STATE_DISCONNECTED = 0,
    DM_LOG_ITEM_WCDMA_RRC_STATE_CONNECTING   = 1,
    DM_LOG_ITEM_WCDMA_RRC_STATE_CELL_FACH    = 2,
    DM_LOG_ITEM_WCDMA_RRC_STATE_CELL_DCH     = 3,
    DM_LOG_ITEM_WCDMA_RRC_STATE_CELL_PCH     = 4,
    DM_LOG_ITEM_WCDMA_RRC_STATE_URA_PCH      = 5,
};

struct DMLogItemWcdmaRrcState {
    uint8_t rrc_state;
} __attribute__ ((packed));
typedef struct DMLogItemWcdmaRrcState DMLogItemWcdmaRrcState;


/* DM_LOG_ITEM_WCDMA_CELL_ID */
struct DMLogItemWcdmaCellId {
    uint8_t unknown1[8];
    uint32_t cellid;
    uint8_t unknown2[4];
} __attribute__ ((packed));
typedef struct DMLogItemWcdmaCellId DMLogItemWcdmaCellId;


/* DM_LOG_ITEM_GSM_BURST_METRICS */
struct DMLogItemGsmBurstMetric {
    uint32_t fn;
    uint16_t arfcn;
    uint32_t rssi;
    uint16_t power;
    uint16_t dc_offset_i;
    uint16_t dc_offset_q;
    uint16_t freq_offset;
    uint16_t timing_offset;
    uint16_t snr;
    uint8_t gain_state;
} __attribute__ ((packed));
typedef struct DMLogItemGsmBurstMetric DMLogItemGsmBurstMetric;

struct DMLogItemGsmBurstMetrics {
    uint8_t channel;
    DMLogItemGsmBurstMetric metrics[4];
} __attribute__ ((packed));
typedef struct DMLogItemGsmBurstMetrics DMLogItemGsmBurstMetrics;


/* DM_LOG_ITEM_GSM_BCCH_MESSAGE */
enum {
    DM_LOG_ITEM_GSM_BCCH_BAND_UNKNOWN  = 0,
    DM_LOG_ITEM_GSM_BCCH_BAND_GSM_900  = 8,
    DM_LOG_ITEM_GSM_BCCH_BAND_DCS_1800 = 9,
    DM_LOG_ITEM_GSM_BCCH_BAND_PCS_1900 = 10,
    DM_LOG_ITEM_GSM_BCCH_BAND_GSM_850  = 11,
    DM_LOG_ITEM_GSM_BCCH_BAND_GSM_450  = 12,
};

struct DMLogItemGsmBcchMessage {
    /* Band is top 4 bits; lower 12 is ARFCN */
    uint16_t bcch_arfcn;
    uint16_t bsic;
    uint16_t cell_id;
    uint8_t lai[5];
    uint8_t cell_selection_prio;
    uint8_t ncc_permitted;
} __attribute__ ((packed));
typedef struct DMLogItemGsmBcchMessage DMLogItemGsmBcchMessage;

#endif  /* LIBQCDM_LOG_ITEMS_H */
