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

#include "mm-sms-part.h"
#include "mm-modem-helpers.h"
#include "mm-helper-enums-types.h"
#include "mm-log-object.h"

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

gchar **
mm_split_string_groups (const gchar *str)
{
    GPtrArray *array;
    const gchar *start;
    const gchar *next;

    array = g_ptr_array_new ();

    /*
     * Manually parse splitting groups. Groups may be single elements, or otherwise
     * lists given between parenthesis, e.g.:
     *
     *    ("SM","ME"),("SM","ME"),("SM","ME")
     *    "SM","SM","SM"
     *    "SM",("SM","ME"),("SM","ME")
     */

    /* Iterate string splitting groups */
    for (start = str; start; start = next) {
        gchar *item;
        gssize len = -1;

        /* skip leading whitespaces */
        while (*start == ' ')
            start++;

        if (*start == '(') {
            start++;
            next = strchr (start, ')');
            if (next) {
                len = next - start;
                next = strchr (next, ',');
                if (next)
                    next++;
            }
        } else {
            next = strchr (start, ',');
            if (next) {
                len = next - start;
                next++;
            }
        }

        if (len < 0)
            item = g_strdup (start);
        else
            item = g_strndup (start, len);

        g_ptr_array_add (array, item);
    }

    if (array->len > 0) {
        g_ptr_array_add (array, NULL);
        return (gchar **) g_ptr_array_free (array, FALSE);
    }

    g_ptr_array_unref (array);
    return NULL;
}

/*****************************************************************************/

static int uint_compare_func (gconstpointer a, gconstpointer b)
{
   return (*(guint *)a - *(guint *)b);
}

GArray *
mm_parse_uint_list (const gchar  *str,
                    GError      **error)
{
    GArray *array;
    gchar  *dupstr;
    gchar  *aux;
    GError *inner_error = NULL;

    if (!str || !str[0])
        return NULL;

    /* Parses into a GArray of guints, the list of numbers given in the string,
     * also supporting number intervals.
     * E.g.:
     *   1-6      --> 1,2,3,4,5,6
     *   1,2,4-6  --> 1,2,4,5,6
     */
    array = g_array_new (FALSE, FALSE, sizeof (guint));
    aux = dupstr = g_strdup (str);

    while (aux) {
        gchar *next;
        gchar *interval;

        next = strchr (aux, ',');
        if (next) {
            *next = '\0';
            next++;
        }

        interval = strchr (aux, '-');
        if (interval) {
            guint start = 0;
            guint stop = 0;

            *interval = '\0';
            interval++;

            if (!mm_get_uint_from_str (aux, &start)) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "couldn't parse interval start integer: '%s'", aux);
                goto out;
            }
            if (!mm_get_uint_from_str (interval, &stop)) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "couldn't parse interval stop integer: '%s'", interval);
                goto out;
            }

            if (start > stop) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "interval start (%u) cannot be bigger than interval stop (%u)", start, stop);
                goto out;
            }

            for (; start <= stop; start++)
                g_array_append_val (array, start);
        } else {
            guint num;

            if (!mm_get_uint_from_str (aux, &num)) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "couldn't parse integer: '%s'", aux);
                goto out;
            }

            g_array_append_val (array, num);
        }

        aux = next;
    }

    if (!array->len)
        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "couldn't parse list of integers: '%s'", str);
    else
        g_array_sort (array, uint_compare_func);

out:
    g_free (dupstr);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_array_unref (array);
        return NULL;
    }

    return array;
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

guint
mm_find_bit_set (gulong number)
{
    guint c = 0;

    for (c = 0; !(number & 0x1); c++)
        number >>= 1;
    return c;
}

/*****************************************************************************/

gchar *
mm_create_device_identifier (guint        vid,
                             guint        pid,
                             gpointer     log_object,
                             const gchar *ati,
                             const gchar *ati1,
                             const gchar *gsn,
                             const gchar *revision,
                             const gchar *model,
                             const gchar *manf)
{
    g_autoptr(GString) devid = NULL;
    g_autoptr(GString) msg = NULL;
    g_autoptr(GChecksum) sum = NULL;
    const gchar *ret;
    gchar *p = NULL;
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

    if (!strlen (devid->str))
        return NULL;

    p = devid->str;
    msg = g_string_sized_new (strlen (devid->str) + 17);
    sum = g_checksum_new (G_CHECKSUM_SHA1);

    if (vid) {
        snprintf (str_vid, sizeof (str_vid) - 1, "%08x", vid);
        g_checksum_update (sum, (const guchar *) &str_vid[0], strlen (str_vid));
        g_string_append_printf (msg, "%08x", vid);
    }
    if (pid) {
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

    ret = g_checksum_get_string (sum);
    mm_obj_dbg (log_object, "device identifier built: %s -> %s", msg->str, ret);
    return g_strdup (ret);
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

gchar *
mm_new_iso8601_time (guint year,
                     guint month,
                     guint day,
                     guint hour,
                     guint minute,
                     guint second,
                     gboolean have_offset,
                     gint offset_minutes)
{
    GString *str;

    str = g_string_sized_new (30);
    g_string_append_printf (str, "%04d-%02d-%02dT%02d:%02d:%02d",
                            year, month, day, hour, minute, second);
    if (have_offset) {
        if (offset_minutes >=0 ) {
            g_string_append_printf (str, "+%02d:%02d",
                                    offset_minutes / 60,
                                    offset_minutes % 60);
        } else {
            offset_minutes *= -1;
            g_string_append_printf (str, "-%02d:%02d",
                                    offset_minutes / 60,
                                    offset_minutes % 60);
        }
    }
    return g_string_free (str, FALSE);
}

/*****************************************************************************/

GArray *
mm_filter_supported_modes (const GArray *all,
                           const GArray *supported_combinations,
                           gpointer      log_object)
{
    MMModemModeCombination all_item;
    guint i;
    GArray *filtered_combinations;
    gboolean all_item_added = FALSE;

    g_return_val_if_fail (all != NULL, NULL);
    g_return_val_if_fail (all->len == 1, NULL);
    g_return_val_if_fail (supported_combinations != NULL, NULL);

    mm_obj_dbg (log_object, "filtering %u supported mode combinations with %u modes",
                supported_combinations->len, all->len);

    all_item = g_array_index (all, MMModemModeCombination, 0);
    g_return_val_if_fail (all_item.allowed != MM_MODEM_MODE_NONE, NULL);

    /* We will filter out all combinations which have modes not listed in 'all' */
    filtered_combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), supported_combinations->len);
    for (i = 0; i < supported_combinations->len; i++) {
        MMModemModeCombination *mode;

        mode = &g_array_index (supported_combinations, MMModemModeCombination, i);
        if (!(mode->allowed & ~all_item.allowed)) {
            /* Compare only 'allowed', *not* preferred. If there is at least one item with allowed
             * containing all supported modes, we're already good to go. This allows us to have a
             * default with preferred != NONE (e.g. Wavecom 2G modem with allowed=CS+2G and
             * preferred=2G */
            if (all_item.allowed == mode->allowed)
                all_item_added = TRUE;
            g_array_append_val (filtered_combinations, *mode);
        }
    }

    if (filtered_combinations->len == 0)
        mm_obj_warn (log_object, "all supported mode combinations were filtered out");

    /* Add default entry with the generic mask including all items */
    if (!all_item_added) {
        mm_obj_dbg (log_object, "adding an explicit item with all supported modes allowed");
        g_array_append_val (filtered_combinations, all_item);
    }

    mm_obj_dbg (log_object, "device supports %u different mode combinations",
                filtered_combinations->len);

    return filtered_combinations;
}

/*****************************************************************************/

static const gchar bcd_chars[] = "0123456789\0\0\0\0\0\0";

gchar *
mm_bcd_to_string (const guint8 *bcd, gsize bcd_len, gboolean low_nybble_first)
{
    GString *str;
    gsize i;

    g_return_val_if_fail (bcd != NULL, NULL);

    str = g_string_sized_new (bcd_len * 2 + 1);
    for (i = 0 ; i < bcd_len; i++) {
        if (low_nybble_first)
            str = g_string_append_c (str, bcd_chars[bcd[i] & 0xF]);
        str = g_string_append_c (str, bcd_chars[(bcd[i] >> 4) & 0xF]);
        if (!low_nybble_first)
            str = g_string_append_c (str, bcd_chars[bcd[i] & 0xF]);
    }
    return g_string_free (str, FALSE);
}

/*****************************************************************************/

