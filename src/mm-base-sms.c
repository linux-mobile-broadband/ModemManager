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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-sms.h"
#include "mm-auth-provider.h"
#include "mm-sms-part-3gpp.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-auth-provider.h"
#include "mm-bind.h"

static void log_object_iface_init (MMLogObjectInterface *iface);
static void bind_iface_init (MMBindInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMBaseSms, mm_base_sms, MM_GDBUS_TYPE_SMS_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_BIND, bind_iface_init))

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_BIND_TO,
    PROP_IS_MULTIPART,
    PROP_MAX_PARTS,
    PROP_MULTIPART_REFERENCE,
    PROP_IS_3GPP,
    PROP_DEFAULT_STORAGE,
    PROP_SUPPORTED_STORAGES,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum {
    SIGNAL_SET_LOCAL_MULTIPART_REFERENCE,
    SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

struct _MMBaseSmsPrivate {
    gboolean initialized;

    /* The connection to the system bus */
    GDBusConnection *connection;
    guint            dbus_id;
    /* Object to which our connection property should be bound */
    GObject *connection_parent;
    /* GObject property name of the parent's connection property to
     * which this SMS"s connection should be bound.
     */
    gchar   *connection_parent_property_name;

    /* The authorization provider */
    MMAuthProvider *authp;
    GCancellable   *authp_cancellable;

    /* The object this SMS is bound to */
    GObject *bind_to;

    /* The path where the SMS object is exported */
    gchar *path;

    /* Multipart SMS specific stuff */
    gboolean is_multipart;
    guint multipart_reference;

    /* List of SMS parts */
    guint max_parts;
    GList *parts;

    /* Set to true when all needed parts were received,
     * parsed and assembled */
    gboolean is_assembled;

    /* TRUE for 3GPP SMS; FALSE for CDMA */
    gboolean is_3gpp;

    /* SMS storage */
    MMSmsStorage  default_storage;
    GArray       *supported_storages;
};

/*****************************************************************************/

static guint
get_validity_relative (GVariant *tuple)
{
    guint type;
    GVariant *value;
    guint value_integer = 0;

    if (!tuple)
        return 0;

    g_variant_get (tuple, "(uv)", &type, &value);

    if (type == MM_SMS_VALIDITY_TYPE_RELATIVE)
        value_integer = g_variant_get_uint32 (value);

    g_variant_unref (value);

    return value_integer;
}

static gboolean
generate_3gpp_submit_pdus (MMBaseSms *self,
                           GError **error)
{
    guint i;
    guint n_parts;

    const gchar *text;
    GVariant *data_variant;
    const guint8 *data;
    gsize data_len = 0;

    MMSmsEncoding encoding;
    MMModemCharset charset;
    gchar **split_text = NULL;
    GByteArray **split_data = NULL;

    g_assert (self->priv->parts == NULL);

    text = mm_gdbus_sms_get_text (MM_GDBUS_SMS (self));
    data_variant = mm_gdbus_sms_get_data (MM_GDBUS_SMS (self));
    data = (data_variant ?
            g_variant_get_fixed_array (data_variant,
                                       &data_len,
                                       sizeof (guchar)) :
            NULL);

    g_assert (text != NULL || data != NULL);
    g_assert (!(text != NULL && data != NULL));

    if (text) {
        split_text = mm_charset_util_split_text (text, &charset, self);
        if (!split_text) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "Cannot generate PDUs: Error processing input text");
            return FALSE;
        }
        encoding = (charset == MM_MODEM_CHARSET_GSM) ? MM_SMS_ENCODING_GSM7 : MM_SMS_ENCODING_UCS2;
        n_parts = g_strv_length (split_text);
    } else if (data) {
        encoding = MM_SMS_ENCODING_8BIT;
        split_data = mm_sms_part_3gpp_util_split_data (data, data_len);
        g_assert (split_data != NULL);
        /* noop within the for */
        for (n_parts = 0; split_data[n_parts]; n_parts++);
    } else
        g_assert_not_reached ();

    g_assert (split_text != NULL || split_data != NULL);
    g_assert (!(split_text != NULL && split_data != NULL));

    if (n_parts > 255) {
        if (split_text)
            g_strfreev (split_text);
        else if (split_data) {
            i = 0;
            while (split_data[i])
                g_byte_array_unref (split_data[i++]);
            g_free (split_data);
        }

        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_TOO_MANY,
                     "Cannot generate PDUs: Text or Data too long");
        return FALSE;
    }

    /* Loop text/data chunks */
    i = 0;
    while (1) {
        MMSmsPart *part;
        gchar *part_text = NULL;
        GByteArray *part_data = NULL;

        if (split_text) {
            if (!split_text[i])
                break;
            part_text = split_text[i];
            mm_obj_dbg (self, "  processing chunk '%u' of text with '%u' bytes",
                        i, (guint) strlen (part_text));
        } else if (split_data) {
            if (!split_data[i])
                break;
            part_data = split_data[i];
            mm_obj_dbg (self, "  processing chunk '%u' of data with '%u' bytes",
                        i, part_data->len);

        } else
            g_assert_not_reached ();

        /* Create new part */
        part = mm_sms_part_new (SMS_PART_INVALID_INDEX, MM_SMS_PDU_TYPE_SUBMIT);
        mm_sms_part_take_text (part, part_text);
        mm_sms_part_take_data (part, part_data);
        mm_sms_part_set_encoding (part, encoding);
        mm_sms_part_set_number (part, mm_gdbus_sms_get_number (MM_GDBUS_SMS (self)));
        mm_sms_part_set_smsc (part, mm_gdbus_sms_get_smsc (MM_GDBUS_SMS (self)));
        mm_sms_part_set_validity_relative (part, get_validity_relative (mm_gdbus_sms_get_validity (MM_GDBUS_SMS (self))));
        mm_sms_part_set_class (part, mm_gdbus_sms_get_class (MM_GDBUS_SMS (self)));
        mm_sms_part_set_delivery_report_request (part, mm_gdbus_sms_get_delivery_report_request (MM_GDBUS_SMS (self)));

        if (n_parts > 1) {
            mm_sms_part_set_concat_reference (part, 0); /* We don't set a concat reference here */
            mm_sms_part_set_concat_sequence (part, i + 1);
            mm_sms_part_set_concat_max (part, n_parts);
            mm_obj_dbg (self, "created SMS part '%u' for multipart SMS ('%u' parts expected)", i + 1, n_parts);
        } else {
            mm_obj_dbg (self, "created SMS part for singlepart SMS");
        }

        /* Add to the list of parts */
        self->priv->parts = g_list_append (self->priv->parts, part);

        i++;
    }

    /* Free array (not contents, which were taken for the part) */
    g_free (split_text);
    g_free (split_data);

    /* Set additional multipart specific properties */
    if (n_parts > 1) {
        self->priv->is_multipart = TRUE;
        self->priv->max_parts = n_parts;
    }

    /* No more parts are expected */
    self->priv->is_assembled = TRUE;

    return TRUE;
}

