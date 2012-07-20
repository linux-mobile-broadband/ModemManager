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
 *
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-samsung.h"
#include "mm-broadband-bearer-samsung.h"
#include "mm-iface-icera.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-time.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

static void iface_icera_init (MMIfaceIcera *iface);
static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_time_init (MMIfaceModemTime *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSamsung, mm_broadband_modem_samsung, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_ICERA, iface_icera_init));

/*****************************************************************************/
/* Create bearer (Modem interface) */

static MMBearer *
create_bearer_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_BEARER (g_object_ref (
                          g_simple_async_result_get_op_res_gpointer (
                              G_SIMPLE_ASYNC_RESULT (res))));
}

static void
broadband_bearer_samsung_new_ready (GObject *source,
                                    GAsyncResult *res,
                                    GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_samsung_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
create_bearer (MMIfaceModem *self,
               MMBearerProperties *properties,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        create_bearer);

    mm_broadband_bearer_samsung_new (MM_BROADBAND_MODEM (self),
                                     properties,
                                     NULL, /* cancellable */
                                     (GAsyncReadyCallback)broadband_bearer_samsung_new_ready,
                                     result);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

typedef struct {
    MMModemBand band;
    const char *name;
} BandTable;

static BandTable modem_bands[] = {
    /* Sort 3G first since it's preferred */
    { MM_MODEM_BAND_U2100, "FDD_BAND_I" },
    { MM_MODEM_BAND_U1900, "FDD_BAND_II" },
    /*
     * Several bands are forbidden to set and will always read as
     * disabled, so we won't present them to the rest of
     * modemmanager.
     */
    /* { MM_MODEM_BAND_U1800, "FDD_BAND_III" }, */
    /* { MM_MODEM_BAND_U17IV, "FDD_BAND_IV" },  */
    /* { MM_MODEM_BAND_U800,  "FDD_BAND_VI" },  */
    { MM_MODEM_BAND_U850,  "FDD_BAND_V" },
    { MM_MODEM_BAND_U900,  "FDD_BAND_VIII" },
    { MM_MODEM_BAND_G850,  "G850" },
    /* 2G second */
    { MM_MODEM_BAND_DCS,   "DCS" },
    { MM_MODEM_BAND_EGSM,  "EGSM" },
    { MM_MODEM_BAND_PCS,   "PCS" },
    /* And ANY last since it's most inclusive */
    { MM_MODEM_BAND_ANY,   "ANY" },
};


static GArray *
load_supported_bands_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    /* Never fails */
    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_supported_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;
    GArray *bands;
    guint i;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_bands);

    /*
     * The modem doesn't support telling us what bands are supported;
     * list everything we know about.
     */
    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), G_N_ELEMENTS (modem_bands));
    for (i = 0 ; i < G_N_ELEMENTS (modem_bands) ; i++) {
        if (modem_bands[i].band != MM_MODEM_BAND_ANY)
            g_array_append_val(bands, modem_bands[i].band);
    }

    g_simple_async_result_set_op_res_gpointer (result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_current_bands_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GSimpleAsyncResult *operation_result)
{
    GRegex *r;
    GMatchInfo *info;
    GArray *bands;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query current bands: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /*
     * Response is a number of lines of the form:
     *   "EGSM": 0
     *   "FDD_BAND_I": 1
     *   ...
     * with 1 and 0 indicating whether the particular band is enabled or not.
     */
    r = g_regex_new ("^\"(\\w+)\": (\\d)",
                     G_REGEX_MULTILINE, G_REGEX_MATCH_NEWLINE_ANY,
                     NULL);
    g_assert (r != NULL);

    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand),
                               G_N_ELEMENTS (modem_bands));

    g_regex_match (r, response, 0, &info);
    while (g_match_info_matches (info)) {
        gchar *band, *enabled;

        band = g_match_info_fetch (info, 1);
        enabled = g_match_info_fetch (info, 2);
        if (enabled[0] == '1') {
            guint i;
            for (i = 0 ; i < G_N_ELEMENTS (modem_bands); i++) {
                if (!strcmp (band, modem_bands[i].name)) {
                    g_array_append_val (bands, modem_bands[i].band);
                    break;
                }
            }
        }
        g_free (band);
        g_free (enabled);
        g_match_info_next (info, NULL);
    }
    g_match_info_free (info);
    g_regex_unref (r);

    g_simple_async_result_set_op_res_gpointer (operation_result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
load_current_bands (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_current_bands);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%IPBM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_ready,
        result);
}

