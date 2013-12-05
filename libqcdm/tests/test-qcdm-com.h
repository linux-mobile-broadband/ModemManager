/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TEST_QCDM_COM_H
#define TEST_QCDM_COM_H

gpointer test_com_setup (const char *port);
void test_com_teardown (gpointer d);

void test_com_port_init (void *f, void *data);

void test_com_version_info (void *f, void *data);

void test_com_esn (void *f, void *data);

void test_com_mdn (void *f, void *data);

void test_com_read_roam_pref (void *f, void *data);

void test_com_read_mode_pref (void *f, void *data);

void test_com_read_hybrid_pref (void *f, void *data);

void test_com_read_ipv6_enabled (void *f, void *data);

void test_com_read_hdr_rev_pref (void *f, void *data);

void test_com_status (void *f, void *data);

void test_com_sw_version (void *f, void *data);

void test_com_status_snapshot (void *f, void *data);

void test_com_pilot_sets (void *f, void *data);

void test_com_cm_subsys_state_info (void *f, void *data);

void test_com_hdr_subsys_state_info (void *f, void *data);

void test_com_ext_logmask (void *f, void *data);

void test_com_event_report (void *f, void *data);

void test_com_log_config (void *f, void *data);

void test_com_zte_subsys_status (void *f, void *data);

void test_com_nw_subsys_modem_snapshot_cdma (void *f, void *data);

void test_com_nw_subsys_eri (void *f, void *data);

void test_com_wcdma_subsys_state_info (void *f, void *data);

void test_com_gsm_subsys_state_info (void *f, void *data);

#endif  /* TEST_QCDM_COM_H */