static gboolean
generate_cdma_submit_pdus (MMBaseSms *self,
                           GError **error)
{
    const gchar *text;
    GVariant *data_variant;
    const guint8 *data;
    gsize data_len = 0;

    MMSmsPart *part;

    g_assert (self->priv->parts == NULL);

    text = mm_gdbus_sms_get_text (MM_GDBUS_SMS (self));
    data_variant = mm_gdbus_sms_get_data (MM_GDBUS_SMS (self));
    data = (data_variant ?
            g_variant_get_fixed_array (data_variant,
                                       &data_len,
                                       sizeof (guchar)) :
            NULL);

    g_assert (text != NULL || data != NULL);
    g_assert (!(text != NULL && data != NULL));

    /* Create new part */
    part = mm_sms_part_new (SMS_PART_INVALID_INDEX, MM_SMS_PDU_TYPE_CDMA_SUBMIT);
    if (text)
        mm_sms_part_set_text (part, text);
    else if (data) {
        GByteArray *part_data;

        part_data = g_byte_array_sized_new (data_len);
        g_byte_array_append (part_data, data, data_len);
        mm_sms_part_take_data (part, part_data);
    } else
        g_assert_not_reached ();
    mm_sms_part_set_encoding (part, data ? MM_SMS_ENCODING_8BIT : MM_SMS_ENCODING_UNKNOWN);
    mm_sms_part_set_number (part, mm_gdbus_sms_get_number (MM_GDBUS_SMS (self)));

    /* If creating a CDMA SMS part but we don't have a Teleservice ID, we default to WMT */
    if (mm_gdbus_sms_get_teleservice_id (MM_GDBUS_SMS (self)) == MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN) {
        mm_obj_dbg (self, "defaulting to WMT teleservice ID when creating SMS part");
        mm_sms_part_set_cdma_teleservice_id (part, MM_SMS_CDMA_TELESERVICE_ID_WMT);
    } else
        mm_sms_part_set_cdma_teleservice_id (part, mm_gdbus_sms_get_teleservice_id (MM_GDBUS_SMS (self)));

    mm_sms_part_set_cdma_service_category (part, mm_gdbus_sms_get_service_category (MM_GDBUS_SMS (self)));

    mm_obj_dbg (self, "created SMS part for CDMA SMS");

    /* Add to the list of parts */
    self->priv->parts = g_list_append (self->priv->parts, part);

    /* No more parts are expected */
    self->priv->is_assembled = TRUE;

    return TRUE;
}

static gboolean
generate_submit_pdus (MMBaseSms *self,
                      GError **error)
{
    return self->priv->is_3gpp ? generate_3gpp_submit_pdus (self, error) : generate_cdma_submit_pdus (self, error);
}

/*****************************************************************************/
/* Store SMS (DBus call handling) */

typedef struct {
    MMBaseSms             *self;
    GDBusMethodInvocation *invocation;
    MMSmsStorage           storage;
} HandleStoreContext;

static void
handle_store_context_free (HandleStoreContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleStoreContext, ctx);
}

static void
handle_store_ready (MMBaseSms          *self,
                    GAsyncResult       *res,
                    HandleStoreContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_SMS_GET_CLASS (self)->store_finish (self, res, &error)) {
        mm_obj_warn (self, "failed storing SMS message: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_store_context_free (ctx);
        return;
    }

    mm_gdbus_sms_set_storage (MM_GDBUS_SMS (ctx->self), ctx->storage);

    /* Transition from Unknown->Stored for SMS which were created by the user */
    if (mm_gdbus_sms_get_state (MM_GDBUS_SMS (ctx->self)) == MM_SMS_STATE_UNKNOWN)
        mm_gdbus_sms_set_state (MM_GDBUS_SMS (ctx->self), MM_SMS_STATE_STORED);

    mm_obj_info (self, "stored SMS message");
    mm_gdbus_sms_complete_store (MM_GDBUS_SMS (ctx->self), ctx->invocation);
    handle_store_context_free (ctx);
}

