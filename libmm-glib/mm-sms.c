/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Google, Inc.
 */

#include "string.h"

#include "mm-helpers.h"
#include "mm-sms.h"
#include "mm-modem.h"

/**
 * mm_sms_get_path:
 * @self: A #MMSms.
 *
 * Gets the DBus path of the #MMSms object.
 *
 * Returns: (transfer none): The DBus path of the #MMSms object.
 */
const gchar *
mm_sms_get_path (MMSms *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_sms_dup_path:
 * @self: A #MMSms.
 *
 * Gets a copy of the DBus path of the #MMSms object.
 *
 * Returns: (transfer full): The DBus path of the #MMSms object. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_path (MMSms *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_sms_get_text:
 * @self: A #MMSms.
 *
 * TODO
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_sms_dup_text() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the text, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_text (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_text (self));
}

/**
 * mm_sms_dup_text:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: (transfer full): The name of the text, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_text (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_text (self));
}

/**
 * mm_sms_get_data:
 * @self: A #MMSms.
 * @data_len: (out) Size of the output data, if any given.
 *
 * TODO
 *
 * Returns: (transfer none): The data, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
const guint8 *
mm_sms_get_data (MMSms *self,
                 gsize *data_len)
{
    GVariant *data;

    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    data = mm_gdbus_sms_get_data (MM_GDBUS_SMS (self));
    return (data ?
            g_variant_get_fixed_array (
                mm_gdbus_sms_get_data (MM_GDBUS_SMS (self)),
                data_len,
                sizeof (guchar)):
            NULL);
}

/**
 * mm_sms_dup_data:
 * @self: A #MMSms.
 * @data_len: (out) Size of the output data, if any given.
 *
 * TODO
 *
 * Returns: (transfer full): The data, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
guint8 *
mm_sms_dup_data (MMSms *self,
                 gsize *data_len)
{
    guint8 *out;
    GVariant *data_variant;
    const guint8 *orig_data;
    gsize orig_data_len = 0;

    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    /* Get a ref to ensure the variant is valid as long as we use it */
    data_variant = mm_gdbus_sms_dup_data (MM_GDBUS_SMS (self));
    if (!data_variant)
        return NULL;

    orig_data = (g_variant_get_fixed_array (
                     mm_gdbus_sms_get_data (MM_GDBUS_SMS (self)),
                     &orig_data_len,
                     sizeof (guchar)));

    out = g_new (guint8, orig_data_len);
    memcpy (out, orig_data, orig_data_len);
    g_variant_unref (data_variant);

    if (data_len)
        *data_len = orig_data_len;
    return out;
}

/**
 * mm_sms_get_number:
 * @self: A #MMSms.
 *
 * TODO
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_sms_dup_number() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the number, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_number (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_number (self));
}

/**
 * mm_sms_dup_number:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: (transfer full): The name of the number, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_number (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_number (self));
}

/**
 * mm_sms_get_smsc:
 * @self: A #MMSms.
 *
 * TODO
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_sms_dup_smsc() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the smsc, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_smsc (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_smsc (self));
}

/**
 * mm_sms_dup_smsc:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: (transfer full): The name of the smsc, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_smsc (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_smsc (self));
}

/**
 * mm_sms_get_timestamp:
 * @self: A #MMSms.
 *
 * TODO
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_sms_dup_timestamp() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the timestamp, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_timestamp (self));
}

/**
 * mm_sms_dup_timestamp:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: (transfer full): The name of the timestamp, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_timestamp (self));
}

/**
 * mm_sms_get_discharge_timestamp:
 * @self: A #MMSms.
 *
 * TODO
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_sms_dup_timestamp() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the timestamp, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_discharge_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_discharge_timestamp (self));
}

/**
 * mm_sms_dup_discharge_timestamp:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: (transfer full): The name of the timestamp, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_discharge_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_discharge_timestamp (self));
}

/**
 * mm_sms_get_validity:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: TODO
 */
guint
mm_sms_get_validity (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), 0);

    return mm_gdbus_sms_get_validity (self);
}

/**
 * mm_sms_get_class:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: TODO
 */
guint
mm_sms_get_class (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), 0);

    return mm_gdbus_sms_get_class (self);
}

/**
 * mm_sms_get_message_reference:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: TODO
 */
