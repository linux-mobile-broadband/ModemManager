/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_LOCATION_3GPP_H
#define MM_LOCATION_3GPP_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_LOCATION_3GPP            (mm_location_3gpp_get_type ())
#define MM_LOCATION_3GPP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_LOCATION_3GPP, MMLocation3gpp))
#define MM_LOCATION_3GPP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_LOCATION_3GPP, MMLocation3gppClass))
#define MM_IS_LOCATION_3GPP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_LOCATION_3GPP))
#define MM_IS_LOCATION_3GPP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_LOCATION_3GPP))
#define MM_LOCATION_3GPP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_LOCATION_3GPP, MMLocation3gppClass))

typedef struct _MMLocation3gpp MMLocation3gpp;
typedef struct _MMLocation3gppClass MMLocation3gppClass;
typedef struct _MMLocation3gppPrivate MMLocation3gppPrivate;

/**
 * MMLocation3gpp:
 *
 * The #MMLocation3gpp structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMLocation3gpp {
    /*< private >*/
    GObject parent;
    MMLocation3gppPrivate *priv;
};

struct _MMLocation3gppClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_location_3gpp_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMLocation3gpp, g_object_unref)

guint        mm_location_3gpp_get_mobile_country_code (MMLocation3gpp *self);
gulong       mm_location_3gpp_get_location_area_code  (MMLocation3gpp *self);
gulong       mm_location_3gpp_get_cell_id             (MMLocation3gpp *self);
gulong       mm_location_3gpp_get_tracking_area_code  (MMLocation3gpp *self);
const gchar *mm_location_3gpp_get_operator_code       (MMLocation3gpp *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

GVariant *mm_location_3gpp_get_string_variant (MMLocation3gpp *self);

MMLocation3gpp *mm_location_3gpp_new (void);
MMLocation3gpp *mm_location_3gpp_new_from_string_variant (GVariant *string,
                                                          GError **error);

gboolean mm_location_3gpp_set_operator_code      (MMLocation3gpp *self,
                                                  const gchar *operator_code);
gboolean mm_location_3gpp_set_location_area_code (MMLocation3gpp *self,
                                                  gulong location_area_code);
gboolean mm_location_3gpp_set_cell_id            (MMLocation3gpp *self,
                                                  gulong cell_id);
gboolean mm_location_3gpp_set_tracking_area_code (MMLocation3gpp *self,
                                                  gulong tracking_area_code);
gboolean mm_location_3gpp_reset                  (MMLocation3gpp *self);

#endif

G_END_DECLS

#endif /* MM_LOCATION_3GPP_H */