static gboolean
prepare_sms_to_be_stored (MMBaseSms  *self,
                          GError    **error)
{
    /* Create parts if we did not create them already before (e.g. when
     * sending) */
    if (!self->priv->parts && !generate_submit_pdus (self, error)) {
        g_prefix_error (error, "Cannot create submit PDUs: ");
        return FALSE;
    }

    /* If the message is a multipart message, we need to set a proper
     * multipart reference. When sending a message which wasn't stored
     * yet, we chose a random multipart reference, but that doesn't work
     * when storing locally, as we could collide with the references used
     * in other existing messages. */
    if (self->priv->is_multipart) {
        const gchar *number;

        number = mm_gdbus_sms_get_number (MM_GDBUS_SMS (self));
        g_signal_emit (self,
                       signals[SIGNAL_SET_LOCAL_MULTIPART_REFERENCE],
                       0,
                       number);
        if (self->priv->multipart_reference == 0) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_TOO_MANY,
                         "Cannot create multipart SMS: No valid multipart reference "
                         "available for destination number '%s'",
                         number);
            return FALSE;
        }
    }

    return TRUE;
}

static void
handle_store_auth_ready (MMAuthProvider     *authp,
                         GAsyncResult       *res,
                         HandleStoreContext *ctx)
{
    GError *error = NULL;
    gboolean storage_supported = FALSE;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_store_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to store SMS message in storage '%s'...",
                 mm_sms_storage_get_string (ctx->storage));

    /* First of all, check if we already have the SMS stored. */
    if (mm_base_sms_get_storage (ctx->self) != MM_SMS_STORAGE_UNKNOWN) {
        /* Check if SMS stored in some other storage */
        if (mm_base_sms_get_storage (ctx->self) == ctx->storage) {
            /* Good, same storage */
            mm_obj_info (ctx->self, "SMS message already stored");
            mm_gdbus_sms_complete_store (MM_GDBUS_SMS (ctx->self), ctx->invocation);
        } else {
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "SMS is already stored in storage '%s', cannot store it in storage '%s'",
                                 mm_sms_storage_get_string (mm_base_sms_get_storage (ctx->self)),
                                 mm_sms_storage_get_string (ctx->storage));
            mm_obj_warn (ctx->self, "failed storing SMS message: %s", error->message);
            mm_dbus_method_invocation_take_error (ctx->invocation, error);
        }
        handle_store_context_free (ctx);
        return;
    }

    /* Check if the requested storage is allowed for storing */
    if (ctx->self->priv->supported_storages) {
        guint i;

        for (i = 0; i < ctx->self->priv->supported_storages->len; i++) {
            if (ctx->storage == g_array_index (ctx->self->priv->supported_storages, MMSmsStorage, i)) {
                storage_supported = TRUE;
                break;
            }
        }
    }

    if (!storage_supported) {
        g_set_error (&error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Storage '%s' is not supported for storing",
                     mm_sms_storage_get_string (ctx->storage));
        mm_obj_warn (ctx->self, "failed storing SMS message: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_store_context_free (ctx);
        return;
    }

    /* If not stored, check if we do support doing it */
    if (!MM_BASE_SMS_GET_CLASS (ctx->self)->store ||
        !MM_BASE_SMS_GET_CLASS (ctx->self)->store_finish) {
        mm_obj_warn (ctx->self, "failed storing SMS message: unsupported");
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Storing SMS is not supported by this modem");
        handle_store_context_free (ctx);
        return;
    }

    /* Prepare the SMS to be stored, creating the PDU list if required */
    if (!prepare_sms_to_be_stored (ctx->self, &error)) {
        mm_obj_warn (ctx->self, "failed preparing SMS message to be stored: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_store_context_free (ctx);
        return;
    }

    MM_BASE_SMS_GET_CLASS (ctx->self)->store (ctx->self,
                                              ctx->storage,
                                              (GAsyncReadyCallback)handle_store_ready,
                                              ctx);
}

static gboolean
handle_store (MMBaseSms             *self,
              GDBusMethodInvocation *invocation,
              guint32                storage)
{
    HandleStoreContext *ctx;

    ctx = g_slice_new0 (HandleStoreContext);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    ctx->storage = (MMSmsStorage)storage;

    if (ctx->storage == MM_SMS_STORAGE_UNKNOWN) {
        /* We'll set now the proper storage, taken from the default mem2 one */
        ctx->storage = self->priv->default_storage;
        g_assert (ctx->storage != MM_SMS_STORAGE_UNKNOWN);
    }

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_MESSAGING,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_store_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Send SMS (DBus call handling) */

typedef struct {
    MMBaseSms             *self;
    GDBusMethodInvocation *invocation;
} HandleSendContext;

static void
handle_send_context_free (HandleSendContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSendContext, ctx);
}

static void
handle_send_ready (MMBaseSms         *self,
                   GAsyncResult      *res,
                   HandleSendContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_SMS_GET_CLASS (self)->send_finish (self, res, &error)) {
        mm_obj_warn (self, "failed sending SMS message: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_context_free (ctx);
        return;
    }

    /* Transition from Unknown->Sent or Stored->Sent */
    if (mm_gdbus_sms_get_state (MM_GDBUS_SMS (ctx->self)) == MM_SMS_STATE_UNKNOWN ||
        mm_gdbus_sms_get_state (MM_GDBUS_SMS (ctx->self)) == MM_SMS_STATE_STORED) {
        GList *l;

        /* Update state */
        mm_gdbus_sms_set_state (MM_GDBUS_SMS (ctx->self), MM_SMS_STATE_SENT);
        /* Grab last message reference */
        l = g_list_last (mm_base_sms_get_parts (ctx->self));
        mm_gdbus_sms_set_message_reference (MM_GDBUS_SMS (ctx->self),
                                            mm_sms_part_get_message_reference ((MMSmsPart *)l->data));
    }

    mm_obj_info (self, "sent SMS message");
    mm_gdbus_sms_complete_send (MM_GDBUS_SMS (ctx->self), ctx->invocation);
    handle_send_context_free (ctx);
}

static gboolean
prepare_sms_to_be_sent (MMBaseSms  *self,
                        GError    **error)
{
    GList *l;

    /* If we created the parts when storing, we're fine already */
    if (self->priv->parts)
        return TRUE;

    if (!generate_submit_pdus (self, error)) {
        g_prefix_error (error, "Cannot create submit PDUs: ");
        return FALSE;
    }

    /* If the message is a multipart message, we need to set a proper
     * multipart reference. When sending a message which wasn't stored
     * yet, we can just get a random multipart reference. */
    if (self->priv->is_multipart) {
        self->priv->multipart_reference = g_random_int_range (1, 255);
        for (l = self->priv->parts; l; l = g_list_next (l)) {
            mm_sms_part_set_concat_reference ((MMSmsPart *)l->data,
                                              self->priv->multipart_reference);
        }
    }

    return TRUE;
}

static void
handle_send_auth_ready (MMAuthProvider    *authp,
                        GAsyncResult      *res,
                        HandleSendContext *ctx)
{
    MMSmsState  state;
    GError     *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_context_free (ctx);
        return;
    }

    /* We can only send SMS created by the user */
    state = mm_gdbus_sms_get_state (MM_GDBUS_SMS (ctx->self));
    if (state == MM_SMS_STATE_RECEIVED || state == MM_SMS_STATE_RECEIVING) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "This SMS was received, cannot send it");
        handle_send_context_free (ctx);
        return;
    }

    /* Don't allow sending the same SMS multiple times, we would lose the message reference */
    if (state == MM_SMS_STATE_SENT) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "This SMS was already sent, cannot send it again");
        handle_send_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to send SMS message...");

    /* Prepare the SMS to be sent, creating the PDU list if required */
    if (!prepare_sms_to_be_sent (ctx->self, &error)) {
        mm_obj_warn (ctx->self, "failed preparing SMS message to be sent: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_SMS_GET_CLASS (ctx->self)->send ||
        !MM_BASE_SMS_GET_CLASS (ctx->self)->send_finish) {
        mm_obj_warn (ctx->self, "failed sending SMS message: unsupported");
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Sending SMS is not supported by this modem");
        handle_send_context_free (ctx);
        return;
    }

    MM_BASE_SMS_GET_CLASS (ctx->self)->send (ctx->self,
                                             (GAsyncReadyCallback)handle_send_ready,
                                             ctx);
}

