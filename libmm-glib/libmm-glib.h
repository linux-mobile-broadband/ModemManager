/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#ifndef _LIBMM_GLIB_H_
#define _LIBMM_GLIB_H_

#define __LIBMM_GLIB_H_INSIDE__

/* ModemManager generic headers */
#include <ModemManager.h>

/* libmm-glib headers */

#if !defined (_LIBMM_INSIDE_MM)
/* This headers are not exported within ModemManager */
# include <mm-manager.h>
# include <mm-object.h>
# include <mm-sim.h>
# include <mm-sms.h>
# include <mm-call.h>
# include <mm-bearer.h>
# include <mm-modem.h>
# include <mm-modem-3gpp.h>
# include <mm-modem-3gpp-profile-manager.h>
# include <mm-modem-3gpp-ussd.h>
# include <mm-modem-cdma.h>
# include <mm-modem-simple.h>
# include <mm-modem-location.h>
# include <mm-modem-messaging.h>
# include <mm-modem-voice.h>
# include <mm-modem-time.h>
# include <mm-modem-firmware.h>
# include <mm-modem-signal.h>
# include <mm-modem-oma.h>
#endif

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)
/* This one is not even installed */
# include <mm-common-helpers.h>
#endif

#include <mm-helper-types.h>
#include <mm-simple-status.h>
#include <mm-simple-connect-properties.h>
#include <mm-sms-properties.h>
#include <mm-call-properties.h>
#include <mm-bearer-properties.h>
#include <mm-bearer-ip-config.h>
#include <mm-bearer-stats.h>
#include <mm-location-common.h>
#include <mm-location-3gpp.h>
#include <mm-location-gps-raw.h>
#include <mm-location-gps-nmea.h>
#include <mm-location-cdma-bs.h>
#include <mm-unlock-retries.h>
#include <mm-network-timezone.h>
#include <mm-firmware-properties.h>
#include <mm-firmware-update-settings.h>
#include <mm-cdma-manual-activation-properties.h>
#include <mm-signal.h>
#include <mm-kernel-event-properties.h>
#include <mm-pco.h>
#include <mm-sim-preferred-network.h>
#include <mm-3gpp-profile.h>
#include <mm-compat.h>

/* generated */
#include <mm-errors-types.h>
#include <mm-enums-types.h>
#include <mm-gdbus-manager.h>
#include <mm-gdbus-modem.h>
#include <mm-gdbus-bearer.h>
#include <mm-gdbus-sim.h>
#include <mm-gdbus-sms.h>
#include <mm-gdbus-call.h>

#endif /* _LIBMM_GLIB_H_ */
