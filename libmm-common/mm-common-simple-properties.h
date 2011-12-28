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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_COMMON_SIMPLE_PROPERTIES_H
#define MM_COMMON_SIMPLE_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_COMMON_SIMPLE_PROPERTIES            (mm_common_simple_properties_get_type ())
#define MM_COMMON_SIMPLE_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_COMMON_SIMPLE_PROPERTIES, MMCommonSimpleProperties))
#define MM_COMMON_SIMPLE_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_COMMON_SIMPLE_PROPERTIES, MMCommonSimplePropertiesClass))
#define MM_IS_COMMON_SIMPLE_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_COMMON_SIMPLE_PROPERTIES))
#define MM_IS_COMMON_SIMPLE_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_COMMON_SIMPLE_PROPERTIES))
#define MM_COMMON_SIMPLE_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_COMMON_SIMPLE_PROPERTIES, MMCommonSimplePropertiesClass))

#define MM_COMMON_SIMPLE_PROPERTY_STATE                   "state"
#define MM_COMMON_SIMPLE_PROPERTY_SIGNAL_QUALITY          "signal-quality"
#define MM_COMMON_SIMPLE_PROPERTY_BANDS                   "bands"
#define MM_COMMON_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES     "access-technologies"
#define MM_COMMON_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE "registration-state"
#define MM_COMMON_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE      "operator-code"
#define MM_COMMON_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME      "operator-name"

typedef struct _MMCommonSimpleProperties MMCommonSimpleProperties;
typedef struct _MMCommonSimplePropertiesClass MMCommonSimplePropertiesClass;
typedef struct _MMCommonSimplePropertiesPrivate MMCommonSimplePropertiesPrivate;

struct _MMCommonSimpleProperties {
    GObject parent;
    MMCommonSimplePropertiesPrivate *priv;
};

struct _MMCommonSimplePropertiesClass {
    GObjectClass parent;
};

GType mm_common_simple_properties_get_type (void);

MMCommonSimpleProperties *mm_common_simple_properties_new (void);

MMModemState                  mm_common_simple_properties_get_state               (MMCommonSimpleProperties *self);
guint32                       mm_common_simple_properties_get_signal_quality      (MMCommonSimpleProperties *self,
                                                                                   gboolean *recent);
void                          mm_common_simple_properties_get_bands               (MMCommonSimpleProperties *self,
                                                                                   const MMModemBand **bands,
                                                                                   guint *n_bands);
MMModemAccessTechnology       mm_common_simple_properties_get_access_technologies (MMCommonSimpleProperties *self);
MMModem3gppRegistrationState  mm_common_simple_properties_get_registration_state  (MMCommonSimpleProperties *self);
const gchar                  *mm_common_simple_properties_get_operator_code       (MMCommonSimpleProperties *self);
const gchar                  *mm_common_simple_properties_get_operator_name       (MMCommonSimpleProperties *self);

GVariant *mm_common_simple_properties_get_dictionary (MMCommonSimpleProperties *self);

G_END_DECLS

#endif /* MM_COMMON_SIMPLE_PROPERTIES_H */
