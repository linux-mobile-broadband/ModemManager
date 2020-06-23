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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-signal.h"

/**
 * SECTION: mm-modem-signal
 * @title: MMModemSignal
 * @short_description: The extended Signal interface
 *
 * The #MMModemSignal is an object providing access to the methods, signals and
 * properties of the Signal interface.
 *
 * The Signal interface is exposed whenever a modem has extended signal
 * retrieval capabilities.
 */

G_DEFINE_TYPE (MMModemSignal, mm_modem_signal, MM_GDBUS_TYPE_MODEM_SIGNAL_PROXY)

typedef struct {
    GMutex mutex;
    guint id;
    MMSignal *info;
} UpdatedProperty;

typedef enum {
    UPDATED_PROPERTY_TYPE_CDMA = 0,
    UPDATED_PROPERTY_TYPE_EVDO = 1,
    UPDATED_PROPERTY_TYPE_GSM  = 2,
    UPDATED_PROPERTY_TYPE_UMTS = 3,
    UPDATED_PROPERTY_TYPE_LTE  = 4,
    UPDATED_PROPERTY_TYPE_NR5G = 5,
    UPDATED_PROPERTY_TYPE_LAST
} UpdatedPropertyType;

struct _MMModemSignalPrivate {
    UpdatedProperty values [UPDATED_PROPERTY_TYPE_LAST];
};

/*****************************************************************************/

/**
 * mm_modem_signal_get_path:
 * @self: A #MMModemSignal.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.2
 */
const gchar *
mm_modem_signal_get_path (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_signal_dup_path:
 * @self: A #MMModemSignal.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.2
 */
gchar *
mm_modem_signal_dup_path (MMModemSignal *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_signal_setup_finish:
 * @self: A #MMModemSignal.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_signal_setup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_signal_setup().
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_signal_setup_finish (MMModemSignal *self,
                              GAsyncResult *res,
                              GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_call_setup_finish (MM_GDBUS_MODEM_SIGNAL (self), res, error);
}

/**
 * mm_modem_signal_setup:
 * @self: A #MMModemSignal.
 * @rate: Rate to use when refreshing signal values.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously setups the extended signal quality retrieval.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_signal_setup_finish() to get the result of the operation.
 *
 * See mm_modem_signal_setup_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.2
 */
void
mm_modem_signal_setup (MMModemSignal *self,
                       guint rate,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_SIGNAL (self));

    mm_gdbus_modem_signal_call_setup (MM_GDBUS_MODEM_SIGNAL (self), rate, cancellable, callback, user_data);
}

/**
 * mm_modem_signal_setup_sync:
 * @self: A #MMModemSignal.
 * @rate: Rate to use when refreshing signal values.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously setups the extended signal quality retrieval.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_signal_setup() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_signal_setup_sync (MMModemSignal *self,
                            guint rate,
                            GCancellable *cancellable,
                            GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_call_setup_sync (MM_GDBUS_MODEM_SIGNAL (self), rate, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_rate:
 * @self: A #MMModemSignal.
 *
 * Gets the currently configured refresh rate.
 *
 * Returns: the refresh rate, in seconds.
 *
 * Since: 1.2
 */
guint
mm_modem_signal_get_rate (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), 0);

    return mm_gdbus_modem_signal_get_rate (MM_GDBUS_MODEM_SIGNAL (self));
}

/*****************************************************************************/

static void values_updated (MMModemSignal *self, GParamSpec *pspec, UpdatedPropertyType type);

static void
cdma_updated (MMModemSignal *self,
              GParamSpec *pspec)
{
    values_updated (self, pspec, UPDATED_PROPERTY_TYPE_CDMA);
}

static void
evdo_updated (MMModemSignal *self,
              GParamSpec *pspec)
{
    values_updated (self, pspec, UPDATED_PROPERTY_TYPE_EVDO);
}

static void
gsm_updated (MMModemSignal *self,
             GParamSpec *pspec)
{
    values_updated (self, pspec, UPDATED_PROPERTY_TYPE_GSM);
}

