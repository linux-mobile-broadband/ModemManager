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
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem-qmi.h"
#include "mm-log-object.h"
#include "mm-sim-qmi.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-shared-qmi.h"

G_DEFINE_TYPE (MMSimQmi, mm_sim_qmi, MM_TYPE_BASE_SIM)

enum {
    PROP_0,
    PROP_DMS_UIM_DEPRECATED,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMSimQmiPrivate {
    gboolean  dms_uim_deprecated;
    gchar    *imsi;
};

static const guint16 mf_file_path[]  = { 0x3F00 };
static const guint16 adf_file_path[] = { 0x3F00, 0x7FFF };

/*****************************************************************************/

static gboolean
ensure_qmi_client (GTask       *task,
                   MMSimQmi    *self,
                   QmiService   service,
                   QmiClient  **o_client)
{
    MMBaseModem *modem = NULL;
    QmiClient *client;
    g_autoptr(GError) error = NULL;

    g_object_get (self,
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    g_assert (MM_IS_SHARED_QMI (modem));
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (modem),
                                        service,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        &error);

    g_object_unref (modem);

    if (!client) {
        if (task) {
            g_task_return_error (task, g_steal_pointer (&error));
            g_object_unref (task);
        }
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

/*****************************************************************************/
/* Wait for SIM ready */

#define SIM_READY_CHECKS_MAX 5
#define SIM_READY_CHECKS_TIMEOUT_SECS 1

typedef struct {
    QmiClient *client_uim;
    guint      ready_checks_n;
} WaitSimReadyContext;

static void
wait_sim_ready_context_free (WaitSimReadyContext *ctx)
{
    g_clear_object (&ctx->client_uim);
    g_slice_free (WaitSimReadyContext, ctx);
}

static gboolean
wait_sim_ready_finish (MMBaseSim     *self,
                       GAsyncResult  *res,
                       GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void sim_ready_check (GTask *task);

static gboolean
sim_ready_retry_cb (GTask *task)
{
    sim_ready_check (task);
    return G_SOURCE_REMOVE;
}

static void
sim_ready_retry (GTask *task)
{
    g_timeout_add_seconds (SIM_READY_CHECKS_TIMEOUT_SECS, (GSourceFunc) sim_ready_retry_cb, task);
}

static void
uim_get_card_status_ready (QmiClientUim *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    g_autoptr(QmiMessageUimGetCardStatusOutput)  output = NULL;
    g_autoptr(GError)                            error = NULL;
    MMSimQmi                                    *self;

    self = g_task_get_source_object (task);

    output = qmi_client_uim_get_card_status_finish (client, res, &error);
    if (!output ||
        !qmi_message_uim_get_card_status_output_get_result (output, &error) ||
        (!mm_qmi_uim_get_card_status_output_parse (self, output, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &error) &&
         (g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
          g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY)))) {
        mm_obj_dbg (self, "sim not yet considered ready... retrying");
        sim_ready_retry (task);
        return;
    }

    /* SIM is considered ready now */
    mm_obj_dbg (self, "sim is ready");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
sim_ready_check (GTask *task)
{
    WaitSimReadyContext *ctx;
    MMSimQmi            *self;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    ctx->ready_checks_n++;
    if (ctx->ready_checks_n == SIM_READY_CHECKS_MAX) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "failed waiting for SIM readiness");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "checking SIM readiness");
    qmi_client_uim_get_card_status (QMI_CLIENT_UIM (ctx->client_uim),
                                    NULL,
                                    5,
                                    NULL,
                                    (GAsyncReadyCallback) uim_get_card_status_ready,
                                    task);
}

static void
wait_sim_ready (MMBaseSim           *_self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    QmiClient           *client;
    MMSimQmi            *self;
    GTask               *task;
    WaitSimReadyContext *ctx;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "waiting for SIM to be ready...");
    if (!self->priv->dms_uim_deprecated) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if (!ensure_qmi_client (task, self, QMI_SERVICE_UIM, &client))
        return;

    ctx = g_slice_new0 (WaitSimReadyContext);
    ctx->client_uim = g_object_ref (client);
    g_task_set_task_data (task, ctx, (GDestroyNotify) wait_sim_ready_context_free);

    sim_ready_check (task);
}

/*****************************************************************************/
/* Load SIM ID (ICCID) */

static GArray *
uim_read_finish (QmiClientUim  *client,
                 GAsyncResult  *res,
                 GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
uim_read_ready (QmiClientUim *client,
                GAsyncResult *res,
                GTask        *task)
{
    QmiMessageUimReadTransparentOutput *output;
    GError *error = NULL;

    output = qmi_client_uim_read_transparent_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_uim_read_transparent_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't read data from UIM: ");
        g_task_return_error (task, error);
    } else {
        GArray *read_result = NULL;

        qmi_message_uim_read_transparent_output_get_read_result (output, &read_result, NULL);
        if (read_result)
            g_task_return_pointer (task,
                                   g_array_ref (read_result),
                                   (GDestroyNotify) g_array_unref);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Read malformed data from UIM");
    }

    if (output)
        qmi_message_uim_read_transparent_output_unref (output);

    g_object_unref (task);
}

static void
uim_read (MMSimQmi            *self,
          guint16              file_id,
          const guint16       *file_path,
          gsize                file_path_len,
          GAsyncReadyCallback  callback,
          gpointer             user_data)
{
    GTask *task;
    QmiClient *client = NULL;
    GArray *file_path_bytes;
    gsize i;
    QmiMessageUimReadTransparentInput *input;
    GArray *aid;

    task = g_task_new (self, NULL, callback, user_data);

    if (!ensure_qmi_client (task,
                            self,
                            QMI_SERVICE_UIM, &client))
        return;

    file_path_bytes = g_array_sized_new (FALSE, FALSE, 1, file_path_len * 2);
    for (i = 0; i < file_path_len; ++i) {
        guint8 byte;

        byte = file_path[i] & 0xFF;
        g_array_append_val (file_path_bytes, byte);
        byte = (file_path[i] >> 8) & 0xFF;
        g_array_append_val (file_path_bytes, byte);
    }

    input = qmi_message_uim_read_transparent_input_new ();
    aid = g_array_new (FALSE, FALSE, sizeof (guint8)); /* empty AID */
    qmi_message_uim_read_transparent_input_set_session (
        input,
        QMI_UIM_SESSION_TYPE_PRIMARY_GW_PROVISIONING,
        aid,
        NULL);
    g_array_unref (aid);
    qmi_message_uim_read_transparent_input_set_file (input,
                                                     file_id,
                                                     file_path_bytes,
                                                     NULL);
    qmi_message_uim_read_transparent_input_set_read_information (input,
                                                                 0,
                                                                 0,
                                                                 NULL);
    g_array_unref (file_path_bytes);

    qmi_client_uim_read_transparent (QMI_CLIENT_UIM (client),
                                     input,
                                     10,
                                     NULL,
                                     (GAsyncReadyCallback)uim_read_ready,
                                     task);
    qmi_message_uim_read_transparent_input_unref (input);
}

static gchar *
load_sim_identifier_finish (MMBaseSim     *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
uim_get_iccid_ready (QmiClientUim *client,
                     GAsyncResult *res,
                     GTask        *task)
{
    GError            *error = NULL;
    g_autoptr(GArray)  read_result = NULL;
    g_autofree gchar  *raw_iccid = NULL;
    gchar             *iccid;

    read_result = uim_read_finish (client, res, &error);
    if (!read_result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    raw_iccid = mm_utils_bin2hexstr ((const guint8 *) read_result->data, read_result->len);
    g_assert (raw_iccid);
    iccid = mm_3gpp_parse_iccid (raw_iccid, &error);
    if (!iccid)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, iccid, g_free);
    g_object_unref (task);
}

static void
uim_get_iccid (MMSimQmi *self,
               GTask    *task)
{
    uim_read (self,
              0x2FE2,
              mf_file_path,
              G_N_ELEMENTS (mf_file_path),
              (GAsyncReadyCallback)uim_get_iccid_ready,
              task);
}

static void
dms_uim_get_iccid_ready (QmiClientDms *client,
                         GAsyncResult *res,
                         GTask        *task)
{
    QmiMessageDmsUimGetIccidOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_get_iccid_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_uim_get_iccid_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get UIM ICCID: ");
        g_task_return_error (task, error);
    } else {
        const gchar *str = NULL;

        qmi_message_dms_uim_get_iccid_output_get_iccid (output, &str, NULL);
        g_task_return_pointer (task, g_strdup (str), g_free);
    }

    if (output)
        qmi_message_dms_uim_get_iccid_output_unref (output);

    g_object_unref (task);
}

static void
dms_uim_get_iccid (MMSimQmi *self,
                   GTask    *task)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (task,
                            self,
                            QMI_SERVICE_DMS, &client))
        return;

    qmi_client_dms_uim_get_iccid (QMI_CLIENT_DMS (client),
                                  NULL,
                                  5,
                                  NULL,
                                  (GAsyncReadyCallback)dms_uim_get_iccid_ready,
                                  task);
}

