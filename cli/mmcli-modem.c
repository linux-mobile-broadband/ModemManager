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
 * Copyright (C) 2011-2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"
#include "mmcli-output.h"

/* Context */
typedef struct {
    GDBusConnection *connection;
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModem *modem;
    MMModem3gpp *modem_3gpp;
    MMModemCdma *modem_cdma;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */
static gboolean monitor_state_flag;
static gboolean enable_flag;
static gboolean disable_flag;
static gboolean set_power_state_on_flag;
static gboolean set_power_state_low_flag;
static gboolean set_power_state_off_flag;
static gboolean reset_flag;
static gchar *factory_reset_str;
static gchar *command_str;
static gchar *create_bearer_str;
static gchar *delete_bearer_str;
static gchar *set_current_capabilities_str;
static gchar *set_allowed_modes_str;
static gchar *set_preferred_mode_str;
static gchar *set_current_bands_str;
static gint set_primary_sim_slot_int;
static gboolean inhibit_flag;

static GOptionEntry entries[] = {
    { "monitor-state", 'w', 0, G_OPTION_ARG_NONE, &monitor_state_flag,
      "Monitor state of a given modem",
      NULL
    },
    { "enable", 'e', 0, G_OPTION_ARG_NONE, &enable_flag,
      "Enable a given modem",
      NULL
    },
    { "disable", 'd', 0, G_OPTION_ARG_NONE, &disable_flag,
      "Disable a given modem",
      NULL
    },
    { "set-power-state-on", 0, 0, G_OPTION_ARG_NONE, &set_power_state_on_flag,
      "Set full power state in the modem",
      NULL
    },
    { "set-power-state-low", 0, 0, G_OPTION_ARG_NONE, &set_power_state_low_flag,
      "Set low power state in the modem",
      NULL
    },
    { "set-power-state-off", 0, 0, G_OPTION_ARG_NONE, &set_power_state_off_flag,
      "Power off the modem",
      NULL
    },
    { "reset", 'r', 0, G_OPTION_ARG_NONE, &reset_flag,
      "Reset a given modem",
      NULL
    },
    { "factory-reset", 0, 0, G_OPTION_ARG_STRING, &factory_reset_str,
      "Reset a given modem to its factory state",
      "[CODE]"
    },
    { "command", 0, 0, G_OPTION_ARG_STRING, &command_str,
      "Send an AT command to the modem",
      "[COMMAND]"
    },
    { "create-bearer", 0, 0, G_OPTION_ARG_STRING, &create_bearer_str,
      "Create a new packet data bearer in a given modem",
      "[\"key=value,...\"]"
    },
    { "delete-bearer", 0, 0, G_OPTION_ARG_STRING, &delete_bearer_str,
      "Delete a data bearer from a given modem",
      "[PATH|INDEX]"
    },
    { "set-current-capabilities", 0, 0, G_OPTION_ARG_STRING, &set_current_capabilities_str,
      "Set current modem capabilities.",
      "[CAPABILITY1|CAPABILITY2...]"
    },
    { "set-allowed-modes", 0, 0, G_OPTION_ARG_STRING, &set_allowed_modes_str,
      "Set allowed modes in a given modem.",
      "[MODE1|MODE2...]"
    },
    { "set-preferred-mode", 0, 0, G_OPTION_ARG_STRING, &set_preferred_mode_str,
      "Set preferred mode in a given modem (Must give allowed modes with --set-allowed-modes)",
      "[MODE]"
    },
    { "set-current-bands", 0, 0, G_OPTION_ARG_STRING, &set_current_bands_str,
      "Set bands to be used by a given modem.",
      "[BAND1|BAND2...]"
    },
    { "set-primary-sim-slot", 0, 0, G_OPTION_ARG_INT, &set_primary_sim_slot_int,
      "Switch to the selected SIM slot",
      "[SLOT NUMBER]"
    },
    { "inhibit", 0, 0, G_OPTION_ARG_NONE, &inhibit_flag,
      "Inhibit the modem",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_get_option_group (void)
{
    GOptionGroup *group;

    /* Status options */
    group = g_option_group_new ("modem",
                                "Modem options:",
                                "Show modem options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (monitor_state_flag +
                 enable_flag +
                 disable_flag +
                 set_power_state_on_flag +
                 set_power_state_low_flag +
                 set_power_state_off_flag +
                 reset_flag +
                 !!create_bearer_str +
                 !!delete_bearer_str +
                 !!factory_reset_str +
                 !!command_str +
                 !!set_current_capabilities_str +
                 !!set_allowed_modes_str +
                 !!set_preferred_mode_str +
                 !!set_current_bands_str +
                 (set_primary_sim_slot_int > 0) +
                 inhibit_flag);

    if (n_actions == 0 && mmcli_get_common_modem_string ()) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
    }

    if (set_preferred_mode_str) {
        if (!set_allowed_modes_str) {
            g_printerr ("error: setting preferred mode requires list of allowed modes\n");
            exit (EXIT_FAILURE);
        }
        n_actions--;
    }

    if (n_actions > 1) {
        g_printerr ("error: too many modem actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (monitor_state_flag || inhibit_flag)
        mmcli_force_async_operation ();

    if (info_flag)
        mmcli_force_sync_operation ();

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (void)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->modem)
        g_object_unref (ctx->modem);
    if (ctx->modem_3gpp)
        g_object_unref (ctx->modem_3gpp);
    if (ctx->modem_cdma)
        g_object_unref (ctx->modem_cdma);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->connection)
        g_object_unref (ctx->connection);
    g_free (ctx);
}

void
mmcli_modem_shutdown (void)
{
    context_free ();
}

static void
inhibition_cancelled (GCancellable *cancellable,
                      const gchar  *uid)
{
    GError *error = NULL;

    if (!mm_manager_uninhibit_device_sync (ctx->manager, uid, NULL, &error)) {
        g_printerr ("error: couldn't uninhibit device: '%s'\n",
                    error ? error->message : "unknown error");
    } else
        g_print ("successfully uninhibited device with uid '%s'\n", uid);

    mmcli_async_operation_done ();
}

static void
inhibit_device_ready (MMManager    *manager,
                      GAsyncResult *result,
                      gchar        *uid)
{
    GError *error = NULL;

    if (!mm_manager_inhibit_device_finish (manager, result, &error)) {
        g_printerr ("error: couldn't inhibit device: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully inhibited device with uid '%s'\n", uid);
    g_print ("type Ctrl+C to abort this program and remove the inhibition\n");

    g_cancellable_connect (ctx->cancellable,
                           G_CALLBACK (inhibition_cancelled),
                           uid, g_free);
}

static void
cancelled (GCancellable *cancellable)
{
    mmcli_async_operation_done ();
}

static void
print_bearer_short_info (MMBearer *bearer)
{
    g_print ("\t%s\n",
             mm_bearer_get_path (bearer));
}

static void
print_modem_info (void)
{
    gchar *supported_capabilities_string;
    MMModemCapability *capabilities = NULL;
    guint n_capabilities = 0;
    gchar *current_capabilities_string;
    gchar *access_technologies_string;
    MMModemModeCombination *modes = NULL;
    guint n_modes = 0;
    gchar *supported_modes_string;
    MMModemMode allowed_modes;
    gchar *allowed_modes_string = NULL;
    MMModemMode preferred_mode;
    gchar *preferred_mode_string = NULL;
    gchar *supported_bands_string;
    gchar *current_bands_string;
    gchar *supported_ip_families_string;
    gchar *unlock_retries_string;
    MMModemBand *bands = NULL;
    guint n_bands = 0;
    MMModemPortInfo *ports = NULL;
    guint n_ports = 0;
    gchar *ports_string;
    MMUnlockRetries *unlock_retries;
    guint signal_quality = 0;
    gboolean signal_quality_recent = FALSE;
    const gchar *sim_path;
    const gchar **bearer_paths;

    /* Strings in heap */
    mm_modem_get_supported_capabilities (ctx->modem, &capabilities, &n_capabilities);
    supported_capabilities_string = mm_common_build_capabilities_string (capabilities, n_capabilities);
    g_free (capabilities);
    current_capabilities_string = mm_modem_capability_build_string_from_mask (
        mm_modem_get_current_capabilities (ctx->modem));
    access_technologies_string = mm_modem_access_technology_build_string_from_mask (
        mm_modem_get_access_technologies (ctx->modem));
    mm_modem_get_supported_modes (ctx->modem, &modes, &n_modes);
    supported_modes_string = mm_common_build_mode_combinations_string (modes, n_modes);
    g_free (modes);
    mm_modem_get_current_bands (ctx->modem, &bands, &n_bands);
    current_bands_string = mm_common_build_bands_string (bands, n_bands);
    g_free (bands);
    mm_modem_get_supported_bands (ctx->modem, &bands, &n_bands);
    supported_bands_string = mm_common_build_bands_string (bands, n_bands);
    g_free (bands);
    mm_modem_get_ports (ctx->modem, &ports, &n_ports);
    ports_string = mm_common_build_ports_string (ports, n_ports);
    mm_modem_port_info_array_free (ports, n_ports);
    if (mm_modem_get_current_modes (ctx->modem, &allowed_modes, &preferred_mode)) {
        allowed_modes_string = mm_modem_mode_build_string_from_mask (allowed_modes);
        preferred_mode_string = mm_modem_mode_build_string_from_mask (preferred_mode);
    }
    supported_ip_families_string = mm_bearer_ip_family_build_string_from_mask (
        mm_modem_get_supported_ip_families (ctx->modem));

    unlock_retries = mm_modem_get_unlock_retries (ctx->modem);
    unlock_retries_string = mm_unlock_retries_build_string (unlock_retries);
    g_object_unref (unlock_retries);

    signal_quality = mm_modem_get_signal_quality (ctx->modem, &signal_quality_recent);

    mmcli_output_string           (MMC_F_GENERAL_DBUS_PATH,               mm_modem_get_path (ctx->modem));
    mmcli_output_string           (MMC_F_GENERAL_DEVICE_ID,               mm_modem_get_device_identifier (ctx->modem));

    mmcli_output_string           (MMC_F_HARDWARE_MANUFACTURER,           mm_modem_get_manufacturer (ctx->modem));
    mmcli_output_string           (MMC_F_HARDWARE_MODEL,                  mm_modem_get_model (ctx->modem));
    mmcli_output_string           (MMC_F_HARDWARE_REVISION,               mm_modem_get_revision (ctx->modem));
    mmcli_output_string           (MMC_F_HARDWARE_CARRIER_CONF,           mm_modem_get_carrier_configuration (ctx->modem));
    mmcli_output_string           (MMC_F_HARDWARE_CARRIER_CONF_REV,       mm_modem_get_carrier_configuration_revision (ctx->modem));
    mmcli_output_string           (MMC_F_HARDWARE_HW_REVISION,            mm_modem_get_hardware_revision (ctx->modem));
    mmcli_output_string_multiline (MMC_F_HARDWARE_SUPPORTED_CAPABILITIES, supported_capabilities_string);
    mmcli_output_string_multiline (MMC_F_HARDWARE_CURRENT_CAPABILITIES,   current_capabilities_string);
    mmcli_output_string           (MMC_F_HARDWARE_EQUIPMENT_ID,           mm_modem_get_equipment_identifier (ctx->modem));

    mmcli_output_string           (MMC_F_SYSTEM_DEVICE,                   mm_modem_get_device (ctx->modem));
    mmcli_output_string_array     (MMC_F_SYSTEM_DRIVERS,                  (const gchar **) mm_modem_get_drivers (ctx->modem), FALSE);
    mmcli_output_string           (MMC_F_SYSTEM_PLUGIN,                   mm_modem_get_plugin (ctx->modem));
    mmcli_output_string           (MMC_F_SYSTEM_PRIMARY_PORT,             mm_modem_get_primary_port (ctx->modem));
    mmcli_output_string_list      (MMC_F_SYSTEM_PORTS,                    ports_string);

    mmcli_output_string_array     (MMC_F_NUMBERS_OWN,                     (const gchar **) mm_modem_get_own_numbers (ctx->modem), FALSE);

    mmcli_output_string           (MMC_F_STATUS_LOCK,                     mm_modem_lock_get_string (mm_modem_get_unlock_required (ctx->modem)));
    mmcli_output_string_list      (MMC_F_STATUS_UNLOCK_RETRIES,           unlock_retries_string);
    mmcli_output_state            (mm_modem_get_state (ctx->modem), mm_modem_get_state_failed_reason (ctx->modem));
    mmcli_output_string           (MMC_F_STATUS_POWER_STATE,              mm_modem_power_state_get_string (mm_modem_get_power_state (ctx->modem)));
    mmcli_output_string_list      (MMC_F_STATUS_ACCESS_TECH,              access_technologies_string);
    mmcli_output_signal_quality   (signal_quality, signal_quality_recent);

    mmcli_output_string_multiline (MMC_F_MODES_SUPPORTED,                 supported_modes_string);
    mmcli_output_string_take      (MMC_F_MODES_CURRENT,                   g_strdup_printf ("allowed: %s; preferred: %s",
                                                                                           allowed_modes_string, preferred_mode_string));

    mmcli_output_string_list      (MMC_F_BANDS_SUPPORTED,                 supported_bands_string);
    mmcli_output_string_list      (MMC_F_BANDS_CURRENT,                   current_bands_string);

    mmcli_output_string_list      (MMC_F_IP_SUPPORTED,                    supported_ip_families_string);

    /* 3GPP */
    {
        const gchar *imei = NULL;
        gchar       *facility_locks = NULL;
        const gchar *operator_code = NULL;
        const gchar *operator_name = NULL;
        const gchar *registration = NULL;
        const gchar *eps_ue_mode = NULL;
        GList       *pco_list = NULL;
        const gchar *initial_eps_bearer_path = NULL;
        const gchar *initial_eps_bearer_apn = NULL;
        gchar       *initial_eps_bearer_ip_family_str = NULL;
        const gchar *initial_eps_bearer_user = NULL;
        const gchar *initial_eps_bearer_password = NULL;

        if (ctx->modem_3gpp) {
            imei = mm_modem_3gpp_get_imei (ctx->modem_3gpp);
            facility_locks = mm_modem_3gpp_facility_build_string_from_mask (mm_modem_3gpp_get_enabled_facility_locks (ctx->modem_3gpp));
            operator_code = mm_modem_3gpp_get_operator_code (ctx->modem_3gpp);
            operator_name = mm_modem_3gpp_get_operator_name (ctx->modem_3gpp);
            registration = mm_modem_3gpp_registration_state_get_string (mm_modem_3gpp_get_registration_state (ctx->modem_3gpp));
            eps_ue_mode = mm_modem_3gpp_eps_ue_mode_operation_get_string (mm_modem_3gpp_get_eps_ue_mode_operation (ctx->modem_3gpp));
            pco_list = mm_modem_3gpp_get_pco (ctx->modem_3gpp);
            initial_eps_bearer_path = mm_modem_3gpp_get_initial_eps_bearer_path (ctx->modem_3gpp);

            if (mm_modem_get_current_capabilities (ctx->modem) & (MM_MODEM_CAPABILITY_LTE)) {
                MMBearerProperties *initial_eps_bearer_properties;

                initial_eps_bearer_properties = mm_modem_3gpp_peek_initial_eps_bearer_settings (ctx->modem_3gpp);
                if (initial_eps_bearer_properties) {
                    initial_eps_bearer_apn           = mm_bearer_properties_get_apn (initial_eps_bearer_properties);
                    initial_eps_bearer_ip_family_str = mm_bearer_ip_family_build_string_from_mask (mm_bearer_properties_get_ip_type (initial_eps_bearer_properties));
                    initial_eps_bearer_user          = mm_bearer_properties_get_user (initial_eps_bearer_properties);
                    initial_eps_bearer_password      = mm_bearer_properties_get_password (initial_eps_bearer_properties);
                }
            }
        }

        mmcli_output_string      (MMC_F_3GPP_IMEI,          imei);
        mmcli_output_string_list (MMC_F_3GPP_ENABLED_LOCKS, facility_locks);
        mmcli_output_string      (MMC_F_3GPP_OPERATOR_ID,   operator_code);
        mmcli_output_string      (MMC_F_3GPP_OPERATOR_NAME, operator_name);
        mmcli_output_string      (MMC_F_3GPP_REGISTRATION,  registration);
        mmcli_output_string      (MMC_F_3GPP_EPS_UE_MODE,   eps_ue_mode);
        mmcli_output_string      (MMC_F_3GPP_EPS_INITIAL_BEARER_PATH,      g_strcmp0 (initial_eps_bearer_path, "/") != 0 ? initial_eps_bearer_path : NULL);
        mmcli_output_string      (MMC_F_3GPP_EPS_BEARER_SETTINGS_APN,      initial_eps_bearer_apn);
        mmcli_output_string_take (MMC_F_3GPP_EPS_BEARER_SETTINGS_IP_TYPE,  initial_eps_bearer_ip_family_str);
        mmcli_output_string      (MMC_F_3GPP_EPS_BEARER_SETTINGS_USER,     initial_eps_bearer_user);
        mmcli_output_string      (MMC_F_3GPP_EPS_BEARER_SETTINGS_PASSWORD, initial_eps_bearer_password);
        mmcli_output_pco_list    (pco_list);

        g_free (facility_locks);
        g_list_free_full (pco_list, g_object_unref);
    }

    /* CDMA */
    {
        const gchar *meid = NULL;
        const gchar *esn = NULL;
        gchar       *sid = NULL;
        gchar       *nid = NULL;
        const gchar *registration_cdma1x = NULL;
        const gchar *registration_evdo = NULL;
        const gchar *activation = NULL;

        if (ctx->modem_cdma) {
            guint sid_n;
            guint nid_n;

            meid = mm_modem_cdma_get_meid (ctx->modem_cdma);
            esn  = mm_modem_cdma_get_esn (ctx->modem_cdma);
            sid_n = mm_modem_cdma_get_sid (ctx->modem_cdma);
            if (sid_n != MM_MODEM_CDMA_SID_UNKNOWN)
                sid = g_strdup_printf ("%u", sid_n);
            nid_n = mm_modem_cdma_get_nid (ctx->modem_cdma);
            if (nid_n != MM_MODEM_CDMA_NID_UNKNOWN)
                nid = g_strdup_printf ("%u", nid_n);
            registration_cdma1x = mm_modem_cdma_registration_state_get_string (mm_modem_cdma_get_cdma1x_registration_state (ctx->modem_cdma));
            registration_evdo = mm_modem_cdma_registration_state_get_string (mm_modem_cdma_get_evdo_registration_state (ctx->modem_cdma));
            activation = mm_modem_cdma_activation_state_get_string (mm_modem_cdma_get_activation_state (ctx->modem_cdma));
        }

        mmcli_output_string      (MMC_F_CDMA_MEID,                meid);
        mmcli_output_string      (MMC_F_CDMA_ESN,                 esn);
        mmcli_output_string_take (MMC_F_CDMA_SID,                 sid);
        mmcli_output_string_take (MMC_F_CDMA_NID,                 nid);
        mmcli_output_string      (MMC_F_CDMA_REGISTRATION_CDMA1X, registration_cdma1x);
        mmcli_output_string      (MMC_F_CDMA_REGISTRATION_EVDO,   registration_evdo);
        mmcli_output_string      (MMC_F_CDMA_ACTIVATION,          activation);
    }

    sim_path = mm_modem_get_sim_path (ctx->modem);
    mmcli_output_string (MMC_F_SIM_PATH, g_strcmp0 (sim_path, "/") != 0 ? sim_path : NULL);
    mmcli_output_sim_slots (mm_modem_dup_sim_slot_paths (ctx->modem),
                            mm_modem_get_primary_sim_slot (ctx->modem));

    bearer_paths = (const gchar **) mm_modem_get_bearer_paths (ctx->modem);
    mmcli_output_string_array (MMC_F_BEARER_PATHS, (bearer_paths && bearer_paths[0]) ? bearer_paths : NULL, TRUE);

    mmcli_output_dump ();

    g_free (ports_string);
    g_free (supported_ip_families_string);
    g_free (current_bands_string);
    g_free (supported_bands_string);
    g_free (access_technologies_string);
    g_free (supported_capabilities_string);
    g_free (current_capabilities_string);
    g_free (allowed_modes_string);
    g_free (preferred_mode_string);
    g_free (supported_modes_string);
    g_free (unlock_retries_string);
}

static void
enable_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't enable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully enabled the modem\n");
}

static void
enable_ready (MMModem      *modem,
              GAsyncResult *result,
              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_enable_finish (modem, result, &error);
    enable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disable_process_reply (gboolean      result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disabled the modem\n");
}

static void
disable_ready (MMModem      *modem,
               GAsyncResult *result,
               gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_disable_finish (modem, result, &error);
    disable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
set_power_state_process_reply (gboolean      result,
                               const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set new power state in the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set new power state in the modem\n");
}

static void
set_power_state_ready (MMModem      *modem,
                       GAsyncResult *result,
                       gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_set_power_state_finish (modem, result, &error);
    set_power_state_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
reset_process_reply (gboolean      result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't reset the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully reseted the modem\n");
}

static void
reset_ready (MMModem      *modem,
             GAsyncResult *result,
             gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_reset_finish (modem, result, &error);
    reset_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
factory_reset_process_reply (gboolean      result,
                             const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't reset the modem to factory state: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully reseted the modem to factory state\n");
}

static void
factory_reset_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_factory_reset_finish (modem, result, &error);
    factory_reset_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}


static void
command_process_reply (gchar  *result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: command failed: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("response: '%s'\n", result);
    g_free (result);
}

static void
command_ready (MMModem      *modem,
               GAsyncResult *result,
               gpointer      nothing)
{
    gchar * operation_result;
    GError *error = NULL;

    operation_result = mm_modem_command_finish (modem, result, &error);
    command_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static guint
command_get_timeout (MMModem *modem)
{
    gint timeout;

    /* If --timeout was given, it should already have been set in the proxy */
    timeout = (g_dbus_proxy_get_default_timeout (G_DBUS_PROXY (modem)) / 1000) - 1;
    if (timeout <= 0) {
        g_printerr ("error: timeout is too short (%d)\n", timeout);
        exit (EXIT_FAILURE);
    }

    return (guint)timeout;
}

static void
create_bearer_process_reply (MMBearer     *bearer,
                             const GError *error)
{
    if (!bearer) {
        g_printerr ("error: couldn't create new bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully created new bearer in modem:\n");
    print_bearer_short_info (bearer);
    g_object_unref (bearer);
}

static void
create_bearer_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    MMBearer *bearer;
    GError *error = NULL;

    bearer = mm_modem_create_bearer_finish (modem, result, &error);
    create_bearer_process_reply (bearer, error);

    mmcli_async_operation_done ();
}

static void
delete_bearer_process_reply (gboolean      result,
                             const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't delete bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deleted bearer from modem\n");
}

static void
delete_bearer_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_delete_bearer_finish (modem, result, &error);
    delete_bearer_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_bearer_to_delete_ready (GDBusConnection *connection,
                            GAsyncResult *res)
{
    MMBearer *bearer;
    MMObject *obj = NULL;

    bearer = mmcli_get_bearer_finish (res, NULL, &obj);
    if (!g_str_equal (mm_object_get_path (obj), mm_modem_get_path (ctx->modem))) {
        g_printerr ("error: bearer '%s' not owned by modem '%s'",
                    mm_bearer_get_path (bearer),
                    mm_modem_get_path (ctx->modem));
        exit (EXIT_FAILURE);
    }

    mm_modem_delete_bearer (ctx->modem,
                            mm_bearer_get_path (bearer),
                            ctx->cancellable,
                            (GAsyncReadyCallback)delete_bearer_ready,
                            NULL);
    g_object_unref (bearer);
    g_object_unref (obj);
}

static void
set_current_capabilities_process_reply (gboolean      result,
                                        const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set current capabilities: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set current capabilities in the modem\n");
}

static void
set_current_capabilities_ready (MMModem      *modem,
                                GAsyncResult *result,
                                gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_set_current_capabilities_finish (modem, result, &error);
    set_current_capabilities_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
parse_current_capabilities (MMModemCapability *capabilities)
{
    GError *error = NULL;

    *capabilities = mm_common_get_capabilities_from_string (set_current_capabilities_str,
                                                            &error);
    if (error) {
        g_printerr ("error: couldn't parse list of capabilities: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }
}

static void
set_current_modes_process_reply (gboolean      result,
                                 const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set current modes: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set current modes in the modem\n");
}

static void
set_current_modes_ready (MMModem      *modem,
                         GAsyncResult *result,
                         gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_set_current_modes_finish (modem, result, &error);
    set_current_modes_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
parse_modes (MMModemMode *allowed,
             MMModemMode *preferred)
{
    GError *error = NULL;

    *allowed = mm_common_get_modes_from_string (set_allowed_modes_str, &error);
    if (error) {
        g_printerr ("error: couldn't parse list of allowed modes: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    *preferred = (set_preferred_mode_str ?
                  mm_common_get_modes_from_string (set_preferred_mode_str, &error) :
                  MM_MODEM_MODE_NONE);
    if (error) {
        g_printerr ("error: couldn't parse preferred mode: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }
}

static void
set_current_bands_process_reply (gboolean      result,
                                 const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set current bands: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set current bands in the modem\n");
}

static void
set_current_bands_ready (MMModem      *modem,
                         GAsyncResult *result,
                         gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_set_current_bands_finish (modem, result, &error);
    set_current_bands_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
parse_current_bands (MMModemBand **bands,
                     guint *n_bands)
{
    GError *error = NULL;

    mm_common_get_bands_from_string (set_current_bands_str,
                                     bands,
                                     n_bands,
                                     &error);
    if (error) {
        g_printerr ("error: couldn't parse list of bands: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }
}

static void
set_primary_sim_slot_process_reply (gboolean      result,
                                    const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't request primary SIM switch: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully requested primary SIM switch in modem\n");
}

static void
set_primary_sim_slot_ready (MMModem      *modem,
                            GAsyncResult *result)
{
    gboolean          operation_result;
    g_autoptr(GError) error = NULL;

    operation_result = mm_modem_set_primary_sim_slot_finish (modem, result, &error);
    set_primary_sim_slot_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
state_changed (MMModem                  *modem,
               MMModemState              old_state,
               MMModemState              new_state,
               MMModemStateChangeReason  reason)
{
    g_print ("\t%s: State changed, '%s' --> '%s' (Reason: %s)\n",
             mm_modem_get_path (modem),
             mm_modem_state_get_string (old_state),
             mm_modem_state_get_string (new_state),
             mmcli_get_state_reason_string (reason));
    fflush (stdout);
}

static void
device_removed (MMManager *manager,
                MMObject  *object)
{
    if (object != ctx->object)
        return;

    g_print ("\t%s: Removed\n", mm_object_get_path (object));
    fflush (stdout);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem = mm_object_get_modem (ctx->object);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);
    ctx->modem_cdma = mm_object_get_modem_cdma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem));
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));
    if (ctx->modem_cdma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cdma));

    if (info_flag)
        g_assert_not_reached ();

    /* Request to monitor modems? */
    if (monitor_state_flag) {
        MMModemState current;

        g_signal_connect (ctx->modem,
                          "state-changed",
                          G_CALLBACK (state_changed),
                          NULL);

        g_signal_connect (ctx->manager,
                          "object-removed",
                          G_CALLBACK (device_removed),
                          NULL);

        current = mm_modem_get_state (ctx->modem);
        g_print ("\t%s: Initial state, '%s'\n",
                 mm_object_get_path (ctx->object),
                 mm_modem_state_get_string (current));

        /* If we get cancelled, operation done */
        g_cancellable_connect (ctx->cancellable,
                               G_CALLBACK (cancelled),
                               NULL,
                               NULL);
        return;
    }

    /* Request to enable the modem? */
    if (enable_flag) {
        g_debug ("Asynchronously enabling modem...");
        mm_modem_enable (ctx->modem,
                         ctx->cancellable,
                         (GAsyncReadyCallback)enable_ready,
                         NULL);
        return;
    }

    /* Request to disable the modem? */
    if (disable_flag) {
        g_debug ("Asynchronously disabling modem...");
        mm_modem_disable (ctx->modem,
                          ctx->cancellable,
                          (GAsyncReadyCallback)disable_ready,
                          NULL);
        return;
    }

    /* Request to full power the modem? */
    if (set_power_state_on_flag) {
        g_debug ("Asynchronously setting full power...");
        mm_modem_set_power_state (ctx->modem,
                                  MM_MODEM_POWER_STATE_ON,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)set_power_state_ready,
                                  NULL);
        return;
    }

    /* Request to low power the modem? */
    if (set_power_state_low_flag) {
        g_debug ("Asynchronously setting low power...");
        mm_modem_set_power_state (ctx->modem,
                                  MM_MODEM_POWER_STATE_LOW,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)set_power_state_ready,
                                  NULL);
        return;
    }

    /* Request to power off the modem? */
    if (set_power_state_off_flag) {
        g_debug ("Asynchronously powering off...");
        mm_modem_set_power_state (ctx->modem,
                                  MM_MODEM_POWER_STATE_OFF,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)set_power_state_ready,
                                  NULL);
        return;
    }

    /* Request to reset the modem? */
    if (reset_flag) {
        g_debug ("Asynchronously reseting modem...");
        mm_modem_reset (ctx->modem,
                        ctx->cancellable,
                        (GAsyncReadyCallback)reset_ready,
                        NULL);
        return;
    }

    /* Request to reset the modem to factory state? */
    if (factory_reset_str) {
        g_debug ("Asynchronously factory-reseting modem...");
        mm_modem_factory_reset (ctx->modem,
                                factory_reset_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)factory_reset_ready,
                                NULL);
        return;
    }

    /* Request to send a command to the modem? */
    if (command_str) {
        guint timeout;

        timeout = command_get_timeout (ctx->modem);

        g_debug ("Asynchronously sending a command to the modem (%u seconds timeout)...",
                 timeout);

        mm_modem_command (ctx->modem,
                          command_str,
                          timeout,
                          ctx->cancellable,
                          (GAsyncReadyCallback)command_ready,
                          NULL);
        return;
    }

    /* Request to create a new bearer? */
    if (create_bearer_str) {
        GError *error = NULL;
        MMBearerProperties *properties;

        properties = mm_bearer_properties_new_from_string (create_bearer_str, &error);
        if (!properties) {
            g_printerr ("Error parsing properties string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously creating new bearer in modem...");
        mm_modem_create_bearer (ctx->modem,
                                properties,
                                ctx->cancellable,
                                (GAsyncReadyCallback)create_bearer_ready,
                                NULL);
        g_object_unref (properties);
        return;
    }

    /* Request to delete a given bearer? */
    if (delete_bearer_str) {
        mmcli_get_bearer (ctx->connection,
                          delete_bearer_str,
                          ctx->cancellable,
                          (GAsyncReadyCallback)get_bearer_to_delete_ready,
                          NULL);
        return;
    }

    /* Request to set current capabilities in a given modem? */
    if (set_current_capabilities_str) {
        MMModemCapability current_capabilities;

        parse_current_capabilities (&current_capabilities);
        mm_modem_set_current_capabilities (ctx->modem,
                                           current_capabilities,
                                           ctx->cancellable,
                                           (GAsyncReadyCallback)set_current_capabilities_ready,
                                           NULL);
        return;
    }

    /* Request to set allowed modes in a given modem? */
    if (set_allowed_modes_str) {
        MMModemMode allowed;
        MMModemMode preferred;

        parse_modes (&allowed, &preferred);
        mm_modem_set_current_modes (ctx->modem,
                                    allowed,
                                    preferred,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)set_current_modes_ready,
                                    NULL);
        return;
    }

    /* Request to set current bands in a given modem? */
    if (set_current_bands_str) {
        MMModemBand *current_bands;
        guint n_current_bands;

        parse_current_bands (&current_bands, &n_current_bands);
        mm_modem_set_current_bands (ctx->modem,
                                    current_bands,
                                    n_current_bands,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)set_current_bands_ready,
                                    NULL);
        g_free (current_bands);
        return;
    }

    /* Request to switch SIM? */
    if (set_primary_sim_slot_int > 0) {
        mm_modem_set_primary_sim_slot (ctx->modem,
                                       set_primary_sim_slot_int,
                                       ctx->cancellable,
                                       (GAsyncReadyCallback)set_primary_sim_slot_ready,
                                       NULL);
        return;
    }

    /* Request to inhibit the modem? */
    if (inhibit_flag) {
        gchar *uid;

        g_debug ("Asynchronously inhibiting modem...");
        uid = mm_modem_dup_device (ctx->modem);

        mm_manager_inhibit_device (ctx->manager,
                                   uid,
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)inhibit_device_ready,
                                   uid);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_run_asynchronous (GDBusConnection *connection,
                              GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);
    ctx->connection = g_object_ref (connection);

    /* Get proper modem */
    mmcli_get_modem  (connection,
                      mmcli_get_common_modem_string (),
                      cancellable,
                      (GAsyncReadyCallback)get_modem_ready,
                      NULL);
}

void
mmcli_modem_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    if (monitor_state_flag || inhibit_flag)
        g_assert_not_reached ();

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem = mm_object_get_modem (ctx->object);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);
    ctx->modem_cdma = mm_object_get_modem_cdma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem));
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));
    if (ctx->modem_cdma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cdma));

    /* Request to get info from modem? */
    if (info_flag) {
        g_debug ("Printing modem info...");
        print_modem_info ();
        return;
    }

    /* Request to enable the modem? */
    if (enable_flag) {
        gboolean result;

        g_debug ("Synchronously enabling modem...");
        result = mm_modem_enable_sync (ctx->modem, NULL, &error);
        enable_process_reply (result, error);
        return;
    }

    /* Request to disable the modem? */
    if (disable_flag) {
        gboolean result;

        g_debug ("Synchronously disabling modem...");
        result = mm_modem_disable_sync (ctx->modem, NULL, &error);
        disable_process_reply (result, error);
        return;
    }

    /* Request to set full power state? */
    if (set_power_state_on_flag) {
        gboolean result;

        g_debug ("Synchronously setting full power...");
        result = mm_modem_set_power_state_sync (ctx->modem, MM_MODEM_POWER_STATE_ON, NULL, &error);
        set_power_state_process_reply (result, error);
        return;
    }

    /* Request to set low power state? */
    if (set_power_state_low_flag) {
        gboolean result;

        g_debug ("Synchronously setting low power...");
        result = mm_modem_set_power_state_sync (ctx->modem, MM_MODEM_POWER_STATE_LOW, NULL, &error);
        set_power_state_process_reply (result, error);
        return;
    }

    /* Request to power off? */
    if (set_power_state_off_flag) {
        gboolean result;

        g_debug ("Synchronously powering off...");
        result = mm_modem_set_power_state_sync (ctx->modem, MM_MODEM_POWER_STATE_OFF, NULL, &error);
        set_power_state_process_reply (result, error);
        return;
    }

    /* Request to reset the modem? */
    if (reset_flag) {
        gboolean result;

        g_debug ("Synchronously reseting modem...");
        result = mm_modem_reset_sync (ctx->modem, NULL, &error);
        reset_process_reply (result, error);
        return;
    }

    /* Request to reset the modem to factory state? */
    if (factory_reset_str) {
        gboolean result;

        g_debug ("Synchronously factory-reseting modem...");
        result = mm_modem_factory_reset_sync (ctx->modem,
                                              factory_reset_str,
                                              NULL,
                                              &error);
        factory_reset_process_reply (result, error);
        return;
    }


    /* Request to send a command to the modem? */
    if (command_str) {
        gchar *result;
        guint timeout;

        timeout = command_get_timeout (ctx->modem);

        g_debug ("Synchronously sending command to modem (%u seconds timeout)...",
                 timeout);

        result = mm_modem_command_sync (ctx->modem,
                                        command_str,
                                        timeout,
                                        NULL,
                                        &error);
        command_process_reply (result, error);
        return;
    }

    /* Request to create a new bearer? */
    if (create_bearer_str) {
        MMBearer *bearer;
        MMBearerProperties *properties;

        properties = mm_bearer_properties_new_from_string (create_bearer_str, &error);
        if (!properties) {
            g_printerr ("Error parsing properties string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously creating new bearer in modem...");
        bearer = mm_modem_create_bearer_sync (ctx->modem,
                                              properties,
                                              NULL,
                                              &error);
        g_object_unref (properties);

        create_bearer_process_reply (bearer, error);
        return;
    }

    /* Request to delete a given bearer? */
    if (delete_bearer_str) {
        gboolean result;
        MMBearer *bearer;
        MMObject *obj = NULL;

        bearer = mmcli_get_bearer_sync (connection,
                                        delete_bearer_str,
                                        NULL,
                                        &obj);
        if (!g_str_equal (mm_object_get_path (obj), mm_modem_get_path (ctx->modem))) {
            g_printerr ("error: bearer '%s' not owned by modem '%s'",
                        mm_bearer_get_path (bearer),
                        mm_modem_get_path (ctx->modem));
            exit (EXIT_FAILURE);
        }

        result = mm_modem_delete_bearer_sync (ctx->modem,
                                              mm_bearer_get_path (bearer),
                                              NULL,
                                              &error);
        g_object_unref (bearer);
        g_object_unref (obj);

        delete_bearer_process_reply (result, error);
        return;
    }

    /* Request to set capabilities in a given modem? */
    if (set_current_capabilities_str) {
        gboolean result;
        MMModemCapability current_capabilities;

        parse_current_capabilities (&current_capabilities);
        result = mm_modem_set_current_capabilities_sync (ctx->modem,
                                                         current_capabilities,
                                                         NULL,
                                                         &error);
        set_current_capabilities_process_reply (result, error);
        return;
    }

    /* Request to set allowed modes in a given modem? */
    if (set_allowed_modes_str) {
        MMModemMode allowed;
        MMModemMode preferred;
        gboolean result;

        parse_modes (&allowed, &preferred);
        result = mm_modem_set_current_modes_sync (ctx->modem,
                                                  allowed,
                                                  preferred,
                                                  NULL,
                                                  &error);

        set_current_modes_process_reply (result, error);
        return;
    }

    /* Request to set allowed bands in a given modem? */
    if (set_current_bands_str) {
        gboolean result;
        MMModemBand *current_bands;
        guint n_current_bands;

        parse_current_bands (&current_bands, &n_current_bands);
        result = mm_modem_set_current_bands_sync (ctx->modem,
                                                  current_bands,
                                                  n_current_bands,
                                                  NULL,
                                                  &error);
        g_free (current_bands);
        set_current_bands_process_reply (result, error);
        return;
    }

    /* Request to switch current SIM? */
    if (set_primary_sim_slot_int > 0) {
        gboolean result;

        result = mm_modem_set_primary_sim_slot_sync (ctx->modem, set_primary_sim_slot_int, NULL, &error);
        set_primary_sim_slot_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