static void
umts_updated (MMModemSignal *self,
              GParamSpec *pspec)
{
    values_updated (self, pspec, UPDATED_PROPERTY_TYPE_UMTS);
}

static void
lte_updated (MMModemSignal *self,
             GParamSpec *pspec)
{
    values_updated (self, pspec, UPDATED_PROPERTY_TYPE_LTE);
}

static void
nr5g_updated (MMModemSignal *self,
             GParamSpec *pspec)
{
    values_updated (self, pspec, UPDATED_PROPERTY_TYPE_NR5G);
}

typedef GVariant * (* Getter)          (MmGdbusModemSignal *self);
typedef GVariant * (* Dupper)          (MmGdbusModemSignal *self);
typedef void       (* UpdatedCallback) (MMModemSignal *self, GParamSpec *pspec);
typedef struct {
    const gchar *signal_name;
    Getter get;
    Dupper dup;
    UpdatedCallback updated_callback;
} SignalData;

static const SignalData signal_data [UPDATED_PROPERTY_TYPE_LAST] = {
    { "notify::cdma", mm_gdbus_modem_signal_get_cdma, mm_gdbus_modem_signal_dup_cdma, cdma_updated },
    { "notify::evdo", mm_gdbus_modem_signal_get_evdo, mm_gdbus_modem_signal_dup_evdo, evdo_updated },
    { "notify::gsm",  mm_gdbus_modem_signal_get_gsm,  mm_gdbus_modem_signal_dup_gsm,  gsm_updated  },
    { "notify::umts", mm_gdbus_modem_signal_get_umts, mm_gdbus_modem_signal_dup_umts, umts_updated },
    { "notify::lte",  mm_gdbus_modem_signal_get_lte,  mm_gdbus_modem_signal_dup_lte,  lte_updated  },
    { "notify::nr5g", mm_gdbus_modem_signal_get_nr5g, mm_gdbus_modem_signal_dup_nr5g, nr5g_updated }
};

static void
values_updated (MMModemSignal *self,
                GParamSpec *pspec,
                UpdatedPropertyType type)
{
    g_mutex_lock (&self->priv->values[type].mutex);
    {
        GVariant *dictionary;

        g_clear_object (&self->priv->values[type].info);
        dictionary = signal_data[type].get (MM_GDBUS_MODEM_SIGNAL (self));
        if (dictionary) {
            GError *error = NULL;

            self->priv->values[type].info = mm_signal_new_from_dictionary (dictionary, &error);
            if (error) {
                g_warning ("Invalid signal info update received: %s", error->message);
                g_error_free (error);
            }
        }
    }
    g_mutex_unlock (&self->priv->values[type].mutex);
}

