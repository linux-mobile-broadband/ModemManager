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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_H_
#define _MM_MODEM_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-unlock-retries.h"
#include "mm-sim.h"
#include "mm-bearer.h"
#include "mm-helper-types.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM            (mm_modem_get_type ())
#define MM_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM, MMModem))
#define MM_MODEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM, MMModemClass))
#define MM_IS_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM))
#define MM_IS_MODEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM))
#define MM_MODEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM, MMModemClass))

typedef struct _MMModem MMModem;
typedef struct _MMModemClass MMModemClass;
typedef struct _MMModemPrivate MMModemPrivate;

/**
 * MMModem:
 *
 * The #MMModem structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModem {
    /*< private >*/
    MmGdbusModemProxy parent;
    MMModemPrivate *priv;
};

struct _MMModemClass {
    /*< private >*/
    MmGdbusModemProxyClass parent;
};

GType mm_modem_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModem, g_object_unref)

const gchar *mm_modem_get_path (MMModem *self);
gchar       *mm_modem_dup_path (MMModem *self);

const gchar       *mm_modem_get_sim_path             (MMModem *self);
gchar             *mm_modem_dup_sim_path             (MMModem *self);

const gchar * const *mm_modem_get_sim_slot_paths (MMModem *self);
gchar              **mm_modem_dup_sim_slot_paths (MMModem *self);

guint              mm_modem_get_primary_sim_slot (MMModem *self);

gboolean           mm_modem_peek_supported_capabilities (MMModem *self,
                                                         const MMModemCapability **capabilities,
                                                         guint *n_capabilities);
gboolean           mm_modem_get_supported_capabilities (MMModem *self,
                                                        MMModemCapability **capabilities,
                                                        guint *n_capabilities);

MMModemCapability  mm_modem_get_current_capabilities (MMModem *self);

guint              mm_modem_get_max_active_bearers             (MMModem *self);
guint              mm_modem_get_max_active_multiplexed_bearers (MMModem *self);

const gchar * const *mm_modem_get_bearer_paths       (MMModem *self);
gchar              **mm_modem_dup_bearer_paths       (MMModem *self);

const gchar       *mm_modem_get_manufacturer         (MMModem *self);
gchar             *mm_modem_dup_manufacturer         (MMModem *self);

const gchar       *mm_modem_get_model                (MMModem *self);
gchar             *mm_modem_dup_model                (MMModem *self);

const gchar       *mm_modem_get_revision             (MMModem *self);
gchar             *mm_modem_dup_revision             (MMModem *self);

const gchar       *mm_modem_get_carrier_configuration          (MMModem *self);
gchar             *mm_modem_dup_carrier_configuration          (MMModem *self);
const gchar       *mm_modem_get_carrier_configuration_revision (MMModem *self);
gchar             *mm_modem_dup_carrier_configuration_revision (MMModem *self);

const gchar       *mm_modem_get_hardware_revision    (MMModem *self);
gchar             *mm_modem_dup_hardware_revision    (MMModem *self);

const gchar       *mm_modem_get_device_identifier    (MMModem *self);
gchar             *mm_modem_dup_device_identifier    (MMModem *self);

const gchar       *mm_modem_get_device               (MMModem *self);
gchar             *mm_modem_dup_device               (MMModem *self);

const gchar * const  *mm_modem_get_drivers           (MMModem *self);
gchar               **mm_modem_dup_drivers           (MMModem *self);

const gchar       *mm_modem_get_plugin               (MMModem *self);
gchar             *mm_modem_dup_plugin               (MMModem *self);

const gchar       *mm_modem_get_primary_port         (MMModem *self);
gchar             *mm_modem_dup_primary_port         (MMModem *self);

gboolean           mm_modem_peek_ports               (MMModem *self,
                                                      const MMModemPortInfo **ports,
                                                      guint *n_ports);
