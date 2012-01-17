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

#ifndef MM_COMMON_LOCATION_3GPP_H
#define MM_COMMON_LOCATION_3GPP_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_COMMON_LOCATION_3GPP            (mm_common_location_3gpp_get_type ())
#define MM_COMMON_LOCATION_3GPP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_COMMON_LOCATION_3GPP, MMCommonLocation3gpp))
#define MM_COMMON_LOCATION_3GPP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_COMMON_LOCATION_3GPP, MMCommonLocation3gppClass))
#define MM_IS_COMMON_LOCATION_3GPP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_COMMON_LOCATION_3GPP))
#define MM_IS_COMMON_LOCATION_3GPP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_COMMON_LOCATION_3GPP))
#define MM_COMMON_LOCATION_3GPP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_COMMON_LOCATION_3GPP, MMCommonLocation3gppClass))

typedef struct _MMCommonLocation3gpp MMCommonLocation3gpp;
typedef struct _MMCommonLocation3gppClass MMCommonLocation3gppClass;
typedef struct _MMCommonLocation3gppPrivate MMCommonLocation3gppPrivate;

struct _MMCommonLocation3gpp {
    GObject parent;
    MMCommonLocation3gppPrivate *priv;
};

struct _MMCommonLocation3gppClass {
    GObjectClass parent;
};

GType mm_common_location_3gpp_get_type (void);

MMCommonLocation3gpp *mm_common_location_3gpp_new (void);
MMCommonLocation3gpp *mm_common_location_3gpp_new_from_string_variant (GVariant *string,
                                                                       GError **error);

gboolean mm_common_location_3gpp_set_mobile_country_code (MMCommonLocation3gpp *self,
                                                          guint mobile_country_code);
gboolean mm_common_location_3gpp_set_mobile_network_code (MMCommonLocation3gpp *self,
                                                          guint mobile_network_code);
gboolean mm_common_location_3gpp_set_location_area_code  (MMCommonLocation3gpp *self,
                                                          gulong location_area_code);
gboolean mm_common_location_3gpp_set_cell_id             (MMCommonLocation3gpp *self,
                                                          gulong cell_id);

guint  mm_common_location_3gpp_get_mobile_country_code (MMCommonLocation3gpp *self);
guint  mm_common_location_3gpp_get_mobile_network_code (MMCommonLocation3gpp *self);
gulong mm_common_location_3gpp_get_location_area_code  (MMCommonLocation3gpp *self);
gulong mm_common_location_3gpp_get_cell_id             (MMCommonLocation3gpp *self);

GVariant *mm_common_location_3gpp_get_string_variant (MMCommonLocation3gpp *self);

G_END_DECLS

#endif /* MM_COMMON_LOCATION_3GPP_H */
