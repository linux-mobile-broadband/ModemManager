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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
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

#include "mm-sms-at.h"
#include "mm-iface-modem-messaging.h"
#include "mm-sms-part-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-log-object.h"
#include "mm-bind.h"

G_DEFINE_TYPE (MMSmsAt, mm_sms_at, MM_TYPE_BASE_SMS)

struct _MMSmsAtPrivate {
    MMBaseModem *modem;
};

/*****************************************************************************/

static gboolean
sms_get_store_or_send_command (MMSmsAt  *self,
                               MMSmsPart  *part,
                               gboolean    text_or_pdu,   /* TRUE for PDU */
                               gboolean    store_or_send, /* TRUE for send */
                               gchar     **out_cmd,
                               gchar     **out_msg_data,
                               GError    **error)
{
    g_assert (out_cmd != NULL);
    g_assert (out_msg_data != NULL);

    if (!text_or_pdu) {
        /* Text mode */
        *out_cmd = g_strdup_printf ("+CMG%c=\"%s\"",
                                    store_or_send ? 'S' : 'W',
                                    mm_sms_part_get_number (part));
        *out_msg_data = g_strdup_printf ("%s\x1a", mm_sms_part_get_text (part));
    } else {
        g_autofree gchar  *hex = NULL;
        g_autofree guint8 *pdu = NULL;
        guint              pdulen = 0;
        guint              msgstart = 0;

        /* AT+CMGW=<length>[, <stat>]<CR> PDU can be entered. <CTRL-Z>/<ESC> */

        pdu = mm_sms_part_3gpp_get_submit_pdu (part, &pdulen, &msgstart, self, error);
        if (!pdu)
            /* 'error' should already be set */
            return FALSE;

        /* Convert PDU to hex */
        hex = mm_utils_bin2hexstr (pdu, pdulen);
        if (!hex) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Not enough memory to send SMS PDU");
            return FALSE;
        }

        /* CMGW/S length is the size of the PDU without SMSC information */
        *out_cmd = g_strdup_printf ("+CMG%c=%d",
                                    store_or_send ? 'S' : 'W',
                                    pdulen - msgstart);
        *out_msg_data = g_strdup_printf ("%s\x1a", hex);
    }

    return TRUE;
}

/*****************************************************************************/
/* Store the SMS */

typedef struct {
    MMBaseModem    *modem;
    MMIfacePortAt  *port;
    MMSmsStorage    storage;
    gboolean        need_unlock;
    gboolean        use_pdu_mode;
    GList          *current;
    gchar          *msg_data;
} SmsStoreContext;

static void
sms_store_context_free (SmsStoreContext *ctx)
{
    /* Unlock mem2 storage if we had the lock */
    if (ctx->need_unlock) {
        mm_iface_modem_messaging_unlock_storages (MM_IFACE_MODEM_MESSAGING (ctx->modem),
                                                  FALSE,
                                                  TRUE);
    }
    g_object_unref (ctx->port);
    g_object_unref (ctx->modem);
    g_free (ctx->msg_data);
    g_slice_free (SmsStoreContext, ctx);
}

static gboolean
sms_store_finish (MMBaseSms    *self,
                  GAsyncResult  *res,
                  GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void sms_store_next_part (GTask *task);

static void
store_msg_data_ready (MMBaseModem  *modem,
                      GAsyncResult *res,
                      GTask        *task)
{
    SmsStoreContext *ctx;
    const gchar     *response;
    GError          *error = NULL;
    gint             rv;
    gint             idx;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Read the new part index from the reply */
    rv = sscanf (response, "+CMGW: %d", &idx);
    if (rv != 1 || idx < 0) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't read index of already stored part: "
                                 "%d fields parsed",
                                 rv);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Set the index in the part we hold */
    mm_sms_part_set_index ((MMSmsPart *)ctx->current->data, (guint)idx);

    ctx->current = g_list_next (ctx->current);
    sms_store_next_part (task);
}

