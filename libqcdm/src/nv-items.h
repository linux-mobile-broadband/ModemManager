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

enum {
    DIAG_NV_MODE_PREF  = 10,   /* Mode preference: 1x, HDR, auto */
    DIAG_NV_DIR_NUMBER = 178,  /* Mobile Directory Number (MDN) */
    DIAG_NV_ROAM_PREF  = 442,  /* Roaming preference */
};


/* Mode preference values */
enum {
    DIAG_NV_MODE_PREF_AUTO     = 0x04,
    DIAG_NV_MODE_PREF_1X_ONLY  = 0x09,
    DIAG_NV_MODE_PREF_HDR_ONLY = 0x0A,
};

/* DIAG_NV_MODE_PREF */
struct DMNVItemModePref {
    guint8 profile;
    guint8 mode_pref;
} __attribute__ ((packed));
typedef struct DMNVItemModePref DMNVItemModePref;

/* DIAG_NV_DIR_NUMBER */
struct DMNVItemMdn {
  guint8 profile; 
  guint8 mdn[10]; 
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
    guint8 profile;
    guint8 roam_pref;
} __attribute__ ((packed));
typedef struct DMNVItemRoamPref DMNVItemRoamPref;

#endif  /* LIBQCDM_NV_ITEMS_H */