/*****************************************************************************/
/* Set bands (Modem interface) */

typedef struct {
    GSimpleAsyncResult *result;
    guint bandbits;
    guint enablebits;
    guint disablebits;
} SetBandsContext;

/*
 * The modem's band-setting command (%IPBM=) enables or disables one
 * band at a time, and one band must always be enabled. Here, we first
 * get the set of enabled bands, compute the difference between that
 * set and the requested set, enable any added bands, and finally
 * disable any removed bands.
 */
static gboolean
set_bands_finish (MMIfaceModem *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void set_one_band (MMIfaceModem *self, SetBandsContext *ctx);

static void
set_bands_context_complete_and_free (SetBandsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_free (ctx);
}

static void
set_bands_next (MMIfaceModem *self,
                GAsyncResult *res,
                SetBandsContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        mm_dbg ("Couldn't set bands: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    set_one_band (self, ctx);
}

static void
set_one_band (MMIfaceModem *self,
              SetBandsContext *ctx)
{
    guint enable, band;
    gchar *command;

    /* Find the next band to enable or disable, always doing enables first */
    enable = 1;
    band = ffs (ctx->enablebits);
    if (band == 0) {
        enable = 0;
        band = ffs (ctx->disablebits);
    }
    if (band == 0) {
        /* Both enabling and disabling are done */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    /* Note that ffs() returning 2 corresponds to 1 << 1, not 1 << 2 */
    band--;
    mm_dbg("1. enablebits %x disablebits %x band %d enable %d",
           ctx->enablebits, ctx->disablebits, band, enable);

    if (enable)
        ctx->enablebits &= ~(1 << band);
    else
        ctx->disablebits &= ~(1 << band);
    mm_dbg("2. enablebits %x disablebits %x",
           ctx->enablebits, ctx->disablebits);

    command = g_strdup_printf ("%%IPBM=\"%s\",%d",
                               modem_bands[band].name,
                               enable);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback)set_bands_next,
        ctx);
    g_free (command);
}

static guint
band_array_to_bandbits (GArray *bands)
{
    MMModemBand band;
    guint i, j, bandbits;

    bandbits = 0;
    for (i = 0 ; i < bands->len ; i++) {
        band = g_array_index (bands, MMModemBand, i);
        for (j = 0 ; j < G_N_ELEMENTS (modem_bands) ; j++) {
            if (modem_bands[j].band == band) {
                bandbits |= 1 << j;
                break;
            }
        }
        g_assert (j <  G_N_ELEMENTS (modem_bands));
    }

    return bandbits;
}

static void
set_bands_got_current_bands (MMIfaceModem *self,
                             GAsyncResult *res,
                             SetBandsContext *ctx)
{
    GArray *bands;
    GError *error = NULL;
    guint currentbits;

    bands = load_current_bands_finish (self, res, &error);
    if (!bands) {
        g_simple_async_result_take_error (ctx->result, error);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    currentbits = band_array_to_bandbits (bands);
    ctx->enablebits = ctx->bandbits & ~currentbits;
    ctx->disablebits = currentbits & ~ctx->bandbits;

    set_one_band (self, ctx);
}

static void
set_bands (MMIfaceModem *self,
           GArray *bands_array,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    SetBandsContext *ctx;

    ctx = g_new0 (SetBandsContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             set_bands);
    ctx->bandbits = band_array_to_bandbits (bands_array);
    /*
     * For the sake of efficiency, convert "ANY" to the actual set of
     * bands; this matches what we get from load_current_bands and
     * minimizes the number of changes we need to make.
     *
     * This requires that ANY is last in modem_bands and that all the
     * other bits are valid.
     */
    if (ctx->bandbits == (1 << (G_N_ELEMENTS (modem_bands) - 1)))
        ctx->bandbits--; /* clear the top bit, set all lower bits */
    load_current_bands (self,
                        (GAsyncReadyCallback)set_bands_got_current_bands,
                        ctx);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    return (MMUnlockRetries *) g_object_ref (g_simple_async_result_get_op_res_gpointer (
                                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_unlock_retries_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    int pin1, puk1, pin2, puk2;


    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query unlock retries: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    response = mm_strip_tag (response, "%PINNUM:");
    if (sscanf (response, " %d, %d, %d, %d", &pin1, &puk1, &pin2, &puk2) == 4) {
        MMUnlockRetries *retries;
        retries = mm_unlock_retries_new ();
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, pin1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, puk1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, pin2);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, puk2);
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   retries,
                                                   (GDestroyNotify)g_object_unref);
    } else {
        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Invalid unlock retries response: '%s'",
                                         response);
    }
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%PINNUM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_unlock_retries_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_unlock_retries));
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    /* Use AT+CFUN=4 for power down. It will stop the RF (IMSI detach), and
     * keeps access to the SIM */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else {
        /* Our own setup now */
        mm_iface_icera_modem_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self), TRUE);
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_setup_unsolicited_events_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_3gpp_setup_unsolicited_events));
}

