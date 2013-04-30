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
 * SECTION: mm-sms
 * @title: MMSms
 * @short_description: The SMS interface
 *
 * The #MMSms is an object providing access to the methods, signals and
 * properties of the SMS interface.
 *
 * When the SMS is exposed and available in the bus, it is ensured that at
 * least this interface is also available.
 */

G_DEFINE_TYPE (MMSms, mm_sms, MM_GDBUS_TYPE_SMS_PROXY)

/*****************************************************************************/

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
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

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

    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_sms_get_text:
 * @self: A #MMSms.
 *
 * Gets the message text, in UTF-8.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_sms_dup_text() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The message text, or %NULL if it doesn't contain any (e.g. contains data instead).
 */
const gchar *
mm_sms_get_text (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_text (MM_GDBUS_SMS (self)));
}

/**
 * mm_sms_dup_text:
 * @self: A #MMSms.
 *
 * Gets the message text, in UTF-8.
 *
 * Returns: (transfer full): The message text, or %NULL if it doesn't contain any (e.g. contains data instead). The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_text (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_text (MM_GDBUS_SMS (self)));
}

/*****************************************************************************/

/**
 * mm_sms_get_data:
 * @self: A #MMSms.
 * @data_len: (out): Size of the output data, if any given.
 *
 * Gets the message data.
 *
 * Returns: (transfer none): The message data, or %NULL if it doesn't contain any (e.g. contains text instead).
 */