static void
store_ready (MMBaseModem  *modem,
             GAsyncResult *res,
             GTask        *task)
{
    SmsStoreContext *ctx;
    GError          *error = NULL;

    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Send the actual message data.
     * We send the data as 'raw' data because we do NOT want it to
     * be treated as an AT command (i.e. we don't want it prefixed
     * with AT+ and suffixed with <CR><LF>), plus, we want it to be
     * sent right away (not queued after other AT commands). */
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->port,
                                   ctx->msg_data,
                                   10,
                                   FALSE,
                                   TRUE, /* raw */
                                   NULL,
                                   (GAsyncReadyCallback)store_msg_data_ready,
                                   task);
}

static void
sms_store_next_part (GTask *task)
{
    MMSmsAt        *self;
    SmsStoreContext  *ctx;
    GError           *error = NULL;
    g_autofree gchar *cmd = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->current) {
        /* Done we are */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_clear_pointer (&ctx->msg_data, g_free);

    if (!sms_get_store_or_send_command (self,
                                        (MMSmsPart *)ctx->current->data,
                                        ctx->use_pdu_mode,
                                        FALSE,
                                        &cmd,
                                        &ctx->msg_data,
                                        &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_assert (cmd != NULL);
    g_assert (ctx->msg_data != NULL);

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->port,
                                   cmd,
                                   10,
                                   FALSE,
                                   FALSE,  /* raw */
                                   NULL,
                                   (GAsyncReadyCallback)store_ready,
                                   task);
}

static void
store_lock_sms_storages_ready (MMIfaceModemMessaging *messaging,
                               GAsyncResult     *res,
                               GTask            *task)
{
    MMSmsAt       *self;
    SmsStoreContext *ctx;
    GError          *error = NULL;

    if (!mm_iface_modem_messaging_lock_storages_finish (messaging, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* We are now locked. Whatever result we have here, we need to make sure
     * we unlock the storages before finishing. */
    ctx->need_unlock = TRUE;

    /* Go on to store the parts */
    ctx->current = mm_base_sms_get_parts (MM_BASE_SMS (self));
    sms_store_next_part (task);
}

static void
sms_store (MMBaseSms           *sms,
           MMSmsStorage         storage,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    MMSmsAt         *self = MM_SMS_AT (sms);
    SmsStoreContext *ctx;
    GTask           *task;
    MMIfacePortAt   *port;
    GError          *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    /* Select port for the operation */
    port = mm_base_modem_peek_best_at_port (self->priv->modem, &error);
    if (!port) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Setup the context */
    ctx = g_slice_new0 (SmsStoreContext);
    ctx->modem = g_object_ref (self->priv->modem);
    ctx->port = g_object_ref (port);
    ctx->storage = storage;

    /* Different ways to do it if on PDU or text mode */
    g_assert (MM_IS_IFACE_MODEM_MESSAGING (self->priv->modem));
    g_object_get (self->priv->modem,
                  MM_IFACE_MODEM_MESSAGING_SMS_PDU_MODE, &ctx->use_pdu_mode,
                  NULL);
    g_task_set_task_data (task, ctx, (GDestroyNotify)sms_store_context_free);

    /* First, lock storage to use */
    mm_iface_modem_messaging_lock_storages (
        MM_IFACE_MODEM_MESSAGING (self->priv->modem),
        MM_SMS_STORAGE_UNKNOWN, /* none required for mem1 */
        ctx->storage,
        (GAsyncReadyCallback)store_lock_sms_storages_ready,
        task);
}

/*****************************************************************************/
/* Send the SMS */

typedef struct {
    MMBaseModem   *modem;
    MMIfacePortAt *port;
    gboolean       need_unlock;
    gboolean       from_storage;
    gboolean       use_pdu_mode;
    GList         *current;
    gchar         *msg_data;
} SmsSendContext;

static void
sms_send_context_free (SmsSendContext *ctx)
{
    /* Unlock mem2 storage if we had the lock */
    if (ctx->need_unlock) {
        mm_iface_modem_messaging_unlock_storages (MM_IFACE_MODEM_MESSAGING (ctx->modem),
                                                  FALSE,
                                                  TRUE);
    }
    g_object_unref (ctx->port);
    g_object_unref (ctx->modem);
    g_free (ctx->msg_data);
    g_slice_free (SmsSendContext, ctx);
}

static gboolean
sms_send_finish (MMBaseSms     *self,
                 GAsyncResult  *res,
                 GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void sms_send_next_part (GTask *task);

static gint
read_message_reference_from_reply (const gchar  *response,
                                   GError      **error)
{
    gint rv = 0;
    gint idx = -1;

    if (strstr (response, "+CMGS"))
        rv = sscanf (strstr (response, "+CMGS"), "+CMGS: %d", &idx);
    else if (strstr (response, "+CMSS"))
        rv = sscanf (strstr (response, "+CMSS"), "+CMSS: %d", &idx);

    if (rv != 1 || idx < 0) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't read message reference: "
                     "%d fields parsed from response '%s'",
                     rv, response);
        return -1;
    }

    return idx;
}

static void
send_generic_msg_data_ready (MMBaseModem  *modem,
                             GAsyncResult *res,
                             GTask        *task)
{
    SmsSendContext *ctx;
    GError         *error = NULL;
    const gchar    *response;
    gint            message_reference;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    message_reference = read_message_reference_from_reply (response, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    mm_sms_part_set_message_reference ((MMSmsPart *)ctx->current->data,
                                       (guint)message_reference);

    ctx->current = g_list_next (ctx->current);
    sms_send_next_part (task);
}

static void
send_generic_ready (MMBaseModem  *modem,
                    GAsyncResult *res,
                    GTask        *task)
{
    SmsSendContext *ctx;
    GError         *error = NULL;

    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Send the actual message data.
     * We send the data as 'raw' data because we do NOT want it to
     * be treated as an AT command (i.e. we don't want it prefixed
     * with AT+ and suffixed with <CR><LF>), plus, we want it to be
     * sent right away (not queued after other AT commands). */
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->port,
                                   ctx->msg_data,
                                   MM_BASE_SMS_DEFAULT_SEND_TIMEOUT,
                                   FALSE,
                                   TRUE, /* raw */
                                   NULL,
                                   (GAsyncReadyCallback)send_generic_msg_data_ready,
                                   task);
}

static void
send_from_storage_ready (MMBaseModem  *modem,
                         GAsyncResult *res,
                         GTask        *task)
{
    MMSmsAt      *self;
    SmsSendContext *ctx;
    GError         *error = NULL;
    const gchar    *response;
    gint            message_reference;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        mm_obj_dbg (self, "couldn't send SMS from storage: %s; trying generic send...", error->message);
        g_error_free (error);

        ctx->from_storage = FALSE;
        sms_send_next_part (task);
        return;
    }

    message_reference = read_message_reference_from_reply (response, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_sms_part_set_message_reference ((MMSmsPart *)ctx->current->data,
                                       (guint)message_reference);

    ctx->current = g_list_next (ctx->current);
    sms_send_next_part (task);
}

static void
sms_send_next_part (GTask *task)
{
    MMSmsAt        *self;
    SmsSendContext   *ctx;
    GError           *error = NULL;
    g_autofree gchar *cmd = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->current) {
        /* Done we are */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Send from storage */
    if (ctx->from_storage) {
        cmd = g_strdup_printf ("+CMSS=%d", mm_sms_part_get_index ((MMSmsPart *)ctx->current->data));
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->port,
                                       cmd,
                                       MM_BASE_SMS_DEFAULT_SEND_TIMEOUT,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)send_from_storage_ready,
                                       task);
        return;
    }

    /* Generic send */

    g_clear_pointer (&ctx->msg_data, g_free);

    if (!sms_get_store_or_send_command (self,
                                        (MMSmsPart *)ctx->current->data,
                                        ctx->use_pdu_mode,
                                        TRUE,
                                        &cmd,
                                        &ctx->msg_data,
                                        &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_assert (cmd != NULL);
    g_assert (ctx->msg_data != NULL);

    /* no network involved in this initial AT command, so lower timeout */
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->port,
                                   cmd,
                                   10,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL,
                                   (GAsyncReadyCallback)send_generic_ready,
                                   task);
}

static void
send_lock_sms_storages_ready (MMIfaceModemMessaging *messaging,
                              GAsyncResult          *res,
                              GTask                 *task)
{
    MMSmsAt      *self;
    SmsSendContext *ctx;
    GError         *error = NULL;

    if (!mm_iface_modem_messaging_lock_storages_finish (messaging, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* We are now locked. Whatever result we have here, we need to make sure
     * we unlock the storages before finishing. */
    ctx->need_unlock = TRUE;

    /* Go on to send the parts */
    ctx->current = mm_base_sms_get_parts (MM_BASE_SMS (self));
    sms_send_next_part (task);
}

static void
sms_send (MMBaseSms           *sms,
          GAsyncReadyCallback  callback,
          gpointer             user_data)
{
    MMSmsAt        *self = MM_SMS_AT (sms);
    SmsSendContext *ctx;
    GTask          *task;
    MMIfacePortAt  *port;
    GError         *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    /* Select port for the operation */
    port = mm_base_modem_peek_best_at_port (self->priv->modem, &error);
    if (!port) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Setup the context */
    ctx = g_slice_new0 (SmsSendContext);
    ctx->modem = g_object_ref (self->priv->modem);
    ctx->port = g_object_ref (port);
    g_task_set_task_data (task, ctx, (GDestroyNotify)sms_send_context_free);

    /* If the SMS is STORED, try to send from storage */
    ctx->from_storage = (mm_base_sms_get_storage (MM_BASE_SMS (self)) != MM_SMS_STORAGE_UNKNOWN);
    if (ctx->from_storage) {
        /* When sending from storage, first lock storage to use */
        g_assert (MM_IS_IFACE_MODEM_MESSAGING (self->priv->modem));
        mm_iface_modem_messaging_lock_storages (
            MM_IFACE_MODEM_MESSAGING (self->priv->modem),
            MM_SMS_STORAGE_UNKNOWN, /* none required for mem1 */
            mm_base_sms_get_storage (MM_BASE_SMS (self)),
            (GAsyncReadyCallback)send_lock_sms_storages_ready,
            task);
        return;
    }

    /* Different ways to do it if on PDU or text mode */
    g_object_get (self->priv->modem,
                  MM_IFACE_MODEM_MESSAGING_SMS_PDU_MODE, &ctx->use_pdu_mode,
                  NULL);
    ctx->current = mm_base_sms_get_parts (MM_BASE_SMS (self));
    sms_send_next_part (task);
}

/*****************************************************************************/

typedef struct {
    MMBaseModem *modem;
    gboolean     need_unlock;
    GList       *current;
    guint        n_failed;
} SmsDeletePartsContext;

static void
sms_delete_parts_context_free (SmsDeletePartsContext *ctx)
{
    /* Unlock mem1 storage if we had the lock */
    if (ctx->need_unlock) {
        mm_iface_modem_messaging_unlock_storages (MM_IFACE_MODEM_MESSAGING (ctx->modem),
                                                  TRUE,
                                                  FALSE);
    }
    g_object_unref (ctx->modem);
    g_slice_free (SmsDeletePartsContext, ctx);
}

static gboolean
sms_delete_finish (MMBaseSms     *self,
                   GAsyncResult  *res,
                   GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void delete_next_part (GTask *task);

static void
delete_part_ready (MMBaseModem  *modem,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMSmsAt             *self;
    SmsDeletePartsContext *ctx;
    g_autoptr(GError)      error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        ctx->n_failed++;
        mm_obj_dbg (self, "couldn't delete SMS part with index %u: %s",
                    mm_sms_part_get_index ((MMSmsPart *)ctx->current->data),
                    error->message);
    }

    /* We reset the index, as there is no longer that part */
    mm_sms_part_set_index ((MMSmsPart *)ctx->current->data, SMS_PART_INVALID_INDEX);

    ctx->current = g_list_next (ctx->current);
    delete_next_part (task);
}

static void
delete_next_part (GTask *task)
{
    SmsDeletePartsContext *ctx;
    g_autofree gchar      *cmd = NULL;

    ctx = g_task_get_task_data (task);

    /* Skip non-stored parts */
    while (ctx->current && (mm_sms_part_get_index ((MMSmsPart *)ctx->current->data) == SMS_PART_INVALID_INDEX))
        ctx->current = g_list_next (ctx->current);

    /* If all removed, we're done */
    if (!ctx->current) {
        if (ctx->n_failed > 0)
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't delete %u parts from this SMS",
                                     ctx->n_failed);
        else
            g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    cmd = g_strdup_printf ("+CMGD=%d", mm_sms_part_get_index ((MMSmsPart *)ctx->current->data));
    mm_base_modem_at_command (ctx->modem,
                              cmd,
                              10,
                              FALSE,
                              (GAsyncReadyCallback)delete_part_ready,
                              task);
}

static void
delete_lock_sms_storages_ready (MMIfaceModemMessaging *messaging,
                                GAsyncResult          *res,
                                GTask                 *task)
{
    MMSmsAt             *self;
    SmsDeletePartsContext *ctx;
    GError                *error = NULL;

    if (!mm_iface_modem_messaging_lock_storages_finish (messaging, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* We are now locked. Whatever result we have here, we need to make sure
     * we unlock the storages before finishing. */
    ctx->need_unlock = TRUE;

    /* Go on deleting parts */
    ctx->current = mm_base_sms_get_parts (MM_BASE_SMS (self));
    delete_next_part (task);
}

static void
sms_delete (MMBaseSms           *sms,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    MMSmsAt               *self = MM_SMS_AT (sms);
    SmsDeletePartsContext *ctx;
    GTask                 *task;

    ctx = g_slice_new0 (SmsDeletePartsContext);
    ctx->modem = g_object_ref (self->priv->modem);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)sms_delete_parts_context_free);

    if (mm_base_sms_get_storage (MM_BASE_SMS (self)) == MM_SMS_STORAGE_UNKNOWN) {
        mm_obj_dbg (self, "not removing parts from non-stored SMS");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Select specific storage to delete from */
    mm_iface_modem_messaging_lock_storages (
        MM_IFACE_MODEM_MESSAGING (self->priv->modem),
        mm_base_sms_get_storage (MM_BASE_SMS (self)),
        MM_SMS_STORAGE_UNKNOWN, /* none required for mem2 */
        (GAsyncReadyCallback)delete_lock_sms_storages_ready,
        task);
}

/*****************************************************************************/

MMBaseSms *
mm_sms_at_new (MMBaseModem  *modem,
               gboolean      is_3gpp,
               MMSmsStorage  default_storage)
{
    MMBaseSms *sms;

    sms = MM_BASE_SMS (g_object_new (MM_TYPE_SMS_AT,
                                     MM_BIND_TO, G_OBJECT (modem),
                                     MM_BASE_SMS_IS_3GPP, is_3gpp,
                                     MM_BASE_SMS_DEFAULT_STORAGE, default_storage,
                                     NULL));
    MM_SMS_AT (sms)->priv->modem = g_object_ref (modem);
    return sms;
}

static void
mm_sms_at_init (MMSmsAt *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_SMS_AT, MMSmsAtPrivate);
}

static void
dispose (GObject *object)
{
    MMSmsAt *self = MM_SMS_AT (object);

    g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_sms_at_parent_class)->dispose (object);
}

static void
mm_sms_at_class_init (MMSmsAtClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    MMBaseSmsClass *base_sms_class = MM_BASE_SMS_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSmsAtPrivate));

    object_class->dispose = dispose;

    base_sms_class->store = sms_store;
    base_sms_class->store_finish = sms_store_finish;
    base_sms_class->send = sms_send;
    base_sms_class->send_finish = sms_send_finish;
    base_sms_class->delete = sms_delete;
    base_sms_class->delete_finish = sms_delete_finish;
}