static void
load_sim_identifier (MMBaseSim           *_self,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    MMSimQmi *self;
    GTask *task;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "loading SIM identifier...");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_get_iccid (self, task);
    else
        uim_get_iccid (self, task);
}

/*****************************************************************************/
/* Load IMSI */

static gchar *
load_imsi_finish (MMBaseSim     *self,
                  GAsyncResult  *res,
                  GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
uim_get_imsi_ready (QmiClientUim *client,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMSimQmi          *self;
    GError            *error = NULL;
    g_autoptr(GArray)  read_result = NULL;
    g_autofree gchar  *imsi = NULL;

    self = g_task_get_source_object (task);

    read_result = uim_read_finish (client, res, &error);
    if (!read_result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    imsi = mm_bcd_to_string ((const guint8 *) read_result->data, read_result->len,
                             TRUE /* low_nybble_first */);
    g_assert (imsi);
    if (strlen (imsi) < 3)
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "IMSI is malformed");
    else {
        /* EFimsi contains a length byte, follwed by a nibble for parity,
         * and then followed by the actual IMSI in BCD. After converting
         * the BCD into a decimal string, we simply skip the first 3
         * decimal digits to obtain the IMSI. */

        /* Cache IMSI */
        g_free (self->priv->imsi);
        self->priv->imsi = g_strdup (imsi + 3);

        g_task_return_pointer (task, g_strdup (imsi + 3), g_free);
    }
    g_object_unref (task);
}

static void
uim_get_imsi (MMSimQmi *self,
              GTask    *task)
{
    uim_read (self,
              0x6F07,
              adf_file_path,
              G_N_ELEMENTS (adf_file_path),
              (GAsyncReadyCallback)uim_get_imsi_ready,
              task);
}

static void
dms_uim_get_imsi_ready (QmiClientDms *client,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMSimQmi                      *self;
    QmiMessageDmsUimGetImsiOutput *output = NULL;
    GError                        *error = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_dms_uim_get_imsi_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_uim_get_imsi_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get UIM IMSI: ");
        g_task_return_error (task, error);
    } else {
        const gchar *str = NULL;

        qmi_message_dms_uim_get_imsi_output_get_imsi (output, &str, NULL);

        /* Cache IMSI */
        g_free (self->priv->imsi);
        self->priv->imsi = g_strdup (str);

        g_task_return_pointer (task, g_strdup (str), g_free);
    }

    if (output)
        qmi_message_dms_uim_get_imsi_output_unref (output);

    g_object_unref (task);
}

static void
dms_uim_get_imsi (MMSimQmi *self,
                  GTask    *task)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (task,
                            self,
                            QMI_SERVICE_DMS, &client))
        return;

    qmi_client_dms_uim_get_imsi (QMI_CLIENT_DMS (client),
                                 NULL,
                                 5,
                                 NULL,
                                 (GAsyncReadyCallback)dms_uim_get_imsi_ready,
                                 task);
}

static void
load_imsi (MMBaseSim           *_self,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    MMSimQmi *self;
    GTask *task;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "loading IMSI...");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_get_imsi (self, task);
    else
        uim_get_imsi (self, task);
}

/*****************************************************************************/
/* Load GID1 and GID2 */

