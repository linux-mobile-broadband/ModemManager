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
 * Copyright (C) 2022 Google Inc.
 */

#include <config.h>
#include <glib.h>

#include "mm-plugin.h"
#include "mm-builtin-plugins.h"

#if defined ENABLE_PLUGIN_ALTAIR_LTE
MMPlugin *mm_plugin_create_altair_lte (void);
#endif
#if defined ENABLE_PLUGIN_ANYDATA
MMPlugin *mm_plugin_create_anydata (void);
#endif
#if defined ENABLE_PLUGIN_BROADMOBI
MMPlugin *mm_plugin_create_broadmobi (void);
#endif
#if defined ENABLE_PLUGIN_CINTERION
MMPlugin *mm_plugin_create_cinterion (void);
#endif
#if defined ENABLE_PLUGIN_DELL
MMPlugin *mm_plugin_create_dell (void);
#endif
#if defined ENABLE_PLUGIN_DLINK
MMPlugin *mm_plugin_create_dlink (void);
#endif
#if defined ENABLE_PLUGIN_FIBOCOM
MMPlugin *mm_plugin_create_fibocom (void);
#endif
#if defined ENABLE_PLUGIN_FOXCONN
MMPlugin *mm_plugin_create_foxconn (void);
#endif
#if defined ENABLE_PLUGIN_GENERIC
MMPlugin *mm_plugin_create_generic (void);
#endif
#if defined ENABLE_PLUGIN_GOSUNCN
MMPlugin *mm_plugin_create_gosuncn (void);
#endif
#if defined ENABLE_PLUGIN_HAIER
MMPlugin *mm_plugin_create_haier (void);
#endif
#if defined ENABLE_PLUGIN_HUAWEI
MMPlugin *mm_plugin_create_huawei (void);
#endif
#if defined ENABLE_PLUGIN_INTEL
MMPlugin *mm_plugin_create_intel (void);
#endif
#if defined ENABLE_PLUGIN_IRIDIUM
MMPlugin *mm_plugin_create_iridium (void);
#endif
#if defined ENABLE_PLUGIN_LINKTOP
MMPlugin *mm_plugin_create_linktop (void);
#endif
#if defined ENABLE_PLUGIN_LONGCHEER
MMPlugin *mm_plugin_create_longcheer (void);
#endif
#if defined ENABLE_PLUGIN_MBM
MMPlugin *mm_plugin_create_mbm (void);
#endif
#if defined ENABLE_PLUGIN_MOTOROLA
MMPlugin *mm_plugin_create_motorola (void);
#endif
#if defined ENABLE_PLUGIN_MTK
MMPlugin *mm_plugin_create_mtk (void);
#endif
#if defined ENABLE_PLUGIN_NOKIA
MMPlugin *mm_plugin_create_nokia (void);
#endif
#if defined ENABLE_PLUGIN_NOKIA_ICERA
MMPlugin *mm_plugin_create_nokia_icera (void);
#endif
#if defined ENABLE_PLUGIN_NOVATEL
MMPlugin *mm_plugin_create_novatel (void);
#endif
#if defined ENABLE_PLUGIN_NOVATEL_LTE
MMPlugin *mm_plugin_create_novatel_lte (void);
#endif
#if defined ENABLE_PLUGIN_OPTION
MMPlugin *mm_plugin_create_option (void);
#endif
#if defined ENABLE_PLUGIN_OPTION_HSO
MMPlugin *mm_plugin_create_hso (void);
#endif
#if defined ENABLE_PLUGIN_PANTECH
MMPlugin *mm_plugin_create_pantech (void);
#endif
#if defined ENABLE_PLUGIN_QCOM_SOC
MMPlugin *mm_plugin_create_qcom_soc (void);
#endif
#if defined ENABLE_PLUGIN_QUECTEL
MMPlugin *mm_plugin_create_quectel (void);
#endif
#if defined ENABLE_PLUGIN_SAMSUNG
MMPlugin *mm_plugin_create_samsung (void);
#endif
#if defined ENABLE_PLUGIN_SIERRA
MMPlugin *mm_plugin_create_sierra (void);
#endif
#if defined ENABLE_PLUGIN_SIERRA_LEGACY
MMPlugin *mm_plugin_create_sierra_legacy (void);
#endif
#if defined ENABLE_PLUGIN_SIMTECH
MMPlugin *mm_plugin_create_simtech (void);
#endif
#if defined ENABLE_PLUGIN_TELIT
MMPlugin *mm_plugin_create_telit (void);
#endif
#if defined ENABLE_PLUGIN_THURAYA
MMPlugin *mm_plugin_create_thuraya (void);
#endif
#if defined ENABLE_PLUGIN_TPLINK
MMPlugin *mm_plugin_create_tplink (void);
#endif
#if defined ENABLE_PLUGIN_UBLOX
MMPlugin *mm_plugin_create_ublox (void);
#endif
#if defined ENABLE_PLUGIN_VIA
MMPlugin *mm_plugin_create_via (void);
#endif
#if defined ENABLE_PLUGIN_WAVECOM
MMPlugin *mm_plugin_create_wavecom (void);
#endif
#if defined ENABLE_PLUGIN_X22X
MMPlugin *mm_plugin_create_x22x (void);
#endif
#if defined ENABLE_PLUGIN_ZTE
MMPlugin *mm_plugin_create_zte (void);
#endif

