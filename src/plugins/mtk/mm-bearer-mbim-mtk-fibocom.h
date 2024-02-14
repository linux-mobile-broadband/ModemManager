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
 * Copyright (C) 2024 Google, Inc.
 */

#ifndef MM_BEARER_MBIM_MTK_FIBOCOM_H
#define MM_BEARER_MBIM_MTK_FIBOCOM_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-bearer-mbim.h"

#define MM_TYPE_BEARER_MBIM_MTK_FIBOCOM            (mm_bearer_mbim_mtk_fibocom_get_type ())
#define MM_BEARER_MBIM_MTK_FIBOCOM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_MBIM_MTK_FIBOCOM, MMBearerMbimMtkFibocom))
#define MM_BEARER_MBIM_MTK_FIBOCOM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_MBIM_MTK_FIBOCOM, MMBearerMbimMtkFibocomClass))
#define MM_IS_BEARER_MBIM_MTK_FIBOCOM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_MBIM_MTK_FIBOCOM))
#define MM_IS_BEARER_MBIM_MTK_FIBOCOM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_MBIM_MTK_FIBOCOM))
#define MM_BEARER_MBIM_MTK_FIBOCOM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_MBIM_MTK_FIBOCOM, MMBearerMbimMtkFibocomClass))

typedef struct _MMBearerMbimMtkFibocom MMBearerMbimMtkFibocom;
typedef struct _MMBearerMbimMtkFibocomClass MMBearerMbimMtkFibocomClass;
typedef struct _MMBearerMbimMtkFibocomPrivate MMBearerMbimMtkFibocomPrivate;

struct _MMBearerMbimMtkFibocom {
    MMBearerMbim parent;
    MMBearerMbimMtkFibocomPrivate *priv;
};

struct _MMBearerMbimMtkFibocomClass {
    MMBearerMbimClass parent;
};

GType mm_bearer_mbim_mtk_fibocom_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBearerMbimMtkFibocom, g_object_unref)

/* MBIM bearer creation implementation */
MMBaseBearer *mm_bearer_mbim_mtk_fibocom_new (MMBroadbandModemMbim *modem,
                                              gboolean              is_async_slaac_supported,
                                              gboolean              remove_ip_packet_filters,
                                              MMBearerProperties   *config);

#endif /* MM_BEARER_MBIM_MTK_FIBOCOM_H */