GRegex *
mm_voice_ring_regex_get (void)
{
    /* Example:
     * <CR><LF>RING<CR><LF>
     */
    return g_regex_new ("\\r\\nRING\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

GRegex *
mm_voice_cring_regex_get (void)
{
    /* Example:
     * <CR><LF>+CRING: VOICE<CR><LF>
     * <CR><LF>+CRING: DATA<CR><LF>
     */
    return g_regex_new ("\\r\\n\\+CRING:\\s*(\\S+)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

GRegex *
mm_voice_clip_regex_get (void)
{
    /*
     * Only first 2 fields are mandatory:
     *   +CLIP: <number>,<type>[,<subaddr>,<satype>[,[<alpha>][,<CLI_validity>]]]
     *
     * Example:
     *   <CR><LF>+CLIP: "+393351391306",145,,,,0<CR><LF>
     *                   \_ Number      \_ Type
     */
    return g_regex_new ("\\r\\n\\+CLIP:\\s*([^,\\s]*)\\s*,\\s*(\\d+)\\s*,?(.*)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

GRegex *
mm_voice_ccwa_regex_get (void)
{
    /*
     * Only first 3 fields are mandatory, but we read only the first one
     *   +CCWA: <number>,<type>,<class>,[<alpha>][,<CLI_validity>[,<subaddr>,<satype>[,<priority>]]]
     *
     * Example:
     *   <CR><LF>+CCWA: "+393351391306",145,1
     *                   \_ Number      \_ Type
     */
    return g_regex_new ("\\r\\n\\+CCWA:\\s*([^,\\s]*)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,?(.*)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE,
                        0,
                        NULL);
}

static void
call_info_free (MMCallInfo *info)
{
    if (!info)
        return;
    g_free (info->number);
    g_slice_free (MMCallInfo, info);
}

gboolean
mm_3gpp_parse_clcc_response (const gchar  *str,
                             gpointer      log_object,
                             GList       **out_list,
                             GError      **error)
{
    GRegex     *r;
    GList      *list = NULL;
    GError     *inner_error = NULL;
    GMatchInfo *match_info  = NULL;

    static const MMCallDirection call_direction[] = {
        [0] = MM_CALL_DIRECTION_OUTGOING,
        [1] = MM_CALL_DIRECTION_INCOMING,
    };

    static const MMCallState call_state[] = {
        [0] = MM_CALL_STATE_ACTIVE,
        [1] = MM_CALL_STATE_HELD,
        [2] = MM_CALL_STATE_DIALING,     /* Dialing  (MOC) */
        [3] = MM_CALL_STATE_RINGING_OUT, /* Alerting (MOC) */
        [4] = MM_CALL_STATE_RINGING_IN,  /* Incoming (MTC) */
        [5] = MM_CALL_STATE_WAITING,     /* Waiting  (MTC) */

        /* This next call state number isn't defined by 3GPP, because it
         * doesn't make sense to have it when reporting a full list of calls
         * via +CLCC (i.e. the absence of the call would mean it's terminated).
         * But, this value may be used by other implementations (e.g. SimTech
         * plugin) to report that a call is terminated even when the full
         * call list isn't being reported. So, let's support it in the generic,
         * parser, even if not strictly standard. */
        [6] = MM_CALL_STATE_TERMINATED,
    };

    g_assert (out_list);

    /*
     *         1     2     3      4      5       6        7       8        9           10
     *  +CLCC: <idx>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>[,<alpha>[,<priority>[,<CLI validity>]]]]
     *  +CLCC: <idx>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>[,<alpha>[,<priority>[,<CLI validity>]]]]
     *  ...
     */

    r = g_regex_new ("\\+CLCC:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)" /* mandatory fields */
                     "(?:,\\s*([^,]*),\\s*(\\d+)"                                     /* number and type */
                     "(?:,\\s*([^,]*)"                                                /* alpha */
                     "(?:,\\s*(\\d*)"                                                 /* priority */
                     "(?:,\\s*(\\d*)"                                                 /* CLI validity */
                     ")?)?)?)?$",
                     G_REGEX_RAW | G_REGEX_MULTILINE | G_REGEX_NEWLINE_CRLF,
                     G_REGEX_MATCH_NEWLINE_CRLF,
                     NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, str, strlen (str), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    /* Parse the results */
    while (g_match_info_matches (match_info)) {
        MMCallInfo *call_info;
        guint       aux;

        call_info = g_slice_new0 (MMCallInfo);

        if (!mm_get_uint_from_match_info (match_info, 1, &call_info->index)) {
            mm_obj_warn (log_object, "couldn't parse call index from +CLCC line");
            goto next;
        }

        if (!mm_get_uint_from_match_info (match_info, 2, &aux) ||
            (aux >= G_N_ELEMENTS (call_direction))) {
            mm_obj_warn (log_object, "couldn't parse call direction from +CLCC line");
            goto next;
        }
        call_info->direction = call_direction[aux];

        if (!mm_get_uint_from_match_info (match_info, 3, &aux) ||
            (aux >= G_N_ELEMENTS (call_state))) {
            mm_obj_warn (log_object, "couldn't parse call state from +CLCC line");
            goto next;
        }
        call_info->state = call_state[aux];

        if (g_match_info_get_match_count (match_info) >= 7)
            call_info->number = mm_get_string_unquoted_from_match_info (match_info, 6);

        list = g_list_append (list, call_info);
        call_info = NULL;

    next:
        call_info_free (call_info);
        g_match_info_next (match_info, NULL);
    }

out:
    g_clear_pointer (&match_info, g_match_info_free);
    g_regex_unref (r);

    if (inner_error) {
        mm_3gpp_call_info_list_free (list);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *out_list = list;

    return TRUE;
}

void
mm_3gpp_call_info_list_free (GList *call_info_list)
{
    g_list_free_full (call_info_list, (GDestroyNotify) call_info_free);
}

/*************************************************************************/

static MMFlowControl
flow_control_array_to_mask (GArray      *array,
                            const gchar *item,
                            gpointer     log_object)
{
    MMFlowControl mask = MM_FLOW_CONTROL_UNKNOWN;
    guint         i;

    for (i = 0; i < array->len; i++) {
        guint mode;

        mode = g_array_index (array, guint, i);
        switch (mode) {
            case 0:
                mm_obj_dbg (log_object, "%s supports no flow control", item);
                mask |= MM_FLOW_CONTROL_NONE;
                break;
            case 1:
                mm_obj_dbg (log_object, "%s supports XON/XOFF flow control", item);
                mask |= MM_FLOW_CONTROL_XON_XOFF;
                break;
            case 2:
                mm_obj_dbg (log_object, "%s supports RTS/CTS flow control", item);
                mask |= MM_FLOW_CONTROL_RTS_CTS;
                break;
            default:
                break;
        }
    }

    return mask;
}

static MMFlowControl
flow_control_match_info_to_mask (GMatchInfo   *match_info,
                                 guint         index,
                                 const gchar  *item,
                                 gpointer      log_object,
                                 GError      **error)
{
    MMFlowControl  mask  = MM_FLOW_CONTROL_UNKNOWN;
    gchar         *aux   = NULL;
    GArray        *array = NULL;

    if (!(aux = mm_get_string_unquoted_from_match_info (match_info, index))) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Error retrieving list of supported %s flow control methods", item);
        goto out;
    }

    if (!(array = mm_parse_uint_list (aux, error))) {
        g_prefix_error (error, "Error parsing list of supported %s flow control methods: ", item);
        goto out;
    }

    if ((mask = flow_control_array_to_mask (array, item, log_object)) == MM_FLOW_CONTROL_UNKNOWN) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No known %s flow control method given", item);
        goto out;
    }

out:
    g_clear_pointer (&aux,  g_free);
    g_clear_pointer (&array, g_array_unref);

    return mask;
}

MMFlowControl
mm_parse_ifc_test_response (const gchar  *response,
                            gpointer      log_object,
                            GError      **error)
{
    GRegex        *r;
    GError        *inner_error = NULL;
    GMatchInfo    *match_info  = NULL;
    MMFlowControl  te_mask     = MM_FLOW_CONTROL_UNKNOWN;
    MMFlowControl  ta_mask     = MM_FLOW_CONTROL_UNKNOWN;
    MMFlowControl  mask        = MM_FLOW_CONTROL_UNKNOWN;

    r = g_regex_new ("(?:\\+IFC:)?\\s*\\((.*)\\),\\((.*)\\)(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match response");
        goto out;
    }

    /* Parse TE flow control methods */
    if ((te_mask = flow_control_match_info_to_mask (match_info, 1, "TE", log_object, &inner_error)) == MM_FLOW_CONTROL_UNKNOWN)
        goto out;

    /* Parse TA flow control methods */
    if ((ta_mask = flow_control_match_info_to_mask (match_info, 2, "TA", log_object, &inner_error)) == MM_FLOW_CONTROL_UNKNOWN)
        goto out;

    /* Only those methods in both TA and TE will be the ones we report */
    mask = te_mask & ta_mask;

out:

    g_clear_pointer (&match_info, g_match_info_free);
    g_regex_unref (r);

    if (inner_error)
        g_propagate_error (error, inner_error);

    return mask;
}

MMFlowControl
mm_flow_control_from_string (const gchar  *str,
                             GError      **error)
{
    GFlagsClass *flags_class;
    guint value;
    guint i;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_FLOW_CONTROL));

    for (i = 0; flags_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, flags_class->values[i].value_nick)) {
            value = flags_class->values[i].value;
            g_type_class_unref (flags_class);
            return value;
        }
    }

    g_type_class_unref (flags_class);

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMFlowControl value",
                 str);
    return MM_FLOW_CONTROL_UNKNOWN;
}

/*************************************************************************/

static const gchar *creg_regex[] = {
    /* +CREG: <stat>                      (GSM 07.07 CREG=1 unsolicited) */
    [0] = "\\+(CREG|CGREG|CEREG|C5GREG):\\s*0*([0-9])",
    /* +CREG: <n>,<stat>                  (GSM 07.07 CREG=1 solicited) */
    [1] = "\\+(CREG|CGREG|CEREG|C5GREG):\\s*0*([0-9]),\\s*0*([0-9])",
    /* +CREG: <stat>,<lac>,<ci>           (GSM 07.07 CREG=2 unsolicited) */
    [2] = "\\+(CREG|CGREG|CEREG):\\s*0*([0-9]),\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)",
    /* +CREG: <n>,<stat>,<lac>,<ci>       (GSM 07.07 solicited and some CREG=2 unsolicited) */
    [3] = "\\+(CREG|CGREG|CEREG):\\s*([0-9]),\\s*([0-9])\\s*,\\s*([^,]*)\\s*,\\s*([^,\\s]*)",
    [4] = "\\+(CREG|CGREG|CEREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*(\"[^,]*\")\\s*,\\s*(\"[^,\\s]*\")",
    /* +CREG: <stat>,<lac>,<ci>,<AcT>     (ETSI 27.007 CREG=2 unsolicited) */
    [5] = "\\+(CREG|CGREG|CEREG):\\s*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([0-9])",
    [6] = "\\+(CREG|CGREG|CEREG):\\s*0*([0-9])\\s*,\\s*(\"[^,\\s]*\")\\s*,\\s*(\"[^,\\s]*\")\\s*,\\s*0*([0-9])",
    /* +CREG: <n>,<stat>,<lac>,<ci>,<AcT> (ETSI 27.007 solicited and some CREG=2 unsolicited) */
    [7] = "\\+(CREG|CGREG|CEREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])",
    /* +CREG: <n>,<stat>,<lac>,<ci>,<AcT?>,<something> (Samsung Wave S8500) */
    /* '<CR><LF>+CREG: 2,1,000B,2816, B, C2816<CR><LF><CR><LF>OK<CR><LF>' */
    [8] = "\\+(CREG|CGREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*[^,\\s]*",
    /* +CREG: <stat>,<lac>,<ci>,<AcT>,<RAC> (ETSI 27.007 v9.20 CREG=2 unsolicited with RAC) */
    [9] = "\\+(CREG|CGREG):\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])\\s*,\\s*([^,\\s]*)",
    /* +CEREG: <stat>,<lac>,<rac>,<ci>,<AcT>     (ETSI 27.007 v8.6 CREG=2 unsolicited with RAC) */
    [10] = "\\+(CEREG):\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])",
    /* +CEREG: <n>,<stat>,<lac>,<rac>,<ci>,<AcT> (ETSI 27.007 v8.6 CREG=2 solicited with RAC) */
    [11] = "\\+(CEREG):\\s*0*([0-9]),\\s*0*([0-9])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*0*([0-9])",
    /* +C5GREG: <stat>,<lac>,<ci>,<AcT>,<Allowed_NSSAI_length>,<Allowed_NSSAI>   (ETSI 27.007 CREG=2 unsolicited) */
    [12] = "\\+(C5GREG):\\s*([0-9]+)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([0-9]+)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)",
    /* +C5GREG: <n>,<stat>,<lac>,<ci>,<AcT>,<Allowed_NSSAI_length>,<Allowed_NSSAI> (ETSI 27.007 solicited) */
    [13] = "\\+(C5GREG):\\s*([0-9]+)\\s*,\\s*([0-9+])\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)\\s*,\\s*([0-9]+)\\s*,\\s*([^,\\s]*)\\s*,\\s*([^,\\s]*)",
};

GPtrArray *
mm_3gpp_creg_regex_get (gboolean solicited)
{
    GPtrArray *array;
    guint      i;

    array = g_ptr_array_sized_new (G_N_ELEMENTS (creg_regex));
    for (i = 0; i < G_N_ELEMENTS (creg_regex); i++) {
        GRegex           *regex;
        g_autofree gchar *pattern = NULL;

        if (solicited) {
            pattern = g_strdup_printf ("%s$", creg_regex[i]);
            regex = g_regex_new (pattern, G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        } else {
            pattern = g_strdup_printf ("\\r\\n%s\\r\\n", creg_regex[i]);
            regex = g_regex_new (pattern, G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        }
        g_assert (regex);
        g_ptr_array_add (array, regex);
    }
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
mm_3gpp_cgev_regex_get (void)
{
    return g_regex_new ("\\r\\n\\+CGEV:\\s*(.*)\\r\\n",
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
    return g_regex_new ("\\r\\n\\+CMTI:\\s*\"(\\S+)\",\\s*(\\d+)\\r\\n",
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
/* AT+WS46=? response parser
 *
 * More than one numeric ID may appear in the list, that's why
 * they are checked separately.
 *
 * NOTE: ignore WS46 prefix or it will break Cinterion handling.
 *
 * For the specific case of '25', we will check if any other mode supports
 * 4G, and if there is none, we'll remove 4G caps from it. This is needed
 * because pre-LTE modems used '25' to report GERAN+URAN instead of the
 * new '29' value since LTE modems are around.
 */

typedef struct {
    guint       ws46;
    MMModemMode mode;
} Ws46Mode;

/* 3GPP TS 27.007 v16.3.0, section 5.9: select wireless network +WS46 */
static const Ws46Mode ws46_modes[] = {
    /* GSM Digital Cellular Systems (GERAN only) */
    { 12, MM_MODEM_MODE_2G },
    /* UTRAN only */
    { 22, MM_MODEM_MODE_3G },
    /* 3GPP Systems (GERAN, UTRAN and E-UTRAN) */
    { 25, MM_MODEM_MODE_ANY },
    /* E-UTRAN only */
    { 28, MM_MODEM_MODE_4G },
    /* GERAN and UTRAN */
    { 29, MM_MODEM_MODE_2G | MM_MODEM_MODE_3G },
    /* GERAN and E-UTRAN */
    { 30, MM_MODEM_MODE_2G | MM_MODEM_MODE_4G },
    /* UTRAN and E-UTRAN */
    { 31, MM_MODEM_MODE_3G | MM_MODEM_MODE_4G },
    /* GERAN, UTRAN, E-UTRAN and NG-RAN */
    { 35, MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G },
    /* NG-RAN only */
    { 36, MM_MODEM_MODE_5G },
    /* E-UTRAN and NG-RAN */
    { 37, MM_MODEM_MODE_4G | MM_MODEM_MODE_5G },
    /* UTRAN, E-UTRAN and NG-RAN */
    { 38, MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G },
    /* GERAN, E-UTRAN and NG-RAN */
    { 39, MM_MODEM_MODE_2G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G },
    /* UTRAN and NG-RAN */
    { 40, MM_MODEM_MODE_3G | MM_MODEM_MODE_5G },
    /* GERAN, UTRAN and NG-RAN */
    { 41, MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_5G },
    /* GERAN and NG-RAN */
    { 42, MM_MODEM_MODE_2G | MM_MODEM_MODE_5G },
};

GArray *
mm_3gpp_parse_ws46_test_response (const gchar  *response,
                                  GError      **error)
{
    GArray     *modes = NULL;
    GArray     *tech_values = NULL;
    GRegex     *r;
    GError     *inner_error = NULL;
    GMatchInfo *match_info = NULL;
    gchar      *full_list = NULL;
    guint       val;
    guint       i;
    guint       j;
    gboolean    supported_5g = FALSE;
    gboolean    supported_4g = FALSE;
    gboolean    supported_3g = FALSE;
    gboolean    supported_2g = FALSE;
    gboolean    supported_mode_25 = FALSE;
    gboolean    supported_mode_29 = FALSE;

    r = g_regex_new ("(?:\\+WS46:)?\\s*\\((.*)\\)(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match response");
        goto out;
    }

    if (!(full_list = mm_get_string_unquoted_from_match_info (match_info, 1))) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing full list string");
        goto out;
    }

    if (!(tech_values = mm_parse_uint_list (full_list, &inner_error)))
        goto out;

    modes = g_array_new (FALSE, FALSE, sizeof (MMModemMode));

    for (i = 0; i < tech_values->len; i++) {
        val = g_array_index (tech_values, guint, i);

        for (j = 0; j < G_N_ELEMENTS (ws46_modes); j++) {
            if (ws46_modes[j].ws46 == val) {
                if (val == 25)
                    supported_mode_25 = TRUE;
                else {
                    if (val == 29)
                        supported_mode_29 = TRUE;
                    if (ws46_modes[j].mode & MM_MODEM_MODE_5G)
                        supported_5g = TRUE;
                    if (ws46_modes[j].mode & MM_MODEM_MODE_4G)
                        supported_4g = TRUE;
                    if (ws46_modes[j].mode & MM_MODEM_MODE_3G)
                        supported_3g = TRUE;
                    if (ws46_modes[j].mode & MM_MODEM_MODE_2G)
                        supported_2g = TRUE;
                    g_array_append_val (modes, ws46_modes[j].mode);
                }
                break;
            }
        }

        if (j == G_N_ELEMENTS (ws46_modes))
            g_warning ("Unknown +WS46 mode reported: %u", val);
    }

    if (supported_mode_25) {
        MMModemMode mode_25;

        mode_25 = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
        if (supported_4g) {
            mode_25 |= MM_MODEM_MODE_4G;
            g_array_append_val (modes, mode_25);
        } else if (!supported_mode_29)
            g_array_append_val (modes, mode_25);
    }

    if (modes->len == 0) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "No valid modes reported");
        g_clear_pointer (&modes, g_array_unref);
        goto out;
    }

    /* Fixup the ANY value, based on which are the supported modes */
    for (i = 0; i < modes->len; i++) {
        MMModemMode *mode;

        mode = &g_array_index (modes, MMModemMode, i);
        if (*mode == MM_MODEM_MODE_ANY) {
            *mode = 0;
            if (supported_2g)
                *mode |= MM_MODEM_MODE_2G;
            if (supported_3g)
                *mode |= MM_MODEM_MODE_3G;
            if (supported_4g)
                *mode |= MM_MODEM_MODE_4G;
            if (supported_5g)
                *mode |= MM_MODEM_MODE_5G;

            if (*mode == 0) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "No way to fixup the ANY value");
                g_clear_pointer (&modes, g_array_unref);
                goto out;
            }
        }
    }

out:
    if (tech_values)
        g_array_unref (tech_values);

    g_free (full_list);

    g_clear_pointer (&match_info, g_match_info_free);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    g_assert (modes && modes->len);
    return modes;
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
    case 0: /* GSM */
    case 8: /* EC-GSM-IoT (A/Gb mode) */
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
    case 7:  /* E-UTRAN */
    case 9:  /* E-UTRAN (NB-S1) */
    case 10: /* E-UTRA connected to a 5GCN */
        return MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    case 11: /* NR connected to a 5G CN */
    case 12: /* NG-RAN */
        return MM_MODEM_ACCESS_TECHNOLOGY_5GNR;
    case 13: /* E-UTRA-NR dual connectivity */
        return (MM_MODEM_ACCESS_TECHNOLOGY_5GNR | MM_MODEM_ACCESS_TECHNOLOGY_LTE);
    default:
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }
}