static gboolean
handle_send (MMBaseSms             *self,
             GDBusMethodInvocation *invocation)
{
    HandleSendContext *ctx;

    ctx = g_slice_new0 (HandleSendContext);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_MESSAGING,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_send_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/

void
mm_base_sms_export (MMBaseSms *self)
{
    gchar *path;

    path = g_strdup_printf (MM_DBUS_SMS_PREFIX "/%d", self->priv->dbus_id);
    g_object_set (self,
                  MM_BASE_SMS_PATH, path,
                  NULL);
    g_free (path);
}

void
mm_base_sms_unexport (MMBaseSms *self)
{
    g_object_set (self,
                  MM_BASE_SMS_PATH, NULL,
                  NULL);
}

/*****************************************************************************/

static void
sms_dbus_export (MMBaseSms *self)
{
    GError *error = NULL;

    /* Handle method invocations */
    g_signal_connect (self,
                      "handle-store",
                      G_CALLBACK (handle_store),
                      NULL);
    g_signal_connect (self,
                      "handle-send",
                      G_CALLBACK (handle_send),
                      NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_obj_warn (self, "couldn't export SMS: %s", error->message);
        g_error_free (error);
    }
}

static void
sms_dbus_unexport (MMBaseSms *self)
{
    /* Only unexport if currently exported */
    if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
}

/*****************************************************************************/

const gchar *
mm_base_sms_get_path (MMBaseSms *self)
{
    return self->priv->path;
}

MMSmsStorage
mm_base_sms_get_storage (MMBaseSms *self)
{
    return mm_gdbus_sms_get_storage (MM_GDBUS_SMS (self));
}

gboolean
mm_base_sms_is_multipart (MMBaseSms *self)
{
    return self->priv->is_multipart;
}

guint
mm_base_sms_get_max_parts (MMBaseSms *self)
{
    return self->priv->max_parts;
}

guint
mm_base_sms_get_multipart_reference (MMBaseSms *self)
{
    g_return_val_if_fail (self->priv->is_multipart, 0);

    return self->priv->multipart_reference;
}

void
mm_base_sms_set_multipart_reference (MMBaseSms *self, guint reference)
{
    GList *l;

    g_return_if_fail (self->priv->is_multipart);
    g_return_if_fail (reference > 0);
    g_return_if_fail (reference <= 255);
    g_return_if_fail (self->priv->multipart_reference == 0);

    self->priv->multipart_reference = reference;
    for (l = self->priv->parts; l; l = g_list_next (l)) {
        mm_sms_part_set_concat_reference ((MMSmsPart *)l->data,
                                          self->priv->multipart_reference);
    }
}

gboolean
mm_base_sms_multipart_is_complete (MMBaseSms *self)
{
    return (g_list_length (self->priv->parts) == self->priv->max_parts);
}

gboolean
mm_base_sms_multipart_is_assembled (MMBaseSms *self)
{
    return self->priv->is_assembled;
}

/*****************************************************************************/

static guint
cmp_sms_part_index (MMSmsPart *part,
                    gpointer user_data)
{
    return (GPOINTER_TO_UINT (user_data) - mm_sms_part_get_index (part));
}

gboolean
mm_base_sms_has_part_index (MMBaseSms *self,
                            guint index)
{
    return !!g_list_find_custom (self->priv->parts,
                                 GUINT_TO_POINTER (index),
                                 (GCompareFunc)cmp_sms_part_index);
}

GList *
mm_base_sms_get_parts (MMBaseSms *self)
{
    return self->priv->parts;
}

/*****************************************************************************/

gboolean
mm_base_sms_delete_finish (MMBaseSms *self,
                           GAsyncResult *res,
                           GError **error)
{
    if (MM_BASE_SMS_GET_CLASS (self)->delete_finish) {
        gboolean deleted;

        deleted = MM_BASE_SMS_GET_CLASS (self)->delete_finish (self, res, error);
        if (deleted)
            /* We do change the state of this SMS back to UNKNOWN, as it is no
             * longer stored in the device */
            mm_gdbus_sms_set_state (MM_GDBUS_SMS (self), MM_SMS_STATE_UNKNOWN);

        return deleted;
    }

    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_base_sms_delete (MMBaseSms *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    if (MM_BASE_SMS_GET_CLASS (self)->delete &&
        MM_BASE_SMS_GET_CLASS (self)->delete_finish) {
        MM_BASE_SMS_GET_CLASS (self)->delete (self, callback, user_data);
        return;
    }

    g_task_report_new_error (self,
                             callback,
                             user_data,
                             mm_base_sms_delete,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "Deleting SMS is not supported by this modem");
}

/*****************************************************************************/

static void
initialize_sms (MMBaseSms *self)
{
    MMSmsPart *part;
    guint      validity_relative;

    /* Some of the fields of the SMS object may be initialized as soon as we have
     * one part already available, even if it's not exactly the first one */
    g_assert (self->priv->parts);
    part = (MMSmsPart *)(self->priv->parts->data);

    /* Prepare for validity tuple */
    validity_relative = mm_sms_part_get_validity_relative (part);

    g_object_set (self,
                  "pdu-type",            mm_sms_part_get_pdu_type (part),
                  "smsc",                mm_sms_part_get_smsc (part),
                  "class",               mm_sms_part_get_class (part),
                  "teleservice-id",      mm_sms_part_get_cdma_teleservice_id (part),
                  "service-category",    mm_sms_part_get_cdma_service_category (part),
                  "number",              mm_sms_part_get_number (part),
                  "validity",            (validity_relative ?
                                          g_variant_new ("(uv)", MM_SMS_VALIDITY_TYPE_RELATIVE, g_variant_new_uint32 (validity_relative)) :
                                          g_variant_new ("(uv)", MM_SMS_VALIDITY_TYPE_UNKNOWN, g_variant_new_boolean (FALSE))),
                  "timestamp",           mm_sms_part_get_timestamp (part),
                  "discharge-timestamp", mm_sms_part_get_discharge_timestamp (part),
                  "delivery-state",      mm_sms_part_get_delivery_state (part),
                  NULL);
}

static gboolean
assemble_sms (MMBaseSms  *self,
              GError    **error)
{
    GList      *l;
    guint       idx;
    MMSmsPart **sorted_parts;
    GString    *fulltext;
    GByteArray *fulldata;

    sorted_parts = g_new0 (MMSmsPart *, self->priv->max_parts);

    /* Note that sequence in multipart messages start with '1', while singlepart
     * messages have '0' as sequence. */

    if (self->priv->max_parts == 1) {
        if (g_list_length (self->priv->parts) != 1) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Single part message with multiple parts (%u) found",
                         g_list_length (self->priv->parts));
            g_free (sorted_parts);
            return FALSE;
        }

        sorted_parts[0] = (MMSmsPart *)self->priv->parts->data;
    } else {
        /* Check if we have duplicate parts */
        for (l = self->priv->parts; l; l = g_list_next (l)) {
            idx = mm_sms_part_get_concat_sequence ((MMSmsPart *)l->data);

            if (idx < 1 || idx > self->priv->max_parts) {
                mm_obj_warn (self, "invalid part index (%u) found, ignoring", idx);
                continue;
            }

            if (sorted_parts[idx - 1]) {
                mm_obj_warn (self, "duplicate part index (%u) found, ignoring", idx);
                continue;
            }

            /* Add the part to the proper index */
            sorted_parts[idx - 1] = (MMSmsPart *)l->data;
        }
    }

    fulltext = g_string_new ("");
    fulldata = g_byte_array_sized_new (160 * self->priv->max_parts);

    /* Assemble text and data from all parts. Now 'idx' is the index of the
     * array, so for multipart messages the real index of the part is 'idx + 1'
     */
    for (idx = 0; idx < self->priv->max_parts; idx++) {
        const gchar *parttext;
        const GByteArray *partdata;

        if (!sorted_parts[idx]) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Cannot assemble SMS, missing part at index (%u)",
                         self->priv->max_parts == 1 ? idx : idx + 1);
            g_string_free (fulltext, TRUE);
            g_byte_array_free (fulldata, TRUE);
            g_free (sorted_parts);
            return FALSE;
        }

        /* When the user creates the SMS, it will have either 'text' or 'data',
         * not both. Also status report PDUs may not have neither text nor data. */
        parttext = mm_sms_part_get_text (sorted_parts[idx]);
        partdata = mm_sms_part_get_data (sorted_parts[idx]);

        if (!parttext && !partdata &&
            mm_sms_part_get_pdu_type (sorted_parts[idx]) != MM_SMS_PDU_TYPE_STATUS_REPORT) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Cannot assemble SMS, part at index (%u) has neither text nor data",
                         self->priv->max_parts == 1 ? idx : idx + 1);
            g_string_free (fulltext, TRUE);
            g_byte_array_free (fulldata, TRUE);
            g_free (sorted_parts);
            return FALSE;
        }

        if (parttext)
            g_string_append (fulltext, parttext);
        if (partdata)
            g_byte_array_append (fulldata, partdata->data, partdata->len);
    }

    /* If we got all parts, we also have the first one always */
    g_assert (sorted_parts[0] != NULL);

    /* If we got everything, assemble the text! */
    g_object_set (self,
                  "text", fulltext->str,
                  "data", g_variant_new_from_data (G_VARIANT_TYPE ("ay"),
                                                   fulldata->data,
                                                   fulldata->len * sizeof (guint8),
                                                   TRUE,
                                                   (GDestroyNotify) g_byte_array_unref,
                                                   g_byte_array_ref (fulldata)),
                  /* delivery report request and message reference taken always from the last part */
                  "message-reference",       mm_sms_part_get_message_reference (sorted_parts[self->priv->max_parts - 1]),
                  "delivery-report-request", mm_sms_part_get_delivery_report_request (sorted_parts[self->priv->max_parts - 1]),
                  NULL);

    g_string_free (fulltext, TRUE);
    g_byte_array_unref (fulldata);
    g_free (sorted_parts);

    self->priv->is_assembled = TRUE;

    return TRUE;
}

