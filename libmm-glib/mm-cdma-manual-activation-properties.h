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
 * Copyright (C) 2013 Google Inc.
 */

#ifndef MM_CDMA_MANUAL_ACTIVATION_PROPERTIES_H
#define MM_CDMA_MANUAL_ACTIVATION_PROPERTIES_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES            (mm_cdma_manual_activation_properties_get_type ())
#define MM_CDMA_MANUAL_ACTIVATION_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES, MMCdmaManualActivationProperties))
#define MM_CDMA_MANUAL_ACTIVATION_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES, MMCdmaManualActivationPropertiesClass))
#define MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES))
#define MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES))
#define MM_CDMA_MANUAL_ACTIVATION_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES, MMCdmaManualActivationPropertiesClass))

typedef struct _MMCdmaManualActivationProperties MMCdmaManualActivationProperties;
typedef struct _MMCdmaManualActivationPropertiesClass MMCdmaManualActivationPropertiesClass;
typedef struct _MMCdmaManualActivationPropertiesPrivate MMCdmaManualActivationPropertiesPrivate;

/**
 * MMCdmaManualActivationProperties:
 *
 * The #MMCdmaManualActivationProperties structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMCdmaManualActivationProperties {
    /*< private >*/
    GObject parent;
    MMCdmaManualActivationPropertiesPrivate *priv;
};

struct _MMCdmaManualActivationPropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_cdma_manual_activation_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCdmaManualActivationProperties, g_object_unref)

MMCdmaManualActivationProperties *mm_cdma_manual_activation_properties_new (void);

gboolean mm_cdma_manual_activation_properties_set_spc           (MMCdmaManualActivationProperties *self,
                                                                 const gchar *spc,
                                                                 GError **error);
void      mm_cdma_manual_activation_properties_set_sid          (MMCdmaManualActivationProperties *self,
                                                                 guint16 sid);
gboolean mm_cdma_manual_activation_properties_set_mdn           (MMCdmaManualActivationProperties *self,
                                                                 const gchar *mdn,
                                                                 GError **error);
gboolean mm_cdma_manual_activation_properties_set_min           (MMCdmaManualActivationProperties *self,
                                                                 const gchar *min,
                                                                 GError **error);
gboolean mm_cdma_manual_activation_properties_set_mn_ha_key     (MMCdmaManualActivationProperties *self,
                                                                 const gchar *mn_ha_key,
                                                                 GError **error);
gboolean mm_cdma_manual_activation_properties_set_mn_aaa_key    (MMCdmaManualActivationProperties *self,
                                                                 const gchar *mn_aaa_key,
                                                                 GError **error);
gboolean mm_cdma_manual_activation_properties_set_prl           (MMCdmaManualActivationProperties *self,
                                                                 const guint8 *prl,
                                                                 gsize prl_length,
                                                                 GError **error);
gboolean mm_cdma_manual_activation_properties_set_prl_bytearray (MMCdmaManualActivationProperties *self,
                                                                 GByteArray *prl,
                                                                 GError **error);

const gchar  *mm_cdma_manual_activation_properties_get_spc            (MMCdmaManualActivationProperties *self);
guint16       mm_cdma_manual_activation_properties_get_sid            (MMCdmaManualActivationProperties *self);
const gchar  *mm_cdma_manual_activation_properties_get_mdn            (MMCdmaManualActivationProperties *self);
const gchar  *mm_cdma_manual_activation_properties_get_min            (MMCdmaManualActivationProperties *self);
const gchar  *mm_cdma_manual_activation_properties_get_mn_ha_key      (MMCdmaManualActivationProperties *self);
const gchar  *mm_cdma_manual_activation_properties_get_mn_aaa_key     (MMCdmaManualActivationProperties *self);
const guint8 *mm_cdma_manual_activation_properties_get_prl            (MMCdmaManualActivationProperties *self,
                                                                       gsize *prl_len);
GByteArray   *mm_cdma_manual_activation_properties_peek_prl_bytearray (MMCdmaManualActivationProperties *self);
GByteArray   *mm_cdma_manual_activation_properties_get_prl_bytearray  (MMCdmaManualActivationProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMCdmaManualActivationProperties *mm_cdma_manual_activation_properties_new_from_string (const gchar *str,
                                                                                        GError **error);
MMCdmaManualActivationProperties *mm_cdma_manual_activation_properties_new_from_dictionary (GVariant *dictionary,
                                                                                            GError **error);
GVariant *mm_cdma_manual_activation_properties_get_dictionary (MMCdmaManualActivationProperties *self);

#endif

G_END_DECLS

#endif /* MM_CDMA_MANUAL_ACTIVATION_PROPERTIES_H */
