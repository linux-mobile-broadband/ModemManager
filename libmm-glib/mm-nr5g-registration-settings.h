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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_NR5G_REGISTRATION_SETTINGS_H
#define MM_NR5G_REGISTRATION_SETTINGS_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_NR5G_REGISTRATION_SETTINGS            (mm_nr5g_registration_settings_get_type ())
#define MM_NR5G_REGISTRATION_SETTINGS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_NR5G_REGISTRATION_SETTINGS, MMNr5gRegistrationSettings))
#define MM_NR5G_REGISTRATION_SETTINGS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_NR5G_REGISTRATION_SETTINGS, MMNr5gRegistrationSettingsClass))
#define MM_IS_NR5G_REGISTRATION_SETTINGS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_NR5G_REGISTRATION_SETTINGS))
#define MM_IS_NR5G_REGISTRATION_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_NR5G_REGISTRATION_SETTINGS))
#define MM_NR5G_REGISTRATION_SETTINGS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_NR5G_REGISTRATION_SETTINGS, MMNr5gRegistrationSettingsClass))

typedef struct _MMNr5gRegistrationSettings MMNr5gRegistrationSettings;
typedef struct _MMNr5gRegistrationSettingsClass MMNr5gRegistrationSettingsClass;
typedef struct _MMNr5gRegistrationSettingsPrivate MMNr5gRegistrationSettingsPrivate;

/**
 * MMNr5gRegistrationSettings:
 *
 * The #MMNr5gRegistrationSettings structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMNr5gRegistrationSettings {
    /*< private >*/
    GObject parent;
    MMNr5gRegistrationSettingsPrivate *priv;
};

struct _MMNr5gRegistrationSettingsClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_nr5g_registration_settings_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMNr5gRegistrationSettings, g_object_unref)

MMNr5gRegistrationSettings *mm_nr5g_registration_settings_new           (void);

void                        mm_nr5g_registration_settings_set_mico_mode (MMNr5gRegistrationSettings *self,
                                                                         MMModem3gppMicoMode         mico_mode);
MMModem3gppMicoMode         mm_nr5g_registration_settings_get_mico_mode (MMNr5gRegistrationSettings *self);

void                        mm_nr5g_registration_settings_set_drx_cycle (MMNr5gRegistrationSettings *self,
                                                                         MMModem3gppDrxCycle         drx_cycle);
MMModem3gppDrxCycle         mm_nr5g_registration_settings_get_drx_cycle (MMNr5gRegistrationSettings *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMNr5gRegistrationSettings *mm_nr5g_registration_settings_new_from_string     (const gchar                 *str,
                                                                               GError                     **error);
MMNr5gRegistrationSettings *mm_nr5g_registration_settings_new_from_dictionary (GVariant                    *dictionary,
                                                                               GError                     **error);
GVariant                   *mm_nr5g_registration_settings_get_dictionary      (MMNr5gRegistrationSettings  *self);
gboolean                    mm_nr5g_registration_settings_cmp                 (MMNr5gRegistrationSettings *a,
                                                                               MMNr5gRegistrationSettings *b);

#endif

G_END_DECLS

#endif /* MM_NR5G_REGISTRATION_SETTINGS_H */
