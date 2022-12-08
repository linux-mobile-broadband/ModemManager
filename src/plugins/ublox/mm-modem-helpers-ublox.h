/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_MODEM_HELPERS_UBLOX_H
#define MM_MODEM_HELPERS_UBLOX_H

#include <glib.h>
#include <ModemManager.h>

/*****************************************************************************/
/* AT Commands Support */

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_SUPPORTED,
    FEATURE_UNSUPPORTED,
} FeatureSupport;

typedef enum {
    SETTINGS_UPDATE_METHOD_UNKNOWN,
    SETTINGS_UPDATE_METHOD_CFUN,
    SETTINGS_UPDATE_METHOD_COPS,
} SettingsUpdateMethod;

typedef struct UbloxSupportConfig {
    gboolean             loaded;
    SettingsUpdateMethod method;
    FeatureSupport       uact;
    FeatureSupport       ubandsel;
} UbloxSupportConfig;

/*****************************************************************************/
/* +UPINCNT response parser */

gboolean mm_ublox_parse_upincnt_response (const gchar  *response,
                                          guint        *out_pin_attempts,
                                          guint        *out_pin2_attempts,
                                          guint        *out_puk_attempts,
                                          guint        *out_puk2_attempts,
                                          GError      **error);

/*****************************************************************************/
/* UUSBCONF? response parser */

typedef enum { /*< underscore_name=mm_ublox_usb_profile >*/
    MM_UBLOX_USB_PROFILE_UNKNOWN,
    MM_UBLOX_USB_PROFILE_RNDIS,
    MM_UBLOX_USB_PROFILE_ECM,
    MM_UBLOX_USB_PROFILE_BACK_COMPATIBLE,
} MMUbloxUsbProfile;

gboolean mm_ublox_parse_uusbconf_response (const gchar        *response,
                                           MMUbloxUsbProfile  *out_profile,
                                           GError            **error);

/*****************************************************************************/
/* UBMCONF? response parser */

typedef enum { /*< underscore_name=mm_ublox_networking_mode >*/
    MM_UBLOX_NETWORKING_MODE_UNKNOWN,
    MM_UBLOX_NETWORKING_MODE_ROUTER,
    MM_UBLOX_NETWORKING_MODE_BRIDGE,
} MMUbloxNetworkingMode;

gboolean mm_ublox_parse_ubmconf_response (const gchar            *response,
                                          MMUbloxNetworkingMode  *out_mode,
                                          GError                **error);

/*****************************************************************************/
/* UIPADDR=N response parser */

gboolean mm_ublox_parse_uipaddr_response (const gchar  *response,
                                          guint        *out_cid,
                                          gchar       **out_if_name,
                                          gchar       **out_ipv4_address,
                                          gchar       **out_ipv4_subnet,
                                          gchar       **out_ipv6_global_address,
                                          gchar       **out_ipv6_link_local_address,
                                          GError      **error);

/*****************************************************************************/
/* CFUN? response parser */

gboolean mm_ublox_parse_cfun_response (const gchar        *response,
                                       MMModemPowerState  *out_state,
                                       GError            **error);

/*****************************************************************************/
/* URAT=? response parser */

GArray *mm_ublox_parse_urat_test_response (const gchar  *response,
                                           gpointer      log_object,
                                           GError      **error);

/*****************************************************************************/
/* Model-based config support loading */

gboolean mm_ublox_get_support_config (const gchar         *model,
                                      UbloxSupportConfig  *config,
                                      GError             **error);

/*****************************************************************************/
/* Model-based supported modes filtering */

GArray *mm_ublox_filter_supported_modes (const gchar  *model,
                                         GArray       *combinations,
                                         gpointer      logger,
                                         GError      **error);

/*****************************************************************************/
/* Model-based supported bands loading */

GArray *mm_ublox_get_supported_bands (const gchar *model,
                                      gpointer     log_object,
                                      GError     **error);

/*****************************************************************************/
/* UBANDSEL? response parser */

GArray *mm_ublox_parse_ubandsel_response (const gchar  *response,
                                          const gchar  *model,
                                          gpointer      log_object,
                                          GError      **error);

/*****************************************************************************/
/* UBANDSEL=X command builder */

gchar *mm_ublox_build_ubandsel_set_command (GArray       *bands,
                                            const gchar  *model,
                                            GError      **error);

/*****************************************************************************/
/* UACT? response parser */

GArray *mm_ublox_parse_uact_response (const gchar  *response,
                                      GError      **error);

/*****************************************************************************/
/* UACT=? test parser */

gboolean mm_ublox_parse_uact_test (const gchar  *response,
                                   gpointer      log_object,
                                   GArray      **bands_2g,
                                   GArray      **bands_3g,
                                   GArray      **bands_4g,
                                   GError      **error);

/*****************************************************************************/
/* UACT=X command builder */

gchar *mm_ublox_build_uact_set_command (GArray  *bands,
                                        GError **error);

/*****************************************************************************/
/* Get mode to apply when ANY */

MMModemMode mm_ublox_get_modem_mode_any (const GArray *combinations);

/*****************************************************************************/
/* URAT? response parser */

gboolean mm_ublox_parse_urat_read_response (const gchar  *response,
                                            gpointer      log_object,
                                            MMModemMode  *out_allowed,
                                            MMModemMode  *out_preferred,
                                            GError      **error);

/*****************************************************************************/
/* URAT=X command builder */

gchar *mm_ublox_build_urat_set_command (MMModemMode   allowed,
                                        MMModemMode   preferred,
                                        GError      **error);

/*****************************************************************************/
/* +UAUTHREQ=? test parser */

typedef enum { /*< underscore_name=mm_ublox_bearer_allowed_auth >*/
    MM_UBLOX_BEARER_ALLOWED_AUTH_UNKNOWN = 0,
    MM_UBLOX_BEARER_ALLOWED_AUTH_NONE    = 1 << 0,
    MM_UBLOX_BEARER_ALLOWED_AUTH_PAP     = 1 << 1,
    MM_UBLOX_BEARER_ALLOWED_AUTH_CHAP    = 1 << 2,
    MM_UBLOX_BEARER_ALLOWED_AUTH_AUTO    = 1 << 3,
} MMUbloxBearerAllowedAuth;

MMUbloxBearerAllowedAuth mm_ublox_parse_uauthreq_test (const char  *response,
                                                       gpointer     log_object,
                                                       GError     **error);

/*****************************************************************************/
/* +UGCNTRD response parser */

gboolean mm_ublox_parse_ugcntrd_response_for_cid (const gchar  *response,
                                                  guint         in_cid,
                                                  guint64      *session_tx_bytes,
                                                  guint64      *session_rx_bytes,
                                                  guint64      *total_tx_bytes,
                                                  guint64      *total_rx_bytes,
                                                  GError      **error);

#endif  /* MM_MODEM_HELPERS_UBLOX_H */
