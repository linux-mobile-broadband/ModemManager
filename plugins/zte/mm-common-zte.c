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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include "ModemManager.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-common-zte.h"

struct _MMCommonZteUnsolicitedSetup {
    /* Regex for access-technology related notifications */
    GRegex *zpasr_regex;

    /* Requests to always ignore */
    GRegex *zusimr_regex; /* SMS related */
    GRegex *zdonr_regex; /* Unsolicited operator display */
    GRegex *zpstm_regex; /* SIM request to Build Main Menu */
    GRegex *zend_regex;  /* SIM request to Rebuild Main Menu */
};

MMCommonZteUnsolicitedSetup *
mm_common_zte_unsolicited_setup_new (void)
{
    MMCommonZteUnsolicitedSetup *setup;

    setup = g_new (MMCommonZteUnsolicitedSetup, 1);

    /* Prepare regular expressions to setup */

    setup->zusimr_regex = g_regex_new ("\\r\\n\\+ZUSIMR:(.*)\\r\\n",
                                       G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->zusimr_regex != NULL);

    setup->zdonr_regex = g_regex_new ("\\r\\n\\+ZDONR: (.*)\\r\\n",
                                      G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->zdonr_regex != NULL);

    setup->zpasr_regex = g_regex_new ("\\r\\n\\+ZPASR:\\s*(.*)\\r\\n",
                                      G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->zpasr_regex != NULL);

    setup->zpstm_regex = g_regex_new ("\\r\\n\\+ZPSTM: (.*)\\r\\n",
                                      G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->zpstm_regex != NULL);

    setup->zend_regex = g_regex_new ("\\r\\n\\+ZEND\\r\\n",
                                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (setup->zend_regex != NULL);

    return setup;
}

void
mm_common_zte_unsolicited_setup_free (MMCommonZteUnsolicitedSetup *setup)
{
    g_regex_unref (setup->zusimr_regex);
    g_regex_unref (setup->zdonr_regex);
    g_regex_unref (setup->zpasr_regex);
    g_regex_unref (setup->zpstm_regex);
    g_regex_unref (setup->zend_regex);
    g_free (setup);
}

static void
zpasr_received (MMPortSerialAt *port,
                GMatchInfo *info,
                MMBroadbandModem *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gchar *str;

    str = g_match_info_fetch (info, 1);
    if (str) {
        act = mm_string_to_access_tech (str);
        g_free (str);
    }

    mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                               act,
                                               MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
}

void
mm_common_zte_set_unsolicited_events_handlers (MMBroadbandModem *self,
                                               MMCommonZteUnsolicitedSetup *setup,
                                               gboolean enable)
{
    MMPortSerialAt *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        /* Access technology related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            setup->zpasr_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)zpasr_received : NULL,
            enable ? self : NULL,
            NULL);

        /* Other unsolicited events to always ignore */
        if (!enable) {
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->zusimr_regex,
                NULL, NULL, NULL);
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->zdonr_regex,
                NULL, NULL, NULL);
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->zpstm_regex,
                NULL, NULL, NULL);
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                setup->zend_regex,
                NULL, NULL, NULL);
        }
    }
}