guint
mm_sms_get_message_reference (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), 0);

    return mm_gdbus_sms_get_message_reference (self);
}

gboolean
mm_sms_get_delivery_report_request (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), FALSE);

    return mm_gdbus_sms_get_delivery_report_request (self);
}

/**
 * mm_sms_get_delivery_state:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: TODO
 */
guint
mm_sms_get_delivery_state (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), MM_SMS_DELIVERY_STATE_UNKNOWN);

    return mm_gdbus_sms_get_delivery_state (self);
}

/**
 * mm_sms_get_state:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: TODO
 */
MMSmsState
mm_sms_get_state (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), MM_SMS_STATE_UNKNOWN);

    return mm_gdbus_sms_get_state (self);
}

/**
 * mm_sms_get_storage:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: TODO
 */
MMSmsStorage
mm_sms_get_storage (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), MM_SMS_STORAGE_UNKNOWN);

    return mm_gdbus_sms_get_storage (self);
}

/**
 * mm_sms_get_pdu_type:
 * @self: A #MMSms.
 *
 * TODO
 *
 * Returns: TODO
 */
MMSmsPduType
mm_sms_get_pdu_type (MMSms *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), MM_SMS_PDU_TYPE_UNKNOWN);

    return (MMSmsPduType)mm_gdbus_sms_get_pdu_type (self);
}

/**
 * mm_sms_send:
 * @self: A #MMSms.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * TODO
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_sms_send_finish() to get the result of the operation.
 *
 * See mm_sms_send_sync() for the synchronous, blocking version of this method.
 */
void
mm_sms_send (MMSms *self,
             GCancellable *cancellable,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_SMS (self));

    mm_gdbus_sms_call_send (self,
                            cancellable,
                            callback,
                            user_data);
}

/**
 * mm_sms_send_finish:
 * @self: A #MMSms.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_sms_send().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sms_send().
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_send_finish (MMSms *self,
                    GAsyncResult *res,
                    GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_send_finish (self, res, error);
}

/**
 * mm_sms_send_sync:
 * @self: A #MMSms.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * TODO
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sms_send() for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_send_sync (MMSms *self,
                  GCancellable *cancellable,
                  GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_send_sync (self,
                                        cancellable,
                                        error);
}

/**
 * mm_sms_store:
 * @self: A #MMSms.
 * @storage: A #MMSmsStorage specifying where to store the SMS, or #MM_SMS_STORAGE_UNKNOWN to use the default.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * TODO
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_sms_store_finish() to get the result of the operation.
 *
 * See mm_sms_store_sync() for the synchronous, blocking version of this method.
 */
void
mm_sms_store (MMSms *self,
              MMSmsStorage storage,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_SMS (self));

    mm_gdbus_sms_call_store (self,
                             storage,
                             cancellable,
                             callback,
                             user_data);
}

/**
 * mm_sms_store_finish:
 * @self: A #MMSms.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_sms_store().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sms_store().
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_store_finish (MMSms *self,
                     GAsyncResult *res,
                     GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_store_finish (self, res, error);
}

/**
 * mm_sms_store_sync:
 * @self: A #MMSms.
 * @storage: A #MMSmsStorage specifying where to store the SMS, or #MM_SMS_STORAGE_UNKNOWN to use the default.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * TODO
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sms_store() for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_store_sync (MMSms *self,
                   MMSmsStorage storage,
                   GCancellable *cancellable,
                   GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_store_sync (self,
                                         storage,
                                         cancellable,
                                         error);
}

void
mm_sms_new (GDBusConnection *connection,
            const gchar *path,
            GCancellable *cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    return mm_gdbus_sms_proxy_new (connection,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                   MM_DBUS_SERVICE,
                                   path,
                                   cancellable,
                                   callback,
                                   user_data);
}

MMSms *
mm_sms_new_finish (GAsyncResult *res,
                   GError **error)
{
    return mm_gdbus_sms_proxy_new_finish (res, error);
}

MMSms *
mm_sms_new_sync (GDBusConnection *connection,
                 const gchar *path,
                 GCancellable *cancellable,
                 GError **error)
{
    return mm_gdbus_sms_proxy_new_sync (connection,
                                        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                        MM_DBUS_SERVICE,
                                        path,
                                        cancellable,
                                        error);
}