static MMModem3gppNetworkAvailability
parse_network_status (const gchar *str,
                      gpointer     log_object)
{
    /* Expecting a value between '0' and '3' inclusive */
    if (!str ||
        strlen (str) != 1 ||
        str[0] < '0' ||
        str[0] > '3') {
        mm_obj_warn (log_object, "cannot parse network status value '%s'", str);
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN;
    }

    return (MMModem3gppNetworkAvailability) (str[0] - '0');
}

static MMModemAccessTechnology
parse_access_tech (const gchar *str,
                   gpointer     log_object)
{
    /* Recognized access technologies are between '0' and '7' inclusive... */
    if (!str ||
        strlen (str) != 1 ||
        str[0] < '0' ||
        str[0] > '7') {
        mm_obj_warn (log_object, "cannot parse access technology value '%s'", str);
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }

    return get_mm_access_tech_from_etsi_access_tech (str[0] - '0');
}

GList *
mm_3gpp_parse_cops_test_response (const gchar     *reply,
                                  MMModemCharset   cur_charset,
                                  gpointer         log_object,
                                  GError         **error)
{
    GRegex *r;
    GList *info_list = NULL;
    GMatchInfo *match_info;
    gboolean umts_format = TRUE;

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

    r = g_regex_new ("\\((\\d),\"([^\"\\)]*)\",([^,\\)]*),([^,\\)]*)[\\)]?,(\\d)\\)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r);

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

        r = g_regex_new ("\\((\\d),([^,\\)]*),([^,\\)]*),([^\\)]*)\\)", G_REGEX_UNGREEDY, 0, NULL);
        g_assert (r);

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
        info->status = parse_network_status (tmp, log_object);
        g_free (tmp);

        info->operator_long = mm_get_string_unquoted_from_match_info (match_info, 2);
        info->operator_short = mm_get_string_unquoted_from_match_info (match_info, 3);
        info->operator_code = mm_get_string_unquoted_from_match_info (match_info, 4);

        /* The returned strings may be given in e.g. UCS2 */
        mm_3gpp_normalize_operator (&info->operator_long,  cur_charset, log_object);
        mm_3gpp_normalize_operator (&info->operator_short, cur_charset, log_object);
        mm_3gpp_normalize_operator (&info->operator_code,  cur_charset, log_object);

        /* Only try for access technology with UMTS-format matches.
         * If none give, assume GSM */
        tmp = (umts_format ?
               mm_get_string_unquoted_from_match_info (match_info, 5) :
               NULL);
        info->access_tech = (tmp ?
                             parse_access_tech (tmp, log_object) :
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
            g_autofree gchar *access_tech_str = NULL;

            access_tech_str = mm_modem_access_technology_build_string_from_mask (info->access_tech);
            mm_obj_dbg (log_object, "found network '%s' ('%s','%s'); availability: %s, access tech: %s",
                        info->operator_code,
                        info->operator_short ? info->operator_short : "no short name",
                        info->operator_long ? info->operator_long : "no long name",
                        mm_modem_3gpp_network_availability_get_string (info->status),
                        access_tech_str);
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

gboolean
mm_3gpp_parse_cops_read_response (const gchar              *response,
                                  guint                    *out_mode,
                                  guint                    *out_format,
                                  gchar                   **out_operator,
                                  MMModemAccessTechnology  *out_act,
                                  GError                  **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;
    guint mode = 0;
    guint format = 0;
    gchar *operator = NULL;
    guint actval = 0;
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    g_assert (out_mode || out_format || out_operator || out_act);

    /* We assume the response to be either:
     *   +COPS: <mode>,<format>,<oper>
     * or:
     *   +COPS: <mode>,<format>,<oper>,<AcT>
     */
    r = g_regex_new ("\\+COPS:\\s*(\\d+),(\\d+),([^,]*)(?:,(\\d+))?(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match response");
        goto out;
    }

    if (out_mode && !mm_get_uint_from_match_info (match_info, 1, &mode)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing mode");
        goto out;
    }

    if (out_format && !mm_get_uint_from_match_info (match_info, 2, &format)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing format");
        goto out;
    }

    if (out_operator && !(operator = mm_get_string_unquoted_from_match_info (match_info, 3))) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing operator");
        goto out;
    }

    /* AcT is optional */
    if (out_act && g_match_info_get_match_count (match_info) >= 5) {
        if (!mm_get_uint_from_match_info (match_info, 4, &actval)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing AcT");
            goto out;
        }
        act = get_mm_access_tech_from_etsi_access_tech (actval);
    }

out:
    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_free (operator);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_mode)
        *out_mode = mode;
    if (out_format)
        *out_format = format;
    if (out_operator)
        *out_operator = operator;
    if (out_act)
        *out_act = act;
    return TRUE;
}

/*************************************************************************/
/* Logic to compare two APN names */

gboolean
mm_3gpp_cmp_apn_name (const gchar *requested,
                      const gchar *existing)
{
    size_t requested_len;
    size_t existing_len;

    /* If both empty, that's a good match */
    if ((!existing || !existing[0]) && (!requested || !requested[0]))
        return TRUE;

    /* Both must be given to compare properly */
    if (!existing || !existing[0] || !requested || !requested[0])
        return FALSE;

    requested_len = strlen (requested);

    /*
     * 1) The requested APN should be at least the prefix of the existing one.
     */
    if (g_ascii_strncasecmp (existing, requested, requested_len) != 0)
        return FALSE;

    /*
     * 2) If the existing one is actually the same as the requested one (i.e.
     *    there are no more different chars in the existing one), we're done.
     */
    if (existing[requested_len] == '\0')
        return TRUE;

    existing_len = strlen (existing);

    /* 3) Special handling for PDP contexts reported by u-blox modems once the
     *    contexts have been activated at least once:
     *      "ac.vodafone.es.MNC001.MCC214.GPRS" should match "ac.vodafone.es"
     */
    if ((existing_len > (requested_len + 14)) &&
        g_ascii_strncasecmp (&existing[requested_len], ".mnc", 4) == 0 &&
        g_ascii_strncasecmp (&existing[requested_len + 7], ".mcc", 4) == 0)
        return TRUE;

    /* No match */
    return FALSE;
}

/*************************************************************************/

static guint
find_max_allowed_cid (GList            *context_format_list,
                      MMBearerIpFamily  ip_family)
{
    GList *l;

    for (l = context_format_list; l; l = g_list_next (l)) {
        MM3gppPdpContextFormat *format = l->data;

        /* Found exact PDP type? */
        if (format->pdp_type == ip_family)
            return format->max_cid;
    }
    return 0;
}

guint
mm_3gpp_select_best_cid (const gchar      *apn,
                         MMBearerIpFamily  ip_family,
                         GList            *context_list,
                         GList            *context_format_list,
                         gpointer          log_object,
                         gboolean         *out_cid_reused,
                         gboolean         *out_cid_overwritten)
{
    GList *l;
    guint  prev_cid = 0;
    guint  exact_cid = 0;
    guint  unused_cid = 0;
    guint  max_cid = 0;
    guint  max_allowed_cid = 0;
    guint  blank_cid = 0;
    gchar *ip_family_str;

    g_assert (out_cid_reused);
    g_assert (out_cid_overwritten);

    ip_family_str = mm_bearer_ip_family_build_string_from_mask (ip_family);
    mm_obj_dbg (log_object, "looking for best CID matching APN '%s' and PDP type '%s'...",
                apn, ip_family_str);
    g_free (ip_family_str);

    /* Look for the exact PDP context we want */
    for (l = context_list; l; l = g_list_next (l)) {
        MM3gppPdpContext *pdp = l->data;

        /* Match PDP type */
        if (pdp->pdp_type == ip_family) {
            /* Try to match exact APN and PDP type */
            if (mm_3gpp_cmp_apn_name (apn, pdp->apn)) {
                exact_cid = pdp->cid;
                break;
            }

            /* Same PDP type but with no APN set? we may use that one if no exact match found */
            if ((!pdp->apn || !pdp->apn[0]) && !blank_cid)
                blank_cid = pdp->cid;
        }

        /* If an unused CID was not found yet and the previous CID is not (CID - 1),
         * this means that (previous CID + 1) is an unused CID that can be used.
         * This logic will allow us using unused CIDs that are available in the gaps
         * between already defined contexts.
         */
        if (!unused_cid && prev_cid && ((prev_cid + 1) < pdp->cid))
            unused_cid = prev_cid + 1;

        /* Update previous CID value to the current CID for use in the next loop,
         * unless an unused CID was already found.
         */
        if (!unused_cid)
            prev_cid = pdp->cid;

        /* Update max CID if we found a bigger one */
        if (max_cid < pdp->cid)
            max_cid = pdp->cid;
    }

    /* Always prefer an exact match */
    if (exact_cid) {
        mm_obj_dbg (log_object, "found exact context at CID %u", exact_cid);
        *out_cid_reused = TRUE;
        *out_cid_overwritten = FALSE;
        return exact_cid;
    }

    /* Try to use an unused CID detected in between the already defined contexts */
    if (unused_cid) {
        mm_obj_dbg (log_object, "found unused context at CID %u", unused_cid);
        *out_cid_reused = FALSE;
        *out_cid_overwritten = FALSE;
        return unused_cid;
    }

    /* If the max existing CID found during CGDCONT? is below the max allowed
     * CID, then we can use the next available CID because it's an unused one. */
    max_allowed_cid = find_max_allowed_cid (context_format_list, ip_family);
    if (max_cid && (max_cid < max_allowed_cid)) {
        mm_obj_dbg (log_object, "found unused context at CID %u (<%u)", max_cid + 1, max_allowed_cid);
        *out_cid_reused = FALSE;
        *out_cid_overwritten = FALSE;
        return (max_cid + 1);
    }

    /* Rewrite a context defined with no APN, if any */
    if (blank_cid) {
        mm_obj_dbg (log_object, "rewriting context with empty APN at CID %u", blank_cid);
        *out_cid_reused = FALSE;
        *out_cid_overwritten = TRUE;
        return blank_cid;
    }

    /* Rewrite the last existing one found */
    if (max_cid) {
        mm_obj_dbg (log_object, "rewriting last context detected at CID %u", max_cid);
        *out_cid_reused = FALSE;
        *out_cid_overwritten = TRUE;
        return max_cid;
    }

    /* Otherwise, just fallback to CID=1 */
    mm_obj_dbg (log_object, "falling back to CID 1");
    *out_cid_reused = FALSE;
    *out_cid_overwritten = TRUE;
    return 1;
}

