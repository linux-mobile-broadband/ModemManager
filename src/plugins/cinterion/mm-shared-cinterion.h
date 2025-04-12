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
 * Copyright (C) 2014 Ammonit Measurement GmbH
 * Copyright (C) 2014 - 2018 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2019 Purism SPC
 */

#ifndef MM_SHARED_CINTERION_H
#define MM_SHARED_CINTERION_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-voice.h"
#include "mm-iface-modem-time.h"

#define MM_TYPE_SHARED_CINTERION mm_shared_cinterion_get_type ()
G_DECLARE_INTERFACE (MMSharedCinterion, mm_shared_cinterion, MM, SHARED_CINTERION, MMIfaceModem)

struct _MMSharedCinterionInterface {
    GTypeInterface g_iface;

    /* Peek modem interface of the parent class of the object */
    MMIfaceModemInterface *  (* peek_parent_interface) (MMSharedCinterion *self);

    /* Peek firmware interface of the parent class of the object */
    MMIfaceModemFirmwareInterface *  (* peek_parent_firmware_interface) (MMSharedCinterion *self);

    /* Peek location interface of the parent class of the object */
    MMIfaceModemLocationInterface *  (* peek_parent_location_interface) (MMSharedCinterion *self);

    /* Peek voice interface of the parent class of the object */
    MMIfaceModemVoiceInterface *  (* peek_parent_voice_interface) (MMSharedCinterion *self);

    /* Peek time interface of the parent class of the object */
    MMIfaceModemTimeInterface *  (* peek_parent_time_interface) (MMSharedCinterion *self);
};

/*****************************************************************************/
/* Modem interface */

void     mm_shared_cinterion_modem_reset        (MMIfaceModem        *self,
                                                 GAsyncReadyCallback  callback,
                                                 gpointer             user_data);
gboolean mm_shared_cinterion_modem_reset_finish (MMIfaceModem        *self,
                                                 GAsyncResult        *res,
                                                 GError             **error);

/*****************************************************************************/
/* Firmware interface */

void mm_shared_cinterion_firmware_load_update_settings (MMIfaceModemFirmware *self,
                                                        GAsyncReadyCallback   callback,
                                                        gpointer              user_data);

MMFirmwareUpdateSettings *mm_shared_cinterion_firmware_load_update_settings_finish (MMIfaceModemFirmware  *self,
                                                                                    GAsyncResult          *res,
                                                                                    GError               **error);

/*****************************************************************************/
/* Location interface */

void                  mm_shared_cinterion_location_load_capabilities        (MMIfaceModemLocation *self,
                                                                             GAsyncReadyCallback callback,
                                                                             gpointer user_data);
MMModemLocationSource mm_shared_cinterion_location_load_capabilities_finish (MMIfaceModemLocation *self,
                                                                             GAsyncResult *res,
                                                                             GError **error);

void                  mm_shared_cinterion_enable_location_gathering         (MMIfaceModemLocation *self,
                                                                             MMModemLocationSource source,
                                                                             GAsyncReadyCallback callback,
                                                                             gpointer user_data);
gboolean              mm_shared_cinterion_enable_location_gathering_finish  (MMIfaceModemLocation *self,
                                                                             GAsyncResult *res,
                                                                             GError **error);

void                  mm_shared_cinterion_disable_location_gathering        (MMIfaceModemLocation *self,
                                                                             MMModemLocationSource source,
                                                                             GAsyncReadyCallback callback,
                                                                             gpointer user_data);
gboolean              mm_shared_cinterion_disable_location_gathering_finish (MMIfaceModemLocation *self,
                                                                             GAsyncResult *res,
                                                                             GError **error);

/*****************************************************************************/
/* Voice interface */

MMBaseCall *mm_shared_cinterion_create_call (MMIfaceModemVoice *self,
                                             MMCallDirection    direction,
                                             const gchar       *number,
                                             const guint        dtmf_tone_duration);

void     mm_shared_cinterion_voice_check_support                     (MMIfaceModemVoice   *self,
                                                                      GAsyncReadyCallback  callback,
                                                                      gpointer             user_data);
gboolean mm_shared_cinterion_voice_check_support_finish              (MMIfaceModemVoice    *self,
                                                                      GAsyncResult         *res,
                                                                      GError              **error);

void     mm_shared_cinterion_voice_setup_unsolicited_events          (MMIfaceModemVoice    *self,
                                                                      GAsyncReadyCallback   callback,
                                                                      gpointer              user_data);
gboolean mm_shared_cinterion_voice_setup_unsolicited_events_finish   (MMIfaceModemVoice    *self,
                                                                      GAsyncResult         *res,
                                                                      GError              **error);

void     mm_shared_cinterion_voice_cleanup_unsolicited_events        (MMIfaceModemVoice    *self,
                                                                      GAsyncReadyCallback   callback,
                                                                      gpointer              user_data);
gboolean mm_shared_cinterion_voice_cleanup_unsolicited_events_finish (MMIfaceModemVoice    *self,
                                                                      GAsyncResult         *res,
                                                                      GError              **error);

void     mm_shared_cinterion_voice_enable_unsolicited_events         (MMIfaceModemVoice    *self,
                                                                      GAsyncReadyCallback   callback,
                                                                      gpointer              user_data);
gboolean mm_shared_cinterion_voice_enable_unsolicited_events_finish  (MMIfaceModemVoice    *self,
                                                                      GAsyncResult         *res,
                                                                      GError              **error);

void     mm_shared_cinterion_voice_disable_unsolicited_events        (MMIfaceModemVoice    *self,
                                                                      GAsyncReadyCallback   callback,
                                                                      gpointer              user_data);
gboolean mm_shared_cinterion_voice_disable_unsolicited_events_finish (MMIfaceModemVoice    *self,
                                                                      GAsyncResult         *res,
                                                                      GError              **error);

/*****************************************************************************/
/* Time interface */

void     mm_shared_cinterion_time_setup_unsolicited_events          (MMIfaceModemTime     *self,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);
gboolean mm_shared_cinterion_time_setup_unsolicited_events_finish   (MMIfaceModemTime     *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);

void     mm_shared_cinterion_time_cleanup_unsolicited_events        (MMIfaceModemTime     *self,
                                                                     GAsyncReadyCallback  callback,
                                                                     gpointer             user_data);
gboolean mm_shared_cinterion_time_cleanup_unsolicited_events_finish (MMIfaceModemTime     *self,
                                                                     GAsyncResult         *res,
                                                                     GError              **error);

#endif  /* MM_SHARED_CINTERION_H */