const guint8 *
mm_sms_get_data (MMSms *self,
                 gsize *data_len)
{
    GVariant *data;

    g_return_val_if_fail (MM_IS_SMS (self), NULL);
    g_return_val_if_fail (data_len != NULL, NULL);

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
 * Gets the message data.
 *
 * Returns: (transfer full): The message data, or %NULL if it doesn't contain any (e.g. contains text instead). The returned value should be freed with g_free().
 */
guint8 *
mm_sms_dup_data (MMSms *self,
                 gsize *data_len)
{
    guint8 *out;
    GVariant *data_variant;
    const guint8 *orig_data;
    gsize orig_data_len = 0;

    g_return_val_if_fail (MM_IS_SMS (self), NULL);

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

/*****************************************************************************/

/**
 * mm_sms_get_number:
 * @self: A #MMSms.
 *
 * Gets the number to which the message is addressed.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_sms_dup_number() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The number, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_number (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_number (MM_GDBUS_SMS (self)));
}

/**
 * mm_sms_dup_number:
 * @self: A #MMSms.
 *
 * Gets the number to which the message is addressed.
 *
 * Returns: (transfer full): The number, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_number (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_number (MM_GDBUS_SMS (self)));
}

/*****************************************************************************/

/**
 * mm_sms_get_smsc:
 * @self: A #MMSms.
 *
 * Gets the SMS service center number.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_sms_dup_smsc() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The number of the SMSC, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_smsc (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_smsc (MM_GDBUS_SMS (self)));
}

/**
 * mm_sms_dup_smsc:
 * @self: A #MMSms.
 *
 * Gets the SMS service center number.
 *
 * Returns: (transfer full): The number of the SMSC, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_smsc (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_smsc (MM_GDBUS_SMS (self)));
}

/*****************************************************************************/

/**
 * mm_sms_get_timestamp:
 * @self: A #MMSms.
 *
 * Gets the time when the first PDU of the SMS message arrived the SMSC, in
 * <ulink url="http://en.wikipedia.org/wiki/ISO_8601">ISO8601</ulink>
 * format.
 *
 * This field is only applicable if the PDU type is %MM_SMS_PDU_TYPE_DELIVER or
 * %MM_SMS_PDU_TYPE_STATUS_REPORT.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_sms_dup_timestamp() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The timestamp, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_timestamp (MM_GDBUS_SMS (self)));
}

/**
 * mm_sms_dup_timestamp:
 * @self: A #MMSms.
 *
 * Gets the time when the first PDU of the SMS message arrived the SMSC, in
 * <ulink url="http://en.wikipedia.org/wiki/ISO_8601">ISO8601</ulink>
 * format.
 *
 * This field is only applicable if the PDU type is %MM_SMS_PDU_TYPE_DELIVER or
 * %MM_SMS_PDU_TYPE_STATUS_REPORT.
 *
 * Returns: (transfer full): The timestamp, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_timestamp (MM_GDBUS_SMS (self)));
}

/*****************************************************************************/

/**
 * mm_sms_get_discharge_timestamp:
 * @self: A #MMSms.
 *
 * Gets the time when the first PDU of the SMS message left the SMSC, in
 * <ulink url="http://en.wikipedia.org/wiki/ISO_8601">ISO8601</ulink>
 * format.
 *
 * This field is only applicable if the PDU type is %MM_SMS_PDU_TYPE_STATUS_REPORT.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_sms_dup_discharge_timestamp() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The timestamp, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_sms_get_discharge_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sms_get_discharge_timestamp (MM_GDBUS_SMS (self)));
}

/**
 * mm_sms_dup_discharge_timestamp:
 * @self: A #MMSms.
 *
 * Gets the time when the first PDU of the SMS message left the SMSC, in
 * <ulink url="http://en.wikipedia.org/wiki/ISO_8601">ISO8601</ulink>
 * format.
 *
 * This field is only applicable if the PDU type is %MM_SMS_PDU_TYPE_STATUS_REPORT.
 *
 * Returns: (transfer full): The timestamp, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_sms_dup_discharge_timestamp (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sms_dup_discharge_timestamp (MM_GDBUS_SMS (self)));
}

/*****************************************************************************/

/**
 * mm_sms_get_validity_type:
 * @self: A #MMSms.
 *
 * Gets the type of validity information in the SMS.
 *
 * Returns: the validity type or #MM_SMS_VALIDITY_TYPE_UNKNOWN.
 */
MMSmsValidityType
mm_sms_get_validity_type (MMSms *self)
{
    GVariant *variant;
    guint type;
    GVariant *value;

    g_return_val_if_fail (MM_IS_SMS (self), MM_SMS_VALIDITY_TYPE_UNKNOWN);

    variant = mm_gdbus_sms_dup_validity (MM_GDBUS_SMS (self));
    if (!variant)
        return MM_SMS_VALIDITY_TYPE_UNKNOWN;

    g_variant_get (variant, "(uv)", &type, &value);
    g_variant_unref (variant);
    g_variant_unref (value);

    return (MMSmsValidityType)type;
}

/**
 * mm_sms_get_validity_relative:
 * @self: A #MMSms.
 *
 * Gets the length of the validity period, in minutes.
 *
 * Only applicable if the type of validity is #MM_SMS_VALIDITY_TYPE_RELATIVE.
 *
 * Returns: the length of the validity period, or 0 if unknown.
 */
guint
mm_sms_get_validity_relative (MMSms *self)
{
    GVariant *variant;
    guint type;
    GVariant *value;
    guint value_integer = 0;

    g_return_val_if_fail (MM_IS_SMS (self), MM_SMS_VALIDITY_TYPE_UNKNOWN);

    variant = mm_gdbus_sms_dup_validity (MM_GDBUS_SMS (self));
    if (!variant)
        return 0;

    g_variant_get (variant, "(uv)", &type, &value);

    if (type == MM_SMS_VALIDITY_TYPE_RELATIVE)
        value_integer = g_variant_get_uint32 (value);

    g_variant_unref (variant);
    g_variant_unref (value);

    return value_integer;
}

/*****************************************************************************/

/**
 * mm_sms_get_class:
 * @self: A #MMSms.
 *
 * Gets the 3GPP message class of the SMS.
 *
 * Returns: the message class, or -1 for invalid/unset class.
 */
gint
mm_sms_get_class (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), -1);

    return mm_gdbus_sms_get_class (MM_GDBUS_SMS (self));
}

/*****************************************************************************/

/**
 * mm_sms_get_message_reference:
 * @self: A #MMSms.
 *
 * Gets the message reference of the last PDU sent/received within this SMS.
 *
 * If the PDU type is %MM_SMS_PDU_TYPE_STATUS_REPORT, this field identifies the
 * message reference of the PDU associated to the status report.
 *
 * Returns: The message reference.
 */
guint
mm_sms_get_message_reference (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), 0);

    return mm_gdbus_sms_get_message_reference (MM_GDBUS_SMS (self));
}

/*****************************************************************************/

/**
 * mm_sms_get_delivery_report_request:
 * @self: A #MMSms.
 *
 * Checks whether delivery report is requested for this SMS.
 *
 * Returns: %TRUE if delivery report is requested, %FALSE otherwise.
 */
gboolean
mm_sms_get_delivery_report_request (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), FALSE);

    return mm_gdbus_sms_get_delivery_report_request (MM_GDBUS_SMS (self));
}

/*****************************************************************************/

/**
 * mm_sms_get_delivery_state:
 * @self: A #MMSms.
 *
 * Gets the delivery state of this SMS.
 *
 * This field is only applicable if the PDU type is %MM_SMS_PDU_TYPE_STATUS_REPORT.
 *
 * Returns: A #MMSmsDeliveryState specifying the delivery state.
 */