/*************************************************************************/

static void
mm_3gpp_pdp_context_format_free (MM3gppPdpContextFormat *format)
{
    g_slice_free (MM3gppPdpContextFormat, format);
}

void
mm_3gpp_pdp_context_format_list_free (GList *pdp_format_list)
{
    g_list_free_full (pdp_format_list, (GDestroyNotify) mm_3gpp_pdp_context_format_free);
}

GList *
mm_3gpp_parse_cgdcont_test_response (const gchar  *response,
                                     gpointer      log_object,
                                     GError      **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;
    GList *list = NULL;

    if (!response || !g_str_has_prefix (response, "+CGDCONT:")) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing +CGDCONT prefix");
        return NULL;
    }

    r = g_regex_new ("\\+CGDCONT:\\s*\\(\\s*(\\d+)\\s*-?\\s*(\\d+)?[^\\)]*\\)\\s*,\\s*\\(?\"(\\S+)\"",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, &inner_error);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    while (!inner_error && g_match_info_matches (match_info)) {
        gchar *pdp_type_str;
        guint min_cid;
        guint max_cid;
        MMBearerIpFamily pdp_type;

        /* Read PDP type */
        pdp_type_str = mm_get_string_unquoted_from_match_info (match_info, 3);
        pdp_type = mm_3gpp_get_ip_family_from_pdp_type (pdp_type_str);
        if (pdp_type == MM_BEARER_IP_FAMILY_NONE)
            mm_obj_dbg (log_object, "unhandled PDP type in CGDCONT=? reply: '%s'", pdp_type_str);
        else {
            /* Read min CID */
            if (!mm_get_uint_from_match_info (match_info, 1, &min_cid))
                mm_obj_warn (log_object, "invalid min CID in CGDCONT=? reply for PDP type '%s'", pdp_type_str);
            else {
                MM3gppPdpContextFormat *format;

                /* Read max CID: Optional! If no value given, we default to min CID */
                if (!mm_get_uint_from_match_info (match_info, 2, &max_cid))
                    max_cid = min_cid;

                format = g_slice_new (MM3gppPdpContextFormat);
                format->pdp_type = pdp_type;
                format->min_cid = min_cid;
                format->max_cid = max_cid;

                list = g_list_prepend (list, format);
            }
        }

        g_free (pdp_type_str);
        g_match_info_next (match_info, &inner_error);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        mm_obj_warn (log_object, "unexpected error matching +CGDCONT response: '%s'", inner_error->message);
        g_error_free (inner_error);
    }

    return list;
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

    if (!reply || !reply[0])
        /* No APNs configured, all done */
        return NULL;

    list = NULL;
    r = g_regex_new ("\\+CGDCONT:\\s*(\\d+)\\s*,([^, \\)]*)\\s*,([^, \\)]*)\\s*,([^, \\)]*)",
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
            if (ip_family != MM_BEARER_IP_FAMILY_NONE) {
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

static void
mm_3gpp_pdp_context_active_free (MM3gppPdpContextActive *pdp_active)
{
    g_slice_free (MM3gppPdpContextActive, pdp_active);
}

void
mm_3gpp_pdp_context_active_list_free (GList *pdp_active_list)
{
    g_list_free_full (pdp_active_list, (GDestroyNotify) mm_3gpp_pdp_context_active_free);
}

gint
mm_3gpp_pdp_context_active_cmp (MM3gppPdpContextActive *a,
                                MM3gppPdpContextActive *b)
{
    return (a->cid - b->cid);
}

GList *
mm_3gpp_parse_cgact_read_response (const gchar *reply,
                                   GError **error)
{
    GError *inner_error = NULL;
    GRegex *r;
    GMatchInfo *match_info;
    GList *list;

    if (!reply || !reply[0])
        /* Nothing configured, all done */
        return NULL;

    list = NULL;
    r = g_regex_new ("\\+CGACT:\\s*(\\d+),(\\d+)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, &inner_error);
    g_assert (r);

    g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, &inner_error);
    while (!inner_error && g_match_info_matches (match_info)) {
        MM3gppPdpContextActive *pdp_active;
        guint cid = 0;
        guint aux = 0;

        if (!mm_get_uint_from_match_info (match_info, 1, &cid)) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't parse CID from reply: '%s'",
                                       reply);
            break;
        }
        if (!mm_get_uint_from_match_info (match_info, 2, &aux) || (aux != 0 && aux != 1)) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't parse context status from reply: '%s'",
                                       reply);
            break;
        }

        pdp_active = g_slice_new0 (MM3gppPdpContextActive);
        pdp_active->cid = cid;
        pdp_active->active = (gboolean) aux;
        list = g_list_prepend (list, pdp_active);

        g_match_info_next (match_info, &inner_error);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        mm_3gpp_pdp_context_active_list_free (list);
        g_propagate_error (error, inner_error);
        g_prefix_error (error, "Couldn't properly parse list of active/inactive PDP contexts. ");
        return NULL;
    }

    list = g_list_sort (list, (GCompareFunc) mm_3gpp_pdp_context_active_cmp);

    return list;
}

/*************************************************************************/

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
mm_3gpp_parse_creg_response (GMatchInfo                    *info,
                             gpointer                       log_object,
                             MMModem3gppRegistrationState  *out_reg_state,
                             gulong                        *out_lac,
                             gulong                        *out_ci,
                             MMModemAccessTechnology       *out_act,
                             gboolean                      *out_cgreg,
                             gboolean                      *out_cereg,
                             gboolean                      *out_c5greg,
                             GError                       **error)
{
    gint n_matches, act = -1;
    guint stat = 0;
    guint64 lac = 0, ci = 0;
    guint istat = 0, ilac = 0, ici = 0, iact = 0;
    gchar *str;

    g_assert (info != NULL);
    g_assert (out_reg_state != NULL);
    g_assert (out_lac != NULL);
    g_assert (out_ci != NULL);
    g_assert (out_act != NULL);
    g_assert (out_cgreg != NULL);
    g_assert (out_cereg != NULL);
    g_assert (out_c5greg != NULL);

    str = g_match_info_fetch (info, 1);
    *out_cgreg = (str && strstr (str, "CGREG")) ? TRUE : FALSE;
    *out_cereg = (str && strstr (str, "CEREG")) ? TRUE : FALSE;
    *out_c5greg = (str && strstr (str, "C5GREG")) ? TRUE : FALSE;
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
                ilac  = 3;
            } else {
                istat = 3;
                ilac  = 4;
            }
            ici  = 5;
            iact = 6;
        } else {
            /* Check if the third item is the LAC to distinguish the two cases */
            if (item_is_lac_not_stat (info, 3)) {
                istat = 2;
                ilac  = 3;
                ici   = 4;
                iact  = 5;
            } else {
                istat = 3;
                ilac  = 4;
                ici   = 5;
                iact  = 6;
            }
        }
    } else if (n_matches == 8) {
        /* CEREG=2 (solicited with RAC):  +CEREG: <n>,<stat>,<lac>,<rac>,<ci>,<AcT>
         * C5GREG=2 (unsolicited):        +C5GREG: <stat>,<tac>,<ci>,<AcT>,<Allowed_NSSAI_length>,<Allowed_NSSAI>
         */
        if (*out_cereg) {
            istat = 3;
            ilac  = 4;
            ici   = 6;
            iact  = 7;
        } else if (*out_c5greg) {
            istat = 2;
            ilac  = 3;
            ici   = 4;
            iact  = 5;
        }
    } else if (n_matches == 9) {
        /* C5GREG=2 (solicited): +C5GREG: <n>,<stat>,<tac>,<ci>,<AcT>,<Allowed_NSSAI_length>,<Allowed_NSSAI> */
        istat = 3;
        ilac  = 4;
        ici   = 5;
        iact  = 6;
    }

    /* Status */
    if (!mm_get_uint_from_match_info (info, istat, &stat)) {
        g_set_error_literal (error,
                             MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Could not parse the registration status response");
        return FALSE;
    }

    /* 'attached RLOS' is the last valid state */
    if (stat > MM_MODEM_3GPP_REGISTRATION_STATE_ATTACHED_RLOS) {
        mm_obj_warn (log_object, "unknown registration state value '%u'", stat);
        stat = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    }

    /* Location Area Code/Tracking Area Code
     * FIXME: some phones apparently swap the LAC bytes (LG, SonyEricsson,
     * Sagem).  Need to handle that.
     */
    if (ilac)
        mm_get_u64_from_hex_match_info (info, ilac, &lac);

    /* Cell ID */
    if (ici)
        mm_get_u64_from_hex_match_info (info, ici, &ci);

    /* Access Technology */
    if (iact)
        mm_get_int_from_match_info (info, iact, &act);

    *out_reg_state = (MMModem3gppRegistrationState) stat;
    if (stat != MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN) {
        /* Don't fill in lac/ci/act if the device's state is unknown */
        *out_lac = (gulong)lac;
        *out_ci  = (gulong)ci;
        *out_act = (act >= 0 ? get_mm_access_tech_from_etsi_access_tech (act) : MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
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

    if (!g_regex_match (r, reply, 0, &match_info)) {
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

MM3gppPduInfo *
mm_3gpp_parse_cmgr_read_response (const gchar *reply,
                                  guint index,
                                  GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    gint count;
    gint status;
    gchar *pdu;
    MM3gppPduInfo *info = NULL;

    /* +CMGR: <stat>,<alpha>,<length>(whitespace)<pdu> */
    /* The <alpha> and <length> fields are matched, but not currently used */
    r = g_regex_new ("\\+CMGR:\\s*(\\d+)\\s*,([^,]*),\\s*(\\d+)\\s*([^\\r\\n]*)", 0, 0, NULL);
    g_assert (r);

    if (!g_regex_match (r, reply, 0, &match_info)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse CMGR read result: response didn't match '%s'",
                     reply);
        goto done;
    }

    /* g_match_info_get_match_count includes match #0 */
    if ((count = g_match_info_get_match_count (match_info)) != 5) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to match CMGR fields (matched %d) '%s'",
                     count,
                     reply);
        goto done;
    }

    if (!mm_get_int_from_match_info (match_info, 1, &status)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to extract CMGR status field '%s'",
                     reply);
        goto done;
    }


    pdu = mm_get_string_unquoted_from_match_info (match_info, 4);
    if (!pdu) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to extract CMGR pdu field '%s'",
                     reply);
        goto done;
    }

    info = g_new0 (MM3gppPduInfo, 1);
    info->index = index;
    info->status = status;
    info->pdu = pdu;

done:
    g_match_info_free (match_info);
    g_regex_unref (r);

    return info;
}

/*****************************************************************************/
/* AT+CRSM response parser */

gboolean
mm_3gpp_parse_crsm_response (const gchar *reply,
                             guint *sw1,
                             guint *sw2,
                             gchar **hex,
                             GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;

    g_assert (sw1 != NULL);
    g_assert (sw2 != NULL);
    g_assert (hex != NULL);

    *sw1 = 0;
    *sw2 = 0;
    *hex = NULL;

    if (!reply || !g_str_has_prefix (reply, "+CRSM:")) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing +CRSM prefix");
        return FALSE;
    }

    r = g_regex_new ("\\+CRSM:\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*\"?([0-9a-fA-F]+)\"?",
                     G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match (r, reply, 0, &match_info) &&
        mm_get_uint_from_match_info (match_info, 1, sw1) &&
        mm_get_uint_from_match_info (match_info, 2, sw2))
        *hex = mm_get_string_unquoted_from_match_info (match_info, 3);

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (*hex == NULL) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse CRSM query result '%s'",
                     reply);
        return FALSE;
    }

    return TRUE;
}

/*************************************************************************/
/* CGCONTRDP=N response parser */

static gboolean
split_local_address_and_subnet (const gchar  *str,
                                gchar       **local_address,
                                gchar       **subnet)
{
    const gchar *separator;
    guint count = 0;

    /* E.g. split: "2.43.2.44.255.255.255.255"
     * into:
     *    local address: "2.43.2.44",
     *    subnet: "255.255.255.255"
     */
    g_assert (str);
    g_assert (local_address);
    g_assert (subnet);

    separator = str;
    while (1) {
        separator = strchr (separator, '.');
        if (separator) {
            count++;
            if (count == 4) {
                if (local_address)
                    *local_address = g_strndup (str, (separator - str));
                if (subnet)
                    *subnet = g_strdup (++separator);
                return TRUE;
            }
            separator++;
            continue;
        }

        /* Not even the full IP? report error parsing */
        if (count < 3)
            return FALSE;

        if (count == 3) {
            if (local_address)
                *local_address = g_strdup (str);
            if (subnet)
                *subnet = NULL;
            return TRUE;
        }
    }
}