/*****************************************************************************/

static guint
cmp_sms_part_sequence (MMSmsPart *a,
                       MMSmsPart *b)
{
    return (mm_sms_part_get_concat_sequence (a) - mm_sms_part_get_concat_sequence (b));
}

gboolean
mm_base_sms_multipart_take_part (MMBaseSms *self,
                                 MMSmsPart *part,
                                 GError **error)
{
    if (!self->priv->is_multipart) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "This SMS is not a multipart message");
        return FALSE;
    }

    if (g_list_length (self->priv->parts) >= self->priv->max_parts) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Already took %u parts, cannot take more",
                     g_list_length (self->priv->parts));
        return FALSE;
    }

    if (g_list_find_custom (self->priv->parts,
                            part,
                            (GCompareFunc)cmp_sms_part_sequence)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot take part, sequence %u already taken",
                     mm_sms_part_get_concat_sequence (part));
        return FALSE;
    }

    if (mm_sms_part_get_concat_sequence (part) > self->priv->max_parts) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot take part with sequence %u, maximum is %u",
                     mm_sms_part_get_concat_sequence (part),
                     self->priv->max_parts);
        return FALSE;
    }

    /* Insert sorted by concat sequence */
    self->priv->parts = g_list_insert_sorted (self->priv->parts,
                                              part,
                                              (GCompareFunc)cmp_sms_part_sequence);

    /* If this is the first part we take, initialize common SMS fields */
    if (g_list_length (self->priv->parts) == 1)
        initialize_sms (self);

    /* We only populate contents when the multipart SMS is complete */
    if (mm_base_sms_multipart_is_complete (self)) {
        GError *inner_error = NULL;

        if (!assemble_sms (self, &inner_error)) {
            /* We DO NOT propagate the error. The part was properly taken
             * so ownership passed to the MMBaseSms object. */
            mm_obj_warn (self, "couldn't assemble SMS: %s", inner_error->message);
            g_error_free (inner_error);
        } else {
            /* Completed AND assembled
             * Change state RECEIVING->RECEIVED, and signal completeness */
            if (mm_gdbus_sms_get_state (MM_GDBUS_SMS (self)) == MM_SMS_STATE_RECEIVING)
                mm_gdbus_sms_set_state (MM_GDBUS_SMS (self), MM_SMS_STATE_RECEIVED);
        }
    }

    return TRUE;
}

