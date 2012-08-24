/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MM_MODEM_H_
#define _MM_MODEM_H_

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-sim.h"
#include "mm-bearer.h"

G_BEGIN_DECLS

typedef MmGdbusModem     MMModem;
#define MM_TYPE_MODEM(o) MM_GDBUS_TYPE_MODEM (o)
#define MM_MODEM(o)      MM_GDBUS_MODEM(o)
#define MM_IS_MODEM(o)   MM_GDBUS_IS_MODEM(o)

const gchar *mm_modem_get_path (MMModem *self);
gchar       *mm_modem_dup_path (MMModem *self);

const gchar       *mm_modem_get_sim_path             (MMModem *self);
gchar             *mm_modem_dup_sim_path             (MMModem *self);
MMModemCapability  mm_modem_get_modem_capabilities   (MMModem *self);
MMModemCapability  mm_modem_get_current_capabilities (MMModem *self);
guint              mm_modem_get_max_bearers          (MMModem *self);
guint              mm_modem_get_max_active_bearers   (MMModem *self);
const gchar       *mm_modem_get_manufacturer         (MMModem *self);
gchar             *mm_modem_dup_manufacturer         (MMModem *self);
const gchar       *mm_modem_get_model                (MMModem *self);
gchar             *mm_modem_dup_model                (MMModem *self);
const gchar       *mm_modem_get_revision             (MMModem *self);
gchar             *mm_modem_dup_revision             (MMModem *self);
const gchar       *mm_modem_get_device_identifier    (MMModem *self);
gchar             *mm_modem_dup_device_identifier    (MMModem *self);
const gchar       *mm_modem_get_device               (MMModem *self);
gchar             *mm_modem_dup_device               (MMModem *self);
const gchar * const *mm_modem_get_drivers            (MMModem *self);
gchar            **mm_modem_dup_drivers              (MMModem *self);
const gchar       *mm_modem_get_plugin               (MMModem *self);
gchar             *mm_modem_dup_plugin               (MMModem *self);
const gchar       *mm_modem_get_equipment_identifier (MMModem *self);
gchar             *mm_modem_dup_equipment_identifier (MMModem *self);
const gchar *const *mm_modem_get_own_numbers          (MMModem *self);
gchar            **mm_modem_dup_own_numbers          (MMModem *self);
MMModemLock        mm_modem_get_unlock_required      (MMModem *self);
MMUnlockRetries   *mm_modem_get_unlock_retries       (MMModem *self);
MMModemState       mm_modem_get_state                (MMModem *self);
MMModemAccessTechnology mm_modem_get_access_technologies (MMModem *self);
guint              mm_modem_get_signal_quality       (MMModem *self,
                                                      gboolean *recent);
MMModemMode        mm_modem_get_supported_modes      (MMModem *self);
MMModemMode        mm_modem_get_allowed_modes        (MMModem *self);
MMModemMode        mm_modem_get_preferred_mode       (MMModem *self);
void               mm_modem_get_supported_bands      (MMModem *self,
                                                      MMModemBand **bands,
                                                      guint *n_bands);
void               mm_modem_get_bands                (MMModem *self,
                                                      MMModemBand **bands,
                                                      guint *n_bands);

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
                                        const gchar *arg_code,
                                        GCancellable *cancellable,
                                        GError **error);

void     mm_modem_command      (MMModem *self,
                                const gchar *cmd,
                                guint timeout,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gchar *mm_modem_command_finish (MMModem *self,
                                GAsyncResult *res,
                                GError **error);
gchar *mm_modem_command_sync   (MMModem *self,
                                const gchar *arg_cmd,
                                guint timeout,
                                GCancellable *cancellable,
                                GError **error);

void     mm_modem_set_allowed_modes        (MMModem *self,
                                            MMModemMode modes,
                                            MMModemMode preferred,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_modem_set_allowed_modes_finish (MMModem *self,
                                            GAsyncResult *res,
                                            GError **error);
gboolean mm_modem_set_allowed_modes_sync   (MMModem *self,
                                            MMModemMode modes,
                                            MMModemMode preferred,
                                            GCancellable *cancellable,
                                            GError **error);

void     mm_modem_set_bands        (MMModem *self,
                                    const MMModemBand *bands,
                                    guint n_bands,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean mm_modem_set_bands_finish (MMModem *self,
                                    GAsyncResult *res,
                                    GError **error);
gboolean mm_modem_set_bands_sync   (MMModem *self,
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

G_END_DECLS

#endif /* _MM_MODEM_H_ */
