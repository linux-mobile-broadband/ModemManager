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
 * Copyright (C) 2011 Ammonit Gesellschaft für Messtechnik mbH
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mm-errors.h"
#include "mm-modem-helpers.h"
#include "mm-modem-wavecom-gsm.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemWavecomGsm,
                        mm_modem_wavecom_gsm,
                        MM_TYPE_GENERIC_GSM,
                        0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

/* Bit flags for mobile station classes supported by the modem */
typedef enum {
    WAVECOM_MS_CLASS_UNKNOWN = 0,
    /* Class C in circuit switched only mode, CS */
    WAVECOM_MS_CLASS_CC = 1 << 0,
    /* Class C in GPRS only mode, PS */
    WAVECOM_MS_CLASS_CG = 1 << 1,
    /* Class B (either CS or PS, not both at the same time)
     * This should be the default for GSM/GPRS modems */
    WAVECOM_MS_CLASS_B  = 1 << 2,
    /* Class A in 3G only mode */
    WAVECOM_MS_CLASS_A  = 1 << 3
} WavecomMSClass;

#define WAVECOM_MS_CLASS_CC_IDSTR "\"CC\""
#define WAVECOM_MS_CLASS_CG_IDSTR "\"CG\""
#define WAVECOM_MS_CLASS_B_IDSTR  "\"B\""
#define WAVECOM_MS_CLASS_A_IDSTR  "\"A\""

#define MM_MODEM_WAVECOM_GSM_GET_PRIVATE(o)                             \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_WAVECOM_GSM, MMModemWavecomGsmPrivate))

typedef struct {
    /* Bitmask for supported MS classes */
    guint8 supported_ms_classes;
    /* Current MS class */
    WavecomMSClass current_ms_class;
} MMModemWavecomGsmPrivate;

MMModem *
mm_modem_wavecom_gsm_new (const char *device,
                          const char *driver,
                          const char *plugin,
                          guint32 vendor,
                          guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_WAVECOM_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

static const gchar *
wavecom_ms_class_to_str (WavecomMSClass class)
{
    switch (class) {
    case WAVECOM_MS_CLASS_CC:
        return WAVECOM_MS_CLASS_CC_IDSTR;
    case WAVECOM_MS_CLASS_CG:
        return WAVECOM_MS_CLASS_CG_IDSTR;
    case WAVECOM_MS_CLASS_B:
        return WAVECOM_MS_CLASS_B_IDSTR;
    case WAVECOM_MS_CLASS_A:
        return WAVECOM_MS_CLASS_A_IDSTR;
    default:
        g_warn_if_reached ();
        return NULL;
    }
}

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    MMGenericGsm *gsm = MM_GENERIC_GSM (modem);
    MMPortType ptype = MM_PORT_TYPE_IGNORED;
    MMPort *port = NULL;

    if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
        if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY))
            ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_AT_SERIAL_PORT (port)) {
        gpointer parser;
        GRegex *regex;

        parser = mm_serial_parser_v1_new ();

        /* AT+CPIN? replies will never have an OK appended */
        regex = g_regex_new ("\\r\\n\\+CPIN: .*\\r\\n",
                             G_REGEX_RAW | G_REGEX_OPTIMIZE,
                             0, NULL);
        mm_serial_parser_v1_set_custom_regex (parser, regex, NULL);
        g_regex_unref (regex);

        mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (port),
                                               mm_serial_parser_v1_parse,
                                               parser,
                                               mm_serial_parser_v1_destroy);
    }

    return !!port;
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    /* Do nothing... see set_property() in parent, which also does nothing */
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    switch (prop_id) {
    case MM_GENERIC_GSM_PROP_POWER_UP_CMD:
        /* Wavecom doesn't like CFUN=1, it will reset the whole software stack,
         * including the USB connection and therefore connection would get
         * closed */
        g_value_set_string (value, "");
        break;
    case MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD:
        /* Wavecom doesn't have XOFF/XON flow control, so we enable RTS/CTS */
        g_value_set_string (value, "+IFC=2,2");
        break;
    default:
        break;
    }
}