gboolean
mm_3gpp_parse_cgcontrdp_response (const gchar  *response,
                                  guint        *out_cid,
                                  guint        *out_bearer_id,
                                  gchar       **out_apn,
                                  gchar       **out_local_address,
                                  gchar       **out_subnet,
                                  gchar       **out_gateway_address,
                                  gchar       **out_dns_primary_address,
                                  gchar       **out_dns_secondary_address,
                                  GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info;
    GError     *inner_error = NULL;
    guint       cid = 0;
    guint       bearer_id = 0;
    gchar      *apn = NULL;
    gchar      *local_address_and_subnet = NULL;
    gchar      *local_address = NULL;
    gchar      *subnet = NULL;
    gchar      *gateway_address = NULL;
    gchar      *dns_primary_address = NULL;
    gchar      *dns_secondary_address = NULL;
    guint       field_format_extra_index = 0;

    /* Response may be e.g.:
     * +CGCONTRDP: 4,5,"ibox.tim.it.mnc001.mcc222.gprs","2.197.17.49.255.255.255.255","2.197.17.49","10.207.43.46","10.206.56.132","0.0.0.0","0.0.0.0",0
     *
     * We assume only ONE line is returned; because we request +CGCONTRDP with
     * a specific N CID. Also, we don't parse all fields, we stop after
     * secondary DNS.
     *
     * Only the 3 first parameters (cid, bearer id, apn) are mandatory in the
     * response, all the others are optional, but, we'll anyway assume that APN
     * may be empty.
     *
     * The format of the response changed in TS 27.007 v9.4.0, we try to detect
     * both formats ('a' if >= v9.4.0, 'b' if < v9.4.0) with a single regex here.
     */
    r = g_regex_new ("\\+CGCONTRDP: "
                     "(\\d+),(\\d+),([^,]*)" /* cid, bearer id, apn */
                     "(?:,([^,]*))?" /* (a)ip+mask        or (b)ip */
                     "(?:,([^,]*))?" /* (a)gateway        or (b)mask */
                     "(?:,([^,]*))?" /* (a)dns1           or (b)gateway */
                     "(?:,([^,]*))?" /* (a)dns2           or (b)dns1 */
                     "(?:,([^,]*))?" /* (a)p-cscf primary or (b)dns2 */
                     "(?:,(.*))?"    /* others, ignored */
                     "(?:\\r\\n)?",
                     0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Couldn't match +CGCONTRDP response");
        goto out;
    }

    if (out_cid && !mm_get_uint_from_match_info (match_info, 1, &cid)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing cid");
        goto out;
    }

    if (out_bearer_id && !mm_get_uint_from_match_info (match_info, 2, &bearer_id)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing bearer id");
        goto out;
    }

    /* Remaining strings are optional or empty allowed */

    if (out_apn)
        apn = mm_get_string_unquoted_from_match_info (match_info, 3);

    /*
     * The +CGCONTRDP=[cid] response format before version TS 27.007 v9.4.0 had
     * the subnet in its own comma-separated field. Try to detect that.
     */
    local_address_and_subnet = mm_get_string_unquoted_from_match_info (match_info, 4);
    if (local_address_and_subnet && !split_local_address_and_subnet (local_address_and_subnet, &local_address, &subnet)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing local address and subnet");
        goto out;
    }
    /* If we don't have a subnet in field 4, we're using the old format with subnet in an extra field */
    if (!subnet) {
        if (out_subnet)
            subnet = mm_get_string_unquoted_from_match_info (match_info, 5);
        field_format_extra_index = 1;
    }

    if (out_gateway_address)
        gateway_address = mm_get_string_unquoted_from_match_info (match_info, 5 + field_format_extra_index);

    if (out_dns_primary_address)
        dns_primary_address = mm_get_string_unquoted_from_match_info (match_info, 6 + field_format_extra_index);

    if (out_dns_secondary_address)
        dns_secondary_address = mm_get_string_unquoted_from_match_info (match_info, 7 + field_format_extra_index);

out:
    g_match_info_free (match_info);
    g_regex_unref (r);

    g_free (local_address_and_subnet);

    if (inner_error) {
        g_free (apn);
        g_free (local_address);
        g_free (subnet);
        g_free (gateway_address);
        g_free (dns_primary_address);
        g_free (dns_secondary_address);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_cid)
        *out_cid = cid;
    if (out_bearer_id)
        *out_bearer_id = bearer_id;
    if (out_apn)
        *out_apn = apn;

    /* Local address and subnet may always be retrieved, even if not requested
     * by the caller, as we need them to know which +CGCONTRDP=[cid] response is
     * being parsed. So make sure we free them if not needed. */
    if (out_local_address)
        *out_local_address = local_address;
    else
        g_free (local_address);
    if (out_subnet)
        *out_subnet = subnet;
    else
        g_free (subnet);

    if (out_gateway_address)
        *out_gateway_address = gateway_address;
    if (out_dns_primary_address)
        *out_dns_primary_address = dns_primary_address;
    if (out_dns_secondary_address)
        *out_dns_secondary_address = dns_secondary_address;
    return TRUE;
}

/*************************************************************************/

gboolean
mm_3gpp_parse_cfun_query_response (const gchar  *response,
                                   guint        *out_state,
                                   GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info;
    GError     *inner_error = NULL;
    guint       state = G_MAXUINT;

    g_assert (out_state != NULL);

    /* Response may be e.g.:
     * +CFUN: 1,0
     *   ..but we don't care about the second number
     */
    r = g_regex_new ("\\+CFUN: (\\d+)(?:,(?:\\d+))?(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Couldn't parse +CFUN response: %s", response);
        goto out;
    }

    if (!mm_get_uint_from_match_info (match_info, 1, &state)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Couldn't read power state value");
        goto out;
    }

    *out_state = state;

out:
    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/
/* +CESQ response parser */

gboolean
mm_3gpp_parse_cesq_response (const gchar  *response,
                             guint        *out_rxlev,
                             guint        *out_ber,
                             guint        *out_rscp,
                             guint        *out_ecn0,
                             guint        *out_rsrq,
                             guint        *out_rsrp,
                             GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info;
    GError     *inner_error = NULL;
    guint       rxlev = 99;
    guint       ber = 99;
    guint       rscp = 255;
    guint       ecn0 = 255;
    guint       rsrq = 255;
    guint       rsrp = 255;
    gboolean    success = FALSE;

    g_assert (out_rxlev);
    g_assert (out_ber);
    g_assert (out_rscp);
    g_assert (out_ecn0);
    g_assert (out_rsrq);
    g_assert (out_rsrp);

    /* Response may be e.g.:
     * +CESQ: 99,99,255,255,20,80
     */
    r = g_regex_new ("\\+CESQ: (\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        if (!mm_get_uint_from_match_info (match_info, 1, &rxlev)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't read RXLEV");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 2, &ber)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't read BER");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 3, &rscp)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't read RSCP");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 4, &ecn0)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't read Ec/N0");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 5, &rsrq)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't read RSRQ");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 6, &rsrp)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't read RSRP");
            goto out;
        }
        success = TRUE;
    }

out:
    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (!success) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse +CESQ response: %s", response);
        return FALSE;
    }

    *out_rxlev = rxlev;
    *out_ber = ber;
    *out_rscp = rscp;
    *out_ecn0 = ecn0;
    *out_rsrq = rsrq;
    *out_rsrp = rsrp;
    return TRUE;
}

gboolean
mm_3gpp_rxlev_to_rssi (guint     rxlev,
                       gpointer  log_object,
                       gdouble  *out_rssi)
{
    if (rxlev <= 63) {
        *out_rssi = -111.0 + rxlev;
        return TRUE;
    }

    if (rxlev != 99)
        mm_obj_warn (log_object, "unexpected rxlev: %u", rxlev);
    return FALSE;
}

gboolean
mm_3gpp_rscp_level_to_rscp (guint     rscp_level,
                            gpointer  log_object,
                            gdouble  *out_rscp)
{
    if (rscp_level <= 96) {
        *out_rscp = -121.0 + rscp_level;
        return TRUE;
    }

    if (rscp_level != 255)
        mm_obj_warn (log_object, "unexpected rscp level: %u", rscp_level);
    return FALSE;
}

gboolean
mm_3gpp_ecn0_level_to_ecio (guint     ecn0_level,
                            gpointer  log_object,
                            gdouble  *out_ecio)
{
    if (ecn0_level <= 49) {
        *out_ecio = -24.5 + (((gdouble) ecn0_level) * 0.5);
        return TRUE;
    }

    if (ecn0_level != 255)
        mm_obj_warn (log_object, "unexpected Ec/N0 level: %u", ecn0_level);
    return FALSE;
}

gboolean
mm_3gpp_rsrq_level_to_rsrq (guint     rsrq_level,
                            gpointer  log_object,
                            gdouble  *out_rsrq)
{
    if (rsrq_level <= 34) {
        *out_rsrq = -20.0 + (((gdouble) rsrq_level) * 0.5);
        return TRUE;
    }

    if (rsrq_level != 255)
        mm_obj_warn (log_object, "unexpected RSRQ level: %u", rsrq_level);
    return FALSE;
}

gboolean
mm_3gpp_rsrp_level_to_rsrp (guint     rsrp_level,
                            gpointer  log_object,
                            gdouble  *out_rsrp)
{
    if (rsrp_level <= 97) {
        *out_rsrp = -141.0 + rsrp_level;
        return TRUE;
    }

    if (rsrp_level != 255)
        mm_obj_warn (log_object, "unexpected RSRP level: %u", rsrp_level);
    return FALSE;
}

gboolean
mm_3gpp_cesq_response_to_signal_info (const gchar  *response,
                                      gpointer      log_object,
                                      MMSignal    **out_gsm,
                                      MMSignal    **out_umts,
                                      MMSignal    **out_lte,
                                      GError      **error)
{
    guint     rxlev = 0;
    guint     ber = 0;
    guint     rscp_level = 0;
    guint     ecn0_level = 0;
    guint     rsrq_level = 0;
    guint     rsrp_level = 0;
    gdouble   rssi = -G_MAXDOUBLE;
    gdouble   rscp = -G_MAXDOUBLE;
    gdouble   ecio = -G_MAXDOUBLE;
    gdouble   rsrq = -G_MAXDOUBLE;
    gdouble   rsrp = -G_MAXDOUBLE;
    MMSignal *gsm = NULL;
    MMSignal *umts = NULL;
    MMSignal *lte = NULL;

    if (!mm_3gpp_parse_cesq_response (response,
                                      &rxlev, &ber,
                                      &rscp_level, &ecn0_level,
                                      &rsrq_level, &rsrp_level,
                                      error))
        return FALSE;

    /* GERAN RSSI */
    if (mm_3gpp_rxlev_to_rssi (rxlev, log_object, &rssi)) {
        gsm = mm_signal_new ();
        mm_signal_set_rssi (gsm, rssi);
    }

    /* ignore BER */

    /* UMTS RSCP */
    if (mm_3gpp_rscp_level_to_rscp (rscp_level, log_object, &rscp)) {
        umts = mm_signal_new ();
        mm_signal_set_rscp (umts, rscp);
    }

    /* UMTS EcIo (assumed EcN0) */
    if (mm_3gpp_ecn0_level_to_ecio (ecn0_level, log_object, &ecio)) {
        if (!umts)
            umts = mm_signal_new ();
        mm_signal_set_ecio (umts, ecio);
    }

    /* LTE RSRQ */
    if (mm_3gpp_rsrq_level_to_rsrq (rsrq_level, log_object, &rsrq)) {
        lte = mm_signal_new ();
        mm_signal_set_rsrq (lte, rsrq);
    }

    /* LTE RSRP */
    if (mm_3gpp_rsrp_level_to_rsrp (rsrp_level, log_object, &rsrp)) {
        if (!lte)
            lte = mm_signal_new ();
        mm_signal_set_rsrp (lte, rsrp);
    }

    if (!gsm && !umts && !lte) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't build detailed signal info");
        return FALSE;
    }

    if (gsm)
        *out_gsm = gsm;
    if (umts)
        *out_umts = umts;
    if (lte)
        *out_lte = lte;

    return TRUE;
}

gboolean
mm_3gpp_parse_cfun_query_generic_response (const gchar        *response,
                                           MMModemPowerState  *out_state,
                                           GError            **error)
{
    guint state;

    if (!mm_3gpp_parse_cfun_query_response (response, &state, error))
        return FALSE;

    switch (state) {
    case 0:
        *out_state = MM_MODEM_POWER_STATE_OFF;
        return TRUE;
    case 1:
        *out_state = MM_MODEM_POWER_STATE_ON;
        return TRUE;
    case 4:
        *out_state = MM_MODEM_POWER_STATE_LOW;
        return TRUE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown +CFUN state: %u", state);
        return FALSE;
    }
}

static MMModem3gppEpsUeModeOperation cemode_values[] = {
    [0] = MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_PS_2,
    [1] = MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_CSPS_1,
    [2] = MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_CSPS_2,
    [3] = MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_PS_1,
};

gchar *
mm_3gpp_build_cemode_set_request (MMModem3gppEpsUeModeOperation mode)
{
    guint i;

    g_return_val_if_fail (mode != MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN, NULL);

    for (i = 0; i < G_N_ELEMENTS (cemode_values); i++) {
        if (mode == cemode_values[i])
            return g_strdup_printf ("+CEMODE=%u", i);
    }

    g_assert_not_reached ();
    return NULL;
}

