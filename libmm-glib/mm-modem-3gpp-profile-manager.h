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
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef _MM_MODEM_3GPP_PROFILE_MANAGER_H_
#define _MM_MODEM_3GPP_PROFILE_MANAGER_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-3gpp-profile.h"
#include "mm-gdbus-modem.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_3GPP_PROFILE_MANAGER            (mm_modem_3gpp_profile_manager_get_type ())
#define MM_MODEM_3GPP_PROFILE_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_3GPP_PROFILE_MANAGER, MMModem3gppProfileManager))
#define MM_MODEM_3GPP_PROFILE_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_3GPP_PROFILE_MANAGER, MMModem3gppProfileManagerClass))
#define MM_IS_MODEM_3GPP_PROFILE_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_3GPP_PROFILE_MANAGER))
#define MM_IS_MODEM_3GPP_PROFILE_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_3GPP_PROFILE_MANAGER))
#define MM_MODEM_3GPP_PROFILE_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_3GPP_PROFILE_MANAGER, MMModem3gppProfileManagerClass))

typedef struct _MMModem3gppProfileManager MMModem3gppProfileManager;
typedef struct _MMModem3gppProfileManagerClass MMModem3gppProfileManagerClass;

/**
 * MMModem3gppProfileManager:
 *
 * The #MMModem3gppProfileManager structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModem3gppProfileManager {
    /*< private >*/
    MmGdbusModem3gppProfileManagerProxy parent;
    gpointer unused;
};

struct _MMModem3gppProfileManagerClass {
    /*< private >*/
    MmGdbusModem3gppProfileManagerProxyClass parent;
};

GType mm_modem_3gpp_profile_manager_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModem3gppProfileManager, g_object_unref)

const gchar   *mm_modem_3gpp_profile_manager_get_path      (MMModem3gppProfileManager  *self);
gchar         *mm_modem_3gpp_profile_manager_dup_path      (MMModem3gppProfileManager  *self);

void           mm_modem_3gpp_profile_manager_list          (MMModem3gppProfileManager  *self,
                                                            GCancellable               *cancellable,
                                                            GAsyncReadyCallback         callback,
                                                            gpointer                    user_data);
gboolean       mm_modem_3gpp_profile_manager_list_finish   (MMModem3gppProfileManager  *self,
                                                            GAsyncResult               *res,
                                                            GList                     **profiles,
                                                            GError                    **error);
gboolean       mm_modem_3gpp_profile_manager_list_sync     (MMModem3gppProfileManager  *self,
                                                            GCancellable               *cancellable,
                                                            GList                     **profiles,
                                                            GError                    **error);

void           mm_modem_3gpp_profile_manager_set           (MMModem3gppProfileManager  *self,
                                                            MM3gppProfile              *requested,
                                                            GCancellable               *cancellable,
                                                            GAsyncReadyCallback         callback,
                                                            gpointer                    user_data);
MM3gppProfile *mm_modem_3gpp_profile_manager_set_finish    (MMModem3gppProfileManager  *self,
                                                            GAsyncResult               *res,
                                                            GError                    **error);
MM3gppProfile *mm_modem_3gpp_profile_manager_set_sync      (MMModem3gppProfileManager  *self,
                                                            MM3gppProfile              *requested,
                                                            GCancellable               *cancellable,
                                                            GError                    **error);

void           mm_modem_3gpp_profile_manager_delete        (MMModem3gppProfileManager  *self,
                                                            MM3gppProfile              *profile,
                                                            GCancellable               *cancellable,
                                                            GAsyncReadyCallback         callback,
                                                            gpointer                    user_data);
gboolean       mm_modem_3gpp_profile_manager_delete_finish (MMModem3gppProfileManager  *self,
                                                            GAsyncResult               *res,
                                                            GError                    **error);
gboolean       mm_modem_3gpp_profile_manager_delete_sync   (MMModem3gppProfileManager  *self,
                                                            MM3gppProfile              *profile,
                                                            GCancellable               *cancellable,
                                                            GError                    **error);

G_END_DECLS

#endif /* _MM_MODEM_3GPP_PROFILE_MANAGER_H_ */