static void
get_access_technology_cb (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    const gchar *p;

    if (error)
        info->error = g_error_copy (error);
    else {
        p = mm_strip_tag (response->str, "+WGPRSIND:");
        if (!p) {
            g_set_error (&info->error,
                         MM_MODEM_ERROR,
                         MM_MODEM_ERROR_GENERAL,
                         "Couldn't get network capabilities");
        } else {
            switch (*p) {
            case '1':
                /* GPRS only */
                act = MM_MODEM_GSM_ACCESS_TECH_GPRS;
                break;
            case '2':
                /* EGPRS/EDGE supported */
                act = MM_MODEM_GSM_ACCESS_TECH_EDGE;
                break;
            case '3':
                /* 3G R99 supported */
                act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
                break;
            case '4':
                /* HSDPA supported */
                act = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
                break;
            case '5':
                /* HSUPA supported */
                act = MM_MODEM_GSM_ACCESS_TECH_HSUPA;
                break;
            default:
                break;
            }
        }
    }

    mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
    mm_callback_info_schedule (info);
}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+WGPRS=9,2", 3, get_access_technology_cb, info);
}

static void
enable_complete (MMGenericGsm *gsm,
                 GError *error,
                 MMCallbackInfo *info)
{
    /* Do NOT chain up parent do_enable_power_up_done(), as it actually ignores
     * all errors. */

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}

static void
set_highest_ms_class_cb (MMAtSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemWavecomGsmPrivate *priv = MM_MODEM_WAVECOM_GSM_GET_PRIVATE (info->modem);
    guint new_class;

    if (error) {
        enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    new_class = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "new-class"));
    if (new_class)
        priv->current_ms_class = new_class;

    /* All done! */
    mm_dbg ("[4/4] All done...");
    enable_complete (MM_GENERIC_GSM (info->modem), NULL, info);
}

static void
set_highest_ms_class (MMAtSerialPort *port,
                      MMCallbackInfo *info)
{
    MMModemWavecomGsmPrivate *priv = MM_MODEM_WAVECOM_GSM_GET_PRIVATE (info->modem);
    guint new_class = 0;

    if (priv->supported_ms_classes & WAVECOM_MS_CLASS_A) {
        if (priv->current_ms_class != WAVECOM_MS_CLASS_A) {
            /* A is supported but is not currently selected, switch to A */
            new_class = WAVECOM_MS_CLASS_A;
        }
    } else if (priv->supported_ms_classes & WAVECOM_MS_CLASS_B) {
        if (priv->current_ms_class != WAVECOM_MS_CLASS_B) {
            /* B is supported but is not currently selected, switch to B */
            new_class = WAVECOM_MS_CLASS_B;
        }
    } else if (priv->supported_ms_classes & WAVECOM_MS_CLASS_CG) {
        if (priv->current_ms_class != WAVECOM_MS_CLASS_CG) {
            /* CG is supported but is not currently selected, switch to CG */
            new_class = WAVECOM_MS_CLASS_CG;
        }
    }

    if (new_class) {
        const gchar *new_class_str;
        gchar *cmd;

        new_class_str = wavecom_ms_class_to_str (new_class);
        mm_dbg ("  Changing mobile station class to: %s", new_class_str);
        mm_callback_info_set_data (info,
                                   "new-class",
                                   GUINT_TO_POINTER (new_class),
                                   NULL);
        cmd = g_strdup_printf ("+CGCLASS=%s", new_class_str);
        mm_at_serial_port_queue_command (port, cmd, 3, set_highest_ms_class_cb, info);
        g_free (cmd);
        return;
    }

    /* if no need to change station class, then just go on */
    mm_dbg ("  No need to change mobile station class");
    set_highest_ms_class_cb (port, NULL, NULL, info);
}