gboolean           mm_modem_get_ports                (MMModem *self,
                                                      MMModemPortInfo **ports,
                                                      guint *n_ports);

const gchar       *mm_modem_get_equipment_identifier (MMModem *self);
gchar             *mm_modem_dup_equipment_identifier (MMModem *self);

const gchar *const  *mm_modem_get_own_numbers        (MMModem *self);
gchar              **mm_modem_dup_own_numbers        (MMModem *self);

MMModemLock        mm_modem_get_unlock_required      (MMModem *self);

MMUnlockRetries   *mm_modem_get_unlock_retries       (MMModem *self);
MMUnlockRetries   *mm_modem_peek_unlock_retries      (MMModem *self);

MMModemState       mm_modem_get_state                (MMModem *self);

MMModemStateFailedReason mm_modem_get_state_failed_reason (MMModem *self);

MMModemPowerState  mm_modem_get_power_state          (MMModem *self);

MMModemAccessTechnology mm_modem_get_access_technologies (MMModem *self);

guint              mm_modem_get_signal_quality       (MMModem *self,
                                                      gboolean *recent);

gboolean           mm_modem_peek_supported_modes     (MMModem *self,
                                                      const MMModemModeCombination **modes,
                                                      guint *n_modes);
gboolean           mm_modem_get_supported_modes      (MMModem *self,
                                                      MMModemModeCombination **modes,
                                                      guint *n_modes);

gboolean           mm_modem_get_current_modes        (MMModem *self,
                                                      MMModemMode *allowed,
                                                      MMModemMode *preferred);

gboolean           mm_modem_peek_supported_bands     (MMModem *self,
                                                      const MMModemBand **bands,
                                                      guint *n_bands);
gboolean           mm_modem_get_supported_bands      (MMModem *self,
                                                      MMModemBand **bands,
                                                      guint *n_bands);

gboolean           mm_modem_peek_current_bands       (MMModem *self,
                                                      const MMModemBand **bands,
                                                      guint *n_bands);
gboolean           mm_modem_get_current_bands        (MMModem *self,
                                                      MMModemBand **bands,
                                                      guint *n_bands);

MMBearerIpFamily   mm_modem_get_supported_ip_families (MMModem *self);

