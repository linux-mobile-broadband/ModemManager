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
 * Copyright (C) 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <glib.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers.h"
#include "mm-log.h"

/*****************************************************************************/

gchar *
mm_strip_quotes (gchar *str)
{
    gsize len;

    if (!str)
        return NULL;

    len = strlen (str);
    if ((len >= 2) && (str[0] == '"') && (str[len - 1] == '"')) {
        str[0] = ' ';
        str[len - 1] = ' ';
    }

    return g_strstrip (str);
}

const gchar *
mm_strip_tag (const gchar *str, const gchar *cmd)
{
    const gchar *p = str;

    if (p) {
        if (!strncmp (p, cmd, strlen (cmd)))
            p += strlen (cmd);
        while (isspace (*p))
            p++;
    }

    return p;
}

/*****************************************************************************/

guint
mm_count_bits_set (gulong number)
{
    guint c;

    for (c = 0; number; c++)
        number &= number - 1;
    return c;
}

/*****************************************************************************/

gchar *
mm_create_device_identifier (guint vid,
                             guint pid,
                             const gchar *ati,
                             const gchar *ati1,
                             const gchar *gsn,
                             const gchar *revision,
                             const gchar *model,
                             const gchar *manf)
{
    GString *devid, *msg = NULL;
    GChecksum *sum;
    gchar *p, *ret = NULL;
    gchar str_vid[10], str_pid[10];

    /* Build up the device identifier */
    devid = g_string_sized_new (50);
    if (ati)
        g_string_append (devid, ati);
    if (ati1) {
        /* Only append "ATI1" if it's differnet than "ATI" */
        if (!ati || (strcmp (ati, ati1) != 0))
            g_string_append (devid, ati1);
    }
    if (gsn)
        g_string_append (devid, gsn);
    if (revision)
        g_string_append (devid, revision);
    if (model)
        g_string_append (devid, model);
    if (manf)
        g_string_append (devid, manf);

    if (!strlen (devid->str)) {
        g_string_free (devid, TRUE);
        return NULL;
    }

    p = devid->str;
    msg = g_string_sized_new (strlen (devid->str) + 17);

    sum = g_checksum_new (G_CHECKSUM_SHA1);

    if (vid) {
        snprintf (str_vid, sizeof (str_vid) - 1, "%08x", vid);
        g_checksum_update (sum, (const guchar *) &str_vid[0], strlen (str_vid));
        g_string_append_printf (msg, "%08x", vid);
    }
    if (vid) {
        snprintf (str_pid, sizeof (str_pid) - 1, "%08x", pid);
        g_checksum_update (sum, (const guchar *) &str_pid[0], strlen (str_pid));
        g_string_append_printf (msg, "%08x", pid);
    }

    while (*p) {
        /* Strip spaces and linebreaks */
        if (!isblank (*p) && !isspace (*p) && isascii (*p)) {
            g_checksum_update (sum, (const guchar *) p, 1);
            g_string_append_c (msg, *p);
        }
        p++;
    }
    ret = g_strdup (g_checksum_get_string (sum));
    g_checksum_free (sum);

    mm_dbg ("Device ID source '%s'", msg->str);
    mm_dbg ("Device ID '%s'", ret);
    g_string_free (msg, TRUE);
    g_string_free (devid, TRUE);

    return ret;
}

/*****************************************************************************/

guint
mm_netmask_to_cidr (const gchar *netmask)
{
    guint32 num = 0;

    inet_pton (AF_INET, netmask, &num);
    return mm_count_bits_set (num);
}

/*****************************************************************************/

GArray *
mm_filter_current_bands (const GArray *supported_bands,
                         const GArray *current_bands)
{
    /* We will assure that the list given in 'current' bands maps the list
     * given in 'supported' bands, unless 'UNKNOWN' or 'ANY' is given, of
     * course */
    guint i;
    GArray *filtered;

    if (!supported_bands ||
        supported_bands->len == 0 ||
        !current_bands ||
        current_bands->len == 0)
        return NULL;

    if (supported_bands->len == 1 &&
        (g_array_index (supported_bands, MMModemBand, 0) == MM_MODEM_BAND_UNKNOWN ||
         g_array_index (supported_bands, MMModemBand, 0) == MM_MODEM_BAND_ANY))
        return NULL;

    if (current_bands->len == 1 &&
        (g_array_index (current_bands, MMModemBand, 0) == MM_MODEM_BAND_UNKNOWN ||
         g_array_index (current_bands, MMModemBand, 0) == MM_MODEM_BAND_ANY))
        return NULL;

    filtered = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), current_bands->len);

    for (i = 0; i < current_bands->len; i++) {
        guint j;

        for (j = 0; j < supported_bands->len; j++) {
            if (g_array_index (supported_bands, MMModemBand, j) == g_array_index (current_bands, MMModemBand, i)) {
                g_array_append_val (filtered, g_array_index (current_bands, MMModemBand, i));
                /* Found */
                break;
            }
        }
    }

    if (filtered->len == 0) {
        g_array_unref (filtered);
        return NULL;
    }

    return filtered;
}

/*****************************************************************************/

/* +CREG: <stat>                      (GSM 07.07 CREG=1 unsolicited) */
#define CREG1 "\\+(CREG|CGREG|CEREG):\\s*0*([0-9])"

/* +CREG: <n>,<stat>                  (GSM 07.07 CREG=1 solicited) */
#define CREG2 "\\+(CREG|CGREG|CEREG):\\s*0*([0-9]),\\s*0*([0-9])"

/* +CREG: <stat>,<lac>,<ci>           (GSM 07.07 CREG=2 unsolicited) */
#define CREG3 "\\+(CREG|CGREG|CEREG):\\s*0*([0-9]),\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)"

/* +CREG: <n>,<stat>,<lac>,<ci>       (GSM 07.07 solicited and some CREG=2 unsolicited) */
#define CREG4 "\\+(CREG|CGREG|CEREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*([^,]*)\\s*,\\s*([^,\\s]*)"

/* +CREG: <stat>,<lac>,<ci>,<AcT>     (ETSI 27.007 CREG=2 unsolicited) */
#define CREG5 "\\+(CREG|CGREG|CEREG):\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])"

/* +CREG: <n>,<stat>,<lac>,<ci>,<AcT> (ETSI 27.007 solicited and some CREG=2 unsolicited) */
#define CREG6 "\\+(CREG|CGREG|CEREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])"

/* +CREG: <n>,<stat>,<lac>,<ci>,<AcT?>,<something> (Samsung Wave S8500) */
/* '<CR><LF>+CREG: 2,1,000B,2816, B, C2816<CR><LF><CR><LF>OK<CR><LF>' */
#define CREG7 "\\+(CREG|CGREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*[^,\\s]*"

/* +CREG: <stat>,<lac>,<ci>,<AcT>,<RAC> (ETSI 27.007 v9.20 CREG=2 unsolicited with RAC) */
#define CREG8 "\\+(CREG|CGREG):\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])\\s*,\\s*([^,\\s]*)"

/* +CEREG: <stat>,<lac>,<rac>,<ci>,<AcT>     (ETSI 27.007 v8.6 CREG=2 unsolicited with RAC) */
#define CEREG1 "\\+(CEREG):\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])"

/* +CEREG: <n>,<stat>,<lac>,<rac>,<ci>,<AcT> (ETSI 27.007 v8.6 CREG=2 solicited with RAC) */
#define CEREG2 "\\+(CEREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])"