gboolean
mm_3gpp_parse_cemode_query_response (const gchar                    *response,
                                     MMModem3gppEpsUeModeOperation  *out_mode,
                                     GError                        **error)
{
    guint value = 0;

    response = mm_strip_tag (response, "+CEMODE:");
    if (mm_get_uint_from_str (response, &value) && value < G_N_ELEMENTS (cemode_values)) {
        if (out_mode)
            *out_mode = cemode_values[value];
        return TRUE;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse UE mode of operation: '%s' (value %u)",
                 response, value);
    return FALSE;
}

/*************************************************************************/
/* CCWA service query response parser */

gboolean
mm_3gpp_parse_ccwa_service_query_response (const gchar  *response,
                                           gpointer      log_object,
                                           gboolean     *status,
                                           GError      **error)
{
    GRegex     *r;
    GError     *inner_error = NULL;
    GMatchInfo *match_info  = NULL;
    gint        class_1_status = -1;

    /*
     * AT+CCWA=<n>[,<mode>]
     *   +CCWA: <status>,<class1>
     *   [+CCWA: <status>,<class2>
     *   [...]]
     *   OK
     *
     * If <classX> is 255 it applies to ALL classes.
     *
     * We're only interested in class 1 (voice)
     */
    r = g_regex_new ("\\+CCWA:\\s*(\\d+),\\s*(\\d+)$",
                     G_REGEX_RAW | G_REGEX_MULTILINE | G_REGEX_NEWLINE_CRLF,
                     G_REGEX_MATCH_NEWLINE_CRLF,
                     NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    /* Parse the results */
    while (g_match_info_matches (match_info)) {
        guint st;
        guint class;

        if (!mm_get_uint_from_match_info (match_info, 2, &class))
            mm_obj_warn (log_object, "couldn't parse class from +CCWA line");
        else if (class == 1 || class == 255) {
            if (!mm_get_uint_from_match_info (match_info, 1, &st))
                mm_obj_warn (log_object, "couldn't parse status from +CCWA line");
            else {
                class_1_status = st;
                break;
            }
        }
        g_match_info_next (match_info, NULL);
    }

out:
    g_clear_pointer (&match_info, g_match_info_free);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (class_1_status < 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                     "call waiting status for voice class missing");
        return FALSE;
    }

    if (class_1_status != 0 && class_1_status != 1) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "call waiting status for voice class invalid: %d", class_1_status);
        return FALSE;
    }

    if (status)
        *status = (gboolean) class_1_status;

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
mm_3gpp_parse_cpms_test_response (const gchar  *reply,
                                  GArray      **mem1,
                                  GArray      **mem2,
                                  GArray      **mem3,
                                  GError      **error)
{
    guint i;
    g_autoptr(GRegex) r = NULL;
    g_autoptr(GArray) tmp1 = NULL;
    g_autoptr(GArray) tmp2 = NULL;
    g_autoptr(GArray) tmp3 = NULL;
    g_auto(GStrv)     split = NULL;

    g_assert (mem1 != NULL);
    g_assert (mem2 != NULL);
    g_assert (mem3 != NULL);

#define N_EXPECTED_GROUPS 3

    split = mm_split_string_groups (mm_strip_tag (reply, "+CPMS:"));
    if (!split) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't split +CPMS test response in groups");
        return FALSE;
    }

    if (g_strv_length (split) != N_EXPECTED_GROUPS) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Cannot parse +CPMS test response: invalid number of groups (%u != %u)",
                     g_strv_length (split), N_EXPECTED_GROUPS);
        return FALSE;
    }

    r = g_regex_new ("\\s*\"([^,\\)]+)\"\\s*", 0, 0, NULL);
    g_assert (r);

    for (i = 0; i < N_EXPECTED_GROUPS; i++) {
        GMatchInfo *match_info = NULL;
        GArray *array;

        /* We always return a valid array, even if it may be empty */
        array = g_array_new (FALSE, FALSE, sizeof (MMSmsStorage));

        /* Got a range group to match */
        if (g_regex_match (r, split[i], 0, &match_info)) {
            while (g_match_info_matches (match_info)) {
                gchar *str;

                str = g_match_info_fetch (match_info, 1);
                if (str) {
                    MMSmsStorage storage;

                    storage = storage_from_str (str);
                    g_array_append_val (array, storage);
                    g_free (str);
                }

                g_match_info_next (match_info, NULL);
            }
        }
        g_match_info_free (match_info);

        if (!tmp1)
            tmp1 = array;
        else if (!tmp2)
            tmp2 = array;
        else if (!tmp3)
            tmp3 = array;
        else
            g_assert_not_reached ();
    }

    /* Only return TRUE if all sets have been parsed correctly
     * (even if the arrays may be empty) */
    if (tmp1 && tmp2 && tmp3) {
        *mem1 = g_steal_pointer (&tmp1);
        *mem2 = g_steal_pointer (&tmp2);
        *mem3 = g_steal_pointer (&tmp3);
        return TRUE;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Cannot parse +CPMS test response: mem1 %s, mem2 %s, mem3 %s",
                 tmp1 ? "yes" : "no",
                 tmp2 ? "yes" : "no",
                 tmp3 ? "yes" : "no");
    return FALSE;
}

/**********************************************************************
 * AT+CPMS?
 * +CPMS: <memr>,<usedr>,<totalr>,<memw>,<usedw>,<totalw>, <mems>,<useds>,<totals>
 */

#define CPMS_QUERY_REGEX "\\+CPMS:\\s*\"(?P<memr>.*)\",[0-9]+,[0-9]+,\"(?P<memw>.*)\",[0-9]+,[0-9]+,\"(?P<mems>.*)\",[0-9]+,[0-9]"

