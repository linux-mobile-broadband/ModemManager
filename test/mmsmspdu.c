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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>
#include "mm-log-test.h"
#include "mm-sms-part-3gpp.h"

#define PROGRAM_NAME    "mmsmspdu"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Context */
static gchar    *pdu;
static gboolean  verbose_flag;
static gboolean  version_flag;

static GOptionEntry main_entries[] = {
    { "pdu", 'p', 0, G_OPTION_ARG_STRING, &pdu,
      "PDU contents",
      "[0123456789ABCDEF..]"
    },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose_flag,
      "Run action with verbose logs",
      NULL
    },
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version_flag,
      "Print version",
      NULL
    },
    { NULL }
};

static void
show_part_info (MMSmsPart *part)
{
    MMSmsPduType   pdu_type;
    const gchar   *smsc;
    const gchar   *number;
    const gchar   *timestamp;
    const gchar   *text;
    MMSmsEncoding  encoding;
    gint           class;
    guint          validity_relative;
    gboolean       delivery_report_request;
    guint          concat_reference;
    guint          concat_max;
    guint          concat_sequence;
    const GByteArray *data;

    pdu_type = mm_sms_part_get_pdu_type (part);
    g_print ("pdu type: %s\n", mm_sms_pdu_type_get_string (pdu_type));

    smsc = mm_sms_part_get_smsc (part);
    g_print ("smsc: %s\n", smsc ? smsc : "n/a");

    number = mm_sms_part_get_number (part);
    g_print ("number: %s\n", number ? number : "n/a");

    timestamp = mm_sms_part_get_timestamp (part);
    g_print ("timestamp: %s\n", timestamp ? timestamp : "n/a");

    encoding = mm_sms_part_get_encoding (part);
    switch (encoding) {
    case MM_SMS_ENCODING_GSM7:
        g_print ("encoding: GSM7\n");
        break;
    case MM_SMS_ENCODING_UCS2:
        g_print ("encoding: UCS2\n");
        break;
    case MM_SMS_ENCODING_8BIT:
        g_print ("encoding: 8BIT\n");
        break;
    case MM_SMS_ENCODING_UNKNOWN:
    default:
        g_print ("encoding: unknown (0x%x)\n", encoding);
        break;
    }

    text = mm_sms_part_get_text (part);
    g_print ("text: %s\n", text ? text : "n/a");

    data = mm_sms_part_get_data (part);
    if (data) {
      gchar *data_str;

      data_str = mm_utils_bin2hexstr (data->data, data->len);
      g_print ("data: %s\n", data_str);
      g_free (data_str);
    } else
      g_print ("data: n/a\n");

    class = mm_sms_part_get_class (part);
    if (class != -1)
        g_print ("class: %d\n", class);
    else
        g_print ("class: n/a\n");

    validity_relative = mm_sms_part_get_validity_relative (part);
    if (validity_relative != 0)
        g_print ("validity relative: %d\n", validity_relative);
    else
        g_print ("validity relative: n/a\n");

    delivery_report_request = mm_sms_part_get_delivery_report_request (part);
    g_print ("delivery report request: %s\n", delivery_report_request ? "yes" : "no");

    concat_reference = mm_sms_part_get_concat_reference (part);
    g_print ("concat reference: %d\n", concat_reference);

    concat_max = mm_sms_part_get_concat_max (part);
    g_print ("concat max: %d\n", concat_max);

    concat_sequence = mm_sms_part_get_concat_sequence (part);
    g_print ("concat sequence: %d\n", concat_sequence);

    if (mm_sms_part_get_pdu_type (part) == MM_SMS_PDU_TYPE_STATUS_REPORT) {
       const gchar *discharge_timestamp;
       guint        message_reference;
       guint        delivery_state;

       message_reference = mm_sms_part_get_message_reference (part);
       g_print ("message reference: %d\n", message_reference);

       discharge_timestamp = mm_sms_part_get_discharge_timestamp (part);
       g_print ("discharge timestamp: %s\n", discharge_timestamp ? discharge_timestamp : "n/a");

       delivery_state = mm_sms_part_get_delivery_state (part);
       g_print ("delivery state: %s\n", mm_sms_delivery_state_get_string_extended (delivery_state));
    }

    if (MM_SMS_PART_IS_CDMA (part)) {
        MMSmsCdmaTeleserviceId   teleservice_id;
        MMSmsCdmaServiceCategory service_category;

        teleservice_id = mm_sms_part_get_cdma_teleservice_id (part);
        g_print ("teleservice id: %s\n", mm_sms_cdma_teleservice_id_get_string (teleservice_id));

        service_category = mm_sms_part_get_cdma_service_category (part);
        g_print ("service category: %s\n", mm_sms_cdma_service_category_get_string (service_category));
    }
}

static void
print_version_and_exit (void)
{
    g_print ("\n"
             PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "Copyright (2019) Aleksander Morgado\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
}

int main (int argc, char **argv)
{
    GOptionContext *context;
    GError         *error = NULL;
    MMSmsPart      *part;

    setlocale (LC_ALL, "");

    /* Setup option context, process it and destroy it */
    context = g_option_context_new ("- ModemManager SMS PDU parser");
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (version_flag)
        print_version_and_exit ();

    /* No pdu given? */
    if (!pdu) {
        g_printerr ("error: no PDU specified\n");
        exit (EXIT_FAILURE);
    }

    part = mm_sms_part_3gpp_new_from_pdu (0, pdu, NULL, &error);
    if (!part) {
        g_printerr ("error: couldn't parse PDU: %s\n", error->message);
        exit (EXIT_FAILURE);
    }

    show_part_info (part);

    mm_sms_part_free (part);
    g_free (pdu);

    return EXIT_SUCCESS;
}
