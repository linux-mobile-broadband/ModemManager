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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_SIMPLE_STATUS_H
#define MM_SIMPLE_STATUS_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_SIMPLE_STATUS            (mm_simple_status_get_type ())
#define MM_SIMPLE_STATUS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIMPLE_STATUS, MMSimpleStatus))
#define MM_SIMPLE_STATUS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIMPLE_STATUS, MMSimpleStatusClass))
#define MM_IS_SIMPLE_STATUS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIMPLE_STATUS))
#define MM_IS_SIMPLE_STATUS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIMPLE_STATUS))
#define MM_SIMPLE_STATUS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIMPLE_STATUS, MMSimpleStatusClass))

typedef struct _MMSimpleStatus MMSimpleStatus;
typedef struct _MMSimpleStatusClass MMSimpleStatusClass;
typedef struct _MMSimpleStatusPrivate MMSimpleStatusPrivate;

/**
 * MMSimpleStatus:
 *
 * The #MMSimpleStatus structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMSimpleStatus {
    /*< private >*/
    GObject parent;
    MMSimpleStatusPrivate *priv;
};

struct _MMSimpleStatusClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_simple_status_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSimpleStatus, g_object_unref)

MMModemState                  mm_simple_status_get_state               (MMSimpleStatus *self);
guint32                       mm_simple_status_get_signal_quality      (MMSimpleStatus *self,
                                                                        gboolean *recent);
void                          mm_simple_status_get_current_bands       (MMSimpleStatus *self,
                                                                        const MMModemBand **bands,
                                                                        guint *n_bands);
MMModemAccessTechnology       mm_simple_status_get_access_technologies (MMSimpleStatus *self);

MMModem3gppRegistrationState  mm_simple_status_get_3gpp_registration_state (MMSimpleStatus *self);
const gchar                  *mm_simple_status_get_3gpp_operator_code      (MMSimpleStatus *self);
const gchar                  *mm_simple_status_get_3gpp_operator_name      (MMSimpleStatus *self);

MMModemCdmaRegistrationState mm_simple_status_get_cdma_cdma1x_registration_state (MMSimpleStatus *self);
MMModemCdmaRegistrationState mm_simple_status_get_cdma_evdo_registration_state   (MMSimpleStatus *self);
guint                        mm_simple_status_get_cdma_sid                       (MMSimpleStatus *self);
guint                        mm_simple_status_get_cdma_nid                       (MMSimpleStatus *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

#define MM_SIMPLE_PROPERTY_STATE                   "state"
#define MM_SIMPLE_PROPERTY_SIGNAL_QUALITY          "signal-quality"
#define MM_SIMPLE_PROPERTY_CURRENT_BANDS           "current-bands"
#define MM_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES     "access-technologies"

#define MM_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE "m3gpp-registration-state"
#define MM_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE      "m3gpp-operator-code"
#define MM_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME      "m3gpp-operator-name"
#define MM_SIMPLE_PROPERTY_3GPP_SUBSCRIPTION_STATE "m3gpp-subscription-state"

#define MM_SIMPLE_PROPERTY_CDMA_CDMA1X_REGISTRATION_STATE "cdma-cdma1x-registration-state"
#define MM_SIMPLE_PROPERTY_CDMA_EVDO_REGISTRATION_STATE   "cdma-evdo-registration-state"
#define MM_SIMPLE_PROPERTY_CDMA_SID                       "cdma-sid"
#define MM_SIMPLE_PROPERTY_CDMA_NID                       "cdma-nid"

MMSimpleStatus *mm_simple_status_new (void);
MMSimpleStatus *mm_simple_status_new_from_dictionary (GVariant *dictionary,
                                                              GError **error);

GVariant *mm_simple_status_get_dictionary (MMSimpleStatus *self);

#endif

G_END_DECLS

#endif /* MM_SIMPLE_STATUS_H */
