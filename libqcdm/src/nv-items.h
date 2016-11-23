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

#ifndef LIBQCDM_NV_ITEMS_H
#define LIBQCDM_NV_ITEMS_H

#include <sys/types.h>

/* NV read/write status codes */
typedef enum {
    DIAG_NV_STATUS_OK = 0,
    DIAG_NV_STATUS_BUSY = 1,
    DIAG_NV_STATUS_BAD_COMMAND = 2,
    DIAG_NV_STATUS_MEMORY_FULL = 3,
    DIAG_NV_STATUS_FAILED = 4,
    DIAG_NV_STATUS_INACTIVE = 5,          /* NV location not active */
    DIAG_NV_STATUS_BAD_PARAMETER = 6,
    DIAG_NV_STATUS_READ_ONLY = 7,         /* NV location is read-only */
} DMNVStatus;

enum {
    DIAG_NV_MODE_PREF    = 10,   /* Mode preference: 1x, HDR, auto */
    DIAG_NV_DIR_NUMBER   = 178,  /* Mobile Directory Number (MDN) */
    DIAG_NV_ROAM_PREF    = 442,  /* Roaming preference */
    DIAG_NV_HYBRID_PREF  = 562,  /* Hybrid 1x + HDR preference */
    DIAG_NV_IPV6_ENABLED = 1896, /* Enable IPv6 */
    DIAG_NV_HDR_REV_PREF = 4964, /* HDR mode preference(?): rev0, revA, eHRPD */
};


/* Mode preference values */
enum {
    DIAG_NV_MODE_PREF_DIGITAL         = 0x00,
    DIAG_NV_MODE_PREF_DIGITAL_ONLY    = 0x01,
    DIAG_NV_MODE_PREF_ANALOG          = 0x02,
    DIAG_NV_MODE_PREF_ANALOG_ONLY     = 0x03,
    DIAG_NV_MODE_PREF_AUTO            = 0x04,
    DIAG_NV_MODE_PREF_E911            = 0x05,
    DIAG_NV_MODE_PREF_HOME_ONLY       = 0x06,
    DIAG_NV_MODE_PREF_1X_ONLY         = 0x09,
    DIAG_NV_MODE_PREF_HDR_ONLY        = 0x0A,
    DIAG_NV_MODE_PREF_GPRS_ONLY       = 0x0D,
    DIAG_NV_MODE_PREF_UMTS_ONLY       = 0x0E,
    DIAG_NV_MODE_PREF_GSM_UMTS_ONLY   = 0x11,
    DIAG_NV_MODE_PREF_1X_HDR_ONLY     = 0x13,
    DIAG_NV_MODE_PREF_LTE_ONLY        = 0x1E,
    DIAG_NV_MODE_PREF_GSM_UMTS_LTE_ONLY = 0x1F,
    DIAG_NV_MODE_PREF_1X_HDR_LTE_ONLY = 0x24,
};

/* DIAG_NV_MODE_PREF */
struct DMNVItemModePref {
    uint8_t profile;
    uint8_t mode_pref;
} __attribute__ ((packed));
typedef struct DMNVItemModePref DMNVItemModePref;

/* DIAG_NV_DIR_NUMBER */
struct DMNVItemMdn {
  uint8_t profile;
  uint8_t mdn[10];
} __attribute__ ((packed));
typedef struct DMNVItemMdn DMNVItemMdn;

/* Roam preference values */
enum {
    DIAG_NV_ROAM_PREF_HOME_ONLY = 0x01,
    DIAG_NV_ROAM_PREF_ROAM_ONLY = 0x06,
    DIAG_NV_ROAM_PREF_AUTO      = 0xFF,
};

/* DIAG_NV_ROAM_PREF */
struct DMNVItemRoamPref {
    uint8_t profile;
    uint8_t roam_pref;
} __attribute__ ((packed));
typedef struct DMNVItemRoamPref DMNVItemRoamPref;

/* HDR Revision preference values (?) */
enum {
    DIAG_NV_HDR_REV_PREF_0     = 0x00,
    DIAG_NV_HDR_REV_PREF_A     = 0x01,
    DIAG_NV_HDR_REV_PREF_EHRPD = 0x04,
};

/* DIAG_NV_HDR_REV_PREF */
struct DMNVItemHdrRevPref {
    uint8_t rev_pref;
} __attribute__ ((packed));
typedef struct DMNVItemHdrRevPref DMNVItemHdrRevPref;

/* Hybrid pref */
enum {
    DIAG_NV_HYBRID_PREF_OFF   = 0x00,
    DIAG_NV_HYBRID_PREF_ON    = 0x01,
};

/* DIAG_NV_HYBRID_PREF */
struct DMNVItemHybridPref {
    uint8_t hybrid_pref;
} __attribute__ ((packed));
typedef struct DMNVItemHybridPref DMNVItemHybridPref;

/* IPv6 enable */
enum {
    DIAG_NV_IPV6_ENABLED_OFF   = 0x00,
    DIAG_NV_IPV6_ENABLED_ON    = 0x01,
};

/* DIAG_NV_IPV6_ENABLED */
struct DMNVItemIPv6Enabled {
    uint8_t enabled;
} __attribute__ ((packed));
typedef struct DMNVItemIPv6Enabled DMNVItemIPv6Enabled;

#endif  /* LIBQCDM_NV_ITEMS_H */
