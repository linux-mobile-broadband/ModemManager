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

#include <config.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-bearer-mbim-mtk-fibocom.h"

G_DEFINE_TYPE (MMBearerMbimMtkFibocom, mm_bearer_mbim_mtk_fibocom, MM_TYPE_BEARER_MBIM)

/*****************************************************************************/

MMBaseBearer *
mm_bearer_mbim_mtk_fibocom_new (MMBroadbandModemMbim *modem,
                                gboolean              is_async_slaac_supported,
                                MMBearerProperties   *config)
{
    MMBaseBearer *bearer;

    /* The Mbim bearer inherits from MMBaseBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    bearer = g_object_new (MM_TYPE_BEARER_MBIM_MTK_FIBOCOM,
                           MM_BASE_BEARER_MODEM,  modem,
                           MM_BASE_BEARER_CONFIG, config,
                           MM_BEARER_MBIM_ASYNC_SLAAC, is_async_slaac_supported,
                           NULL);

    /* Only export valid bearers */
    mm_base_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_mbim_mtk_fibocom_init (MMBearerMbimMtkFibocom *self)
{
}

static void
mm_bearer_mbim_mtk_fibocom_class_init (MMBearerMbimMtkFibocomClass *klass)
{
}