static void
parent_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_cleanup_unsolicited_events);

    /* Our own cleanup first */
    mm_iface_icera_modem_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
own_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                     GAsyncResult *res,
                                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_iface_icera_modem_3gpp_enable_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Our own enable now */
    mm_iface_icera_modem_3gpp_enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_3gpp_enable_unsolicited_events));
}

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
own_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_iface_icera_modem_3gpp_disable_unsolicited_events_finish (self, res, &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Next, chain up parent's disable */
    iface_modem_3gpp_parent->disable_unsolicited_events (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    mm_iface_icera_modem_3gpp_disable_unsolicited_events (
        self,
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_3gpp_disable_unsolicited_events));
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    MMAtSerialPort *ports[2];
    guint i;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_samsung_parent_class)->setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Configure AT ports */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        g_object_set (ports[i],
                      MM_PORT_CARRIER_DETECT,    FALSE,
                      MM_SERIAL_PORT_SEND_DELAY, (guint64) 0,
                      NULL);
    }

    /* Now reset the unsolicited messages we'll handle when enabled */
    mm_iface_icera_modem_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self), FALSE);
}

/*****************************************************************************/

MMBroadbandModemSamsung *
mm_broadband_modem_samsung_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SAMSUNG,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_samsung_init (MMBroadbandModemSamsung *self)
{
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_bands = set_bands;
    iface->set_bands_finish = set_bands_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->create_bearer = create_bearer;
    iface->create_bearer_finish = create_bearer_finish;

    /* Use default Icera implementation */
    iface->load_allowed_modes = mm_iface_icera_modem_load_allowed_modes;
    iface->load_allowed_modes_finish = mm_iface_icera_modem_load_allowed_modes_finish;
    iface->set_allowed_modes = mm_iface_icera_modem_set_allowed_modes;
    iface->set_allowed_modes_finish = mm_iface_icera_modem_set_allowed_modes_finish;
    iface->load_access_technologies = mm_iface_icera_modem_load_access_technologies;
    iface->load_access_technologies_finish = mm_iface_icera_modem_load_access_technologies_finish;
}

static void
iface_modem_time_init (MMIfaceModemTime *iface)
{
    /* Use default Icera implementation */
    iface->load_network_time = mm_iface_icera_modem_time_load_network_time;
    iface->load_network_time_finish = mm_iface_icera_modem_time_load_network_time_finish;
    iface->load_network_timezone = mm_iface_icera_modem_time_load_network_timezone;
    iface->load_network_timezone_finish = mm_iface_icera_modem_time_load_network_timezone_finish;
}

static void
iface_icera_init (MMIfaceIcera *iface)
{
}

static void
mm_broadband_modem_samsung_class_init (MMBroadbandModemSamsungClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