static void
ensure_internal (MMModemSignal *self,
                 MMSignal **dup,
                 UpdatedPropertyType type)
{
    g_mutex_lock (&self->priv->values[type].mutex);
    {
        /* If this is the first time ever asking for the object, setup the
         * update listener and the initial object, if any. */
        if (!self->priv->values[type].id) {
            GVariant *dictionary;

            dictionary = signal_data[type].dup (MM_GDBUS_MODEM_SIGNAL (self));
            if (dictionary) {
                GError *error = NULL;

                self->priv->values[type].info = mm_signal_new_from_dictionary (dictionary, &error);
                if (error) {
                    g_warning ("Invalid signal info: %s", error->message);
                    g_error_free (error);
                }
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->values[type].id =
                g_signal_connect (self,
                                  signal_data[type].signal_name,
                                  G_CALLBACK (signal_data[type].updated_callback),
                                  NULL);
        }

        if (dup && self->priv->values[type].info)
            *dup = g_object_ref (self->priv->values[type].info);
    }
    g_mutex_unlock (&self->priv->values[type].mutex);
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_cdma:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the CDMA signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_cdma() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_get_cdma (MMModemSignal *self)
{
    MMSignal *info = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, &info, UPDATED_PROPERTY_TYPE_CDMA);
    return info;
}

/**
 * mm_modem_signal_peek_cdma:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the CDMA signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_cdma() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_peek_cdma (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, NULL, UPDATED_PROPERTY_TYPE_CDMA);
    return self->priv->values[UPDATED_PROPERTY_TYPE_CDMA].info;
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_evdo:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the EV-DO signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_evdo() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_get_evdo (MMModemSignal *self)
{
    MMSignal *info = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, &info, UPDATED_PROPERTY_TYPE_EVDO);
    return info;
}

/**
 * mm_modem_signal_peek_evdo:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the EV-DO signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_evdo() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_peek_evdo (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, NULL, UPDATED_PROPERTY_TYPE_EVDO);
    return self->priv->values[UPDATED_PROPERTY_TYPE_EVDO].info;
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_gsm:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the GSM signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_gsm() again to get a new #MMSignal with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_get_gsm (MMModemSignal *self)
{
    MMSignal *info = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, &info, UPDATED_PROPERTY_TYPE_GSM);
    return info;
}

/**
 * mm_modem_signal_peek_gsm:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the GSM signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_gsm() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_peek_gsm (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, NULL, UPDATED_PROPERTY_TYPE_GSM);
    return self->priv->values[UPDATED_PROPERTY_TYPE_GSM].info;
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_umts:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the UMTS signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_umts() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_get_umts (MMModemSignal *self)
{
    MMSignal *info = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, &info, UPDATED_PROPERTY_TYPE_UMTS);
    return info;
}

/**
 * mm_modem_signal_peek_umts:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the UMTS signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_umts() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_peek_umts (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, NULL, UPDATED_PROPERTY_TYPE_UMTS);
    return self->priv->values[UPDATED_PROPERTY_TYPE_UMTS].info;
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_lte:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the LTE signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_lte() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_get_lte (MMModemSignal *self)
{
    MMSignal *info = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, &info, UPDATED_PROPERTY_TYPE_LTE);
    return info;
}

/**
 * mm_modem_signal_get_nr5g:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the 5G signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_nr5g() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.16
 */
MMSignal *
mm_modem_signal_get_nr5g (MMModemSignal *self)
{
    MMSignal *info = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, &info, UPDATED_PROPERTY_TYPE_NR5G);
    return info;
}

/**
 * mm_modem_signal_peek_lte:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the LTE signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_lte() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */
MMSignal *
mm_modem_signal_peek_lte (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, NULL, UPDATED_PROPERTY_TYPE_LTE);
    return self->priv->values[UPDATED_PROPERTY_TYPE_LTE].info;
}

/**
 * mm_modem_signal_peek_nr5g:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the 5G signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_nr5g() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.16
 */
MMSignal *
mm_modem_signal_peek_nr5g (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    ensure_internal (self, NULL, UPDATED_PROPERTY_TYPE_NR5G);
    return self->priv->values[UPDATED_PROPERTY_TYPE_NR5G].info;
}

/*****************************************************************************/

static void
mm_modem_signal_init (MMModemSignal *self)
{
    guint i;

    /* Setup private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MODEM_SIGNAL, MMModemSignalPrivate);

    for (i = 0; i < UPDATED_PROPERTY_TYPE_LAST; i++)
        g_mutex_init (&self->priv->values[i].mutex);
}

static void
finalize (GObject *object)
{
    MMModemSignal *self = MM_MODEM_SIGNAL (object);
    guint i;

    for (i = 0; i < UPDATED_PROPERTY_TYPE_LAST; i++)
        g_mutex_clear (&self->priv->values[i].mutex);

    G_OBJECT_CLASS (mm_modem_signal_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMModemSignal *self = MM_MODEM_SIGNAL (object);
    guint i;

    for (i = 0; i < UPDATED_PROPERTY_TYPE_LAST; i++)
        g_clear_object (&self->priv->values[i].info);

    G_OBJECT_CLASS (mm_modem_signal_parent_class)->dispose (object);
}

static void
mm_modem_signal_class_init (MMModemSignalClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemSignalPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
    object_class->finalize = finalize;
}
