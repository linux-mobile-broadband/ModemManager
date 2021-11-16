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
 * Copyright (C) 2012 Google Inc.
 */

#ifndef MM_FIRMWARE_PROPERTIES_H
#define MM_FIRMWARE_PROPERTIES_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_FIRMWARE_PROPERTIES            (mm_firmware_properties_get_type ())
#define MM_FIRMWARE_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_FIRMWARE_PROPERTIES, MMFirmwareProperties))
#define MM_FIRMWARE_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_FIRMWARE_PROPERTIES, MMFirmwarePropertiesClass))
#define MM_IS_FIRMWARE_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_FIRMWARE_PROPERTIES))
#define MM_IS_FIRMWARE_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_FIRMWARE_PROPERTIES))
#define MM_FIRMWARE_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_FIRMWARE_PROPERTIES, MMFirmwarePropertiesClass))

typedef struct _MMFirmwareProperties MMFirmwareProperties;
typedef struct _MMFirmwarePropertiesClass MMFirmwarePropertiesClass;
typedef struct _MMFirmwarePropertiesPrivate MMFirmwarePropertiesPrivate;

/**
 * MMFirmwareProperties:
 *
 * The #MMFirmwareProperties structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMFirmwareProperties {
    /*< private >*/
    GObject parent;
    MMFirmwarePropertiesPrivate *priv;
};

struct _MMFirmwarePropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_firmware_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMFirmwareProperties, g_object_unref)

const gchar         *mm_firmware_properties_get_unique_id  (MMFirmwareProperties *self);
MMFirmwareImageType  mm_firmware_properties_get_image_type (MMFirmwareProperties *self);

/* Gobi specific */
const gchar *mm_firmware_properties_get_gobi_pri_version     (MMFirmwareProperties *self);
const gchar *mm_firmware_properties_get_gobi_pri_info        (MMFirmwareProperties *self);
const gchar *mm_firmware_properties_get_gobi_boot_version    (MMFirmwareProperties *self);
const gchar *mm_firmware_properties_get_gobi_pri_unique_id   (MMFirmwareProperties *self);
const gchar *mm_firmware_properties_get_gobi_modem_unique_id (MMFirmwareProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMFirmwareProperties *mm_firmware_properties_new (MMFirmwareImageType image_type,
                                                  const gchar *unique_id);
MMFirmwareProperties *mm_firmware_properties_new_from_dictionary (GVariant *dictionary,
                                                                  GError **error);

/* Gobi specific */
void mm_firmware_properties_set_gobi_pri_version     (MMFirmwareProperties *self,
                                                      const gchar *version);
void mm_firmware_properties_set_gobi_pri_info        (MMFirmwareProperties *self,
                                                      const gchar *info);
void mm_firmware_properties_set_gobi_boot_version    (MMFirmwareProperties *self,
                                                      const gchar *version);
void mm_firmware_properties_set_gobi_pri_unique_id   (MMFirmwareProperties *self,
                                                      const gchar *id);
void mm_firmware_properties_set_gobi_modem_unique_id (MMFirmwareProperties *self,
                                                      const gchar *id);

GVariant *mm_firmware_properties_get_dictionary (MMFirmwareProperties *self);

#endif

G_END_DECLS

#endif /* MM_FIRMWARE_PROPERTIES_H */
