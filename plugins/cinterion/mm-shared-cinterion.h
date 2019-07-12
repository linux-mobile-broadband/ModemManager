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
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-voice.h"
#include "mm-iface-modem-time.h"

#define MM_TYPE_SHARED_CINTERION               (mm_shared_cinterion_get_type ())
#define MM_SHARED_CINTERION(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SHARED_CINTERION, MMSharedCinterion))
#define MM_IS_SHARED_CINTERION(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SHARED_CINTERION))
#define MM_SHARED_CINTERION_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_SHARED_CINTERION, MMSharedCinterion))

typedef struct _MMSharedCinterion MMSharedCinterion;

struct _MMSharedCinterion {
    GTypeInterface g_iface;

    /* Peek location interface of the parent class of the object */
    MMIfaceModemLocation *  (* peek_parent_location_interface) (MMSharedCinterion *self);

    /* Peek voice interface of the parent class of the object */
    MMIfaceModemVoice *  (* peek_parent_voice_interface) (MMSharedCinterion *self);

    /* Peek time interface of the parent class of the object */
    MMIfaceModemTime *  (* peek_parent_time_interface) (MMSharedCinterion *self);
};

GType mm_shared_cinterion_get_type (void);

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
                                             const gchar       *number);

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
