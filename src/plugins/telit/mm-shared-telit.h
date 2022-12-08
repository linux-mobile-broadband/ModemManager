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
 * Copyright (C) 2019 Daniele Palmas <dnlplm@gmail.com>
 */

#ifndef MM_SHARED_TELIT_H
#define MM_SHARED_TELIT_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-modem-helpers-telit.h"

#define MM_TYPE_SHARED_TELIT                   (mm_shared_telit_get_type ())
#define MM_SHARED_TELIT(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SHARED_TELIT, MMSharedTelit))
#define MM_IS_SHARED_TELIT(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SHARED_TELIT))
#define MM_SHARED_TELIT_GET_INTERFACE(obj)     (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_SHARED_TELIT, MMSharedTelit))

typedef struct _MMSharedTelit MMSharedTelit;

struct _MMSharedTelit {
    GTypeInterface g_iface;

    /* Peek modem interface of the parent class of the object */
    MMIfaceModem * (* peek_parent_modem_interface) (MMSharedTelit *self);
};

GType mm_shared_telit_get_type (void);

void        mm_shared_telit_store_supported_modes       (MMSharedTelit *self,
                                                         GArray *modes);

gboolean    mm_shared_telit_load_current_modes_finish   (MMIfaceModem *self,
                                                         GAsyncResult *res,
                                                         MMModemMode *allowed,
                                                         MMModemMode *preferred,
                                                         GError **error);

void        mm_shared_telit_load_current_modes          (MMIfaceModem *self,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

gboolean    mm_shared_telit_set_current_modes_finish    (MMIfaceModem *self,
                                                         GAsyncResult *res,
                                                         GError **error);

void        mm_shared_telit_set_current_modes           (MMIfaceModem *self,
                                                         MMModemMode allowed,
                                                         MMModemMode preferred,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

void        mm_shared_telit_modem_load_supported_bands  (MMIfaceModem *self,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

GArray *    mm_shared_telit_modem_load_supported_bands_finish (MMIfaceModem *self,
                                                               GAsyncResult *res,
                                                               GError **error);

void        mm_shared_telit_modem_load_current_bands    (MMIfaceModem *self,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

GArray *    mm_shared_telit_modem_load_current_bands_finish (MMIfaceModem *self,
                                                             GAsyncResult *res,
                                                             GError **error);

gboolean    mm_shared_telit_modem_set_current_bands_finish (MMIfaceModem *self,
                                                            GAsyncResult *res,
                                                            GError **error);

void        mm_shared_telit_modem_set_current_bands     (MMIfaceModem *self,
                                                         GArray *bands_array,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

void       mm_shared_telit_modem_load_revision          (MMIfaceModem *self,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

gchar *   mm_shared_telit_modem_load_revision_finish    (MMIfaceModem *self,
                                                         GAsyncResult *res,
                                                         GError **error);

void      mm_shared_telit_store_revision                (MMSharedTelit *self,
                                                         const gchar   *revision);

void      mm_shared_telit_get_bnd_parse_config          (MMIfaceModem          *self,
                                                         MMTelitBNDParseConfig *config);
#endif  /* MM_SHARED_TELIT_H */