gboolean
mm_3gpp_parse_cpms_query_response (const gchar *reply,
                                   MMSmsStorage *memr,
                                   MMSmsStorage *memw,
                                   GError **error)
{
    GRegex *r = NULL;
    gboolean ret = FALSE;
    GMatchInfo *match_info = NULL;

    r = g_regex_new (CPMS_QUERY_REGEX, G_REGEX_RAW, 0, NULL);

    g_assert (r);

    if (!g_regex_match (r, reply, 0, &match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse CPMS query response '%s'", reply);
        goto end;
    }

    if (!g_match_info_matches (match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find matches in CPMS query reply '%s'", reply);
        goto end;
    }

    if (!mm_3gpp_get_cpms_storage_match (match_info, "memr", memr, error)) {
        goto end;
    }

    if (!mm_3gpp_get_cpms_storage_match (match_info, "memw", memw, error)) {
        goto end;
    }

    ret = TRUE;

end:
    if (r != NULL)
        g_regex_unref (r);

    g_match_info_free (match_info);

    return ret;
}

gboolean
mm_3gpp_get_cpms_storage_match (GMatchInfo *match_info,
                                const gchar *match_name,
                                MMSmsStorage *storage,
                                GError **error)
{
    gboolean ret = TRUE;
    gchar *str = NULL;

    str = g_match_info_fetch_named (match_info, match_name);
    if (str == NULL || str[0] == '\0') {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find '%s' from CPMS reply", match_name);
        ret = FALSE;
    } else {
        *storage = storage_from_str (str);
    }

    g_free (str);

    return ret;
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

    if (g_regex_match (r, p, 0, &match_info)) {
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
    if (g_regex_match (r, reply, 0, &match_info)) {
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
mm_3gpp_parse_cnum_exec_response (const gchar *reply)
{
    g_autoptr(GPtrArray)  array = NULL;
    g_autoptr(GRegex)     r = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;

    /* Empty strings also return NULL list */
    if (!reply || !reply[0])
        return NULL;

    r = g_regex_new ("\\+CNUM:\\s*((\"([^\"]|(\\\"))*\")|([^,]*)),\"(?<num>\\S+)\",\\d",
                     G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    array = g_ptr_array_new ();
    g_regex_match (r, reply, 0, &match_info);
    while (g_match_info_matches (match_info)) {
        g_autofree gchar *number = NULL;

        number = g_match_info_fetch_named (match_info, "num");
        if (number && number[0])
            g_ptr_array_add (array, g_steal_pointer (&number));
        g_match_info_next (match_info, NULL);
    }

    if (!array->len)
        return NULL;

    g_ptr_array_add (array, NULL);
    return (GStrv) g_ptr_array_free (g_steal_pointer (&array), FALSE);
}

/*************************************************************************/

gchar *
mm_3gpp_build_cmer_set_request (MM3gppCmerMode mode,
                                MM3gppCmerInd  ind)
{
    guint mode_val;
    guint ind_val;

    if (mode == MM_3GPP_CMER_MODE_DISCARD_URCS)
        return g_strdup ("+CMER=0");
    if (mode < MM_3GPP_CMER_MODE_DISCARD_URCS || mode > MM_3GPP_CMER_MODE_FORWARD_URCS)
        return NULL;
    mode_val = mm_find_bit_set (mode);

    if (ind < MM_3GPP_CMER_IND_DISABLE || ind > MM_3GPP_CMER_IND_ENABLE_ALL)
        return NULL;
    ind_val = mm_find_bit_set (ind);

    return g_strdup_printf ("+CMER=%u,0,0,%u", mode_val, ind_val);
}

gboolean
mm_3gpp_parse_cmer_test_response (const gchar     *response,
                                  gpointer         log_object,
                                  MM3gppCmerMode  *out_supported_modes,
                                  MM3gppCmerInd   *out_supported_inds,
                                  GError         **error)
{
    gchar          **split;
    GError          *inner_error = NULL;
    GArray          *array_supported_modes = NULL;
    GArray          *array_supported_inds = NULL;
    gchar           *aux   = NULL;
    gboolean         ret = FALSE;
    MM3gppCmerMode   supported_modes = 0;
    MM3gppCmerInd    supported_inds = 0;
    guint            i;

    /*
     * AT+CMER=?
     *  +CMER: 1,0,0,(0-1),0
     *
     * AT+CMER=?
     *  +CMER: (0-3),(0),(0),(0-1),(0-1)
     *
     * AT+CMER=?
     *  +CMER: (1,2),0,0,(0-1),0
     */

    split = mm_split_string_groups (mm_strip_tag (response, "+CMER:"));
    if (!split) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't split +CMER test response in groups");
        goto out;
    }

    /* We want 1st and 4th groups */
    if (g_strv_length (split) < 4) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing groups in +CMER test response (%u < 4)", g_strv_length (split));
        goto out;
    }

    /* Modes in 1st group */
    if (!(array_supported_modes = mm_parse_uint_list (split[0], &inner_error)))
        goto out;
    g_clear_pointer (&aux, g_free);

    /* Ind settings in 4th group */
    if (!(array_supported_inds = mm_parse_uint_list (split[3], &inner_error)))
        goto out;
    g_clear_pointer (&aux, g_free);

    for (i = 0; i < array_supported_modes->len; i++) {
        guint mode_val;

        mode_val = g_array_index (array_supported_modes, guint, i);
        if (mode_val <= 3)
            supported_modes |= (MM3gppCmerMode) (1 << mode_val);
        else
            mm_obj_dbg (log_object, "unknown +CMER mode reported: %u", mode_val);
    }

    for (i = 0; i < array_supported_inds->len; i++) {
        guint ind_val;

        ind_val = g_array_index (array_supported_inds, guint, i);
        if (ind_val <= 2)
            supported_inds |= (MM3gppCmerInd) (1 << ind_val);
        else
            mm_obj_dbg (log_object, "unknown +CMER ind reported: %u", ind_val);
    }

    if (out_supported_modes)
        *out_supported_modes = supported_modes;
    if (out_supported_inds)
        *out_supported_inds = supported_inds;
    ret = TRUE;

out:

    if (array_supported_modes)
        g_array_unref (array_supported_modes);
    if (array_supported_inds)
        g_array_unref (array_supported_inds);
    g_clear_pointer (&aux, g_free);

    g_strfreev (split);

    if (inner_error)
        g_propagate_error (error, inner_error);

    return ret;
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

    if (g_regex_match (r, reply, 0, &match_info)) {
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

    if (!g_regex_match (r, reply, 0, &match_info)) {
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
/* +CGEV indication parser
 *
 * We provide full parsing support, including parameters, for these messages:
 *    +CGEV: NW DETACH
 *    +CGEV: ME DETACH
 *    +CGEV: NW PDN ACT <cid>
 *    +CGEV: ME PDN ACT <cid>[,<reason>[,<cid_other>]]
 *    +CGEV: NW ACT <p_cid>, <cid>, <event_type>
 *    +CGEV: ME ACT <p_cid>, <cid>, <event_type>
 *    +CGEV: NW DEACT <PDP_type>, <PDP_addr>, [<cid>]
 *    +CGEV: ME DEACT <PDP_type>, <PDP_addr>, [<cid>]
 *    +CGEV: NW PDN DEACT <cid>
 *    +CGEV: ME PDN DEACT <cid>
 *    +CGEV: NW DEACT <p_cid>, <cid>, <event_type>
 *    +CGEV: ME DEACT <p_cid>, <cid>, <event_type>
 *    +CGEV: REJECT <PDP_type>, <PDP_addr>
 *    +CGEV: NW REACT <PDP_type>, <PDP_addr>, [<cid>]
 *
 * We don't provide parameter parsing for these messages:
 *    +CGEV: NW CLASS <class>
 *    +CGEV: ME CLASS <class>
 *    +CGEV: NW MODIFY <cid>, <change_reason>, <event_type>
 *    +CGEV: ME MODIFY <cid>, <change_reason>, <event_type>
 */

static gboolean
deact_secondary (const gchar *str)
{
    /* We need to detect the ME/NW DEACT format.
     * Either,
     *    +CGEV: NW DEACT <PDP_type>, <PDP_addr>, [<cid>]
     *    +CGEV: ME DEACT <PDP_type>, <PDP_addr>, [<cid>]
     * or,
     *    +CGEV: NW DEACT <p_cid>, <cid>, <event_type>
     *    +CGEV: ME DEACT <p_cid>, <cid>, <event_type>
     */
    str = strstr (str, "DEACT") + 5;
    while (*str == ' ')
        str++;

    /* We will look for <p_cid> because we know it's NUMERIC */
    return g_ascii_isdigit (*str);
}

MM3gppCgev
mm_3gpp_parse_cgev_indication_action (const gchar *str)
{
    str = mm_strip_tag (str, "+CGEV:");
    if (g_str_has_prefix (str, "NW DETACH"))
        return MM_3GPP_CGEV_NW_DETACH;
    if (g_str_has_prefix (str, "ME DETACH"))
        return MM_3GPP_CGEV_ME_DETACH;
    if (g_str_has_prefix (str, "NW CLASS"))
        return MM_3GPP_CGEV_NW_CLASS;
    if (g_str_has_prefix (str, "ME CLASS"))
        return MM_3GPP_CGEV_ME_CLASS;
    if (g_str_has_prefix (str, "NW PDN ACT"))
        return MM_3GPP_CGEV_NW_ACT_PRIMARY;
    if (g_str_has_prefix (str, "ME PDN ACT"))
        return MM_3GPP_CGEV_ME_ACT_PRIMARY;
    if (g_str_has_prefix (str, "NW ACT"))
        return MM_3GPP_CGEV_NW_ACT_SECONDARY;
    if (g_str_has_prefix (str, "ME ACT"))
        return MM_3GPP_CGEV_ME_ACT_SECONDARY;
    if (g_str_has_prefix (str, "NW DEACT"))
        return (deact_secondary (str) ? MM_3GPP_CGEV_NW_DEACT_SECONDARY : MM_3GPP_CGEV_NW_DEACT_PDP);
    if (g_str_has_prefix (str, "ME DEACT"))
        return (deact_secondary (str) ? MM_3GPP_CGEV_ME_DEACT_SECONDARY : MM_3GPP_CGEV_ME_DEACT_PDP);
    if (g_str_has_prefix (str, "NW PDN DEACT"))
        return MM_3GPP_CGEV_NW_DEACT_PRIMARY;
    if (g_str_has_prefix (str, "ME PDN DEACT"))
        return MM_3GPP_CGEV_ME_DEACT_PRIMARY;
    if (g_str_has_prefix (str, "NW MODIFY"))
        return MM_3GPP_CGEV_NW_MODIFY;
    if (g_str_has_prefix (str, "ME MODIFY"))
        return MM_3GPP_CGEV_ME_MODIFY;
    if (g_str_has_prefix (str, "NW REACT"))
        return MM_3GPP_CGEV_NW_REACT;
    if (g_str_has_prefix (str, "REJECT"))
        return MM_3GPP_CGEV_REJECT;
    return MM_3GPP_CGEV_UNKNOWN;
}

/*
 * +CGEV: NW DEACT <PDP_type>, <PDP_addr>, [<cid>]
 * +CGEV: ME DEACT <PDP_type>, <PDP_addr>, [<cid>]
 * +CGEV: REJECT <PDP_type>, <PDP_addr>
 * +CGEV: NW REACT <PDP_type>, <PDP_addr>, [<cid>]
 */
gboolean
mm_3gpp_parse_cgev_indication_pdp (const gchar  *str,
                                   MM3gppCgev    type,
                                   gchar       **out_pdp_type,
                                   gchar       **out_pdp_addr,
                                   guint        *out_cid,
                                   GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info = NULL;
    GError     *inner_error = NULL;
    gchar      *pdp_type = NULL;
    gchar      *pdp_addr = NULL;
    guint       cid = 0;

    g_assert (type == MM_3GPP_CGEV_REJECT   ||
              type == MM_3GPP_CGEV_NW_REACT ||
              type == MM_3GPP_CGEV_NW_DEACT_PDP ||
              type == MM_3GPP_CGEV_ME_DEACT_PDP);

    r = g_regex_new ("(?:"
                     "REJECT|"
                     "NW REACT|"
                     "NW DEACT|ME DEACT"
                     ")\\s*([^,]*),\\s*([^,]*)(?:,\\s*([0-9]+))?", 0, 0, NULL);

    str = mm_strip_tag (str, "+CGEV:");
    g_regex_match_full (r, str, strlen (str), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match response");
        goto out;
    }

    if (out_pdp_type && !(pdp_type = mm_get_string_unquoted_from_match_info (match_info, 1))) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing PDP type");
        goto out;
    }

    if (out_pdp_addr && !(pdp_addr = mm_get_string_unquoted_from_match_info (match_info, 2))) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing PDP addr");
        goto out;
    }

    /* CID is optional */
    if (out_cid &&
        (g_match_info_get_match_count (match_info) >= 4) &&
        !mm_get_uint_from_match_info (match_info, 3, &cid)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing CID");
        goto out;
    }

out:
    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_free (pdp_type);
        g_free (pdp_addr);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_pdp_type)
        *out_pdp_type = pdp_type;
    if (out_pdp_addr)
        *out_pdp_addr = pdp_addr;
    if (out_cid)
        *out_cid = cid;
    return TRUE;
}

/*
 * +CGEV: NW PDN ACT <cid>
 * +CGEV: ME PDN ACT <cid>[,<reason>[,<cid_other>]]
 * +CGEV: NW PDN DEACT <cid>
 * +CGEV: ME PDN DEACT <cid>
 *
 * NOTE: the special case of a "ME PDN ACT" notification with the additional
 * <reason> and <cid_other> fields is telling us that <cid> was NOT connected
 * but <cid_other> was connected instead, which may happen when trying to
 * connect a IPv4v6 context but the modem ended up connecting a IPv4-only or
 * IPv6-only context instead. We are right now ignoring this, and assuming the
 * <cid> that we requested is the one reported as connected.
 */
gboolean
mm_3gpp_parse_cgev_indication_primary (const gchar  *str,
                                       MM3gppCgev    type,
                                       guint        *out_cid,
                                       GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info = NULL;
    GError     *inner_error = NULL;
    guint       cid = 0;

    g_assert ((type == MM_3GPP_CGEV_NW_ACT_PRIMARY)   ||
              (type == MM_3GPP_CGEV_ME_ACT_PRIMARY)   ||
              (type == MM_3GPP_CGEV_NW_DEACT_PRIMARY) ||
              (type == MM_3GPP_CGEV_ME_DEACT_PRIMARY));

    r = g_regex_new ("(?:"
                     "NW PDN ACT|ME PDN ACT|"
                     "NW PDN DEACT|ME PDN DEACT|"
                     ")\\s*([0-9]+)", 0, 0, NULL);

    str = mm_strip_tag (str, "+CGEV:");
    g_regex_match_full (r, str, strlen (str), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match response");
        goto out;
    }

    if (out_cid && !mm_get_uint_from_match_info (match_info, 1, &cid)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing CID");
        goto out;
    }

out:
    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_cid)
        *out_cid = cid;
    return TRUE;
}

/*
 * +CGEV: NW ACT <p_cid>, <cid>, <event_type>
 * +CGEV: ME ACT <p_cid>, <cid>, <event_type>
 * +CGEV: NW DEACT <p_cid>, <cid>, <event_type>
 * +CGEV: ME DEACT <p_cid>, <cid>, <event_type>
 */
gboolean
mm_3gpp_parse_cgev_indication_secondary (const gchar  *str,
                                         MM3gppCgev    type,
                                         guint        *out_p_cid,
                                         guint        *out_cid,
                                         guint        *out_event_type,
                                         GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info = NULL;
    GError     *inner_error = NULL;
    guint       p_cid = 0;
    guint       cid = 0;
    guint       event_type = 0;

    g_assert (type == MM_3GPP_CGEV_NW_ACT_SECONDARY   ||
              type == MM_3GPP_CGEV_ME_ACT_SECONDARY   ||
              type == MM_3GPP_CGEV_NW_DEACT_SECONDARY ||
              type == MM_3GPP_CGEV_ME_DEACT_SECONDARY);

    r = g_regex_new ("(?:"
                     "NW ACT|ME ACT|"
                     "NW DEACT|ME DEACT"
                     ")\\s*([0-9]+),\\s*([0-9]+),\\s*([0-9]+)", 0, 0, NULL);

    str = mm_strip_tag (str, "+CGEV:");
    g_regex_match_full (r, str, strlen (str), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match response");
        goto out;
    }

    if (out_p_cid && !mm_get_uint_from_match_info (match_info, 1, &p_cid)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing primary CID");
        goto out;
    }

    if (out_cid && !mm_get_uint_from_match_info (match_info, 2, &cid)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing secondary CID");
        goto out;
    }

    if (out_event_type && !mm_get_uint_from_match_info (match_info, 3, &event_type)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing event type");
        goto out;
    }

out:
    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_p_cid)
        *out_p_cid = p_cid;
    if (out_cid)
        *out_cid = cid;
    if (out_event_type)
        *out_event_type = event_type;
    return TRUE;
}

/*************************************************************************/

void
mm_3gpp_pdu_info_free (MM3gppPduInfo *info)
{
    g_free (info->pdu);
    g_free (info);
}

void
mm_3gpp_pdu_info_list_free (GList *info_list)
{
    g_list_free_full (info_list, (GDestroyNotify)mm_3gpp_pdu_info_free);
}

GList *
mm_3gpp_parse_pdu_cmgl_response (const gchar *str,
                                 GError **error)
{
    GError *inner_error = NULL;
    GList *list = NULL;
    GMatchInfo *match_info;
    GRegex *r;

    /*
     * +CMGL: <index>, <status>, [<alpha>], <length>
     *   or
     * +CMGL: <index>, <status>, <length>
     *
     * We just read <index>, <stat> and the PDU itself.
     */
    r = g_regex_new ("\\+CMGL:\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,(.*)\\r\\n([^\\r\\n]*)(\\r\\n)?",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, str, strlen (str), 0, 0, &match_info, &inner_error);
    while (!inner_error && g_match_info_matches (match_info)) {
        MM3gppPduInfo *info;

        info = g_new0 (MM3gppPduInfo, 1);
        if (mm_get_int_from_match_info (match_info, 1, &info->index) &&
            mm_get_int_from_match_info (match_info, 2, &info->status) &&
            (info->pdu = mm_get_string_unquoted_from_match_info (match_info, 4)) != NULL) {
            /* Append to our list of results and keep on */
            list = g_list_append (list, info);
            g_match_info_next (match_info, &inner_error);
        } else {
            mm_3gpp_pdu_info_free (info);
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Error parsing +CMGL response: '%s'",
                                       str);
        }
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        mm_3gpp_pdu_info_list_free (list);
        return NULL;
    }

    return list;
}

/*************************************************************************/

/* Map two letter facility codes into flag values. There are
 * many more facilities defined (for various flavors of call
 * barring); we only map the ones we care about. */
typedef struct {
    MMModem3gppFacility  facility;
    const gchar         *acronym;
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

const gchar *
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
    gsize len;

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

    if (strcasestr (string, "UMTS") ||
        strcasestr (string, "3G") ||
        strcasestr (string, "WCDMA"))
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

    /* Check "EVDO" and "CDMA" as standalone strings since their characters
     * are included in other strings too.
     */
    len = strlen (string);
    if (strncmp (string, "EVDO", 4) && (len >= 4 && !isalnum (string[4])))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    if (strncmp (string, "CDMA", 4) && (len >= 4 && !isalnum (string[4])))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    if (strncmp (string, "CDMA-EVDO", 9) && (len >= 9 && !isalnum (string[9])))
        act |= MM_MODEM_ACCESS_TECHNOLOGY_1XRTT | MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;

    return act;
}

/*************************************************************************/

void
mm_3gpp_normalize_operator (gchar          **operator,
                            MMModemCharset   cur_charset,
                            gpointer         log_object)
{
    g_autofree gchar *normalized = NULL;

    g_assert (operator);

    if (*operator == NULL)
        return;

    /* Despite +CSCS? may claim supporting UCS2, Some modems (e.g. Huawei)
     * always report the operator name in ASCII in a +COPS response. */
    if (cur_charset != MM_MODEM_CHARSET_UNKNOWN) {
        g_autoptr(GError) error = NULL;

        normalized = mm_modem_charset_str_to_utf8 (*operator, -1, cur_charset, TRUE, &error);
        if (normalized)
            goto out;

        mm_obj_dbg (log_object, "couldn't convert operator string '%s' from charset '%s': %s",
                    *operator,
                    mm_modem_charset_to_string (cur_charset),
                    error->message);
    }

    /* Charset is unknown or there was an error in conversion; try to see
     * if the contents we got are valid UTF-8 already. */
    if (g_utf8_validate (*operator, -1, NULL))
        normalized = g_strdup (*operator);

out:

    /* Some modems (Novatel LTE) return the operator name as "Unknown" when
     * it fails to obtain the operator name. Return NULL in such case.
     */
    if (!normalized || g_ascii_strcasecmp (normalized, "unknown") == 0) {
        /* If normalization failed, just cleanup the string */
        g_clear_pointer (operator, g_free);
        return;
    }

    mm_obj_dbg (log_object, "operator normalized '%s'->'%s'", *operator, normalized);
    g_clear_pointer (operator, g_free);
    *operator = g_steal_pointer (&normalized);
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
    case MM_BEARER_IP_FAMILY_NONE:
    case MM_BEARER_IP_FAMILY_ANY:
    default:
        return NULL;
    }
}