guint
mm_sms_get_delivery_state (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), MM_SMS_DELIVERY_STATE_UNKNOWN);

    return mm_gdbus_sms_get_delivery_state (MM_GDBUS_SMS (self));
}

/*****************************************************************************/

/**
 * mm_sms_get_state:
 * @self: A #MMSms.
 *
 * Gets the state of this SMS.
 *
 * Returns: A #MMSmsState specifying the state.
 */
MMSmsState
mm_sms_get_state (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), MM_SMS_STATE_UNKNOWN);

    return (MMSmsState)mm_gdbus_sms_get_state (MM_GDBUS_SMS (self));
}

/*****************************************************************************/

/**
 * mm_sms_get_storage:
 * @self: A #MMSms.
 *
 * Gets the storage in which this SMS is kept.
 *
 * Returns: A #MMSmsStorage specifying the storage.
 */
MMSmsStorage
mm_sms_get_storage (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), MM_SMS_STORAGE_UNKNOWN);

    return (MMSmsStorage)mm_gdbus_sms_get_storage (MM_GDBUS_SMS (self));
}

/*****************************************************************************/

/**
 * mm_sms_get_pdu_type:
 * @self: A #MMSms.
 *
 * Gets the PDU type on which this SMS is based.
 *
 * Returns: A #MMSmsPduType specifying the PDU type.
 */
MMSmsPduType
mm_sms_get_pdu_type (MMSms *self)
{
    g_return_val_if_fail (MM_IS_SMS (self), MM_SMS_PDU_TYPE_UNKNOWN);

    return (MMSmsPduType)mm_gdbus_sms_get_pdu_type (MM_GDBUS_SMS (self));
}

/*****************************************************************************/

/**
 * mm_sms_send_finish:
 * @self: A #MMSms.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_sms_send().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sms_send().
 *
 * Returns:  %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_send_finish (MMSms *self,
                    GAsyncResult *res,
                    GError **error)
{
    g_return_val_if_fail (MM_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_send_finish (MM_GDBUS_SMS (self), res, error);
}

/**
 * mm_sms_send:
 * @self: A #MMSms.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to queue the message for delivery.
 *
 * SMS objects can only be sent once.
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
    g_return_if_fail (MM_IS_SMS (self));

    mm_gdbus_sms_call_send (MM_GDBUS_SMS (self),
                            cancellable,
                            callback,
                            user_data);
}

/**
 * mm_sms_send_sync:
 * @self: A #MMSms.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to queue the message for delivery.
 *
 * SMS objects can only be sent once.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sms_send() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_send_sync (MMSms *self,
                  GCancellable *cancellable,
                  GError **error)
{
    g_return_val_if_fail (MM_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_send_sync (MM_GDBUS_SMS (self),
                                        cancellable,
                                        error);
}

/*****************************************************************************/

/**
 * mm_sms_store_finish:
 * @self: A #MMSms.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_sms_store().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sms_store().
 *
 * Returns: %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_store_finish (MMSms *self,
                     GAsyncResult *res,
                     GError **error)
{
    g_return_val_if_fail (MM_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_store_finish (MM_GDBUS_SMS (self), res, error);
}

/**
 * mm_sms_store:
 * @self: A #MMSms.
 * @storage: A #MMSmsStorage specifying where to store the SMS, or #MM_SMS_STORAGE_UNKNOWN to use the default.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronoulsy requests to store the message in the device if not already done.
 *
 * SMS objects can only be stored once.
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
    g_return_if_fail (MM_IS_SMS (self));

    mm_gdbus_sms_call_store (MM_GDBUS_SMS (self),
                             storage,
                             cancellable,
                             callback,
                             user_data);
}

/**
 * mm_sms_store_sync:
 * @self: A #MMSms.
 * @storage: A #MMSmsStorage specifying where to store the SMS, or #MM_SMS_STORAGE_UNKNOWN to use the default.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronoulsy requests to store the message in the device if not already done.
 *
 * SMS objects can only be stored once.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sms_store() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_sms_store_sync (MMSms *self,
                   MMSmsStorage storage,
                   GCancellable *cancellable,
                   GError **error)
{
    g_return_val_if_fail (MM_IS_SMS (self), FALSE);

    return mm_gdbus_sms_call_store_sync (MM_GDBUS_SMS (self),
                                         storage,
                                         cancellable,
                                         error);
}

/*****************************************************************************/

static void
mm_sms_init (MMSms *self)
{
}

static void
mm_sms_class_init (MMSmsClass *sms_class)
{
}