static GByteArray *
common_load_gid_finish (MMBaseSim     *self,
                        GAsyncResult  *res,
                        GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
uim_get_gid_ready (QmiClientUim *client,
                   GAsyncResult *res,
                   GTask        *task)
{
    GError            *error = NULL;
    g_autoptr(GArray)  read_result = NULL;

    read_result = uim_read_finish (client, res, &error);
    if (!read_result)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task,
                               g_byte_array_append (g_byte_array_sized_new (read_result->len),
                                                    (const guint8 *)(read_result->data),
                                                    read_result->len),
                               (GDestroyNotify)g_byte_array_unref);
    g_object_unref (task);
}

static void
common_load_gid (MMBaseSim           *self,
                 guint16              file_id,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    uim_read (MM_SIM_QMI (self),
              file_id,
              adf_file_path,
              G_N_ELEMENTS (adf_file_path),
              (GAsyncReadyCallback)uim_get_gid_ready,
              task);
}

static GByteArray *
load_gid1_finish (MMBaseSim     *self,
                  GAsyncResult  *res,
                  GError       **error)
{
    return common_load_gid_finish (self, res, error);
}

static void
load_gid1 (MMBaseSim           *self,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    common_load_gid (self, 0x6F3E, callback, user_data);
}

static GByteArray *
load_gid2_finish (MMBaseSim     *self,
                  GAsyncResult  *res,
                  GError       **error)
{
    return common_load_gid_finish (self, res, error);
}

static void
load_gid2 (MMBaseSim           *self,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    common_load_gid (self, 0x6F3F, callback, user_data);
}

/*****************************************************************************/
/* Load operator identifier */

static gchar *
load_operator_identifier_finish (MMBaseSim     *self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
uim_read_efad_ready (QmiClientUim *client,
                     GAsyncResult *res,
                     GTask        *task)
{
    MMSimQmi          *self;
    GError            *error = NULL;
    g_autoptr(GArray)  read_result = NULL;
    guint              mnc_length;

    self = g_task_get_source_object (task);

    read_result = uim_read_finish (client, res, &error);
    if (!read_result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (read_result->len < 4) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Unexpected response length reading EFad: %u", read_result->len);
        g_object_unref (task);
        return;
    }

    /* MNC length is byte 4 of this SIM file */
    mnc_length = read_result->data[3];
    if (mnc_length == 2 || mnc_length == 3) {
        g_task_return_pointer (task, g_strndup (self->priv->imsi, 3 + mnc_length), g_free);
    } else {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "SIM returned invalid MNC length %d (should be either 2 or 3)", mnc_length);
    }
    g_object_unref (task);
}

static void
load_operator_identifier (MMBaseSim           *_self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    MMSimQmi *self;
    GTask    *task;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "loading SIM operator identifier...");

    if (!self->priv->imsi) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't load SIM operator identifier without IMSI");
        g_object_unref (task);
        return;
    }

    uim_read (self,
              0x6FAD,
              adf_file_path,
              G_N_ELEMENTS (adf_file_path),
              (GAsyncReadyCallback)uim_read_efad_ready,
              task);
}

/*****************************************************************************/
/* Load operator name */

static gchar *
load_operator_name_finish (MMBaseSim     *self,
                           GAsyncResult  *res,
                           GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static gchar *
parse_spn (const guint8  *bin,
           gsize          len,
           GError       **error)
{
    g_autoptr(GByteArray)  bin_array = NULL;
    gsize                  binlen;

    /* Remove the FF filler at the end */
    binlen = len;
    while (binlen > 1 && bin[binlen - 1] == 0xff)
        binlen--;
    if (binlen <= 1) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "SIM returned empty spn");
        return NULL;
    }

    /* Setup as bytearray.
     * First byte is metadata; remainder is GSM-7 unpacked into octets; convert to UTF8 */
    bin_array = g_byte_array_sized_new (binlen - 1);
    g_byte_array_append (bin_array, bin + 1, binlen - 1);

    return mm_modem_charset_bytearray_to_utf8 (bin_array, MM_MODEM_CHARSET_GSM, FALSE, error);
}

static void
uim_read_efspn_ready (QmiClientUim *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    GError            *error = NULL;
    g_autoptr(GArray)  read_result = NULL;
    gchar             *spn;

    read_result = uim_read_finish (client, res, &error);
    if (!read_result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    spn = parse_spn ((const guint8 *) read_result->data, read_result->len, &error);

    if (!spn) {
        g_task_return_error (task, error);
    } else {
        g_task_return_pointer (task, spn, g_free);
    }

    g_object_unref (task);
}

static void
load_operator_name (MMBaseSim           *_self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    MMSimQmi *self;
    GTask    *task;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "loading SIM operator name...");

    uim_read (self,
              0x6F46,
              adf_file_path,
              G_N_ELEMENTS (adf_file_path),
              (GAsyncReadyCallback)uim_read_efspn_ready,
              task);
}

/*****************************************************************************/
/* Load preferred networks */

