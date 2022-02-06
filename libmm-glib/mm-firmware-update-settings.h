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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_FIRMWARE_UPDATE_SETTINGS_H
#define MM_FIRMWARE_UPDATE_SETTINGS_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_FIRMWARE_UPDATE_SETTINGS            (mm_firmware_update_settings_get_type ())
#define MM_FIRMWARE_UPDATE_SETTINGS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_FIRMWARE_UPDATE_SETTINGS, MMFirmwareUpdateSettings))
#define MM_FIRMWARE_UPDATE_SETTINGS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_FIRMWARE_UPDATE_SETTINGS, MMFirmwareUpdateSettingsClass))
#define MM_IS_FIRMWARE_UPDATE_SETTINGS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_FIRMWARE_UPDATE_SETTINGS))
#define MM_IS_FIRMWARE_UPDATE_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_FIRMWARE_UPDATE_SETTINGS))
#define MM_FIRMWARE_UPDATE_SETTINGS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_FIRMWARE_UPDATE_SETTINGS, MMFirmwareUpdateSettingsClass))

typedef struct _MMFirmwareUpdateSettings MMFirmwareUpdateSettings;
typedef struct _MMFirmwareUpdateSettingsClass MMFirmwareUpdateSettingsClass;
typedef struct _MMFirmwareUpdateSettingsPrivate MMFirmwareUpdateSettingsPrivate;

/**
 * MMFirmwareUpdateSettings:
 *
 * The #MMFirmwareUpdateSettings structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMFirmwareUpdateSettings {
    /*< private >*/
    GObject parent;
    MMFirmwareUpdateSettingsPrivate *priv;
};

struct _MMFirmwareUpdateSettingsClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_firmware_update_settings_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMFirmwareUpdateSettings, g_object_unref)

MMModemFirmwareUpdateMethod mm_firmware_update_settings_get_method (MMFirmwareUpdateSettings *self);

/* Generic */
const gchar **mm_firmware_update_settings_get_device_ids (MMFirmwareUpdateSettings *self);
const gchar  *mm_firmware_update_settings_get_version    (MMFirmwareUpdateSettings *self);

/* Fastboot specific */
const gchar *mm_firmware_update_settings_get_fastboot_at (MMFirmwareUpdateSettings *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMFirmwareUpdateSettings *mm_firmware_update_settings_new (MMModemFirmwareUpdateMethod method);

MMFirmwareUpdateSettings *mm_firmware_update_settings_new_from_variant (GVariant  *variant,
                                                                        GError   **error);

GVariant *mm_firmware_update_settings_get_variant (MMFirmwareUpdateSettings *self);

/* Generic */
void mm_firmware_update_settings_set_device_ids (MMFirmwareUpdateSettings     *self,
                                                 const gchar                 **device_ids);
void mm_firmware_update_settings_set_version    (MMFirmwareUpdateSettings     *self,
                                                 const gchar                  *version);
void mm_firmware_update_settings_set_method     (MMFirmwareUpdateSettings     *self,
                                                 MMModemFirmwareUpdateMethod   method);

/* Fastboot specific */
void mm_firmware_update_settings_set_fastboot_at (MMFirmwareUpdateSettings *self,
                                                  const gchar              *fastboot_at);

#endif

G_END_DECLS

#endif /* MM_FIRMWARE_UPDATE_SETTINGS_H */