gboolean
mm_base_sms_singlepart_init (MMBaseSms     *self,
                             MMSmsState     state,
                             MMSmsStorage   storage,
                             MMSmsPart     *part,
                             GError       **error)
{
    g_assert (self->priv->initialized == FALSE);

    g_object_set (self,
                  "state",   state,
                  "storage", storage,
                  NULL);

    /* Keep the single part in the list */
    self->priv->parts = g_list_prepend (self->priv->parts, part);

    /* Initialize common SMS fields */
    initialize_sms (self);

    if (!assemble_sms (self, error)) {
        /* Note: we need to remove the part from the list, as we really didn't
         * take it, and therefore the caller is responsible for freeing it. */
        self->priv->parts = g_list_remove (self->priv->parts, part);
        return FALSE;
    }

    /* Only export once properly created */
    self->priv->initialized = TRUE;
    mm_base_sms_export (self);
    return TRUE;
}

gboolean
mm_base_sms_multipart_init (MMBaseSms     *self,
                            MMSmsState     state,
                            MMSmsStorage   storage,
                            guint          reference,
                            guint          max_parts,
                            MMSmsPart     *first_part,
                            GError       **error)
{
    g_assert (self->priv->initialized == FALSE);

    /* If this is the first part of a RECEIVED SMS, we overwrite the state
     * as RECEIVING, to indicate that it is not completed yet. */
    if (state == MM_SMS_STATE_RECEIVED)
        state = MM_SMS_STATE_RECEIVING;

    g_object_set (self,
                  MM_BASE_SMS_IS_MULTIPART,        TRUE,
                  MM_BASE_SMS_MAX_PARTS,           max_parts,
                  MM_BASE_SMS_MULTIPART_REFERENCE, reference,
                  "number",                        mm_sms_part_get_number (first_part),
                  "state",                         state,
                  "storage",                       storage,
                  "validity",                      g_variant_new ("(uv)",
                                                                  MM_SMS_VALIDITY_TYPE_UNKNOWN,
                                                                  g_variant_new_boolean (FALSE)),
                  NULL);

    if (!mm_base_sms_multipart_take_part (self, first_part, error))
        return FALSE;

    /* We do export incomplete multipart messages, in order to be able to
     *  request removal of all parts of those multipart SMS that will never
     *  get completed.
     * Only the STATE of the SMS object will be valid in the exported DBus
     *  interface.*/
    self->priv->initialized = TRUE;
    mm_base_sms_export (self);

    return TRUE;
}