static GList *
load_preferred_networks_finish (MMBaseSim     *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static GList *
parse_get_preferred_networks (QmiMessageNasGetPreferredNetworksOutput *output)
{
    GList  *result = NULL;
    GArray *preferred_nets_array = NULL;
    GArray *preferred_nets_mnc_pcs_digit_array = NULL;
    guint   i;

    if (qmi_message_nas_get_preferred_networks_output_get_preferred_networks (output,
                                                                              &preferred_nets_array,
                                                                              NULL)) {
        qmi_message_nas_get_preferred_networks_output_get_mnc_pcs_digit_include_status (output,
                                                                                        &preferred_nets_mnc_pcs_digit_array,
                                                                                        NULL);
        for (i = 0; i < preferred_nets_array->len; i++) {
            QmiMessageNasGetPreferredNetworksOutputPreferredNetworksElement        *net;
            QmiMessageNasGetPreferredNetworksOutputMncPcsDigitIncludeStatusElement *mnc_pcs_digit = NULL;
            MMSimPreferredNetwork   *new_item;
            g_autofree gchar        *operator_code = NULL;
            MMModemAccessTechnology  act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

            net = &g_array_index (preferred_nets_array,
                                  QmiMessageNasGetPreferredNetworksOutputPreferredNetworksElement, i);
            if (preferred_nets_mnc_pcs_digit_array && i < preferred_nets_mnc_pcs_digit_array->len)
                mnc_pcs_digit = &g_array_index (preferred_nets_mnc_pcs_digit_array,
                                                QmiMessageNasGetPreferredNetworksOutputMncPcsDigitIncludeStatusElement, i);

            new_item = mm_sim_preferred_network_new ();

            if (net->mnc > 99 || (mnc_pcs_digit != NULL && mnc_pcs_digit->includes_pcs_digit))
                operator_code = g_strdup_printf ("%03d%03d", net->mcc, net->mnc);
            else
                operator_code = g_strdup_printf ("%03d%02d", net->mcc, net->mnc);
            mm_sim_preferred_network_set_operator_code (new_item, operator_code);

            if (net->radio_access_technology & QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_GSM)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM;
            if (net->radio_access_technology & QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_GSM_COMPACT)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT;
            if (net->radio_access_technology & QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_UTRAN)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
            if (net->radio_access_technology & QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_EUTRAN)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_LTE;
            if (net->radio_access_technology & QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_NGRAN)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_5GNR;
            mm_sim_preferred_network_set_access_technology (new_item, act);

            result = g_list_append (result, new_item);
        }
    }

    return result;
}

static void
load_preferred_networks_ready (QmiClientNas *client,
                               GAsyncResult *res,
                               GTask        *task)
{
    QmiMessageNasGetPreferredNetworksOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_preferred_networks_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_nas_get_preferred_networks_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get preferred networks: ");
        g_task_return_error (task, error);
    } else
        g_task_return_pointer (task, parse_get_preferred_networks (output), (GDestroyNotify) mm_sim_preferred_network_list_free);

    if (output)
        qmi_message_nas_get_preferred_networks_output_unref (output);
    g_object_unref (task);
}

static void
load_preferred_networks (MMBaseSim           *self,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    task = g_task_new (self, NULL, callback, user_data);
    if (!ensure_qmi_client (task,
                            MM_SIM_QMI (self),
                            QMI_SERVICE_NAS, &client))
        return;

    mm_obj_dbg (self, "loading preferred network list...");
    qmi_client_nas_get_preferred_networks (QMI_CLIENT_NAS (client),
                                           NULL,
                                           5,
                                           NULL,
                                           (GAsyncReadyCallback)load_preferred_networks_ready,
                                           task);
}

/*****************************************************************************/
/* Set preferred networks */

typedef struct {
    /* Preferred network list to be set, used for after-check comparison */
    GList    *set_list;
} SetPreferredNetworksContext;

static void
set_preferred_network_context_free (SetPreferredNetworksContext *ctx)
{
    g_list_free_full (ctx->set_list, (GDestroyNotify) mm_sim_preferred_network_free);
    g_slice_free (SetPreferredNetworksContext, ctx);
}

static gboolean
set_preferred_networks_finish (MMBaseSim *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_preferred_networks_reload_ready (MMBaseSim    *self,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    GError                      *error = NULL;
    GList                       *loaded_list;
    GList                       *loaded_iter;
    GList                       *set_iter;
    SetPreferredNetworksContext *ctx;

    ctx = g_task_get_task_data (task);

    loaded_list = load_preferred_networks_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't reload list of preferred networks: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Compare the set and loaded network list for differences */
    loaded_iter = loaded_list;
    set_iter = ctx->set_list;

    while (loaded_iter && set_iter) {
        const gchar             *loaded_op_code;
        const gchar             *set_op_code;
        MMModemAccessTechnology  loaded_act;
        MMModemAccessTechnology  set_act;

        loaded_op_code = mm_sim_preferred_network_get_operator_code (loaded_iter->data);
        set_op_code = mm_sim_preferred_network_get_operator_code (set_iter->data);
        loaded_act = mm_sim_preferred_network_get_access_technology (loaded_iter->data);
        set_act = mm_sim_preferred_network_get_access_technology (set_iter->data);

        /* Operator code mismatch is never expected, but check it just in case */
        if (g_strcmp0 (loaded_op_code, set_op_code)) {
            mm_obj_warn (self, "operator code mismatch, expected '%s' loaded '%s'",
                         set_op_code, loaded_op_code);
            error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Mismatch in requested and set operator code");
            break;
        }
        /* Check if there are access technology bits requested but unset */
        if ((loaded_act & set_act) != set_act) {
            MMModemAccessTechnology unset = set_act & ~loaded_act;

            mm_obj_warn (self, "access technologies '%s' not set for operator code '%s'",
                         mm_modem_access_technology_build_string_from_mask (unset),
                         set_op_code);
            error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Access technology unsupported by modem or SIM");
            break;
        }
        loaded_iter = g_list_next (loaded_iter);
        set_iter = g_list_next (set_iter);
    }
    if (!error && loaded_iter == NULL && set_iter != NULL) {
        /* Not all networks were written; some modems silently discard networks
         * that exceed the SIM card capacity.
         */
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_TOO_MANY,
                             "Too many networks; %u networks written",
                             g_list_length (loaded_list));
    }

    if (error) {
        /* Update the PreferredNetworks property to real SIM contents */
        mm_gdbus_sim_set_preferred_networks (MM_GDBUS_SIM (self),
                                             mm_sim_preferred_network_list_get_variant (loaded_list));
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);

    g_list_free_full (loaded_list, (GDestroyNotify) mm_sim_preferred_network_free);
    g_object_unref (task);
}

static void
set_preferred_networks_ready (QmiClientNas *client,
                              GAsyncResult *res,
                              GTask        *task)
{
    QmiMessageNasSetPreferredNetworksOutput *output;
    GError                                  *error = NULL;
    MMBaseSim                               *self;

    self = g_task_get_source_object (task);

    output = qmi_client_nas_set_preferred_networks_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_nas_set_preferred_networks_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set preferred networks: ");
        g_task_return_error (task, error);
    } else {
        /* Reload the networks from modem to check whether everything was written */
        load_preferred_networks (self, (GAsyncReadyCallback) set_preferred_networks_reload_ready, task);
        qmi_message_nas_set_preferred_networks_output_unref (output);
        return;
    }

    if (output)
        qmi_message_nas_set_preferred_networks_output_unref (output);
    g_object_unref (task);
}

