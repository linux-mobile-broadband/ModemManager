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
 * Copyright (C) 2021 UROS Ltd
 */

#ifndef MM_SIM_PREFERRED_NETWORK_H
#define MM_SIM_PREFERRED_NETWORK_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * MMSimPreferredNetwork:
 *
 * The #MMSimPreferredNetwork structure contains private data and should only be accessed
 * using the provided API.
 */
typedef struct _MMSimPreferredNetwork MMSimPreferredNetwork;

#define MM_TYPE_SIM_PREFERRED_NETWORK (mm_sim_preferred_network_get_type ())
GType mm_sim_preferred_network_get_type (void);

MMSimPreferredNetwork *         mm_sim_preferred_network_new                   (void);

const gchar                    *mm_sim_preferred_network_get_operator_code     (const MMSimPreferredNetwork *self);
MMModemAccessTechnology         mm_sim_preferred_network_get_access_technology (const MMSimPreferredNetwork *self);

void                            mm_sim_preferred_network_set_operator_code     (MMSimPreferredNetwork *self,
                                                                                const gchar *operator_code);
void                            mm_sim_preferred_network_set_access_technology (MMSimPreferredNetwork *self,
                                                                                MMModemAccessTechnology access_technology);

void                            mm_sim_preferred_network_free                  (MMSimPreferredNetwork *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSimPreferredNetwork, mm_sim_preferred_network_free)

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMSimPreferredNetwork *         mm_sim_preferred_network_new_from_variant      (GVariant *variant);

GVariant *mm_sim_preferred_network_get_tuple             (const MMSimPreferredNetwork *self);
GVariant *mm_sim_preferred_network_list_get_variant      (const GList *preferred_network_list);
GList    *mm_sim_preferred_network_list_new_from_variant (GVariant *variant);
GList    *mm_sim_preferred_network_list_copy             (GList *preferred_network_list);
void      mm_sim_preferred_network_list_free             (GList *preferred_network_list);
#endif

G_END_DECLS

#endif /* MM_SIM_PREFERRED_NETWORK_H */
