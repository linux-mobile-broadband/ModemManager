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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_SHARED_QMI_H
#define MM_SHARED_QMI_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <libqmi-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-port-qmi.h"

#define MM_TYPE_SHARED_QMI            (mm_shared_qmi_get_type ())
#define MM_SHARED_QMI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SHARED_QMI, MMSharedQmi))
#define MM_IS_SHARED_QMI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SHARED_QMI))
#define MM_SHARED_QMI_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_SHARED_QMI, MMSharedQmi))

typedef struct _MMSharedQmi MMSharedQmi;

struct _MMSharedQmi {
    GTypeInterface g_iface;

    QmiClient * (* peek_client) (MMSharedQmi    *self,
                                 QmiService      service,
                                 MMPortQmiFlag   flag,
                                 GError        **error);

    /* Peek location interface of the parent class of the object */
    MMIfaceModemLocation * (* peek_parent_location_interface) (MMSharedQmi *self);
};

GType mm_shared_qmi_get_type (void);

QmiClient *mm_shared_qmi_peek_client   (MMSharedQmi          *self,
                                        QmiService            service,
                                        MMPortQmiFlag         flag,
                                        GError              **error);

gboolean   mm_shared_qmi_ensure_client (MMSharedQmi          *self,
                                        QmiService            service,
                                        QmiClient           **o_client,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data);

/* Shared QMI location support */

void                   mm_shared_qmi_location_load_capabilities        (MMIfaceModemLocation  *self,
                                                                        GAsyncReadyCallback    callback,
                                                                        gpointer               user_data);
MMModemLocationSource  mm_shared_qmi_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                                        GAsyncResult          *res,
                                                                        GError               **error);
void                   mm_shared_qmi_enable_location_gathering         (MMIfaceModemLocation  *_self,
                                                                        MMModemLocationSource  source,
                                                                        GAsyncReadyCallback    callback,
                                                                        gpointer               user_data);
gboolean               mm_shared_qmi_enable_location_gathering_finish  (MMIfaceModemLocation  *self,
                                                                        GAsyncResult          *res,
                                                                        GError               **error);
void                   mm_shared_qmi_disable_location_gathering        (MMIfaceModemLocation  *_self,
                                                                        MMModemLocationSource  source,
                                                                        GAsyncReadyCallback    callback,
                                                                        gpointer               user_data);
gboolean               mm_shared_qmi_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                                        GAsyncResult          *res,
                                                                        GError               **error);
void                   mm_shared_qmi_location_load_supl_server         (MMIfaceModemLocation  *self,
                                                                        GAsyncReadyCallback    callback,
                                                                        gpointer               user_data);
gchar                 *mm_shared_qmi_location_load_supl_server_finish  (MMIfaceModemLocation  *self,
                                                                        GAsyncResult          *res,
                                                                        GError               **error);
void                   mm_shared_qmi_location_set_supl_server          (MMIfaceModemLocation  *self,
                                                                        const gchar           *supl,
                                                                        GAsyncReadyCallback    callback,
                                                                        gpointer               user_data);
gboolean               mm_shared_qmi_location_set_supl_server_finish   (MMIfaceModemLocation  *self,
                                                                        GAsyncResult          *res,
                                                                        GError               **error);

#endif /* MM_SHARED_QMI_H */