static void
set_preferred_networks (MMBaseSim *self,
                        GList *preferred_network_list,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask                                  *task;
    QmiMessageNasSetPreferredNetworksInput *input;
    QmiClient                              *client = NULL;
    GArray                                 *preferred_nets_array;
    GArray                                 *preferred_nets_mnc_pcs_digit_array;
    SetPreferredNetworksContext            *ctx;

    task = g_task_new (self, NULL, callback, user_data);
    if (!ensure_qmi_client (task,
                            MM_SIM_QMI (self),
                            QMI_SERVICE_NAS, &client))
        return;

    ctx = g_slice_new0 (SetPreferredNetworksContext);
    ctx->set_list = mm_sim_preferred_network_list_copy (preferred_network_list);
    g_task_set_task_data (task, ctx, (GDestroyNotify) set_preferred_network_context_free);

    mm_obj_dbg (self, "setting preferred networks...");

    input = qmi_message_nas_set_preferred_networks_input_new ();

    preferred_nets_array = g_array_new (FALSE, TRUE, sizeof (QmiMessageNasSetPreferredNetworksInputPreferredNetworksElement));
    preferred_nets_mnc_pcs_digit_array = g_array_new (FALSE, TRUE, sizeof (QmiMessageNasSetPreferredNetworksInputMncPcsDigitIncludeStatusElement));

    while (preferred_network_list) {
        QmiMessageNasSetPreferredNetworksInputPreferredNetworksElement         preferred_nets_element;
        QmiMessageNasSetPreferredNetworksInputMncPcsDigitIncludeStatusElement  pcs_digit_element;
        const gchar                                                           *operator_code;
        MMModemAccessTechnology                                                act;

        memset (&preferred_nets_element, 0, sizeof (preferred_nets_element));
        memset (&pcs_digit_element, 0, sizeof (pcs_digit_element));

        operator_code = mm_sim_preferred_network_get_operator_code (preferred_network_list->data);
        act = mm_sim_preferred_network_get_access_technology (preferred_network_list->data);
        if (mm_3gpp_parse_operator_id (operator_code, &preferred_nets_element.mcc, &preferred_nets_element.mnc,
                                       &pcs_digit_element.includes_pcs_digit, NULL)) {
            pcs_digit_element.mcc = preferred_nets_element.mcc;
            pcs_digit_element.mnc = preferred_nets_element.mnc;

            preferred_nets_element.radio_access_technology = QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_UNSPECIFIED;
            if (act & MM_MODEM_ACCESS_TECHNOLOGY_GSM)
                preferred_nets_element.radio_access_technology |= QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_GSM;
            if (act & MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT)
                preferred_nets_element.radio_access_technology |= QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_GSM_COMPACT;
            if (act & MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
                preferred_nets_element.radio_access_technology |= QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_UTRAN;
            if (act & MM_MODEM_ACCESS_TECHNOLOGY_LTE)
                preferred_nets_element.radio_access_technology |= QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_EUTRAN;
            if (act & MM_MODEM_ACCESS_TECHNOLOGY_5GNR)
                preferred_nets_element.radio_access_technology |= QMI_NAS_PLMN_ACCESS_TECHNOLOGY_IDENTIFIER_NGRAN;

            g_array_append_val (preferred_nets_array, preferred_nets_element);
            g_array_append_val (preferred_nets_mnc_pcs_digit_array, pcs_digit_element);
        }

        preferred_network_list = g_list_next (preferred_network_list);
    }

    qmi_message_nas_set_preferred_networks_input_set_preferred_networks (input, preferred_nets_array, NULL);
    qmi_message_nas_set_preferred_networks_input_set_mnc_pcs_digit_include_status (input, preferred_nets_mnc_pcs_digit_array, NULL);
    /* Always clear any pre-existing networks */
    qmi_message_nas_set_preferred_networks_input_set_clear_previous_preferred_networks (input, TRUE, NULL);

    qmi_client_nas_set_preferred_networks (QMI_CLIENT_NAS (client),
                                           input,
                                           5,
                                           NULL,
                                           (GAsyncReadyCallback)set_preferred_networks_ready,
                                           task);

    qmi_message_nas_set_preferred_networks_input_unref (input);
    g_array_unref (preferred_nets_array);
    g_array_unref (preferred_nets_mnc_pcs_digit_array);
}

/*****************************************************************************/
/* Send PIN */

static GError *
pin_qmi_error_to_mobile_equipment_error (GError *qmi_error)
{
    GError *me_error = NULL;

    if (g_error_matches (qmi_error,
                         QMI_PROTOCOL_ERROR,
                         QMI_PROTOCOL_ERROR_INCORRECT_PIN)) {
        me_error = g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR,
                                        MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD,
                                        qmi_error->message);
    } else if (g_error_matches (qmi_error,
                                QMI_PROTOCOL_ERROR,
                                QMI_PROTOCOL_ERROR_PIN_BLOCKED)) {
        me_error = g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR,
                                        MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK,
                                        qmi_error->message);
    }

    if (me_error) {
        g_error_free (qmi_error);
        return me_error;
    }

    return qmi_error;
}

