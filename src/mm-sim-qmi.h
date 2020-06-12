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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_SIM_QMI_H
#define MM_SIM_QMI_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-sim.h"

#define MM_TYPE_SIM_QMI            (mm_sim_qmi_get_type ())
#define MM_SIM_QMI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIM_QMI, MMSimQmi))
#define MM_SIM_QMI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIM_QMI, MMSimQmiClass))
#define MM_IS_SIM_QMI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIM_QMI))
#define MM_IS_SIM_QMI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIM_QMI))
#define MM_SIM_QMI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIM_QMI, MMSimQmiClass))

typedef struct _MMSimQmi MMSimQmi;
typedef struct _MMSimQmiClass MMSimQmiClass;
typedef struct _MMSimQmiPrivate MMSimQmiPrivate;

#define MM_SIM_QMI_DMS_UIM_DEPRECATED "dms-uim-deprecated"

struct _MMSimQmi {
    MMBaseSim parent;
    MMSimQmiPrivate *priv;
};

struct _MMSimQmiClass {
    MMBaseSimClass parent;
};

GType mm_sim_qmi_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSimQmi, g_object_unref)

void       mm_sim_qmi_new        (MMBaseModem          *modem,
                                  gboolean              dms_uim_deprecated,
                                  GCancellable         *cancellable,
                                  GAsyncReadyCallback   callback,
                                  gpointer              user_data);
MMBaseSim *mm_sim_qmi_new_finish (GAsyncResult         *res,
                                  GError              **error);

MMBaseSim *mm_sim_qmi_new_initialized (MMBaseModem *modem,
                                       gboolean     dms_uim_deprecated,
                                       guint        slot_number,
                                       gboolean     active,
                                       const gchar *sim_identifier,
                                       const gchar *imsi,
                                       const gchar *eid,
                                       const gchar *operator_identifier,
                                       const gchar *operator_name,
                                       const GStrv  emergency_numbers);

#endif /* MM_SIM_QMI_H */
