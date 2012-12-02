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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 * Copyright (C) 2012 Huawei Technologies Co., Ltd
 *
 * Author: Franko Fang <huananhu@huawei.com>
 */

#ifndef MM_BROADBAND_BEARER_HUAWEI_H
#define MM_BROADBAND_BEARER_HUAWEI_H

#include <glib.h>
#include <glib-object.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-huawei.h"

#define MM_TYPE_BROADBAND_BEARER_HUAWEI				(mm_broadband_bearer_huawei_get_type ())
#define MM_BROADBAND_BEARER_HUAWEI(obj)            	(G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_HUAWEI, MMBroadbandBearerHuawei))
#define MM_BROADBAND_BEARER_HUAWEI_CLASS(klass)    	(G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_HUAWEI, MMBroadbandBearerHuaweiClass))
#define MM_IS_BROADBAND_BEARER_HUAWEI(obj)         	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_HUAWEI))
#define MM_IS_BROADBAND_BEARER_HUAWEI_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_HUAWEI))
#define MM_BROADBAND_BEARER_HUAWEI_GET_CLASS(obj)  	(G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_HUAWEI, MMBroadbandBearerHuaweiClass))

typedef struct _MMBroadbandBearerHuawei MMBroadbandBearerHuawei;
typedef struct _MMBroadbandBearerHuaweiClass MMBroadbandBearerHuaweiClass;
typedef struct _MMBroadbandBearerHuaweiPrivate MMBroadbandBearerHuaweiPrivate;

struct _MMBroadbandBearerHuawei {
    MMBroadbandBearer parent;
    MMBroadbandBearerHuaweiPrivate *priv;
};

struct _MMBroadbandBearerHuaweiClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_huawei_get_type (void);

void      mm_broadband_bearer_huawei_new        (MMBroadbandModemHuawei *modem,
                                                 MMBearerProperties *config,
                                                 GCancellable *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
MMBearer *mm_broadband_bearer_huawei_new_finish (GAsyncResult *res,
                                                 GError **error);

#endif /* MM_BROADBAND_BEARER_HUAWEI_H */