static gboolean
send_pin_finish (MMBaseSim     *self,
                 GAsyncResult  *res,
                 GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
uim_verify_pin_ready (QmiClientUim *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    QmiMessageUimVerifyPinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_uim_verify_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_uim_verify_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't verify PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_uim_verify_pin_output_unref (output);
    g_object_unref (task);
}

static void
uim_verify_pin (MMSimQmi *self,
                GTask    *task)
{
    QmiMessageUimVerifyPinInput *input;
    QmiClient *client = NULL;
    GArray *aid;

    if (!ensure_qmi_client (task,
                            self,
                            QMI_SERVICE_UIM, &client))
        return;

    input = qmi_message_uim_verify_pin_input_new ();
    qmi_message_uim_verify_pin_input_set_info (
        input,
        QMI_UIM_PIN_ID_PIN1,
        g_task_get_task_data (task),
        NULL);
    aid = g_array_new (FALSE, FALSE, sizeof (guint8)); /* empty AID */
    qmi_message_uim_verify_pin_input_set_session (
        input,
        QMI_UIM_SESSION_TYPE_CARD_SLOT_1,
        aid,
        NULL);
    g_array_unref (aid);
    qmi_client_uim_verify_pin (QMI_CLIENT_UIM (client),
                               input,
                               5,
                               NULL,
                               (GAsyncReadyCallback) uim_verify_pin_ready,
                               task);
    qmi_message_uim_verify_pin_input_unref (input);
}

static void
dms_uim_verify_pin_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    QmiMessageDmsUimVerifyPinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_verify_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_uim_verify_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't verify PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_dms_uim_verify_pin_output_unref (output);
    g_object_unref (task);
}

static void
dms_uim_verify_pin (MMSimQmi *self,
                    GTask    *task)
{
    QmiMessageDmsUimVerifyPinInput *input;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (NULL,
                            self,
                            QMI_SERVICE_DMS, &client)) {
        /* Very unlikely that this will ever happen, but anyway, try with
         * UIM service instead */
        uim_verify_pin (self, task);
        return;
    }

    mm_obj_dbg (self, "sending PIN...");
    input = qmi_message_dms_uim_verify_pin_input_new ();
    qmi_message_dms_uim_verify_pin_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        g_task_get_task_data (task),
        NULL);
    qmi_client_dms_uim_verify_pin (QMI_CLIENT_DMS (client),
                                   input,
                                   5,
                                   NULL,
                                   (GAsyncReadyCallback) dms_uim_verify_pin_ready,
                                   task);
    qmi_message_dms_uim_verify_pin_input_unref (input);
}

static void
send_pin (MMBaseSim           *_self,
          const gchar         *pin,
          GAsyncReadyCallback  callback,
          gpointer             user_data)
{
    GTask *task;
    MMSimQmi *self;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    g_task_set_task_data (task, g_strdup (pin), g_free);

    mm_obj_dbg (self, "verifying PIN...");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_verify_pin (self, task);
    else
        uim_verify_pin (self, task);
}

/*****************************************************************************/
/* Send PUK */

typedef struct {
    gchar    *puk;
    gchar    *new_pin;
} UnblockPinContext;

static void
unblock_pin_context_free (UnblockPinContext *ctx)
{
    g_free (ctx->puk);
    g_free (ctx->new_pin);
    g_slice_free (UnblockPinContext, ctx);
}

static gboolean
send_puk_finish (MMBaseSim     *self,
                 GAsyncResult  *res,
                 GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
uim_unblock_pin_ready (QmiClientUim *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    QmiMessageUimUnblockPinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_uim_unblock_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_uim_unblock_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't unblock PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_uim_unblock_pin_output_unref (output);
    g_object_unref (task);
}

static void
uim_unblock_pin (MMSimQmi *self,
                 GTask    *task)
{
    QmiMessageUimUnblockPinInput *input;
    QmiClient *client = NULL;
    UnblockPinContext *ctx;
    GArray *aid;

    if (!ensure_qmi_client (task,
                            self,
                            QMI_SERVICE_UIM, &client))
        return;

    ctx = g_task_get_task_data (task);

    input = qmi_message_uim_unblock_pin_input_new ();
    qmi_message_uim_unblock_pin_input_set_info (
        input,
        QMI_UIM_PIN_ID_PIN1,
        ctx->puk,
        ctx->new_pin,
        NULL);
    aid = g_array_new (FALSE, FALSE, sizeof (guint8)); /* empty AID */
    qmi_message_uim_unblock_pin_input_set_session (
        input,
        QMI_UIM_SESSION_TYPE_CARD_SLOT_1,
        aid,
        NULL);
    g_array_unref (aid);
    qmi_client_uim_unblock_pin (QMI_CLIENT_UIM (client),
                                input,
                                5,
                                NULL,
                                (GAsyncReadyCallback) uim_unblock_pin_ready,
                                task);
    qmi_message_uim_unblock_pin_input_unref (input);
}

static void
dms_uim_unblock_pin_ready (QmiClientDms *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    QmiMessageDmsUimUnblockPinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_unblock_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_uim_unblock_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't unblock PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_dms_uim_unblock_pin_output_unref (output);
    g_object_unref (task);
}

static void
dms_uim_unblock_pin (MMSimQmi *self,
                     GTask    *task)
{
    QmiMessageDmsUimUnblockPinInput *input;
    QmiClient *client = NULL;
    UnblockPinContext *ctx;

    if (!ensure_qmi_client (NULL,
                            self,
                            QMI_SERVICE_DMS, &client)) {
        /* Very unlikely that this will ever happen, but anyway, try with
         * UIM service instead */
        uim_unblock_pin (self, task);
        return;
    }

    ctx = g_task_get_task_data (task);

    input = qmi_message_dms_uim_unblock_pin_input_new ();
    qmi_message_dms_uim_unblock_pin_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        ctx->puk,
        ctx->new_pin,
        NULL);
    qmi_client_dms_uim_unblock_pin (QMI_CLIENT_DMS (client),
                                    input,
                                    5,
                                    NULL,
                                    (GAsyncReadyCallback)dms_uim_unblock_pin_ready,
                                    task);
    qmi_message_dms_uim_unblock_pin_input_unref (input);
}

static void
send_puk (MMBaseSim           *_self,
          const gchar         *puk,
          const gchar         *new_pin,
          GAsyncReadyCallback  callback,
          gpointer             user_data)
{
    GTask *task;
    UnblockPinContext *ctx;
    MMSimQmi *self;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new (UnblockPinContext);
    ctx->puk = g_strdup (puk);
    ctx->new_pin = g_strdup (new_pin);
    g_task_set_task_data (task, ctx, (GDestroyNotify) unblock_pin_context_free);

    mm_obj_dbg (self, "unblocking PIN...");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_unblock_pin (self, task);
    else
        uim_unblock_pin (self, task);
}

/*****************************************************************************/
/* Change PIN */