gboolean
mm_base_sms_init_from_properties (MMBaseSms        *self,
                                  MMSmsProperties  *props,
                                  GError          **error)
{
    const gchar *text;
    GByteArray *data;

    g_assert (self->priv->initialized == FALSE);

    text = mm_sms_properties_get_text (props);
    data = mm_sms_properties_peek_data_bytearray (props);

    /* Don't create SMS from properties if either (text|data) or number is missing */
    if (!mm_sms_properties_get_number (props) ||
        (!text && !data)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create SMS: mandatory parameter '%s' is missing",
                     (!mm_sms_properties_get_number (props)?
                      "number" : "text' or 'data"));
        return FALSE;
    }

    /* Don't create SMS from properties if both text and data are given */
    if (text && data) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create SMS: both 'text' and 'data' given");
        return FALSE;
    }

    g_object_set (self,
                  "state",    MM_SMS_STATE_UNKNOWN,
                  "storage",  MM_SMS_STORAGE_UNKNOWN,
                  "number",   mm_sms_properties_get_number (props),
                  "pdu-type", (mm_sms_properties_get_teleservice_id (props) == MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN ?
                               MM_SMS_PDU_TYPE_SUBMIT :
                               MM_SMS_PDU_TYPE_CDMA_SUBMIT),
                  "text",     text,
                  "data",     (data ?
                               g_variant_new_from_data (G_VARIANT_TYPE ("ay"),
                                                        data->data,
                                                        data->len * sizeof (guint8),
                                                        TRUE,
                                                        (GDestroyNotify) g_byte_array_unref,
                                                        g_byte_array_ref (data)) :
                               NULL),
                  "smsc",     mm_sms_properties_get_smsc (props),
                  "class",    mm_sms_properties_get_class (props),
                  "teleservice-id",          mm_sms_properties_get_teleservice_id (props),
                  "service-category",        mm_sms_properties_get_service_category (props),
                  "delivery-report-request", mm_sms_properties_get_delivery_report_request (props),
                  "delivery-state",          MM_SMS_DELIVERY_STATE_UNKNOWN,
                  "validity", (mm_sms_properties_get_validity_type (props) == MM_SMS_VALIDITY_TYPE_RELATIVE ?
                               g_variant_new ("(uv)", MM_SMS_VALIDITY_TYPE_RELATIVE, g_variant_new_uint32 (mm_sms_properties_get_validity_relative (props))) :
                               g_variant_new ("(uv)", MM_SMS_VALIDITY_TYPE_UNKNOWN, g_variant_new_boolean (FALSE))),
                  NULL);

    /* Only export once properly initialized */
    self->priv->initialized = TRUE;
    mm_base_sms_export (self);

    return TRUE;
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMBaseSms *self;

    self = MM_BASE_SMS (_self);
    return g_strdup_printf ("sms%u", self->priv->dbus_id);
}

/*****************************************************************************/

