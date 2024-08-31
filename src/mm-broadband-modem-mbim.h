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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BROADBAND_MODEM_MBIM_H
#define MM_BROADBAND_MODEM_MBIM_H

#include "mm-broadband-modem.h"
#include "mm-port-mbim.h"

#define MM_TYPE_BROADBAND_MODEM_MBIM            (mm_broadband_modem_mbim_get_type ())
#define MM_BROADBAND_MODEM_MBIM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_MBIM, MMBroadbandModemMbim))
#define MM_BROADBAND_MODEM_MBIM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_MBIM, MMBroadbandModemMbimClass))
#define MM_IS_BROADBAND_MODEM_MBIM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_MBIM))
#define MM_IS_BROADBAND_MODEM_MBIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_MBIM))
#define MM_BROADBAND_MODEM_MBIM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_MBIM, MMBroadbandModemMbimClass))

typedef struct _MMBroadbandModemMbim MMBroadbandModemMbim;
typedef struct _MMBroadbandModemMbimClass MMBroadbandModemMbimClass;
typedef struct _MMBroadbandModemMbimPrivate MMBroadbandModemMbimPrivate;

#define MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED "broadband-modem-mbim-qmi-unsupported"
#define MM_BROADBAND_MODEM_MBIM_INTEL_FIRMWARE_UPDATE_UNSUPPORTED "broadband-modem-mbim-intel-firmware-update-unsupported"

/* Flags to modify the behavior of the generic initial EPS bearer settings
 * operation, given that different modules may end up having different needs at
 * different times.
 *
 * The "home" section is always included and always updated.
 *
 * The "update partner" and "update non partner" flags control which settings are
 * used for the given section. If the flag is given, the section receives the same
 * settings as "home", otherwise the section receives the same settings it already
 * had before.
 *
 * The "skip" partner and "skip non partner" flags control whether the given section
 * is included in the list of configurations given to the device. If any of the
 * section is flagged to be skipped, the "update" flag for that section is irrelevant.
 *
 * By default for now all 3 sections are included, but partner and non partner are not
 * updated.
 */
typedef enum {
    MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_NONE,
    MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_UPDATE_HOME        = 1 << 0,
    MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_UPDATE_PARTNER     = 1 << 1,
    MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_UPDATE_NON_PARTNER = 1 << 2,
    MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_SKIP_PARTNER       = 1 << 3,
    MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_SKIP_NON_PARTNER   = 1 << 4,
} MMBroadbandModemMbimSetInitialEpsBearerSettingsFlag;

/* By default: provide and update all home/partner/non-partner */
#define MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_DEFAULT       \
    (MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_UPDATE_HOME |    \
     MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_UPDATE_PARTNER | \
     MM_BROADBAND_MODEM_MBIM_SET_INITIAL_EPS_BEARER_SETTINGS_FLAG_UPDATE_NON_PARTNER)

struct _MMBroadbandModemMbim {
    MMBroadbandModem parent;
    MMBroadbandModemMbimPrivate *priv;
};

struct _MMBroadbandModemMbimClass{
    MMBroadbandModemClass parent;

    MMPortMbim * (* peek_port_mbim_for_data)                   (MMBroadbandModemMbim  *self,
                                                                MMPort                *data,
                                                                GError               **error);
    guint32      (* normalize_nw_error)                        (MMBroadbandModemMbim *self,
                                                                guint32               nw_error);
    MMBroadbandModemMbimSetInitialEpsBearerSettingsFlag
                 (* load_set_initial_eps_bearer_settings_mask) (MMBroadbandModemMbim *self);
};

GType mm_broadband_modem_mbim_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBroadbandModemMbim, g_object_unref)

MMBroadbandModemMbim *mm_broadband_modem_mbim_new (const gchar  *device,
                                                   const gchar  *physdev,
                                                   const gchar **drivers,
                                                   const gchar  *plugin,
                                                   guint16       vendor_id,
                                                   guint16       product_id);

MMPortMbim *mm_broadband_modem_mbim_peek_port_mbim          (MMBroadbandModemMbim  *self);
MMPortMbim *mm_broadband_modem_mbim_peek_port_mbim_for_data (MMBroadbandModemMbim  *self,
                                                             MMPort                *data,
                                                             GError               **error);
MMPortMbim *mm_broadband_modem_mbim_get_port_mbim           (MMBroadbandModemMbim  *self);
MMPortMbim *mm_broadband_modem_mbim_get_port_mbim_for_data  (MMBroadbandModemMbim  *self,
                                                             MMPort                *data,
                                                             GError               **error);

guint32    mm_broadband_modem_mbim_normalize_nw_error       (MMBroadbandModemMbim *self,
                                                             guint32               nw_error);

void mm_broadband_modem_mbim_set_unlock_retries (MMBroadbandModemMbim *self,
                                                 MMModemLock           lock_type,
                                                 guint32               remaining_attempts);

void mm_broadband_modem_mbim_get_speeds (MMBroadbandModemMbim *self,
                                         guint64              *uplink_speed,
                                         guint64              *downlink_speed);

gboolean mm_broadband_modem_mbim_is_context_type_ext_supported (MMBroadbandModemMbim *self);

#endif /* MM_BROADBAND_MODEM_MBIM_H */