GPtrArray *
mm_3gpp_creg_regex_get (gboolean solicited)
{
    GPtrArray *array = g_ptr_array_sized_new (10);
    GRegex *regex;

    /* #1 */
    if (solicited)
        regex = g_regex_new (CREG1 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG1 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* #2 */
    if (solicited)
        regex = g_regex_new (CREG2 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG2 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* #3 */
    if (solicited)
        regex = g_regex_new (CREG3 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG3 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* #4 */
    if (solicited)
        regex = g_regex_new (CREG4 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG4 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* #5 */
    if (solicited)
        regex = g_regex_new (CREG5 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG5 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* #6 */
    if (solicited)
        regex = g_regex_new (CREG6 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG6 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* #7 */
    if (solicited)
        regex = g_regex_new (CREG7 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG7 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* #8 */
    if (solicited)
        regex = g_regex_new (CREG8 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CREG8 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* CEREG #1 */
    if (solicited)
        regex = g_regex_new (CEREG1 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CEREG1 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    /* CEREG #2 */
    if (solicited)
        regex = g_regex_new (CEREG2 "$", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    else
        regex = g_regex_new ("\\r\\n" CEREG2 "\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (regex);
    g_ptr_array_add (array, regex);

    return array;
}

void
mm_3gpp_creg_regex_destroy (GPtrArray *array)
{
    g_ptr_array_foreach (array, (GFunc) g_regex_unref, NULL);
    g_ptr_array_free (array, TRUE);
}

/*************************************************************************/

GRegex *
mm_3gpp_ciev_regex_get (void)
{
    return g_regex_new ("\\r\\n\\+CIEV: (.*),(\\d)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

/*************************************************************************/

GRegex *
mm_3gpp_cusd_regex_get (void)
{
    return g_regex_new ("\\r\\n\\+CUSD:\\s*(.*)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

/*************************************************************************/

GRegex *
mm_3gpp_cmti_regex_get (void)
{
    return g_regex_new ("\\r\\n\\+CMTI: \"(\\S+)\",(\\d+)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

GRegex *
mm_3gpp_cds_regex_get (void)
{
    /* Example:
     * <CR><LF>+CDS: 24<CR><LF>07914356060013F10659098136395339F6219011707193802190117071938030<CR><LF>
     */
    return g_regex_new ("\\r\\n\\+CDS:\\s*(\\d+)\\r\\n(.*)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

/*************************************************************************/

static void
mm_3gpp_network_info_free (MM3gppNetworkInfo *info)
{
    g_free (info->operator_long);
    g_free (info->operator_short);
    g_free (info->operator_code);
    g_free (info);
}

void
mm_3gpp_network_info_list_free (GList *info_list)
{
    g_list_free_full (info_list, (GDestroyNotify) mm_3gpp_network_info_free);
}

static MMModemAccessTechnology
get_mm_access_tech_from_etsi_access_tech (guint act)
{
    /* See ETSI TS 27.007 */
    switch (act) {
    case 0:
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case 1:
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT;
    case 2:
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 3:
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    case 4:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    case 5:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    case 6:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
    case 7:
        return MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    default:
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }
}

static MMModem3gppNetworkAvailability
parse_network_status (const gchar *str)
{
    /* Expecting a value between '0' and '3' inclusive */
    if (!str ||
        strlen (str) != 1 ||
        str[0] < '0' ||
        str[0] > '3') {
        mm_warn ("Cannot parse network status: '%s'", str);
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN;
    }

    return (MMModem3gppNetworkAvailability) (str[0] - '0');
}

static MMModemAccessTechnology
parse_access_tech (const gchar *str)
{
    /* Recognized access technologies are between '0' and '7' inclusive... */
    if (!str ||
        strlen (str) != 1 ||
        str[0] < '0' ||
        str[0] > '7') {
        mm_warn ("Cannot parse access tech: '%s'", str);
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }

    return get_mm_access_tech_from_etsi_access_tech (str[0] - '0');
}

GList *
mm_3gpp_parse_cops_test_response (const gchar *reply,
                                  GError **error)
{
    GRegex *r;
    GList *info_list = NULL;
    GMatchInfo *match_info;
    gboolean umts_format = TRUE;
    GError *inner_error = NULL;

    g_return_val_if_fail (reply != NULL, NULL);
    if (error)
        g_return_val_if_fail (*error == NULL, NULL);

    if (!strstr (reply, "+COPS: ")) {
        g_set_error_literal (error,
                             MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Could not parse scan results.");
        return NULL;
    }

    reply = strstr (reply, "+COPS: ") + 7;

    /* Cell access technology (GSM, UTRAN, etc) got added later and not all
     * modems implement it.  Some modesm have quirks that make it hard to
     * use one regular experession for matching both pre-UMTS and UMTS
     * responses.  So try UMTS-format first and fall back to pre-UMTS if
     * we get no UMTS-formst matches.
     */

    /* Quirk: Sony-Ericsson TM-506 sometimes includes a stray ')' like so,
     *        which is what makes it hard to match both pre-UMTS and UMTS in
     *        the same regex:
     *
     *       +COPS: (2,"","T-Mobile","31026",0),(1,"AT&T","AT&T","310410"),0)
     */

    r = g_regex_new ("\\((\\d),([^,\\)]*),([^,\\)]*),([^,\\)]*)[\\)]?,(\\d)\\)", G_REGEX_UNGREEDY, 0, &inner_error);
    if (inner_error) {
        mm_err ("Invalid regular expression: %s", inner_error->message);
        g_error_free (inner_error);
        g_set_error_literal (error,
                             MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Could not parse scan results");
        return NULL;
    }

    /* If we didn't get any hits, try the pre-UMTS format match */
    if (!g_regex_match (r, reply, 0, &match_info)) {
        g_regex_unref (r);
        g_match_info_free (match_info);
        match_info = NULL;

        /* Pre-UMTS format doesn't include the cell access technology after
         * the numeric operator element.
         *
         * Ex: Motorola C-series (BUSlink SCWi275u) like so:
         *
         *       +COPS: (2,"T-Mobile","","310260"),(0,"Cingular Wireless","","310410")
         */

        /* Quirk: Some Nokia phones (N80) don't send the quotes for empty values:
         *
         *       +COPS: (2,"T - Mobile",,"31026"),(1,"Einstein PCS",,"31064"),(1,"Cingular",,"31041"),,(0,1,3),(0,2)
         */

        r = g_regex_new ("\\((\\d),([^,\\)]*),([^,\\)]*),([^\\)]*)\\)", G_REGEX_UNGREEDY, 0, &inner_error);
        if (inner_error) {
            mm_err ("Invalid regular expression: %s", inner_error->message);
            g_error_free (inner_error);
            g_set_error_literal (error,
                                 MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Could not parse scan results");
            return NULL;
        }

        g_regex_match (r, reply, 0, &match_info);
        umts_format = FALSE;
    }

    /* Parse the results */
    while (g_match_info_matches (match_info)) {
        MM3gppNetworkInfo *info;
        gchar *tmp;
        gboolean valid = FALSE;

        info = g_new0 (MM3gppNetworkInfo, 1);

        tmp = mm_get_string_unquoted_from_match_info (match_info, 1);
        info->status = parse_network_status (tmp);
        g_free (tmp);

        info->operator_long = mm_get_string_unquoted_from_match_info (match_info, 2);
        info->operator_short = mm_get_string_unquoted_from_match_info (match_info, 3);
        info->operator_code = mm_get_string_unquoted_from_match_info (match_info, 4);

        /* Only try for access technology with UMTS-format matches.
         * If none give, assume GSM */
        tmp = (umts_format ?
               mm_get_string_unquoted_from_match_info (match_info, 5) :
               NULL);
        info->access_tech = (tmp ?
                             parse_access_tech (tmp) :
                             MM_MODEM_ACCESS_TECHNOLOGY_GSM);
        g_free (tmp);

        /* If the operator number isn't valid (ie, at least 5 digits),
         * ignore the scan result; it's probably the parameter stuff at the
         * end of the +COPS response.  The regex will sometimes catch this
         * but there's no good way to ignore it.
         */
        if (info->operator_code && (strlen (info->operator_code) >= 5)) {
            valid = TRUE;
            tmp = info->operator_code;
            while (*tmp) {
                if (!isdigit (*tmp) && (*tmp != '-')) {
                    valid = FALSE;
                    break;
                }
                tmp++;
            }
        }

        if (valid) {
            gchar *access_tech_str;

            access_tech_str = mm_modem_access_technology_build_string_from_mask (info->access_tech);
            mm_dbg ("Found network '%s' ('%s','%s'); availability: %s, access tech: %s",
                    info->operator_code,
                    info->operator_short ? info->operator_short : "no short name",
                    info->operator_long ? info->operator_long : "no long name",
                    mm_modem_3gpp_network_availability_get_string (info->status),
                    access_tech_str);
            g_free (access_tech_str);

            info_list = g_list_prepend (info_list, info);
        }
        else
            mm_3gpp_network_info_free (info);

        g_match_info_next (match_info, NULL);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    return info_list;
}

/*************************************************************************/

static void
mm_3gpp_pdp_context_free (MM3gppPdpContext *pdp)
{
    g_free (pdp->apn);
    g_slice_free (MM3gppPdpContext, pdp);
}

void
mm_3gpp_pdp_context_list_free (GList *list)
{
    g_list_free_full (list, (GDestroyNotify) mm_3gpp_pdp_context_free);
}

static gint
mm_3gpp_pdp_context_cmp (MM3gppPdpContext *a,
                         MM3gppPdpContext *b)
{
    return (a->cid - b->cid);
}

GList *
mm_3gpp_parse_cgdcont_read_response (const gchar *reply,
                                     GError **error)
{
    GError *inner_error = NULL;
    GRegex *r;
    GMatchInfo *match_info;
    GList *list;

    if (!reply[0])
        /* No APNs configured, all done */
        return NULL;

    list = NULL;
    r = g_regex_new ("\\+CGDCONT:\\s*(\\d+)\\s*,([^,\\)]*),([^,\\)]*),([^,\\)]*)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, &inner_error);
    if (r) {
        g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, &inner_error);

        while (!inner_error &&
               g_match_info_matches (match_info)) {
            gchar *str;
            MMBearerIpFamily ip_family;

            str = mm_get_string_unquoted_from_match_info (match_info, 2);
            ip_family = mm_3gpp_get_ip_family_from_pdp_type (str);
            if (ip_family == MM_BEARER_IP_FAMILY_UNKNOWN)
                mm_dbg ("Ignoring PDP context type: '%s'", str);
            else {
                MM3gppPdpContext *pdp;

                pdp = g_slice_new0 (MM3gppPdpContext);
                if (!mm_get_uint_from_match_info (match_info, 1, &pdp->cid)) {
                    inner_error = g_error_new (MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "Couldn't parse CID from reply: '%s'",
                                               reply);
                    break;
                }
                pdp->pdp_type = ip_family;
                pdp->apn = mm_get_string_unquoted_from_match_info (match_info, 3);

                list = g_list_prepend (list, pdp);
            }

            g_free (str);
            g_match_info_next (match_info, &inner_error);
        }

        g_match_info_free (match_info);
        g_regex_unref (r);
    }

    if (inner_error) {
        mm_3gpp_pdp_context_list_free (list);
        g_propagate_error (error, inner_error);
        g_prefix_error (error, "Couldn't properly parse list of PDP contexts. ");
        return NULL;
    }

    list = g_list_sort (list, (GCompareFunc)mm_3gpp_pdp_context_cmp);

    return list;
}

/*************************************************************************/

static gulong
parse_uint (char *str, int base, glong nmin, glong nmax, gboolean *valid)
{
    gulong ret = 0;
    gchar *endquote;

    *valid = FALSE;
    if (!str)
        return 0;

    /* Strip quotes */
    if (str[0] == '"')
        str++;
    endquote = strchr (str, '"');
    if (endquote)
        *endquote = '\0';

    if (strlen (str)) {
        ret = strtol (str, NULL, base);
        if ((nmin == nmax) || (ret >= nmin && ret <= nmax))
            *valid = TRUE;
    }
    return *valid ? (guint) ret : 0;
}

static gboolean
item_is_lac_not_stat (GMatchInfo *info, guint32 item)
{
    gchar *str;
    gboolean is_lac = FALSE;

    /* A <stat> will always be a single digit, without quotes */
    str = g_match_info_fetch (info, item);
    g_assert (str);
    is_lac = (strchr (str, '"') || strlen (str) > 1);
    g_free (str);
    return is_lac;
}

gboolean
mm_3gpp_parse_creg_response (GMatchInfo *info,
                             MMModem3gppRegistrationState *out_reg_state,
                             gulong *out_lac,
                             gulong *out_ci,
                             MMModemAccessTechnology *out_act,
                             gboolean *out_cgreg,
                             gboolean *out_cereg,
                             GError **error)
{
    gboolean success = FALSE, foo;
    gint n_matches, act = -1;
    gulong stat = 0, lac = 0, ci = 0;
    guint istat = 0, ilac = 0, ici = 0, iact = 0;
    gchar *str;

    g_return_val_if_fail (info != NULL, FALSE);
    g_return_val_if_fail (out_reg_state != NULL, FALSE);
    g_return_val_if_fail (out_lac != NULL, FALSE);
    g_return_val_if_fail (out_ci != NULL, FALSE);
    g_return_val_if_fail (out_act != NULL, FALSE);
    g_return_val_if_fail (out_cgreg != NULL, FALSE);
    g_return_val_if_fail (out_cereg != NULL, FALSE);

    str = g_match_info_fetch (info, 1);
    *out_cgreg = (str && strstr (str, "CGREG")) ? TRUE : FALSE;
    *out_cereg = (str && strstr (str, "CEREG")) ? TRUE : FALSE;
    g_free (str);

    /* Normally the number of matches could be used to determine what each
     * item is, but we have overlap in one case.
     */
    n_matches = g_match_info_get_match_count (info);
    if (n_matches == 3) {
        /* CREG=1: +CREG: <stat> */
        istat = 2;
    } else if (n_matches == 4) {
        /* Solicited response: +CREG: <n>,<stat> */
        istat = 3;
    } else if (n_matches == 5) {
        /* CREG=2 (GSM 07.07): +CREG: <stat>,<lac>,<ci> */
        istat = 2;
        ilac = 3;
        ici = 4;
    } else if (n_matches == 6) {
        /* CREG=2 (ETSI 27.007): +CREG: <stat>,<lac>,<ci>,<AcT>
         * CREG=2 (non-standard): +CREG: <n>,<stat>,<lac>,<ci>
         */

        /* Check if the third item is the LAC to distinguish the two cases */
        if (item_is_lac_not_stat (info, 3)) {
            istat = 2;
            ilac = 3;
            ici = 4;
            iact = 5;
        } else {
            istat = 3;
            ilac = 4;
            ici = 5;
        }
    } else if (n_matches == 7) {
        /* CREG=2 (solicited):            +CREG: <n>,<stat>,<lac>,<ci>,<AcT>
         * CREG=2 (unsolicited with RAC): +CREG: <stat>,<lac>,<ci>,<AcT>,<RAC>
         * CEREG=2 (solicited):           +CEREG: <n>,<stat>,<lac>,<ci>,<AcT>
         * CEREG=2 (unsolicited with RAC): +CEREG: <stat>,<lac>,<rac>,<ci>,<AcT>
         */

        if (*out_cereg) {
            /* Check if the third item is the LAC to distinguish the two cases */
            if (item_is_lac_not_stat (info, 3)) {
                istat = 2;
                ilac = 3;
            } else {
                istat = 3;
                ilac = 4;
            }
            ici = 5;
            iact = 6;
        } else {
            /* Check if the third item is the LAC to distinguish the two cases */
            if (item_is_lac_not_stat (info, 3)) {
                istat = 2;
                ilac = 3;
                ici = 4;
                iact = 5;
            } else {
                istat = 3;
                ilac = 4;
                ici = 5;
                iact = 6;
            }
        }
    } else if (n_matches == 8) {
        /* CEREG=2 (solicited with RAC):  +CEREG: <n>,<stat>,<lac>,<rac>,<ci>,<AcT>
         */
        if (*out_cereg) {
            istat = 3;
            ilac = 4;
            ici = 6;
            iact = 7;
        }
     }

    /* Status */
    str = g_match_info_fetch (info, istat);
    stat = parse_uint (str, 10, 0, 5, &success);
    g_free (str);
    if (!success) {
        g_set_error_literal (error,
                             MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Could not parse the registration status response");
        return FALSE;
    }

    /* Location Area Code */
    if (ilac) {
        /* FIXME: some phones apparently swap the LAC bytes (LG, SonyEricsson,
         * Sagem).  Need to handle that.
         */
        str = g_match_info_fetch (info, ilac);
        lac = parse_uint (str, 16, 1, 0xFFFF, &foo);
        g_free (str);
    }

    /* Cell ID */
    if (ici) {
        str = g_match_info_fetch (info, ici);
        ci = parse_uint (str, 16, 1, 0x0FFFFFFE, &foo);
        g_free (str);
    }

    /* Access Technology */
    if (iact) {
        str = g_match_info_fetch (info, iact);
        act = (gint) parse_uint (str, 10, 0, 7, &foo);
        g_free (str);
        if (!foo)
            act = -1;
    }

    /* 'roaming' is the last valid state */
    if (stat > MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        mm_warn ("Registration State '%lu' is unknown", stat);
        stat = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    }

    *out_reg_state = (MMModem3gppRegistrationState) stat;
    if (stat != MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN) {
        /* Don't fill in lac/ci/act if the device's state is unknown */
        *out_lac = lac;
        *out_ci = ci;

        *out_act = get_mm_access_tech_from_etsi_access_tech (act);
    }
    return TRUE;
}

/*************************************************************************/

#define CMGF_TAG "+CMGF:"

gboolean
mm_3gpp_parse_cmgf_test_response (const gchar *reply,
                                  gboolean *sms_pdu_supported,
                                  gboolean *sms_text_supported,
                                  GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    gchar *s;
    guint32 min = -1, max = -1;

    /* Strip whitespace and response tag */
    if (g_str_has_prefix (reply, CMGF_TAG))
        reply += strlen (CMGF_TAG);
    while (isspace (*reply))
        reply++;

    r = g_regex_new ("\\(?\\s*(\\d+)\\s*[-,]?\\s*(\\d+)?\\s*\\)?", 0, 0, error);
    if (!r)
        return FALSE;

    if (!g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, NULL)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse CMGF query result '%s'",
                     reply);
        g_match_info_free (match_info);
        g_regex_unref (r);
        return FALSE;
    }

    s = g_match_info_fetch (match_info, 1);
    if (s)
        min = atoi (s);
    g_free (s);

    s = g_match_info_fetch (match_info, 2);
    if (s)
        max = atoi (s);
    g_free (s);

    /* CMGF=0 for PDU mode */
    *sms_pdu_supported = (min == 0);

    /* CMGF=1 for Text mode */
    *sms_text_supported = (max >= 1);

    g_match_info_free (match_info);
    g_regex_unref (r);
    return TRUE;
}

/*************************************************************************/

static MMSmsStorage
storage_from_str (const gchar *str)
{
    if (g_str_equal (str, "SM"))
        return MM_SMS_STORAGE_SM;
    if (g_str_equal (str, "ME"))
        return MM_SMS_STORAGE_ME;
    if (g_str_equal (str, "MT"))
        return MM_SMS_STORAGE_MT;
    if (g_str_equal (str, "SR"))
        return MM_SMS_STORAGE_SR;
    if (g_str_equal (str, "BM"))
        return MM_SMS_STORAGE_BM;
    if (g_str_equal (str, "TA"))
        return MM_SMS_STORAGE_TA;
    return MM_SMS_STORAGE_UNKNOWN;
}

gboolean
mm_3gpp_parse_cpms_test_response (const gchar *reply,
                                  GArray **mem1,
                                  GArray **mem2,
                                  GArray **mem3)
{
    GRegex *r;
    gchar **split;
    guint i;

    g_assert (mem1 != NULL);
    g_assert (mem2 != NULL);
    g_assert (mem3 != NULL);

    /*
     * +CPMS: ("SM","ME"),("SM","ME"),("SM","ME")
     */
    split = g_strsplit_set (mm_strip_tag (reply, "+CPMS:"), "()", -1);
    if (!split)
        return FALSE;

    r = g_regex_new ("\\s*\"([^,\\)]+)\"\\s*", 0, 0, NULL);
    g_assert (r);

    for (i = 0; split[i]; i++) {
        GMatchInfo *match_info;

        /* Got a range group to match */
        if (g_regex_match_full (r, split[i], strlen (split[i]), 0, 0, &match_info, NULL)) {
            GArray *array = NULL;

            while (g_match_info_matches (match_info)) {
                gchar *str;

                str = g_match_info_fetch (match_info, 1);
                if (str) {
                    MMSmsStorage storage;

                    if (!array)
                        array = g_array_new (FALSE, FALSE, sizeof (MMSmsStorage));

                    storage = storage_from_str (str);
                    g_array_append_val (array, storage);
                    g_free (str);
                }

                g_match_info_next (match_info, NULL);
            }

            if (!*mem1)
                *mem1 = array;
            else if (!*mem2)
                *mem2 = array;
            else if (!*mem3)
                *mem3 = array;
        }
        g_match_info_free (match_info);

        if (*mem3 != NULL)
            break; /* once we got the last group, exit... */
    }

    g_strfreev (split);
    g_regex_unref (r);

    g_warn_if_fail (*mem1 != NULL);
    g_warn_if_fail (*mem2 != NULL);
    g_warn_if_fail (*mem3 != NULL);

    return (*mem1 && *mem2 && *mem3);
}

/*************************************************************************/

gboolean
mm_3gpp_parse_cscs_test_response (const gchar *reply,
                                  MMModemCharset *out_charsets)
{
    MMModemCharset charsets = MM_MODEM_CHARSET_UNKNOWN;
    GRegex *r;
    GMatchInfo *match_info;
    gchar *p, *str;
    gboolean success = FALSE;

    g_return_val_if_fail (reply != NULL, FALSE);
    g_return_val_if_fail (out_charsets != NULL, FALSE);

    /* Find the first '(' or '"'; the general format is:
     *
     * +CSCS: ("IRA","GSM","UCS2")
     *
     * but some devices (some Blackberries) don't include the ().
     */
    p = strchr (reply, '(');
    if (p)
        p++;
    else {
        p = strchr (reply, '"');
        if (!p)
            return FALSE;
    }

    /* Now parse each charset */
    r = g_regex_new ("\\s*([^,\\)]+)\\s*", 0, 0, NULL);
    if (!r)
        return FALSE;

    if (g_regex_match_full (r, p, strlen (p), 0, 0, &match_info, NULL)) {
        while (g_match_info_matches (match_info)) {
            str = g_match_info_fetch (match_info, 1);
            charsets |= mm_modem_charset_from_string (str);
            g_free (str);

            g_match_info_next (match_info, NULL);
            success = TRUE;
        }
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    if (success)
        *out_charsets = charsets;

    return success;
}

/*************************************************************************/

gboolean
mm_3gpp_parse_clck_test_response (const gchar *reply,
                                  MMModem3gppFacility *out_facilities)
{
    GRegex *r;
    GMatchInfo *match_info;

    g_return_val_if_fail (reply != NULL, FALSE);
    g_return_val_if_fail (out_facilities != NULL, FALSE);

    /* the general format is:
     *
     * +CLCK: ("SC","AO","AI","PN")
     */
    reply = mm_strip_tag (reply, "+CLCK:");

    /* Now parse each facility */
    r = g_regex_new ("\\s*\"([^,\\)]+)\"\\s*", 0, 0, NULL);
    g_assert (r != NULL);

    *out_facilities = MM_MODEM_3GPP_FACILITY_NONE;
    if (g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, NULL)) {
        while (g_match_info_matches (match_info)) {
            gchar *str;

            str = g_match_info_fetch (match_info, 1);
            if (str) {
                *out_facilities |= mm_3gpp_acronym_to_facility (str);
                g_free (str);
            }

            g_match_info_next (match_info, NULL);
        }
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return (*out_facilities != MM_MODEM_3GPP_FACILITY_NONE);
}

/*************************************************************************/

gboolean
mm_3gpp_parse_clck_write_response (const gchar *reply,
                                   gboolean *enabled)
{
    GRegex *r;
    GMatchInfo *match_info;
    gboolean success = FALSE;

    g_return_val_if_fail (reply != NULL, FALSE);
    g_return_val_if_fail (enabled != NULL, FALSE);

    reply = mm_strip_tag (reply, "+CLCK:");

    r = g_regex_new ("\\s*([01])\\s*", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match (r, reply, 0, &match_info)) {
        gchar *str;

        str = g_match_info_fetch (match_info, 1);
        if (str) {
            /* We're trying to match either '0' or '1',
             * so we don't expect any other thing */
            if (*str == '0')
                *enabled = FALSE;
            else if (*str == '1')
                *enabled = TRUE;
            else
                g_assert_not_reached ();

            g_free (str);
            success = TRUE;
        }
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return success;
}

/*************************************************************************/

GStrv
mm_3gpp_parse_cnum_exec_response (const gchar *reply,
                                  GError **error)
{
    GArray *array = NULL;
    GRegex *r;
    GMatchInfo *match_info;

    /* Empty strings also return NULL list */
    if (!reply || !reply[0])
        return NULL;

    r = g_regex_new ("\\+CNUM:\\s*((\"([^\"]|(\\\"))*\")|([^,]*)),\"(?<num>\\S+)\",\\d",
                     G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, reply, 0, &match_info);
    while (g_match_info_matches (match_info)) {
        gchar *number;

        number = g_match_info_fetch_named (match_info, "num");

        if (number && number[0]) {
            if (!array)
                array = g_array_new (TRUE, TRUE, sizeof (gchar *));
            g_array_append_val (array, number);
        } else
            g_free (number);

        g_match_info_next (match_info, NULL);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    return (array ? (GStrv) g_array_free (array, FALSE) : NULL);
}

/*************************************************************************/

struct MM3gppCindResponse {
    gchar *desc;
    guint idx;
    gint min;
    gint max;
};

static MM3gppCindResponse *
cind_response_new (const gchar *desc, guint idx, gint min, gint max)
{
    MM3gppCindResponse *r;
    gchar *p;

    g_return_val_if_fail (desc != NULL, NULL);
    g_return_val_if_fail (idx >= 0, NULL);

    r = g_malloc0 (sizeof (MM3gppCindResponse));

    /* Strip quotes */
    r->desc = p = g_malloc0 (strlen (desc) + 1);
    while (*desc) {
        if (*desc != '"' && !isspace (*desc))
            *p++ = tolower (*desc);
        desc++;
    }

    r->idx = idx;
    r->max = max;
    r->min = min;
    return r;
}

static void
cind_response_free (MM3gppCindResponse *r)
{
    g_return_if_fail (r != NULL);

    g_free (r->desc);
    memset (r, 0, sizeof (MM3gppCindResponse));
    g_free (r);
}

const gchar *
mm_3gpp_cind_response_get_desc (MM3gppCindResponse *r)
{
    g_return_val_if_fail (r != NULL, NULL);

    return r->desc;
}

guint
mm_3gpp_cind_response_get_index (MM3gppCindResponse *r)
{
    g_return_val_if_fail (r != NULL, 0);

    return r->idx;
}

gint
mm_3gpp_cind_response_get_min (MM3gppCindResponse *r)
{
    g_return_val_if_fail (r != NULL, -1);

    return r->min;
}

gint
mm_3gpp_cind_response_get_max (MM3gppCindResponse *r)
{
    g_return_val_if_fail (r != NULL, -1);

    return r->max;
}

#define CIND_TAG "+CIND:"

GHashTable *
mm_3gpp_parse_cind_test_response (const gchar *reply,
                                  GError **error)
{
    GHashTable *hash;
    GRegex *r;
    GMatchInfo *match_info;
    guint idx = 1;

    g_return_val_if_fail (reply != NULL, NULL);

    /* Strip whitespace and response tag */
    if (g_str_has_prefix (reply, CIND_TAG))
        reply += strlen (CIND_TAG);
    while (isspace (*reply))
        reply++;

    r = g_regex_new ("\\(([^,]*),\\((\\d+)[-,](\\d+).*\\)", G_REGEX_UNGREEDY, 0, NULL);
    if (!r) {
        g_set_error_literal (error,
                             MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Could not parse scan results.");
        return NULL;
    }

    hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) cind_response_free);

    if (g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, NULL)) {
        while (g_match_info_matches (match_info)) {
            MM3gppCindResponse *resp;
            gchar *desc, *tmp;
            gint min = 0, max = 0;

            desc = g_match_info_fetch (match_info, 1);

            tmp = g_match_info_fetch (match_info, 2);
            min = atoi (tmp);
            g_free (tmp);

            tmp = g_match_info_fetch (match_info, 3);
            max = atoi (tmp);
            g_free (tmp);

            resp = cind_response_new (desc, idx++, min, max);
            if (resp)
                g_hash_table_insert (hash, g_strdup (resp->desc), resp);

            g_free (desc);

            g_match_info_next (match_info, NULL);
        }
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return hash;
}

/*************************************************************************/

GByteArray *
mm_3gpp_parse_cind_read_response (const gchar *reply,
                                  GError **error)
{
    GByteArray *array = NULL;
    GRegex *r = NULL;
    GMatchInfo *match_info;
    GError *inner_error = NULL;
    guint8 t;

    g_return_val_if_fail (reply != NULL, NULL);

    if (!g_str_has_prefix (reply, CIND_TAG)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse the +CIND response '%s': no CIND tag found",
                     reply);
        return NULL;
    }

    reply = mm_strip_tag (reply, CIND_TAG);

    r = g_regex_new ("(\\d+)[^0-9]+", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, NULL)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse the +CIND response '%s': didn't match",
                     reply);
        goto done;
    }

    array = g_byte_array_sized_new (g_match_info_get_match_count (match_info));

    /* Add a zero element so callers can use 1-based indexes returned by
     * mm_3gpp_cind_response_get_index().
     */
    t = 0;
    g_byte_array_append (array, &t, 1);

    while (!inner_error &&
           g_match_info_matches (match_info)) {
        gchar *str;
        guint val = 0;

        str = g_match_info_fetch (match_info, 1);
        if (mm_get_uint_from_str (str, &val) && val < 255) {
            t = (guint8) val;
            g_byte_array_append (array, &t, 1);
        } else {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "Could not parse the +CIND response: invalid index '%s'",
                                       str);
        }

        g_free (str);
        g_match_info_next (match_info, NULL);
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_byte_array_unref (array);
        array = NULL;
    }

done:
    g_match_info_free (match_info);
    g_regex_unref (r);

    return array;
}

/*************************************************************************/

/* Map two letter facility codes into flag values. There are
 * many more facilities defined (for various flavors of call
 * barring); we only map the ones we care about. */
typedef struct {
    MMModem3gppFacility facility;
    gchar *acronym;
} FacilityAcronym;

static const FacilityAcronym facility_acronyms[] = {
    { MM_MODEM_3GPP_FACILITY_SIM,           "SC" },
    { MM_MODEM_3GPP_FACILITY_PH_SIM,        "PS" },
    { MM_MODEM_3GPP_FACILITY_PH_FSIM,       "PF" },
    { MM_MODEM_3GPP_FACILITY_FIXED_DIALING, "FD" },
    { MM_MODEM_3GPP_FACILITY_NET_PERS,      "PN" },
    { MM_MODEM_3GPP_FACILITY_NET_SUB_PERS,  "PU" },
    { MM_MODEM_3GPP_FACILITY_PROVIDER_PERS, "PP" },
    { MM_MODEM_3GPP_FACILITY_CORP_PERS,     "PC" }
};

MMModem3gppFacility
mm_3gpp_acronym_to_facility (const gchar *str)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (facility_acronyms); i++) {
        if (g_str_equal (facility_acronyms[i].acronym, str))
            return facility_acronyms[i].facility;
    }

    return MM_MODEM_3GPP_FACILITY_NONE;
}

gchar *
mm_3gpp_facility_to_acronym (MMModem3gppFacility facility)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (facility_acronyms); i++) {
        if (facility_acronyms[i].facility == facility)
            return facility_acronyms[i].acronym;
    }

    return NULL;
}

/*************************************************************************/

MMModemAccessTechnology
mm_string_to_access_tech (const gchar *string)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    g_return_val_if_fail (string != NULL, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    /* We're returning a MASK of technologies found; so we can include more
     * than one technology in the result */
    if (strcasestr (string, "LTE") || strcasestr (string, "4G"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_LTE;

    if (strcasestr (string, "HSPA+"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;
    else if (strcasestr (string, "HSPA"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_HSPA;


    if (strcasestr (string, "HSUPA"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;

    if (strcasestr (string, "HSDPA"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;

    if (strcasestr (string, "UMTS"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_UMTS;

    if (strcasestr (string, "EDGE"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_EDGE;

    if (strcasestr (string, "GPRS"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_GPRS;

    if (strcasestr (string, "GSM"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM;

    if (strcasestr (string, "EvDO Rel0"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;

    if (strcasestr (string, "EvDO RelA"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;

    if (strcasestr (string, "EvDO RelB"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDOB;

    if (strcasestr (string, "1xRTT") || strcasestr (string, "CDMA2000 1X"))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;

    return act;
}

/*************************************************************************/

gchar *
mm_3gpp_parse_operator (const gchar *reply,
                        MMModemCharset cur_charset)
{
    gchar *operator = NULL;

    if (reply && !strncmp (reply, "+COPS: ", 7)) {
        /* Got valid reply */
		GRegex *r;
		GMatchInfo *match_info;

		reply += 7;
		r = g_regex_new ("(\\d),(\\d),\"(.+)\"", G_REGEX_UNGREEDY, 0, NULL);
		if (!r)
            return NULL;

		g_regex_match (r, reply, 0, &match_info);
		if (g_match_info_matches (match_info))
            operator = g_match_info_fetch (match_info, 3);

		g_match_info_free (match_info);
		g_regex_unref (r);
    }

    if (operator) {
        /* Some modems (Option & HSO) return the operator name as a hexadecimal
         * string of the bytes of the operator name as encoded by the current
         * character set.
         */
        if (cur_charset == MM_MODEM_CHARSET_UCS2) {
            /* In this case we're already checking UTF-8 validity */
            operator = mm_charset_take_and_convert_to_utf8 (operator, MM_MODEM_CHARSET_UCS2);
        }
        /* Ensure the operator name is valid UTF-8 so that we can send it
         * through D-Bus and such.
         */
        else if (!g_utf8_validate (operator, -1, NULL)) {
            g_free (operator);
            return NULL;
        }

        /* Some modems (Novatel LTE) return the operator name as "Unknown" when
         * it fails to obtain the operator name. Return NULL in such case.
         */
        if (operator && g_ascii_strcasecmp (operator, "unknown") == 0) {
            g_free (operator);
            return NULL;
        }
    }

    return operator;
}

/*************************************************************************/

const gchar *
mm_3gpp_get_pdp_type_from_ip_family (MMBearerIpFamily family)
{
    switch (family) {
    case MM_BEARER_IP_FAMILY_IPV4:
        return "IP";
    case MM_BEARER_IP_FAMILY_IPV6:
        return "IPV6";
    case MM_BEARER_IP_FAMILY_IPV4V6:
        return "IPV4V6";
    default:
        return NULL;
    }
}

MMBearerIpFamily
mm_3gpp_get_ip_family_from_pdp_type (const gchar *pdp_type)
{
    if (g_str_equal (pdp_type, "IP"))
        return MM_BEARER_IP_FAMILY_IPV4;
    if (g_str_equal (pdp_type, "IPV6"))
        return MM_BEARER_IP_FAMILY_IPV6;
    if (g_str_equal (pdp_type, "IPV4V6"))
        return MM_BEARER_IP_FAMILY_IPV4V6;
    return MM_BEARER_IP_FAMILY_UNKNOWN;
}

/*************************************************************************/

gboolean
mm_3gpp_parse_operator_id (const gchar *operator_id,
                           guint16 *mcc,
                           guint16 *mnc,
                           GError **error)
{
    guint len;
    guint i;
    gchar aux[4];
    guint16 tmp;

    g_assert (operator_id != NULL);

    len = strlen (operator_id);
    if (len != 5 && len != 6) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Operator ID must have 5 or 6 digits");
        return FALSE;
    }

    for (i = 0; i < len; i++) {
        if (!g_ascii_isdigit (operator_id[i])) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Operator ID must only contain digits");
            return FALSE;
        }
    }

    memcpy (&aux[0], operator_id, 3);
    aux[3] = '\0';
    tmp = atoi (aux);
    if (tmp == 0) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "MCC must not be zero");
        return FALSE;
    }

    if (mcc)
        *mcc = tmp;

    if (mnc) {
        if (len == 5) {
            memcpy (&aux[0], &operator_id[3], 2);
            aux[2] = '\0';
        } else
            memcpy (&aux[0], &operator_id[3], 3);
        *mnc = atoi (aux);
    }

    return TRUE;
}

/*************************************************************************/

gboolean
mm_cdma_parse_spservice_read_response (const gchar *reply,
                                       MMModemCdmaRegistrationState *out_cdma_1x_state,
                                       MMModemCdmaRegistrationState *out_evdo_state)
{
    const gchar *p;

    g_return_val_if_fail (reply != NULL, FALSE);
    g_return_val_if_fail (out_cdma_1x_state != NULL, FALSE);
    g_return_val_if_fail (out_evdo_state != NULL, FALSE);

    p = mm_strip_tag (reply, "+SPSERVICE:");
    if (!isdigit (*p))
        return FALSE;

    switch (atoi (p)) {
    case 0:   /* no service */
        *out_cdma_1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        *out_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        break;
    case 1:   /* 1xRTT */
        *out_cdma_1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        *out_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        break;
    case 2:   /* EVDO rev 0 */
    case 3:   /* EVDO rev A */
        *out_cdma_1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        *out_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

/*************************************************************************/

typedef struct {
    gint num;
    gboolean roam_ind;
    const gchar *banner;
} EriItem;

/* NOTE: these may be Sprint-specific for now... */
static const EriItem eris[] = {
    { 0,  TRUE,  "Digital or Analog Roaming" },
    { 1,  FALSE, "Home" },
    { 2,  TRUE,  "Digital or Analog Roaming" },
    { 3,  TRUE,  "Out of neighborhood" },
    { 4,  TRUE,  "Out of building" },
    { 5,  TRUE,  "Preferred system" },
    { 6,  TRUE,  "Available System" },
    { 7,  TRUE,  "Alliance Partner" },
    { 8,  TRUE,  "Premium Partner" },
    { 9,  TRUE,  "Full Service Functionality" },
    { 10, TRUE,  "Partial Service Functionality" },
    { 64, TRUE,  "Preferred system" },
    { 65, TRUE,  "Available System" },
    { 66, TRUE,  "Alliance Partner" },
    { 67, TRUE,  "Premium Partner" },
    { 68, TRUE,  "Full Service Functionality" },
    { 69, TRUE,  "Partial Service Functionality" },
    { 70, TRUE,  "Analog A" },
    { 71, TRUE,  "Analog B" },
    { 72, TRUE,  "CDMA 800 A" },
    { 73, TRUE,  "CDMA 800 B" },
    { 74, TRUE,  "International Roaming" },
    { 75, TRUE,  "Extended Network" },
    { 76, FALSE,  "Campus" },
    { 77, FALSE,  "In Building" },
    { 78, TRUE,  "Regional" },
    { 79, TRUE,  "Community" },
    { 80, TRUE,  "Business" },
    { 81, TRUE,  "Zone 1" },
    { 82, TRUE,  "Zone 2" },
    { 83, TRUE,  "National" },
    { 84, TRUE,  "Local" },
    { 85, TRUE,  "City" },
    { 86, TRUE,  "Government" },
    { 87, TRUE,  "USA" },
    { 88, TRUE,  "State" },
    { 89, TRUE,  "Resort" },
    { 90, TRUE,  "Headquarters" },
    { 91, TRUE,  "Personal" },
    { 92, FALSE,  "Home" },
    { 93, TRUE,  "Residential" },
    { 94, TRUE,  "University" },
    { 95, TRUE,  "College" },
    { 96, TRUE,  "Hotel Guest" },
    { 97, TRUE,  "Rental" },
    { 98, FALSE,  "Corporate" },
    { 99, FALSE,  "Home Provider" },
    { 100, FALSE,  "Campus" },
    { 101, FALSE,  "In Building" },
    { 102, TRUE,  "Regional" },
    { 103, TRUE,  "Community" },
    { 104, TRUE,  "Business" },
    { 105, TRUE,  "Zone 1" },
    { 106, TRUE,  "Zone 2" },
    { 107, TRUE,  "National" },
    { 108, TRUE,  "Local" },
    { 109, TRUE,  "City" },
    { 110, TRUE,  "Government" },
    { 111, TRUE,  "USA" },
    { 112, TRUE,  "State" },
    { 113, TRUE,  "Resort" },
    { 114, TRUE,  "Headquarters" },
    { 115, TRUE,  "Personal" },
    { 116, FALSE,  "Home" },
    { 117, TRUE,  "Residential" },
    { 118, TRUE,  "University" },
    { 119, TRUE,  "College" },
    { 120, TRUE,  "Hotel Guest" },
    { 121, TRUE,  "Rental" },
    { 122, FALSE,  "Corporate" },
    { 123, FALSE,  "Home Provider" },
    { 124, TRUE,  "International" },
    { 125, TRUE,  "International" },
    { 126, TRUE,  "International" },
    { 127, FALSE,  "Premium Service" },
    { 128, FALSE,  "Enhanced Service" },
    { 129, FALSE,  "Enhanced Digital" },
    { 130, FALSE,  "Enhanced Roaming" },
    { 131, FALSE,  "Alliance Service" },
    { 132, FALSE,  "Alliance Network" },
    { 133, FALSE,  "Data Roaming" },    /* Sprint: Vision Roaming */
    { 134, FALSE,  "Extended Service" },
    { 135, FALSE,  "Expanded Services" },
    { 136, FALSE,  "Expanded Network" },
    { 137, TRUE,  "Premium Service" },
    { 138, TRUE,  "Enhanced Service" },
    { 139, TRUE,  "Enhanced Digital" },
    { 140, TRUE,  "Enhanced Roaming" },
    { 141, TRUE,  "Alliance Service" },
    { 142, TRUE,  "Alliance Network" },
    { 143, TRUE,  "Data Roaming" },    /* Sprint: Vision Roaming */
    { 144, TRUE,  "Extended Service" },
    { 145, TRUE,  "Expanded Services" },
    { 146, TRUE,  "Expanded Network" },
    { 147, TRUE,  "Premium Service" },
    { 148, TRUE,  "Enhanced Service" },
    { 149, TRUE,  "Enhanced Digital" },
    { 150, TRUE,  "Enhanced Roaming" },
    { 151, TRUE,  "Alliance Service" },
    { 152, TRUE,  "Alliance Network" },
    { 153, TRUE,  "Data Roaming" },    /* Sprint: Vision Roaming */
    { 154, TRUE,  "Extended Service" },
    { 155, TRUE,  "Expanded Services" },
    { 156, TRUE,  "Expanded Network" },
    { 157, TRUE,  "Premium International" },
    { 158, TRUE,  "Premium International" },
    { 159, TRUE,  "Premium International" },
    { 160, TRUE,  NULL },
    { 161, TRUE,  NULL },
    { 162, FALSE,  NULL },
    { 163, FALSE,  NULL },
    { 164, FALSE,  "Extended Voice/Data Network" },
    { 165, FALSE,  "Extended Voice/Data Network" },
    { 166, TRUE,  "Extended Voice/Data Network" },
    { 167, FALSE,  "Extended Broadband" },
    { 168, FALSE,  "Extended Broadband" },
    { 169, TRUE,  "Extended Broadband" },
    { 170, FALSE,  "Extended Data" },
    { 171, FALSE,  "Extended Data" },
    { 172, TRUE,  "Extended Data" },
    { 173, FALSE,  "Extended Data Network" },
    { 174, FALSE,  "Extended Data Network" },
    { 175, TRUE,  "Extended Data Network" },
    { 176, FALSE,  "Extended Network" },
    { 177, FALSE,  "Extended Network" },
    { 178, TRUE,  "Extended Network" },
    { 179, FALSE,  "Extended Service" },
    { 180, TRUE,  "Extended Service" },
    { 181, FALSE,  "Extended Voice" },
    { 182, FALSE,  "Extended Voice" },
    { 183, TRUE,  "Extended Voice" },
    { 184, FALSE,  "Extended Voice/Data" },
    { 185, FALSE,  "Extended Voice/Data" },
    { 186, TRUE,  "Extended Voice/Data" },
    { 187, FALSE,  "Extended Voice Network" },
    { 188, FALSE,  "Extended Voice Network" },
    { 189, TRUE,  "Extended Voice Network" },
    { 190, FALSE,  "Extended Voice/Data" },
    { 191, FALSE,  "Extended Voice/Data" },
    { 192, TRUE,  "Extended Voice/Data" },
    { 193, TRUE,  "International" },
    { 194, FALSE,  "International Services" },
    { 195, FALSE,  "International Voice" },
    { 196, FALSE,  "International Voice/Data" },
    { 197, FALSE,  "International Voice/Data" },
    { 198, TRUE,  "International Voice/Data" },
    { 199, FALSE,  "Extended Voice/Data Network" },
    { 200, TRUE,  "Extended Voice/Data Network" },
    { 201, TRUE,  "Extended Voice/Data Network" },
    { 202, FALSE,  "Extended Broadband" },
    { 203, TRUE,  "Extended Broadband" },
    { 204, TRUE,  "Extended Broadband" },
    { 205, FALSE,  "Extended Data" },
    { 206, TRUE,  "Extended Data" },
    { 207, TRUE,  "Extended Data" },
    { 208, FALSE,  "Extended Data Network" },
    { 209, TRUE,  "Extended Data Network" },
    { 210, TRUE,  "Extended Data Network" },
    { 211, FALSE,  "Extended Network" },
    { 212, TRUE,  "Extended Network" },
    { 213, FALSE,  "Extended Service" },
    { 214, TRUE,  "Extended Service" },
    { 215, TRUE,  "Extended Service" },
    { 216, FALSE,  "Extended Voice" },
    { 217, TRUE,  "Extended Voice" },
    { 218, TRUE,  "Extended Voice" },
    { 219, FALSE,  "Extended Voice/Data" },
    { 220, TRUE,  "Extended Voice/Data" },
    { 221, TRUE,  "Extended Voice/Data" },
    { 222, FALSE,  "Extended Voice Network" },
    { 223, FALSE,  "Extended Voice Network" },
    { 224, TRUE,  "Extended Voice Network" },
    { 225, FALSE,  "Extended Voice/Data" },
    { 226, TRUE,  "Extended Voice/Data" },
    { 227, TRUE,  "Extended Voice/Data" },
    { 228, TRUE,  "International" },
    { 229, TRUE,  "International" },
    { 230, TRUE,  "International Services" },
    { 231, TRUE,  "International Voice" },
    { 232, FALSE,  "International Voice/Data" },
    { 233, TRUE,  "International Voice/Data" },
    { 234, TRUE,  "International Voice/Data" },
    { 235, TRUE,  "Premium International" },
    { 236, TRUE,  NULL },
    { 237, TRUE,  NULL },
    { 238, FALSE,  NULL },
    { 239, FALSE,  NULL },
    { -1, FALSE, NULL },
};

gboolean
mm_cdma_parse_eri (const gchar *reply,
                   gboolean *out_roaming,
                   guint *out_ind,
                   const gchar **out_desc)
{
    guint ind;
    const EriItem *iter = &eris[0];
    gboolean found = FALSE;

    g_return_val_if_fail (reply != NULL, FALSE);
    g_return_val_if_fail (out_roaming != NULL, FALSE);

    if (mm_get_uint_from_str (reply, &ind)) {
        if (out_ind)
            *out_ind = ind;

        while (iter->num != -1) {
            if (iter->num == ind) {
                *out_roaming = iter->roam_ind;
                if (out_desc)
                    *out_desc = iter->banner;
                found = TRUE;
                break;
            }
            iter++;
        }
    }

    return found;
}

/*************************************************************************/

gboolean
mm_cdma_parse_crm_test_response (const gchar *reply,
                                 MMModemCdmaRmProtocol *min,
                                 MMModemCdmaRmProtocol *max,
                                 GError **error)
{
    gboolean result = FALSE;
    GRegex *r;


    /* Expected reply format is:
     *   ---> AT+CRM=?
     *   <--- +CRM: (0-2)
     */

    r = g_regex_new ("\\+CRM:\\s*\\((\\d+)-(\\d+)\\)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, error);
    if (r) {
        GMatchInfo *match_info = NULL;

        if (g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, error)) {
            gchar *aux;
            guint min_val = 0;
            guint max_val = 0;

            aux = g_match_info_fetch (match_info, 1);
            min_val = (guint) atoi (aux);
            g_free (aux);

            aux = g_match_info_fetch (match_info, 2);
            max_val = (guint) atoi (aux);
            g_free (aux);

            if (min_val == 0 ||
                max_val == 0 ||
                min_val >= max_val) {
                g_set_error (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Couldn't parse CRM range: "
                             "Unexpected range of RM protocols (%u,%u)",
                             min_val,
                             max_val);
            } else {
                *min = mm_cdma_get_rm_protocol_from_index (min_val, error);
                if (*min != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
                    *max = mm_cdma_get_rm_protocol_from_index (max_val, error);
                    if (*max != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN)
                        result = TRUE;
                }
            }
        }

        g_match_info_free (match_info);
        g_regex_unref (r);
    }

    return result;
}

/*************************************************************************/

MMModemCdmaRmProtocol
mm_cdma_get_rm_protocol_from_index (guint index,
                                    GError **error)
{
    guint protocol;

    /* just adding 1 from the index value should give us the enum */
    protocol = index + 1 ;
    if (protocol > MM_MODEM_CDMA_RM_PROTOCOL_STU_III) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unexpected RM protocol index (%u)",
                     index);
        protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
    }

    return (MMModemCdmaRmProtocol)protocol;
}

guint
mm_cdma_get_index_from_rm_protocol (MMModemCdmaRmProtocol protocol,
                                    GError **error)
{
    if (protocol == MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unexpected RM protocol (%s)",
                     mm_modem_cdma_rm_protocol_get_string (protocol));
        return 0;
    }

    /* just substracting 1 from the enum value should give us the index */
    return (protocol - 1);
}

/*************************************************************************/

gint
mm_cdma_normalize_class (const gchar *orig_class)
{
    gchar class;

    g_return_val_if_fail (orig_class != NULL, '0');

    class = toupper (orig_class[0]);

    /* Cellular (850MHz) */
    if (class == '1' || class == 'C')
        return 1;
    /* PCS (1900MHz) */
    if (class == '2' || class == 'P')
        return 2;

    /* Unknown/not registered */
    return 0;
}

/*************************************************************************/

gchar
mm_cdma_normalize_band (const gchar *long_band,
                        gint *out_class)
{
    gchar band;

    g_return_val_if_fail (long_band != NULL, 'Z');

    /* There are two response formats for the band; one includes the band
     * class and the other doesn't.  For modems that include the band class
     * (ex Novatel S720) you'll see "Px" or "Cx" depending on whether the modem
     * is registered on a PCS/1900 (P) or Cellular/850 (C) system.
     */
    band = toupper (long_band[0]);

    /* Possible band class in first position; return it */
    if (band == 'C' || band == 'P') {
        gchar tmp[2] = { band, '\0' };

        *out_class = mm_cdma_normalize_class (tmp);
        band = toupper (long_band[1]);
    }

    /* normalize to A - F, and Z */
    if (band >= 'A' && band <= 'F')
        return band;

    /* Unknown/not registered */
    return 'Z';
}

/*************************************************************************/

/* Caller must strip any "+GSN:" or "+CGSN" from @gsn */
gboolean
mm_parse_gsn (const char *gsn,
              gchar **out_imei,
              gchar **out_meid,
              gchar **out_esn)
{
    gchar **items, **iter;
    gchar *meid = NULL, *esn = NULL, *imei = NULL, *p;
    gboolean success = FALSE;

    if (!gsn || !gsn[0])
        return FALSE;

    /* IMEI is 15 numeric digits */

    /* ESNs take one of two formats:
     *  (1) 7 or 8 hexadecimal digits
     *  (2) 10 or 11 decimal digits
     *
     * In addition, leading zeros may be present or absent, and hexadecimal
     * ESNs may or may not be prefixed with "0x".
     */

    /* MEIDs take one of two formats:
     *  (1) 14 hexadecimal digits, sometimes padded to 16 digits with leading zeros
     *  (2) 18 decimal digits
     *
     * As with ESNs, leading zeros may be present or absent, and hexadecimal
     * MEIDs may or may not be prefixed with "0x".
     */

    items = g_strsplit_set (gsn, "\r\n\t: ,", 0);
    for (iter = items; iter && *iter && (!esn || !meid); iter++) {
        gboolean expect_hex = FALSE, is_hex, is_digit;
        gchar *s = *iter;
        guint len = 0;

        if (!s[0])
            continue;

        if (g_str_has_prefix (s, "0x") || g_str_has_prefix (s, "0X")) {
            expect_hex = TRUE;
            s += 2;

            /* Skip any leading zeros */
            while (*s == '0')
                s++;
        }

        /* Check whether all digits are hex or decimal */
        is_hex = is_digit = TRUE;
        p = s;
        while (*p && (is_hex || is_digit)) {
            if (!g_ascii_isxdigit (*p))
                is_hex = FALSE;
            if (!g_ascii_isdigit (*p))
                is_digit = FALSE;
            p++, len++;
        }

        /* Note that some hex strings are also valid digit strings */

        if (is_hex) {
            if (len == 7 || len == 8) {
                /* ESN */
                if (!esn) {
                    if (len == 7)
                        esn = g_strdup_printf ("0%s", s);
                    else
                        esn = g_strdup (s);
                }
            } else if (len == 14) {
                /* MEID */
                if (!meid)
                    meid = g_strdup (s);
            }
        }

        if (is_digit) {
            if (!is_hex)
                g_warn_if_fail (expect_hex == FALSE);

            if (len == 15) {
                if (!imei)
                    imei = g_strdup (s);
            }

            /* Decimal ESN/MEID unhandled for now; conversion from decimal to
             * hex isn't a straight dec->hex conversion, as the first 2 digits
             * of the ESN and first 3 digits of the MEID are the manufacturer
             * identifier and must be converted separately from serial number
             * and then concatenated with it.
             */
        }
    }
    g_strfreev (items);

    success = meid || esn || imei;

    if (out_imei)
        *out_imei = imei;
    else
        g_free (imei);

    if (out_meid)
        *out_meid = meid;
    else
        g_free (meid);

    if (out_esn)
        *out_esn = esn;
    else
        g_free (esn);

    return success;
}