MMBearerIpFamily
mm_3gpp_get_ip_family_from_pdp_type (const gchar *pdp_type)
{
    if (!pdp_type)
        return MM_BEARER_IP_FAMILY_NONE;
    if (g_str_equal (pdp_type, "IP"))
        return MM_BEARER_IP_FAMILY_IPV4;
    if (g_str_equal (pdp_type, "IPV4"))
        return MM_BEARER_IP_FAMILY_IPV4;
    if (g_str_equal (pdp_type, "IPV6"))
        return MM_BEARER_IP_FAMILY_IPV6;
    if (g_str_equal (pdp_type, "IPV4V6"))
        return MM_BEARER_IP_FAMILY_IPV4V6;
    return MM_BEARER_IP_FAMILY_NONE;
}

/*************************************************************************/
/* ICCID validation */
/*
 * 89: telecom (2 digits)
 * cc: country (2 digits)
 * oo: operator (2 digits)
 * aaaaaaaaaaaaa: operator-specific account number (13 digits)
 * x: checksum (1 digit)
 */
char *
mm_3gpp_parse_iccid (const char *raw_iccid, GError **error)
{
    gboolean swap;
    char *buf, *swapped = NULL;
    gsize len = 0;
    int i;

    g_return_val_if_fail (raw_iccid != NULL, NULL);

    /* Skip spaces and quotes */
    while (raw_iccid && *raw_iccid && (isspace (*raw_iccid) || *raw_iccid == '"'))
        raw_iccid++;

    /* Make sure the buffer is only digits or 'F' */
    buf = g_strdup (raw_iccid);
    for (len = 0; buf[len]; len++) {
        /* Digit values allowed anywhere */
        if (g_ascii_isdigit (buf[len]))
            continue;

        /* There are operators (e.g. the Chinese CMCC operator) that abuse the
         * fact that 4 bits are used to store the BCD encoded numbers, and also
         * use the [A-F] range as valid characters for the ICCID. Explicitly
         * allow this range in the operator-specific part. */
        if (len >= 6 && g_ascii_isxdigit (buf[len])) {
            /* canonicalize hex digit */
            buf[len] = g_ascii_toupper (buf[len]);
            continue;
        }

        if (buf[len] == '\"') {
            buf[len] = 0;
            break;
        }

        /* Invalid character */
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "ICCID response contained invalid character '%c' at index '%zu'",
                     buf[len], len);
        goto error;
    }

    /* ICCIDs are 19 or 20 digits long */
    if (len < 19 || len > 20) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Invalid ICCID response size (was %zd, expected 19 or 20)",
                     len);
        goto error;
    }

    /* The leading two digits of an ICCID is the major industry identifier and
     * should be '89' for telecommunication purposes according to ISO/IEC 7812.
     */
    if (buf[0] == '8' && buf[1] == '9') {
        swap = FALSE;
    } else if (buf[0] == '9' && buf[1] == '8') {
        /* swapped digits are only expected in raw +CRSM responses, which must all
         * be 20-bytes long */
        if (len != 20) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Invalid ICCID response size while swap needed (expected 20)");
            goto error;
        }
        swap = TRUE;
    } else {
      /* FIXME: Instead of erroring out, revisit this solution if we find any SIM
       * that doesn't use '89' as the major industry identifier of the ICCID.
       */
      g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                   "Invalid ICCID response (leading two digits are not 89)");
      goto error;
    }

    if (swap) {
        /* Swap digits in the ICCID response to get the actual ICCID, each
         * group of 2 digits is reversed.
         *
         *    21436587 -> 12345678
         */
        g_assert (len == 20);
        swapped = g_malloc0 (21);
        for (i = 0; i < 10; i++) {
            swapped[i * 2] = buf[(i * 2) + 1];
            swapped[(i * 2) + 1] = buf[i * 2];
        }
    } else
        swapped = g_strdup (buf);

    /* Zero out the F for 19 digit ICCIDs */
    if (swapped[len - 1] == 'F')
        swapped[len - 1] = 0;

    g_free (buf);
    return swapped;

error:
    g_free (buf);
    g_free (swapped);
    return NULL;
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
/* Emergency numbers (+CRSM output) */

GStrv
mm_3gpp_parse_emergency_numbers (const char *raw, GError **error)
{
    gsize      rawlen;
    guint8    *bin;
    gsize      binlen;
    gsize      max_items;
    GPtrArray *out;
    guint      i;

    /* The emergency call code is of a variable length with a maximum length of
     * 6 digits. Each emergency call code is coded on three bytes, with each
     * digit within the code being coded on four bits. If a code of less that 6
     * digits is chosen, then the unused nibbles shall be set to 'F'. */

    rawlen = strlen (raw);
    if (!rawlen) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "empty emergency numbers list");
        return NULL;
    }

    if (rawlen % 6 != 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "invalid raw emergency numbers list length: %" G_GSIZE_FORMAT, rawlen);
        return NULL;
    }

    bin = mm_utils_hexstr2bin (raw, -1, &binlen, error);
    if (!bin) {
        g_prefix_error (error, "invalid raw emergency numbers list contents: ");
        return NULL;
    }

    max_items = binlen / 3;
    out = g_ptr_array_sized_new (max_items + 1);

    for (i = 0; i < max_items; i++) {
        gchar *number;

        number = mm_bcd_to_string (&bin[i*3], 3, TRUE /* low_nybble_first */);
        if (number && number[0])
            g_ptr_array_add (out, number);
        else
            g_free (number);
    }

    g_free (bin);

    if (!out->len) {
        g_ptr_array_unref (out);
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "uninitialized emergency numbers list");
        return NULL;
    }

    g_ptr_array_add (out, NULL);
    return (GStrv) g_ptr_array_free (out, FALSE);
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
            if ((guint)iter->num == ind) {
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
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;

    /* Expected reply format is:
     *   ---> AT+CRM=?
     *   <--- +CRM: (0-2)
     */

    r = g_regex_new ("\\+CRM:\\s*\\((\\d+)-(\\d+)\\)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, error);
    g_assert (r != NULL);

    if (g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, &match_error)) {
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
    } else if (match_error) {
        g_propagate_error (error, match_error);
    } else {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse CRM range: response didn't match (%s)",
                     reply);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

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

    /* just subtracting 1 from the enum value should give us the index */
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

/*****************************************************************************/
/* +CCLK response parser */

gboolean
mm_parse_cclk_response (const char *response,
                        gchar **iso8601p,
                        MMNetworkTimezone **tzp,
                        GError **error)
{
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;
    guint year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    gint tz = 0;
    gboolean ret = FALSE;

    g_assert (iso8601p || tzp); /* at least one */

    /* Sample replies:
     *  +CCLK: "15/03/05,14:14:26-32"
     *  +CCLK: 17/07/26,11:42:15+01
     */
    r = g_regex_new ("\\+CCLK:\\s*\"?(\\d+)/(\\d+)/(\\d+),(\\d+):(\\d+):(\\d+)([-+]\\d+)?\"?", 0, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, -1, 0, 0, &match_info, &match_error)) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse +CCLK results: ");
        } else {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Couldn't match +CCLK reply: %s", response);
        }
        goto out;
    }

    /* Remember that g_match_info_get_match_count() includes match #0 */
    g_assert (g_match_info_get_match_count (match_info) >= 7);

    /* Parse mandatory date and time fields */
    if (!mm_get_uint_from_match_info (match_info, 1, &year)   ||
        !mm_get_uint_from_match_info (match_info, 2, &month)  ||
        !mm_get_uint_from_match_info (match_info, 3, &day)    ||
        !mm_get_uint_from_match_info (match_info, 4, &hour)   ||
        !mm_get_uint_from_match_info (match_info, 5, &minute) ||
        !mm_get_uint_from_match_info (match_info, 6, &second)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse +CCLK reply: %s", response);
        goto out;
    }

    /* Read optional time zone offset; if not given assume UTC (tz = 0).
     * Note that timezone offset is given in 15 minute intervals.
     */
    if ((g_match_info_get_match_count (match_info) >= 8) &&
        (!mm_get_int_from_match_info (match_info, 7, &tz))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse timezone in +CCLK reply: %s", response);
        goto out;
    }

    /* Adjust year to support YYYY format, as per +CSDF in 3GPP TS 27.007. Also,
     * don't assume the reported date is actually the current real one, as some
     * devices report an initial date of e.g. January 1st 1980. */
    if (year < 100) {
        if (year >= 70)
            year += 1900;
        else
            year += 2000;
    }

    if (tzp) {
        *tzp = mm_network_timezone_new ();
        mm_network_timezone_set_offset (*tzp, tz * 15);
    }

    if (iso8601p) {
        /* Return ISO-8601 format date/time string */
        *iso8601p = mm_new_iso8601_time (year, month, day, hour,
                                         minute, second,
                                         TRUE, (tz * 15));
    }

    ret = TRUE;

 out:
    g_match_info_free (match_info);
    g_regex_unref (r);

    return ret;
}

/*****************************************************************************/
/* +CSIM response parser */
#define MM_MIN_SIM_RETRY_HEX 0x63C0
#define MM_MAX_SIM_RETRY_HEX 0x63CF

gint
mm_parse_csim_response (const gchar *response,
                              GError **error)
{
    GMatchInfo *match_info = NULL;
    GRegex *r = NULL;
    gchar *str_code = NULL;
    gint retries = -1;
    guint hex_code;
    GError *inner_error = NULL;

    r = g_regex_new ("\\+CSIM:\\s*[0-9]+,\\s*\".*([0-9a-fA-F]{4})\"", G_REGEX_RAW, 0, NULL);
    g_regex_match (r, response, 0, &match_info);

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Could not recognize +CSIM response '%s'", response);
        goto out;
    }

    str_code = mm_get_string_unquoted_from_match_info (match_info, 1);
    if (str_code == NULL) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Could not find expected string code in response '%s'", response);
        goto out;
    }

    if (!mm_get_uint_from_hex_str (str_code, &hex_code)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Could not recognize expected hex code in response '%s'", response);
        goto out;
    }

    switch (hex_code) {
        case 0x6300:
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "SIM verification failed");
            goto out;
        case 0x6983:
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "SIM authentication method blocked");
            goto out;
        case 0x6984:
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "SIM reference data invalidated");
            goto out;
        case 0x6A86:
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "Incorrect parameters in SIM request");
            goto out;
        case 0x6A88:
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "SIM reference data not found");
            goto out;
        default:
            break;
    }

    if (hex_code < MM_MIN_SIM_RETRY_HEX || hex_code > MM_MAX_SIM_RETRY_HEX) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Unknown error returned '0x%04x'", hex_code);
        goto out;
    }

    retries = (gint)(hex_code - MM_MIN_SIM_RETRY_HEX);

out:
    g_regex_unref (r);
    g_match_info_free (match_info);
    g_free (str_code);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return -1;
    }

    g_assert (retries >= 0);
    return retries;
}

/*****************************************************************************/

gboolean
mm_parse_supl_address (const gchar  *supl,
                       gchar       **out_fqdn,
                       guint32      *out_ip,
                       guint16      *out_port,
                       GError      **error)
{
    gboolean   valid = FALSE;
    gchar    **split;
    guint      port;
    guint32    ip;

    split = g_strsplit (supl, ":", -1);
    if (g_strv_length (split) != 2) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid SUPL address format: expected FQDN:PORT or IP:PORT");
        goto out;
    }

    if (!mm_get_uint_from_str (split[1], &port)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid SUPL port number specified: not a number: %s", split[1]);
        goto out;
    }

    if (port == 0 || port > G_MAXUINT16) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid SUPL port number specified: out of range: %u", port);
        goto out;
    }

    /* Port is valid */
    if (out_port)
        *out_port = (guint16) port;

    /* Try to parse first item as IP */
    if (inet_pton (AF_INET, split[0], &ip) <= 0) {
        /* Otherwise, assume it's a domain name */
        if (out_fqdn)
            *out_fqdn = g_strdup (split[0]);
        if (out_ip)
            *out_ip = 0;
    } else {
        if (out_ip)
            *out_ip = ip;
        if (out_fqdn)
            *out_fqdn = NULL;
    }

    valid = TRUE;

out:
    g_strfreev (split);
    return valid;
}