typedef struct {
    gchar    *old_pin;
    gchar    *new_pin;
} ChangePinContext;

static void
change_pin_context_free (ChangePinContext *ctx)
{
    g_free (ctx->old_pin);
    g_free (ctx->new_pin);
    g_slice_free (ChangePinContext, ctx);
}

static gboolean
change_pin_finish (MMBaseSim     *self,
                   GAsyncResult  *res,
                   GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
uim_change_pin_ready (QmiClientUim *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    QmiMessageUimChangePinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_uim_change_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_uim_change_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't change PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_uim_change_pin_output_unref (output);
    g_object_unref (task);
}

static void
uim_change_pin (MMSimQmi *self,
                GTask    *task)
{
    QmiMessageUimChangePinInput *input;
    QmiClient *client = NULL;
    ChangePinContext *ctx;
    GArray *aid;

    if (!ensure_qmi_client (task,
                            self,
                            QMI_SERVICE_UIM, &client))
        return;

    ctx = g_task_get_task_data (task);

    input = qmi_message_uim_change_pin_input_new ();
    qmi_message_uim_change_pin_input_set_info (
        input,
        QMI_UIM_PIN_ID_PIN1,
        ctx->old_pin,
        ctx->new_pin,
        NULL);
    aid = g_array_new (FALSE, FALSE, sizeof (guint8)); /* empty AID */
    qmi_message_uim_change_pin_input_set_session (
        input,
        QMI_UIM_SESSION_TYPE_CARD_SLOT_1,
        aid,
        NULL);
    g_array_unref (aid);
    qmi_client_uim_change_pin (QMI_CLIENT_UIM (client),
                               input,
                               5,
                               NULL,
                               (GAsyncReadyCallback) uim_change_pin_ready,
                               task);
    qmi_message_uim_change_pin_input_unref (input);
}

static void
dms_uim_change_pin_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    QmiMessageDmsUimChangePinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_change_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_uim_change_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't change PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_dms_uim_change_pin_output_unref (output);
    g_object_unref (task);
}

static void
dms_uim_change_pin (MMSimQmi *self,
                    GTask    *task)
{
    QmiMessageDmsUimChangePinInput *input;
    QmiClient *client = NULL;
    ChangePinContext *ctx;

    if (!ensure_qmi_client (NULL,
                            self,
                            QMI_SERVICE_DMS, &client)) {
        /* Very unlikely that this will ever happen, but anyway, try with
         * UIM service instead */
        uim_change_pin (self, task);
        return;
    }

    ctx = g_task_get_task_data (task);

    input = qmi_message_dms_uim_change_pin_input_new ();
    qmi_message_dms_uim_change_pin_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        ctx->old_pin,
        ctx->new_pin,
        NULL);
    qmi_client_dms_uim_change_pin (QMI_CLIENT_DMS (client),
                                   input,
                                   5,
                                   NULL,
                                   (GAsyncReadyCallback) dms_uim_change_pin_ready,
                                   task);
    qmi_message_dms_uim_change_pin_input_unref (input);
}

static void
change_pin (MMBaseSim           *_self,
            const gchar         *old_pin,
            const gchar         *new_pin,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    GTask *task;
    ChangePinContext *ctx;
    MMSimQmi *self;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new (ChangePinContext);
    ctx->old_pin = g_strdup (old_pin);
    ctx->new_pin = g_strdup (new_pin);
    g_task_set_task_data (task, ctx, (GDestroyNotify) change_pin_context_free);

    mm_obj_dbg (self, "changing PIN...");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_change_pin (self, task);
    else
        uim_change_pin (self, task);
}

/*****************************************************************************/
/* Enable PIN */

typedef struct {
    gchar    *pin;
    gboolean  enabled;
} EnablePinContext;

static void
enable_pin_context_free (EnablePinContext *ctx)
{
    g_free (ctx->pin);
    g_slice_free (EnablePinContext, ctx);
}

static gboolean
enable_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
uim_set_pin_protection_ready (QmiClientUim *client,
                              GAsyncResult *res,
                              GTask        *task)
{
    QmiMessageUimSetPinProtectionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_uim_set_pin_protection_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_uim_set_pin_protection_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't enable PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_uim_set_pin_protection_output_unref (output);
    g_object_unref (task);
}

static void
uim_enable_pin (MMSimQmi *self,
                GTask    *task)
{
    QmiMessageUimSetPinProtectionInput *input;
    QmiClient *client = NULL;
    EnablePinContext *ctx;
    GArray *aid;

    if (!ensure_qmi_client (task,
                            MM_SIM_QMI (self),
                            QMI_SERVICE_UIM, &client))
        return;

    ctx = g_task_get_task_data (task);

    input = qmi_message_uim_set_pin_protection_input_new ();
    qmi_message_uim_set_pin_protection_input_set_info (
        input,
        QMI_UIM_PIN_ID_PIN1,
        ctx->enabled,
        ctx->pin,
        NULL);
    aid = g_array_new (FALSE, FALSE, sizeof (guint8)); /* empty AID */
    qmi_message_uim_set_pin_protection_input_set_session (
        input,
        QMI_UIM_SESSION_TYPE_CARD_SLOT_1,
        aid,
        NULL);
    g_array_unref (aid);
    qmi_client_uim_set_pin_protection (QMI_CLIENT_UIM (client),
                                       input,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)uim_set_pin_protection_ready,
                                       task);
    qmi_message_uim_set_pin_protection_input_unref (input);
}

static void
dms_uim_set_pin_protection_ready (QmiClientDms *client,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    QmiMessageDmsUimSetPinProtectionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_set_pin_protection_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_uim_set_pin_protection_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't enable PIN: ");
        g_task_return_error (task, pin_qmi_error_to_mobile_equipment_error (error));
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_dms_uim_set_pin_protection_output_unref (output);
    g_object_unref (task);
}