/* FIXME: use g_array_copy() when glib min version is >= 2.62 */
static GArray *
copy_storage_array (GArray *orig)
{
    GArray *copy = NULL;

    if (orig) {
        copy = g_array_sized_new (FALSE, FALSE, sizeof (MMSmsStorage), orig->len);
        g_array_append_vals (copy, orig->data, orig->len);
    }
    return copy;
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBaseSms *self = MM_BASE_SMS (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->path)
            sms_dbus_unexport (self);
        else if (self->priv->connection)
            sms_dbus_export (self);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->connection)
            sms_dbus_unexport (self);
        else if (self->priv->path)
            sms_dbus_export (self);
        break;
    case PROP_BIND_TO:
        g_clear_object (&self->priv->bind_to);
        self->priv->bind_to = g_value_dup_object (value);
        mm_bind_to (MM_BIND (self), MM_BASE_SMS_CONNECTION, self->priv->bind_to);
        break;
    case PROP_IS_MULTIPART:
        self->priv->is_multipart = g_value_get_boolean (value);
        break;
    case PROP_MAX_PARTS:
        self->priv->max_parts = g_value_get_uint (value);
        break;
    case PROP_MULTIPART_REFERENCE:
        self->priv->multipart_reference = g_value_get_uint (value);
        break;
    case PROP_IS_3GPP:
        self->priv->is_3gpp = g_value_get_boolean (value);
        break;
    case PROP_DEFAULT_STORAGE:
        self->priv->default_storage = g_value_get_enum (value);
        break;
    case PROP_SUPPORTED_STORAGES:
        /* Copy the array rather than just ref-ing it */
        g_clear_pointer (&self->priv->supported_storages, (GDestroyNotify)g_array_unref);
        self->priv->supported_storages = copy_storage_array (g_value_get_boxed (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBaseSms *self = MM_BASE_SMS (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_BIND_TO:
        g_value_set_object (value, self->priv->bind_to);
        break;
    case PROP_IS_MULTIPART:
        g_value_set_boolean (value, self->priv->is_multipart);
        break;
    case PROP_MAX_PARTS:
        g_value_set_uint (value, self->priv->max_parts);
        break;
    case PROP_MULTIPART_REFERENCE:
        g_value_set_uint (value, self->priv->multipart_reference);
        break;
    case PROP_IS_3GPP:
        g_value_set_boolean (value, self->priv->is_3gpp);
        break;
    case PROP_DEFAULT_STORAGE:
        g_value_set_enum (value, self->priv->default_storage);
        break;
    case PROP_SUPPORTED_STORAGES:
        g_value_set_boxed (value, copy_storage_array (self->priv->supported_storages));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_base_sms_init (MMBaseSms *self)
{
    static guint id = 0;

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BASE_SMS, MMBaseSmsPrivate);
    /* Defaults */
    self->priv->max_parts = 1;

    /* Each SMS is given a unique id to build its own DBus path */
    self->priv->dbus_id = id++;

    /* Setup authorization provider */
    self->priv->authp = mm_auth_provider_get ();
    self->priv->authp_cancellable = g_cancellable_new ();
}

static void
finalize (GObject *object)
{
    MMBaseSms *self = MM_BASE_SMS (object);

    g_list_free_full (self->priv->parts, (GDestroyNotify)mm_sms_part_free);
    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_base_sms_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseSms *self = MM_BASE_SMS (object);

    if (self->priv->connection) {
        /* If we arrived here with a valid connection, make sure we unexport
         * the object */
        sms_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    g_clear_object (&self->priv->bind_to);
    g_cancellable_cancel (self->priv->authp_cancellable);
    g_clear_object (&self->priv->authp_cancellable);
    g_clear_pointer (&self->priv->supported_storages, (GDestroyNotify)g_array_unref);

    G_OBJECT_CLASS (mm_base_sms_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
bind_iface_init (MMBindInterface *iface)
{
}

static void
mm_base_sms_class_init (MMBaseSmsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseSmsPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BASE_SMS_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_BASE_SMS_PATH,
                             "Path",
                             "DBus path of the SMS",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    g_object_class_override_property (object_class, PROP_BIND_TO, MM_BIND_TO);

    properties[PROP_IS_MULTIPART] =
        g_param_spec_boolean (MM_BASE_SMS_IS_MULTIPART,
                              "Is multipart",
                              "Flag specifying if the SMS is multipart",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_IS_MULTIPART, properties[PROP_IS_MULTIPART]);

    properties[PROP_MAX_PARTS] =
        g_param_spec_uint (MM_BASE_SMS_MAX_PARTS,
                           "Max parts",
                           "Maximum number of parts composing this SMS",
                           1,255, 1,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MAX_PARTS, properties[PROP_MAX_PARTS]);

    properties[PROP_MULTIPART_REFERENCE] =
        g_param_spec_uint (MM_BASE_SMS_MULTIPART_REFERENCE,
                           "Multipart reference",
                           "Common reference for all parts in the multipart SMS",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MULTIPART_REFERENCE, properties[PROP_MULTIPART_REFERENCE]);

    properties[PROP_IS_3GPP] =
        g_param_spec_boolean (MM_BASE_SMS_IS_3GPP,
                              "Is 3GPP",
                              "Whether the SMS is a 3GPP one or a CDMA one",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_IS_3GPP, properties[PROP_IS_3GPP]);

    properties[PROP_DEFAULT_STORAGE] =
        g_param_spec_enum (MM_BASE_SMS_DEFAULT_STORAGE,
                           "Default storage",
                           "Default SMS storage",
                           MM_TYPE_SMS_STORAGE,
                           MM_SMS_STORAGE_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_DEFAULT_STORAGE, properties[PROP_DEFAULT_STORAGE]);

    properties[PROP_SUPPORTED_STORAGES] =
        g_param_spec_boxed (MM_BASE_SMS_SUPPORTED_STORAGES,
                            "Supported storages",
                            "Array of MMSmsStorage indicating supported storages for storing the SMS",
                            G_TYPE_ARRAY,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_SUPPORTED_STORAGES, properties[PROP_SUPPORTED_STORAGES]);

    /* Signals */
    signals[SIGNAL_SET_LOCAL_MULTIPART_REFERENCE] =
        g_signal_new (MM_BASE_SMS_SET_LOCAL_MULTIPART_REFERENCE,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMBaseSmsClass, set_local_multipart_reference),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 0);
}
