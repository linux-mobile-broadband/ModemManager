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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 */

#ifndef MM_CALL_HUAWEI_H
#define MM_CALL_HUAWEI_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-call.h"

#define MM_TYPE_CALL_HUAWEI            (mm_call_huawei_get_type ())
#define MM_CALL_HUAWEI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL_HUAWEI, MMCallHuawei))
#define MM_CALL_HUAWEI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CALL_HUAWEI, MMCallHuaweiClass))
#define MM_IS_CALL_HUAWEI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL_HUAWEI))
#define MM_IS_CALL_HUAWEI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CALL_HUAWEI))
#define MM_CALL_HUAWEI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CALL_HUAWEI, MMCallHuaweiClass))

typedef struct _MMCallHuawei MMCallHuawei;
typedef struct _MMCallHuaweiClass MMCallHuaweiClass;

struct _MMCallHuawei {
    MMBaseCall parent;
};

struct _MMCallHuaweiClass {
    MMBaseCallClass parent;
};

GType mm_call_huawei_get_type (void);

MMBaseCall *mm_call_huawei_new (MMBaseModem *modem);

#endif /* MM_CALL_HUAWEI_H */
