/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <glib.h>

#ifndef __MMCLI_H__
#define __MMCLI_H__

/* Common */
void          mmcli_async_operation_done    (void);
void          mmcli_force_async_operation   (void);
void          mmcli_force_sync_operation    (void);
void          mmcli_force_operation_timeout (GDBusProxy *proxy);

/* Manager group */
GOptionGroup *mmcli_manager_get_option_group (void);
gboolean      mmcli_manager_options_enabled  (void);
void          mmcli_manager_run_asynchronous (GDBusConnection *connection,
                                              GCancellable    *cancellable);
void          mmcli_manager_run_synchronous  (GDBusConnection *connection);
void          mmcli_manager_shutdown         (void);

/* Modem group */
GOptionGroup *mmcli_modem_get_option_group   (void);
gboolean      mmcli_modem_options_enabled    (void);
void          mmcli_modem_run_asynchronous   (GDBusConnection *connection,
                                              GCancellable    *cancellable);
void          mmcli_modem_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_shutdown           (void);

/* 3GPP group */
GOptionGroup *mmcli_modem_3gpp_get_option_group   (void);
gboolean      mmcli_modem_3gpp_options_enabled    (void);
void          mmcli_modem_3gpp_run_asynchronous   (GDBusConnection *connection,
                                                   GCancellable    *cancellable);
void          mmcli_modem_3gpp_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_3gpp_shutdown           (void);

/* CDMA group */
GOptionGroup *mmcli_modem_cdma_get_option_group   (void);
gboolean      mmcli_modem_cdma_options_enabled    (void);
void          mmcli_modem_cdma_run_asynchronous   (GDBusConnection *connection,
                                                   GCancellable    *cancellable);
void          mmcli_modem_cdma_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_cdma_shutdown           (void);

/* Simple group */
GOptionGroup *mmcli_modem_simple_get_option_group   (void);
gboolean      mmcli_modem_simple_options_enabled    (void);
void          mmcli_modem_simple_run_asynchronous   (GDBusConnection *connection,
                                                     GCancellable    *cancellable);
void          mmcli_modem_simple_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_simple_shutdown           (void);

/* Location group */
GOptionGroup *mmcli_modem_location_get_option_group   (void);
gboolean      mmcli_modem_location_options_enabled    (void);
void          mmcli_modem_location_run_asynchronous   (GDBusConnection *connection,
                                                       GCancellable    *cancellable);
void          mmcli_modem_location_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_location_shutdown           (void);

/* Messaging group */
GOptionGroup *mmcli_modem_messaging_get_option_group   (void);
gboolean      mmcli_modem_messaging_options_enabled    (void);
void          mmcli_modem_messaging_run_asynchronous   (GDBusConnection *connection,
                                                        GCancellable    *cancellable);
void          mmcli_modem_messaging_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_messaging_shutdown           (void);

/* Voice group */
GOptionGroup *mmcli_modem_voice_get_option_group   (void);
gboolean      mmcli_modem_voice_options_enabled    (void);
void          mmcli_modem_voice_run_asynchronous   (GDBusConnection *connection,
                                                        GCancellable    *cancellable);
void          mmcli_modem_voice_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_voice_shutdown           (void);

/* Time group */
GOptionGroup *mmcli_modem_time_get_option_group   (void);
gboolean      mmcli_modem_time_options_enabled    (void);
void          mmcli_modem_time_run_asynchronous   (GDBusConnection *connection,
                                                   GCancellable    *cancellable);
void          mmcli_modem_time_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_time_shutdown           (void);

/* Firmware group */
GOptionGroup *mmcli_modem_firmware_get_option_group   (void);
gboolean      mmcli_modem_firmware_options_enabled    (void);
void          mmcli_modem_firmware_run_asynchronous   (GDBusConnection *connection,
                                                       GCancellable    *cancellable);
void          mmcli_modem_firmware_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_firmware_shutdown           (void);

/* Signal group */
GOptionGroup *mmcli_modem_signal_get_option_group   (void);
gboolean      mmcli_modem_signal_options_enabled    (void);
void          mmcli_modem_signal_run_asynchronous   (GDBusConnection *connection,
                                                     GCancellable    *cancellable);
void          mmcli_modem_signal_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_signal_shutdown           (void);

/* Oma group */
GOptionGroup *mmcli_modem_oma_get_option_group   (void);
gboolean      mmcli_modem_oma_options_enabled    (void);
void          mmcli_modem_oma_run_asynchronous   (GDBusConnection *connection,
                                                  GCancellable    *cancellable);
void          mmcli_modem_oma_run_synchronous    (GDBusConnection *connection);
void          mmcli_modem_oma_shutdown           (void);

/* Bearer group */
GOptionGroup *mmcli_bearer_get_option_group   (void);
gboolean      mmcli_bearer_options_enabled    (void);
void          mmcli_bearer_run_asynchronous   (GDBusConnection *connection,
                                              GCancellable    *cancellable);
void          mmcli_bearer_run_synchronous    (GDBusConnection *connection);
void          mmcli_bearer_shutdown           (void);

/* SIM group */
GOptionGroup *mmcli_sim_get_option_group   (void);
gboolean      mmcli_sim_options_enabled    (void);
void          mmcli_sim_run_asynchronous   (GDBusConnection *connection,
                                            GCancellable    *cancellable);
void          mmcli_sim_run_synchronous    (GDBusConnection *connection);
void          mmcli_sim_shutdown           (void);

/* SMS group */
GOptionGroup *mmcli_sms_get_option_group   (void);
gboolean      mmcli_sms_options_enabled    (void);
void          mmcli_sms_run_asynchronous   (GDBusConnection *connection,
                                            GCancellable    *cancellable);
void          mmcli_sms_run_synchronous    (GDBusConnection *connection);
void          mmcli_sms_shutdown           (void);

/* Call group */
GOptionGroup *mmcli_call_get_option_group   (void);
gboolean      mmcli_call_options_enabled    (void);
void          mmcli_call_run_asynchronous   (GDBusConnection *connection,
                                            GCancellable    *cancellable);
void          mmcli_call_run_synchronous    (GDBusConnection *connection);
void          mmcli_call_shutdown           (void);

#endif /* __MMCLI_H__ */