static void
get_current_ms_class_cb (MMAtSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemWavecomGsmPrivate *priv = MM_MODEM_WAVECOM_GSM_GET_PRIVATE (info->modem);
    const gchar *p;

    if (error) {
        enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    p = mm_strip_tag (response->str, "+CGCLASS:");

    if (strncmp (p,
                 WAVECOM_MS_CLASS_A_IDSTR,
                 strlen (WAVECOM_MS_CLASS_A_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class A mobile station");
        priv->current_ms_class = WAVECOM_MS_CLASS_A;
    } else if (strncmp (p,
                        WAVECOM_MS_CLASS_B_IDSTR,
                        strlen (WAVECOM_MS_CLASS_B_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class B mobile station");
        priv->current_ms_class = WAVECOM_MS_CLASS_B;
    } else if (strncmp (p,
                        WAVECOM_MS_CLASS_CG_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CG_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class CG mobile station");
        priv->current_ms_class = WAVECOM_MS_CLASS_CG;
    } else if (strncmp (p,
                        WAVECOM_MS_CLASS_CC_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CC_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class CC mobile station");
        priv->current_ms_class = WAVECOM_MS_CLASS_CC;
    } else {
        GError *inner_error;

        inner_error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Unknown mobile station class: '%s'",
                                   p);
        enable_complete (MM_GENERIC_GSM (info->modem), inner_error, info);
        g_error_free (inner_error);
        return;
    }

    /* 3rd, set highest mobile station class possible */
    mm_dbg ("[3/4] Ensuring highest MS class...");
    set_highest_ms_class (port, info);
}

static void
get_supported_ms_classes_cb (MMAtSerialPort *port,
                             GString *response,
                             GError *error,
                             gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemWavecomGsmPrivate *priv = MM_MODEM_WAVECOM_GSM_GET_PRIVATE (info->modem);
    const gchar *p;

    if (error) {
        enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    /* Reset currently supported MS classes */
    priv->supported_ms_classes = 0;

    p = mm_strip_tag (response->str, "+CGCLASS:");

    if (strstr (p, WAVECOM_MS_CLASS_A_IDSTR)) {
        mm_dbg ("Modem supports Class A mobile station");
        priv->supported_ms_classes |= WAVECOM_MS_CLASS_A;
    }

    if (strstr (p, WAVECOM_MS_CLASS_B_IDSTR)) {
        mm_dbg ("Modem supports Class B mobile station");
        priv->supported_ms_classes |= WAVECOM_MS_CLASS_B;
    }

    if (strstr (p, WAVECOM_MS_CLASS_CG_IDSTR)) {
        mm_dbg ("Modem supports Class CG mobile station");
        priv->supported_ms_classes |= WAVECOM_MS_CLASS_CG;
    }

    if (strstr (p, WAVECOM_MS_CLASS_CC_IDSTR)) {
        mm_dbg ("Modem supports Class CC mobile station");
        priv->supported_ms_classes |= WAVECOM_MS_CLASS_CC;
    }

    /* If none received, error */
    if (!priv->supported_ms_classes) {
        GError *inner_error;

        inner_error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Couldn't get supported mobile station classes");
        enable_complete (MM_GENERIC_GSM (info->modem), inner_error, info);
        g_error_free (inner_error);
        return;
    }

    /* 2nd, query for current MS class */
    mm_dbg ("[2/4] Getting current MS class...");
    mm_at_serial_port_queue_command (port, "+CGCLASS?",  3, get_current_ms_class_cb, info);
}

static void
do_enable_power_up_done (MMGenericGsm *gsm,
                         GString *response,
                         GError *error,
                         MMCallbackInfo *info)
{
    MMAtSerialPort *port;
    GError *inner_error = NULL;

    if (error) {
        enable_complete (gsm, error, info);
        return;
    }

    /* Get port */
    port = mm_generic_gsm_get_best_at_port (gsm, &inner_error);
    if (!port) {
        enable_complete (gsm, inner_error, info);
        g_error_free (inner_error);
        return;
    }

    /* 1st, query for supported MS classes */
    mm_dbg ("[1/4] Getting supported MS classes...");
    mm_at_serial_port_queue_command (port, "+CGCLASS=?", 3, get_supported_ms_classes_cb, info);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
mm_modem_wavecom_gsm_init (MMModemWavecomGsm *self)
{
    MMModemWavecomGsmPrivate *priv = MM_MODEM_WAVECOM_GSM_GET_PRIVATE (self);

    /* Set defaults */
    priv->supported_ms_classes = 0; /* This is a bitmask, so empty */
    priv->current_ms_class = WAVECOM_MS_CLASS_UNKNOWN;
}

static void
mm_modem_wavecom_gsm_class_init (MMModemWavecomGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemWavecomGsmPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_POWER_UP_CMD,
                                      MM_GENERIC_GSM_POWER_UP_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD,
                                      MM_GENERIC_GSM_FLOW_CONTROL_CMD);

    gsm_class->do_enable_power_up_done = do_enable_power_up_done;
    gsm_class->get_access_technology = get_access_technology;
}