void     mm_modem_enable        (MMModem *self,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
gboolean mm_modem_enable_finish (MMModem *self,
                                 GAsyncResult *res,
                                 GError **error);
gboolean mm_modem_enable_sync   (MMModem *self,
                                 GCancellable *cancellable,
                                 GError **error);

void     mm_modem_disable        (MMModem *self,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
gboolean mm_modem_disable_finish (MMModem *self,
                                  GAsyncResult *res,
                                  GError **error);
gboolean mm_modem_disable_sync   (MMModem *self,
                                  GCancellable *cancellable,
                                  GError **error);

void     mm_modem_list_bearers        (MMModem *self,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GList   *mm_modem_list_bearers_finish (MMModem *self,
                                       GAsyncResult *res,
                                       GError **error);
GList   *mm_modem_list_bearers_sync   (MMModem *self,
                                       GCancellable *cancellable,
                                       GError **error);

void     mm_modem_create_bearer         (MMModem *self,
                                         MMBearerProperties *properties,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
MMBearer *mm_modem_create_bearer_finish (MMModem *self,
                                         GAsyncResult *res,
                                         GError **error);
MMBearer *mm_modem_create_bearer_sync   (MMModem *self,
                                         MMBearerProperties *properties,
                                         GCancellable *cancellable,
                                         GError **error);

void     mm_modem_delete_bearer        (MMModem *self,
                                        const gchar *bearer,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_modem_delete_bearer_finish (MMModem *self,
                                        GAsyncResult *res,
                                        GError **error);
gboolean mm_modem_delete_bearer_sync   (MMModem *self,
                                        const gchar *bearer,
                                        GCancellable *cancellable,
                                        GError **error);

void     mm_modem_reset        (MMModem *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean mm_modem_reset_finish (MMModem *self,
                                GAsyncResult *res,
                                GError **error);
gboolean mm_modem_reset_sync   (MMModem *self,
                                GCancellable *cancellable,
                                GError **error);

void     mm_modem_factory_reset        (MMModem *self,
                                        const gchar *code,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_modem_factory_reset_finish (MMModem *self,
                                        GAsyncResult *res,
                                        GError **error);
gboolean mm_modem_factory_reset_sync   (MMModem *self,
                                        const gchar *code,
                                        GCancellable *cancellable,
                                        GError **error);

void      mm_modem_command        (MMModem *self,
                                   const gchar *cmd,
                                   guint timeout,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gchar    *mm_modem_command_finish (MMModem *self,
                                   GAsyncResult *res,
                                   GError **error);
gchar    *mm_modem_command_sync   (MMModem *self,
                                   const gchar *cmd,
                                   guint timeout,
                                   GCancellable *cancellable,
                                   GError **error);

void     mm_modem_set_power_state        (MMModem *self,
                                          MMModemPowerState state,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);
gboolean mm_modem_set_power_state_finish (MMModem *self,
                                          GAsyncResult *res,
                                          GError **error);
gboolean mm_modem_set_power_state_sync   (MMModem *self,
                                          MMModemPowerState state,
                                          GCancellable *cancellable,
                                          GError **error);

void     mm_modem_set_current_capabilities        (MMModem *self,
                                                   MMModemCapability capabilities,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
gboolean mm_modem_set_current_capabilities_finish (MMModem *self,
                                                   GAsyncResult *res,
                                                   GError **error);
gboolean mm_modem_set_current_capabilities_sync   (MMModem *self,
                                                   MMModemCapability capabilities,
                                                   GCancellable *cancellable,
                                                   GError **error);

void     mm_modem_set_current_modes        (MMModem *self,
                                            MMModemMode modes,
                                            MMModemMode preferred,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_modem_set_current_modes_finish (MMModem *self,
                                            GAsyncResult *res,
                                            GError **error);
gboolean mm_modem_set_current_modes_sync   (MMModem *self,
                                            MMModemMode modes,
                                            MMModemMode preferred,
                                            GCancellable *cancellable,
                                            GError **error);

void     mm_modem_set_current_bands        (MMModem *self,
                                            const MMModemBand *bands,
                                            guint n_bands,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_modem_set_current_bands_finish (MMModem *self,
                                            GAsyncResult *res,
                                            GError **error);
gboolean mm_modem_set_current_bands_sync   (MMModem *self,
                                            const MMModemBand *bands,
                                            guint n_bands,
                                            GCancellable *cancellable,
                                            GError **error);

void   mm_modem_get_sim        (MMModem *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
MMSim *mm_modem_get_sim_finish (MMModem *self,
                                GAsyncResult *res,
                                GError **error);
MMSim *mm_modem_get_sim_sync   (MMModem *self,
                                GCancellable *cancellable,
                                GError **error);

void       mm_modem_list_sim_slots        (MMModem              *self,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
GPtrArray *mm_modem_list_sim_slots_finish (MMModem              *self,
                                           GAsyncResult         *res,
                                           GError              **error);
GPtrArray *mm_modem_list_sim_slots_sync   (MMModem              *self,
                                           GCancellable         *cancellable,
                                           GError              **error);

void     mm_modem_set_primary_sim_slot        (MMModem              *self,
                                               guint                 sim_slot,
                                               GCancellable         *cancellable,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data);
gboolean mm_modem_set_primary_sim_slot_finish (MMModem              *self,
                                               GAsyncResult         *res,
                                               GError              **error);
gboolean mm_modem_set_primary_sim_slot_sync   (MMModem              *self,
                                               guint                 sim_slot,
                                               GCancellable         *cancellable,
                                               GError              **error);

G_END_DECLS

#endif /* _MM_MODEM_H_ */
