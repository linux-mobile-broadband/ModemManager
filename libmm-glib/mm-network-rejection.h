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
 * Copyright (C) 2024 Google, Inc.
 */

#ifndef MM_NETWORK_REJECTION_H
#define MM_NETWORK_REJECTION_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_NETWORK_REJECTION   (mm_network_rejection_get_type ())
#define MM_NETWORK_REJECTION(obj)   (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_NETWORK_REJECTION, MMNetworkRejection))
#define MM_NETWORK_REJECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_NETWORK_REJECTION, MMNetworkRejectionClass))
#define MM_IS_NETWORK_REJECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_NETWORK_REJECTION))
#define MM_IS_NETWORK_REJECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_NETWORK_REJECTION))
#define MM_NETWORK_REJECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_NETWORK_REJECTION, MMNetworkRejectionClass))

typedef struct _MMNetworkRejection MMNetworkRejection;
typedef struct _MMNetworkRejectionClass MMNetworkRejectionClass;
typedef struct _MMNetworkRejectionPrivate MMNetworkRejectionPrivate;

/**
 * MMNetworkRejection:
 *
 * The #MMNetworkRejection structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMNetworkRejection {
    /*< private >*/
    GObject parent;
    MMNetworkRejectionPrivate *priv;
};

struct _MMNetworkRejectionClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_network_rejection_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMNetworkRejection, g_object_unref)

MMNetworkError          mm_network_rejection_get_error             (MMNetworkRejection *self);
MMModemAccessTechnology mm_network_rejection_get_access_technology (MMNetworkRejection *self);
const gchar            *mm_network_rejection_get_operator_id       (MMNetworkRejection *self);
const gchar            *mm_network_rejection_get_operator_name     (MMNetworkRejection *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMNetworkRejection *mm_network_rejection_new (void);
MMNetworkRejection *mm_network_rejection_new_from_dictionary (GVariant *dictionary,
                                                              GError   **error);

void mm_network_rejection_set_error             (MMNetworkRejection *self,
                                                 MMNetworkError      error);
void mm_network_rejection_set_operator_id       (MMNetworkRejection *self,
                                                 const gchar        *operator_id);
void mm_network_rejection_set_operator_name     (MMNetworkRejection *self,
                                                 const gchar        *operator_name);
void mm_network_rejection_set_access_technology (MMNetworkRejection     *self,
                                                 MMModemAccessTechnology access_technology);

GVariant *mm_network_rejection_get_dictionary (MMNetworkRejection *self);

#endif

G_END_DECLS

#endif /* MM_NETWORK_REJECTION_H */