static void
dms_uim_enable_pin (MMSimQmi *self,
                    GTask    *task)
{
    QmiMessageDmsUimSetPinProtectionInput *input;
    QmiClient *client = NULL;
    EnablePinContext *ctx;

    if (!ensure_qmi_client (NULL,
                            MM_SIM_QMI (self),
                            QMI_SERVICE_DMS, &client)) {
        /* Very unlikely that this will ever happen, but anyway, try with
         * UIM service instead */
        uim_enable_pin (self, task);
        return;
    }

    ctx = g_task_get_task_data (task);

    input = qmi_message_dms_uim_set_pin_protection_input_new ();
    qmi_message_dms_uim_set_pin_protection_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        ctx->enabled,
        ctx->pin,
        NULL);
    qmi_client_dms_uim_set_pin_protection (QMI_CLIENT_DMS (client),
                                           input,
                                           5,
                                           NULL,
                                           (GAsyncReadyCallback)dms_uim_set_pin_protection_ready,
                                           task);
    qmi_message_dms_uim_set_pin_protection_input_unref (input);
}

static void
enable_pin (MMBaseSim           *_self,
            const gchar         *pin,
            gboolean             enabled,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    GTask *task;
    EnablePinContext *ctx;
    MMSimQmi *self;

    self = MM_SIM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new (EnablePinContext);
    ctx->pin = g_strdup (pin);
    ctx->enabled = enabled;
    g_task_set_task_data (task, ctx, (GDestroyNotify) enable_pin_context_free);

    mm_obj_dbg (self, "%s PIN...", enabled ? "enabling" : "disabling");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_enable_pin (self, task);
    else
        uim_enable_pin (self, task);
}

/*****************************************************************************/

MMBaseSim *
mm_sim_qmi_new_finish (GAsyncResult  *res,
                       GError       **error)
{
    GObject *source;
    GObject *sim;

    source = g_async_result_get_source_object (res);
    sim = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!sim)
        return NULL;

    /* Only export valid SIMs */
    mm_base_sim_export (MM_BASE_SIM (sim));

    return MM_BASE_SIM (sim);
}

void
mm_sim_qmi_new (MMBaseModem         *modem,
                gboolean             dms_uim_deprecated,
                GCancellable        *cancellable,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_QMI,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                MM_SIM_QMI_DMS_UIM_DEPRECATED, dms_uim_deprecated,
                                "active", TRUE, /* by default always active */
                                NULL);
}

MMBaseSim *
mm_sim_qmi_new_initialized (MMBaseModem *modem,
                            gboolean     dms_uim_deprecated,
                            guint        slot_number,
                            gboolean     active,
                            const gchar *sim_identifier,
                            const gchar *imsi,
                            const gchar *eid,
                            const gchar *operator_identifier,
                            const gchar *operator_name,
                            const GStrv  emergency_numbers)
{
    MMBaseSim *sim;

    sim = MM_BASE_SIM (g_object_new (MM_TYPE_SIM_QMI,
                                     MM_BASE_SIM_MODEM,             modem,
                                     MM_SIM_QMI_DMS_UIM_DEPRECATED, dms_uim_deprecated,
                                     MM_BASE_SIM_SLOT_NUMBER,       slot_number,
                                     "active",                      active,
                                     "sim-identifier",              sim_identifier,
                                     "imsi",                        imsi,
                                     "eid",                         eid,
                                     "operator-identifier",         operator_identifier,
                                     "operator-name",               operator_name,
                                     "emergency-numbers",           emergency_numbers,
                                     NULL));

    mm_base_sim_export (sim);
    return sim;
}

/*****************************************************************************/

static void
mm_sim_qmi_init (MMSimQmi *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_SIM_QMI,
                                              MMSimQmiPrivate);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMSimQmi *self = MM_SIM_QMI (object);

    switch (prop_id) {
    case PROP_DMS_UIM_DEPRECATED:
        self->priv->dms_uim_deprecated = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMSimQmi *self = MM_SIM_QMI (object);

    switch (prop_id) {
    case PROP_DMS_UIM_DEPRECATED:
        g_value_set_boolean (value, self->priv->dms_uim_deprecated);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMSimQmi *self = MM_SIM_QMI (object);

    g_free (self->priv->imsi);

    G_OBJECT_CLASS (mm_sim_qmi_parent_class)->finalize (object);
}

static void
mm_sim_qmi_class_init (MMSimQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSimQmiPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    base_sim_class->wait_sim_ready = wait_sim_ready;
    base_sim_class->wait_sim_ready_finish = wait_sim_ready_finish;
    base_sim_class->load_sim_identifier = load_sim_identifier;
    base_sim_class->load_sim_identifier_finish = load_sim_identifier_finish;
    base_sim_class->load_imsi = load_imsi;
    base_sim_class->load_imsi_finish = load_imsi_finish;
    base_sim_class->load_operator_identifier = load_operator_identifier;
    base_sim_class->load_operator_identifier_finish = load_operator_identifier_finish;
    base_sim_class->load_operator_name = load_operator_name;
    base_sim_class->load_operator_name_finish = load_operator_name_finish;
    base_sim_class->load_gid1 = load_gid1;
    base_sim_class->load_gid1_finish = load_gid1_finish;
    base_sim_class->load_gid2 = load_gid2;
    base_sim_class->load_gid2_finish = load_gid2_finish;
    base_sim_class->load_preferred_networks = load_preferred_networks;
    base_sim_class->load_preferred_networks_finish = load_preferred_networks_finish;
    base_sim_class->set_preferred_networks = set_preferred_networks;
    base_sim_class->set_preferred_networks_finish = set_preferred_networks_finish;
    base_sim_class->send_pin = send_pin;
    base_sim_class->send_pin_finish = send_pin_finish;
    base_sim_class->send_puk = send_puk;
    base_sim_class->send_puk_finish = send_puk_finish;
    base_sim_class->change_pin = change_pin;
    base_sim_class->change_pin_finish = change_pin_finish;
    base_sim_class->enable_pin = enable_pin;
    base_sim_class->enable_pin_finish = enable_pin_finish;

    properties[PROP_DMS_UIM_DEPRECATED] =
        g_param_spec_boolean (MM_SIM_QMI_DMS_UIM_DEPRECATED,
                              "DMS UIM deprecated",
                              "Whether DMS UIM commands should be skipped",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DMS_UIM_DEPRECATED, properties[PROP_DMS_UIM_DEPRECATED]);
}
