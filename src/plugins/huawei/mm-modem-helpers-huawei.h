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
 * Copyright (C) 2013 Huawei Technologies Co., Ltd
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_MODEM_HELPERS_HUAWEI_H
#define MM_MODEM_HELPERS_HUAWEI_H

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

/*****************************************************************************/
/* ^NDISSTAT / ^NDISSTATQRY response parser */
gboolean mm_huawei_parse_ndisstatqry_response (const gchar *response,
                                               gboolean *ipv4_available,
                                               gboolean *ipv4_connected,
                                               gboolean *ipv6_available,
                                               gboolean *ipv6_connected,
                                               GError **error);

/*****************************************************************************/
/* ^DHCP response parser */
gboolean mm_huawei_parse_dhcp_response (const char *reply,
                                        guint *out_address,
                                        guint *out_prefix,
                                        guint *out_gateway,
                                        guint *out_dns1,
                                        guint *out_dns2,
                                        GError **error);

/*****************************************************************************/
/* ^SYSINFO response parser */
gboolean mm_huawei_parse_sysinfo_response (const char *reply,
                                           guint *out_srv_status,
                                           guint *out_srv_domain,
                                           guint *out_roam_status,
                                           guint *out_sys_mode,
                                           guint *out_sim_state,
                                           gboolean *out_sys_submode_valid,
                                           guint *out_sys_submode,
                                           GError **error);

/*****************************************************************************/
/* ^SYSINFOEX response parser */
gboolean mm_huawei_parse_sysinfoex_response (const char *reply,
                                             guint *out_srv_status,
                                             guint *out_srv_domain,
                                             guint *out_roam_status,
                                             guint *out_sim_state,
                                             guint *out_sys_mode,
                                             guint *out_sys_submode,
                                             GError **error);

/*****************************************************************************/
/* ^PREFMODE test parser */

typedef struct {
    guint prefmode;
    MMModemMode allowed;
    MMModemMode preferred;
} MMHuaweiPrefmodeCombination;

GArray *mm_huawei_parse_prefmode_test (const gchar  *response,
                                       gpointer      log_object,
                                       GError      **error);

/*****************************************************************************/
/* ^PREFMODE response parser */

const MMHuaweiPrefmodeCombination *mm_huawei_parse_prefmode_response (const gchar *response,
                                                                      const GArray *supported_mode_combinations,
                                                                      GError **error);

/*****************************************************************************/
/* ^SYSCFG test parser */

/* This is the default string we use as fallback when the modem gives
 * an empty response to AT^SYSCFG=? */
#define MM_HUAWEI_DEFAULT_SYSCFG_FMT "^SYSCFG:(2,13,14,16),(0-3),,,"

typedef struct {
    guint mode;
    guint acqorder;
    MMModemMode allowed;
    MMModemMode preferred;
} MMHuaweiSyscfgCombination;

GArray *mm_huawei_parse_syscfg_test (const gchar  *response,
                                     gpointer      log_object,
                                     GError      **error);

/*****************************************************************************/
/* ^SYSCFG response parser */

const MMHuaweiSyscfgCombination *mm_huawei_parse_syscfg_response (const gchar *response,
                                                                  const GArray *supported_mode_combinations,
                                                                  GError **error);

/*****************************************************************************/
/* ^SYSCFGEX test parser */

typedef struct {
    gchar *mode_str;
    MMModemMode allowed;
    MMModemMode preferred;
} MMHuaweiSyscfgexCombination;

GArray *mm_huawei_parse_syscfgex_test (const gchar *response,
                                       GError **error);

/*****************************************************************************/
/* ^SYSCFGEX response parser */

const MMHuaweiSyscfgexCombination *mm_huawei_parse_syscfgex_response (const gchar *response,
                                                                      const GArray *supported_mode_combinations,
                                                                      GError **error);

/*****************************************************************************/
/* ^NWTIME response parser */

gboolean mm_huawei_parse_nwtime_response (const gchar *response,
                                          gchar **iso8601p,
                                          MMNetworkTimezone **tzp,
                                          GError **error);

/*****************************************************************************/
/* ^TIME response parser */

gboolean mm_huawei_parse_time_response (const gchar *response,
                                        gchar **iso8601p,
                                        MMNetworkTimezone **tzp,
                                        GError **error);

/*****************************************************************************/
/* ^HCSQ response parser */

gboolean mm_huawei_parse_hcsq_response (const gchar *response,
                                        MMModemAccessTechnology *out_act,
                                        guint *out_value1,
                                        guint *out_value2,
                                        guint *out_value3,
                                        guint *out_value4,
                                        guint *out_value5,
                                        GError **error);

/*****************************************************************************/
/* ^CVOICE response parser */

gboolean mm_huawei_parse_cvoice_response (const gchar  *response,
                                          guint        *hz,
                                          guint        *bits,
                                          GError      **error);

/*****************************************************************************/
/* ^GETPORTMODE response parser */

typedef enum { /*< underscore_name=mm_huawei_port_mode >*/
    MM_HUAWEI_PORT_MODE_NONE,
    MM_HUAWEI_PORT_MODE_PCUI,
    MM_HUAWEI_PORT_MODE_MODEM,
    MM_HUAWEI_PORT_MODE_DIAG,
    MM_HUAWEI_PORT_MODE_GPS,
    MM_HUAWEI_PORT_MODE_NET,
    MM_HUAWEI_PORT_MODE_CDROM,
    MM_HUAWEI_PORT_MODE_SD,
    MM_HUAWEI_PORT_MODE_BT,
    MM_HUAWEI_PORT_MODE_SHELL,
} MMHuaweiPortMode;

#define MM_HUAWEI_PORT_MODE_IS_SERIAL(mode)  \
    (mode == MM_HUAWEI_PORT_MODE_PCUI  ||    \
     mode == MM_HUAWEI_PORT_MODE_MODEM ||    \
     mode == MM_HUAWEI_PORT_MODE_DIAG  ||    \
     mode == MM_HUAWEI_PORT_MODE_GPS   ||    \
     mode == MM_HUAWEI_PORT_MODE_SHELL)

GArray *mm_huawei_parse_getportmode_response (const gchar  *response,
                                              gpointer      log_object,
                                              GError      **error);

#endif  /* MM_MODEM_HELPERS_HUAWEI_H */