GList *
mm_builtin_plugins_load (void)
{
    GList *builtin_plugins = NULL;

#define PREPEND_PLUGIN(my_plugin) \
    builtin_plugins = g_list_prepend (builtin_plugins, mm_plugin_create_##my_plugin ())

#if defined ENABLE_PLUGIN_ALTAIR_LTE
    PREPEND_PLUGIN (altair_lte);
#endif
#if defined ENABLE_PLUGIN_ANYDATA
    PREPEND_PLUGIN (anydata);
#endif
#if defined ENABLE_PLUGIN_BROADMOBI
    PREPEND_PLUGIN (broadmobi);
#endif
#if defined ENABLE_PLUGIN_CINTERION
    PREPEND_PLUGIN (cinterion);
#endif
#if defined ENABLE_PLUGIN_DELL
    PREPEND_PLUGIN (dell);
#endif
#if defined ENABLE_PLUGIN_DLINK
    PREPEND_PLUGIN (dlink);
#endif
#if defined ENABLE_PLUGIN_FIBOCOM
    PREPEND_PLUGIN (fibocom);
#endif
#if defined ENABLE_PLUGIN_FOXCONN
    PREPEND_PLUGIN (foxconn);
#endif
#if defined ENABLE_PLUGIN_GENERIC
    PREPEND_PLUGIN (generic);
#endif
#if defined ENABLE_PLUGIN_GOSUNCN
    PREPEND_PLUGIN (gosuncn);
#endif
#if defined ENABLE_PLUGIN_HAIER
    PREPEND_PLUGIN (haier);
#endif
#if defined ENABLE_PLUGIN_HUAWEI
    PREPEND_PLUGIN (huawei);
#endif
#if defined ENABLE_PLUGIN_INTEL
    PREPEND_PLUGIN (intel);
#endif
#if defined ENABLE_PLUGIN_IRIDIUM
    PREPEND_PLUGIN (iridium);
#endif
#if defined ENABLE_PLUGIN_LINKTOP
    PREPEND_PLUGIN (linktop);
#endif
#if defined ENABLE_PLUGIN_LONGCHEER
    PREPEND_PLUGIN (longcheer);
#endif
#if defined ENABLE_PLUGIN_MBM
    PREPEND_PLUGIN (mbm);
#endif
#if defined ENABLE_PLUGIN_MOTOROLA
    PREPEND_PLUGIN (motorola);
#endif
#if defined ENABLE_PLUGIN_MTK
    PREPEND_PLUGIN (mtk);
#endif
#if defined ENABLE_PLUGIN_NOKIA
    PREPEND_PLUGIN (nokia);
#endif
#if defined ENABLE_PLUGIN_NOKIA_ICERA
    PREPEND_PLUGIN (nokia_icera);
#endif
#if defined ENABLE_PLUGIN_NOVATEL
    PREPEND_PLUGIN (novatel);
#endif
#if defined ENABLE_PLUGIN_NOVATEL_LTE
    PREPEND_PLUGIN (novatel_lte);
#endif
#if defined ENABLE_PLUGIN_OPTION
    PREPEND_PLUGIN (option);
#endif
#if defined ENABLE_PLUGIN_OPTION_HSO
    PREPEND_PLUGIN (hso);
#endif
#if defined ENABLE_PLUGIN_PANTECH
    PREPEND_PLUGIN (pantech);
#endif
#if defined ENABLE_PLUGIN_QCOM_SOC
    PREPEND_PLUGIN (qcom_soc);
#endif
#if defined ENABLE_PLUGIN_QUECTEL
    PREPEND_PLUGIN (quectel);
#endif
#if defined ENABLE_PLUGIN_SAMSUNG
    PREPEND_PLUGIN (samsung);
#endif
#if defined ENABLE_PLUGIN_SIERRA
    PREPEND_PLUGIN (sierra);
#endif
#if defined ENABLE_PLUGIN_SIERRA_LEGACY
    PREPEND_PLUGIN (sierra_legacy);
#endif
#if defined ENABLE_PLUGIN_SIMTECH
    PREPEND_PLUGIN (simtech);
#endif
#if defined ENABLE_PLUGIN_TELIT
    PREPEND_PLUGIN (telit);
#endif
#if defined ENABLE_PLUGIN_THURAYA
    PREPEND_PLUGIN (thuraya);
#endif
#if defined ENABLE_PLUGIN_TPLINK
    PREPEND_PLUGIN (tplink);
#endif
#if defined ENABLE_PLUGIN_UBLOX
    PREPEND_PLUGIN (ublox);
#endif
#if defined ENABLE_PLUGIN_VIA
    PREPEND_PLUGIN (via);
#endif
#if defined ENABLE_PLUGIN_WAVECOM
    PREPEND_PLUGIN (wavecom);
#endif
#if defined ENABLE_PLUGIN_X22X
    PREPEND_PLUGIN (x22x);
#endif
#if defined ENABLE_PLUGIN_ZTE
    PREPEND_PLUGIN (zte);
#endif
#undef PREPEND_PLUGIN
    return builtin_plugins;
}
