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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#define _GNU_SOURCE  /* for strcasestr */
#include <string.h>

#include <glib.h>

#include "mm-errors.h"
#include "mm-port-probe.h"
#include "mm-port-probe-at-command.h"

/* ---- AT probing ---- */

static gboolean
parse_at (const gchar *response,
          const GError *error,
          GValue *result,
          GError **result_error)
{
    if (error) {
        /* On timeout, request to retry */
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            return FALSE; /* Retry */

        /* If error is not recognizable, request to abort */
        if (error->domain != MM_MOBILE_ERROR) {
            *result_error = g_error_copy (error);
            g_prefix_error (result_error,
                            "Couldn't parse AT reply. ");
            return FALSE;
        }

        /* If the modem returned a recognizable error,
         * it can do AT commands */
        g_value_init (result, G_TYPE_BOOLEAN);
        g_value_set_boolean (result, TRUE);
        return TRUE;
    }

    /* No error reported, valid AT port! */
    g_value_init (result, G_TYPE_BOOLEAN);
    g_value_set_boolean (result, TRUE);
    return TRUE;
}

static const MMPortProbeAtCommand at_probing[] = {
    { "AT",  parse_at },
    { "AT",  parse_at },
    { "AT",  parse_at },
    { NULL }
};

const MMPortProbeAtCommand *
mm_port_probe_at_command_get_probing (void)
{
    return at_probing;
}

/* ---- CAPABILITIES probing ---- */

struct modem_caps {
	gchar *name;
	guint32 bits;
};

static const struct modem_caps modem_caps[] = {
	{ "+CGSM",     MM_PORT_PROBE_CAPABILITY_GSM     },
	{ "+CIS707-A", MM_PORT_PROBE_CAPABILITY_IS707_A },
	{ "+CIS707A",  MM_PORT_PROBE_CAPABILITY_IS707_A }, /* Cmotech */
	{ "+CIS707",   MM_PORT_PROBE_CAPABILITY_IS707_A },
	{ "CIS707",    MM_PORT_PROBE_CAPABILITY_IS707_A }, /* Qualcomm Gobi */
	{ "+CIS707P",  MM_PORT_PROBE_CAPABILITY_IS707_P },
	{ "CIS-856",   MM_PORT_PROBE_CAPABILITY_IS856   },
	{ "+IS-856",   MM_PORT_PROBE_CAPABILITY_IS856   }, /* Cmotech */
	{ "CIS-856-A", MM_PORT_PROBE_CAPABILITY_IS856_A },
	{ "CIS-856A",  MM_PORT_PROBE_CAPABILITY_IS856_A }, /* Kyocera KPC680 */
	{ "+DS",       MM_PORT_PROBE_CAPABILITY_DS      },
	{ "+ES",       MM_PORT_PROBE_CAPABILITY_ES      },
	{ "+MS",       MM_PORT_PROBE_CAPABILITY_MS      },
	{ "+FCLASS",   MM_PORT_PROBE_CAPABILITY_FCLASS  },
	{ NULL }
};

static gboolean
parse_caps_gcap (const gchar *response,
                 const GError *error,
                 GValue *result,
                 GError **result_error)
{
    const struct modem_caps *cap = modem_caps;
    guint32 ret = 0;

    /* Some modems (Huawei E160g) won't respond to +GCAP with no SIM, but
     * will respond to ATI.  Ignore the error and continue.
     */
    if (response && strstr (response, "+CME ERROR:"))
        return FALSE;

    while (cap->name) {
        if (strstr (response, cap->name))
            ret |= cap->bits;
        cap++;
    }

    /* No result built? */
    if (ret == 0)
        return FALSE;

    g_value_init (result, G_TYPE_UINT);
    g_value_set_uint (result, (guint)ret);
    return TRUE;
}

static gboolean
parse_caps_cpin (const gchar *response,
                 const GError *error,
                 GValue *result,
                 GError **result_error)
{
    if (strcasestr (response, "SIM PIN") ||
        strcasestr (response, "SIM PUK") ||
        strcasestr (response, "PH-SIM PIN") ||
        strcasestr (response, "PH-FSIM PIN") ||
        strcasestr (response, "PH-FSIM PUK") ||
        strcasestr (response, "SIM PIN2") ||
        strcasestr (response, "SIM PUK2") ||
        strcasestr (response, "PH-NET PIN") ||
        strcasestr (response, "PH-NET PUK") ||
        strcasestr (response, "PH-NETSUB PIN") ||
        strcasestr (response, "PH-NETSUB PUK") ||
        strcasestr (response, "PH-SP PIN") ||
        strcasestr (response, "PH-SP PUK") ||
        strcasestr (response, "PH-CORP PIN") ||
        strcasestr (response, "PH-CORP PUK") ||
        strcasestr (response, "READY")) {
        /* At least, it's a GSM modem */
        g_value_init (result, G_TYPE_UINT);
        g_value_set_uint (result, MM_PORT_PROBE_CAPABILITY_GSM);
        return TRUE;
    }
    return FALSE;
}

static gboolean
parse_caps_cgmm (const gchar *response,
                 const GError *error,
                 GValue *result,
                 GError **result_error)
{
    if (strstr (response, "GSM900") ||
        strstr (response, "GSM1800") ||
        strstr (response, "GSM1900") ||
        strstr (response, "GSM850")) {
        /* At least, it's a GSM modem */
        g_value_init (result, G_TYPE_UINT);
        g_value_set_uint (result, MM_PORT_PROBE_CAPABILITY_GSM);
        return TRUE;
    }
    return FALSE;
}

static const MMPortProbeAtCommand capabilities_probing[] = {
    { "+GCAP",  parse_caps_gcap },
    { "I",      parse_caps_gcap },
    { "+CPIN?", parse_caps_cpin },
    { "+CGMM",  parse_caps_cgmm },
    { NULL }
};

const MMPortProbeAtCommand *
mm_port_probe_at_command_get_capabilities_probing (void)
{
    return capabilities_probing;
}

/* ---- VENDOR probing ---- */

static gboolean
parse_vendor (const gchar *response,
              const GError *error,
              GValue *result,
              GError **result_error)
{
    gchar *str;

    str = g_strstrip (g_strdelimit (g_strdup (response), "\r\n", ' '));
    g_value_init (result, G_TYPE_STRING);
    g_value_take_string (result, str);
    return TRUE;
}

static const MMPortProbeAtCommand vendor_probing[] = {
    { "+CGMI", parse_vendor },
    { "+GMI",  parse_vendor },
    { "I",     parse_vendor },
    { NULL }
};

const MMPortProbeAtCommand *
mm_port_probe_at_command_get_vendor_probing (void)
{
    return vendor_probing;
}
