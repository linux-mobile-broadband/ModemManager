/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
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
#include "mm-modem-firmware.h"

/**
 * SECTION: mm-modem-firmware
 * @title: MMModemFirmware
 * @short_description: The Firmware interface
 *
 * The #MMModemFirmware is an object providing access to the methods, signals and
 * properties of the Firmware interface.
 *
 * The Firmware interface is exposed whenever a modem has firmware capabilities.
 */

G_DEFINE_TYPE (MMModemFirmware, mm_modem_firmware, MM_GDBUS_TYPE_MODEM_FIRMWARE_PROXY)

struct _MMModemFirmwarePrivate {
    /* Common mutex to sync access */
    GMutex mutex;

    PROPERTY_OBJECT_DECLARE (update_settings, MMFirmwareUpdateSettings)
};

/*****************************************************************************/

/**
 * mm_modem_firmware_get_path:
 * @self: A #MMModemFirmware.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_firmware_get_path (MMModemFirmware *self)
{
    g_return_val_if_fail (MM_IS_MODEM_FIRMWARE (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_firmware_dup_path:
 * @self: A #MMModemFirmware.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_firmware_dup_path (MMModemFirmware *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_FIRMWARE (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_firmware_get_update_settings:
 * @self: A #MMModemFirmware.
 *
 * Gets a #MMFirmwareUpdateSettings object specifying the expected update
 * settings.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_firmware_get_update_settings() again to get a new
 * #MMFirmwareUpdateSettings with the new values.</warning>
 *
 * Returns: (transfer full): A #MMFirmwareUpdateSettings that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.10
 */

/**
 * mm_modem_firmware_peek_update_settings:
 * @self: A #MMModemFirmware.
 *
 * Gets a #MMFirmwareUpdateSettings object specifying the expected update
 * settings.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_firmware_get_update_settings() if on
 * another thread.</warning>
 *
 * Returns: (transfer none): A #MMFirmwareUpdateSettings. Do not free the
 * returned value, it belongs to @self.
 *
 * Since: 1.10
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (update_settings,
                                 ModemFirmware, modem_firmware, MODEM_FIRMWARE,
                                 MMFirmwareUpdateSettings,
                                 mm_firmware_update_settings_new_from_variant)

/*****************************************************************************/

/**
 * mm_modem_firmware_select_finish:
 * @self: A #MMModemFirmware.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_firmware_select().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_firmware_select().
 *
 * Returns: %TRUE if the selection was successful, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_firmware_select_finish (MMModemFirmware *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_FIRMWARE (self), FALSE);

    return mm_gdbus_modem_firmware_call_select_finish (MM_GDBUS_MODEM_FIRMWARE (self), res, error);
}

/**
 * mm_modem_firmware_select:
 * @self: A #MMModemFirmware.
 * @unique_id: Unique ID of the firmware image to select.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously selects a firmware image to boot.
 *
 * <warning>The modem will possibly disappear once this action is run, as it
 * needs to reboot in order to select the new image.</warning>
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_firmware_select_finish() to get the result of the operation.
 *
 * See mm_modem_firmware_select_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_firmware_select (MMModemFirmware *self,
                          const gchar *unique_id,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_FIRMWARE (self));
    g_return_if_fail (unique_id != NULL && unique_id[0] != '\0');

    mm_gdbus_modem_firmware_call_select (MM_GDBUS_MODEM_FIRMWARE (self), unique_id, cancellable, callback, user_data);
}

/**
 * mm_modem_firmware_select_sync:
 * @self: A #MMModemFirmware.
 * @unique_id: Unique ID of the firmware image to select.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously selects a firmware image to boot.
 *
 * <warning>The modem will possibly disappear once this action is run, as it
 * needs to reboot in order to select the new image.</warning>
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_firmware_select() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the selection was successful, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_firmware_select_sync (MMModemFirmware *self,
                               const gchar *unique_id,
                               GCancellable *cancellable,
                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_FIRMWARE (self), FALSE);
    g_return_val_if_fail (unique_id != NULL && unique_id[0] != '\0', FALSE);

    return mm_gdbus_modem_firmware_call_select_sync (MM_GDBUS_MODEM_FIRMWARE (self), unique_id, cancellable, error);
}

/*****************************************************************************/

static gboolean
build_results (const gchar *str_selected,
               GVariant *dictionaries_installed,
               MMFirmwareProperties **selected,
               GList **installed,
               GError **error)
{
    GError *saved_error = NULL;
    GVariantIter iter;
    guint n;

    g_assert (selected != NULL);
    g_assert (installed != NULL);

    *installed = NULL;
    *selected = NULL;

    if (!dictionaries_installed) {
        if (str_selected && str_selected[0]) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "Selected image specified but no installed images listed");
            return FALSE;
        }

        /* Nothing else to do */
        return TRUE;
    }

    /* Parse array of dictionaries */
    g_variant_iter_init (&iter, dictionaries_installed);
    n = g_variant_iter_n_children (&iter);

    if (n > 0) {
        GVariant *dictionary = NULL;

        while ((dictionary = g_variant_iter_next_value (&iter))) {
            MMFirmwareProperties *firmware;
            GError *inner_error = NULL;

            firmware = mm_firmware_properties_new_from_dictionary (dictionary, &inner_error);
            if (!firmware) {
                g_warning ("Couldn't create firmware properties: %s",
                           inner_error->message);
                if (!saved_error)
                    saved_error = inner_error;
                else
                    g_error_free (inner_error);
            } else {
                /* Save the firmware properties */
                *installed = g_list_append (*installed, firmware);

                if (str_selected && str_selected[0] &&
                    g_str_equal (mm_firmware_properties_get_unique_id (firmware), str_selected))
                    *selected = g_object_ref (firmware);
            }

            g_variant_unref (dictionary);
        }
    }

    if (str_selected && str_selected[0] && *selected == NULL)
        g_warning ("Selected image '%s' was not found in the list of installed images",
                   str_selected);

    if (saved_error) {
        if (*installed == NULL) {
            g_propagate_error (error, saved_error);
            return FALSE;
        }
        g_error_free (saved_error);
    }

    return TRUE;
}

/**
 * mm_modem_firmware_list_finish:
 * @self: A #MMModemFirmware.
 * @selected: (out) (allow-none) (transfer full): The selected firmware slot, or
 *  %NULL if no slot is selected (such as if all slots are empty, or no slots
 *  exist). The returned value should be freed with g_object_unref().
 * @installed: (out) (allow-none) (transfer full) (element-type ModemManager.FirmwareProperties):
 *  A list of #MMFirmwareProperties objects specifying the installed images. The
 *  returned value should be freed with g_list_free_full() using
 *  g_object_unref() as #GDestroyNotify.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_firmware_list().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_firmware_list().
 *
 * Returns: %TRUE if the list was correctly retrieved, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_firmware_list_finish (MMModemFirmware *self,
                               GAsyncResult *res,
                               MMFirmwareProperties **selected,
                               GList **installed,
                               GError **error)
{
    gboolean parsed;
    GVariant *dictionaries_installed = NULL;
    gchar *str_selected = NULL;

    g_return_val_if_fail (MM_IS_MODEM_FIRMWARE (self), FALSE);

    g_return_val_if_fail (selected != NULL, FALSE);
    g_return_val_if_fail (installed != NULL, FALSE);

    if (!mm_gdbus_modem_firmware_call_list_finish (MM_GDBUS_MODEM_FIRMWARE (self),
                                                   &str_selected,
                                                   &dictionaries_installed,
                                                   res,
                                                   error))
        return FALSE;

    parsed = build_results (str_selected,
                            dictionaries_installed,
                            selected,
                            installed,
                            error);

    if (dictionaries_installed)
        g_variant_unref (dictionaries_installed);
    g_free (str_selected);

    return parsed;
}

/**
 * mm_modem_firmware_list:
 * @self: A #MMModemFirmware.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the list of available firmware images.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_firmware_list_finish() to get the result of the operation.
 *
 * See mm_modem_firmware_list_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_firmware_list (MMModemFirmware *self,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_FIRMWARE (self));

    mm_gdbus_modem_firmware_call_list (MM_GDBUS_MODEM_FIRMWARE (self),
                                       cancellable,
                                       callback,
                                       user_data);
}

/**
 * mm_modem_firmware_list_sync:
 * @self: A #MMModemFirmware.
 * @selected: (out) (allow-none) (transfer full): The selected firmware slot,
 *  or NULL if no slot is selected (such as if all slots are empty, or no slots
 *  exist). The returned value should be freed with g_object_unref().
 * @installed: (out) (allow-none) (transfer full) (element-type ModemManager.FirmwareProperties):
 *  A list of #MMFirmwareProperties objects specifying the installed images. The
 *  returned value should be freed with g_list_free_full() using
 *  g_object_unref() as #GDestroyNotify.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return firmware for error or %NULL.
 *
 * Synchronously gets the list of available firmware images.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_firmware_list() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the list was correctly retrieved, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_firmware_list_sync (MMModemFirmware *self,
                             MMFirmwareProperties **selected,
                             GList **installed,
                             GCancellable *cancellable,
                             GError **error)
{
    GVariant *dictionaries_installed = NULL;
    gchar *str_selected = NULL;
    gboolean parsed;

    g_return_val_if_fail (MM_IS_MODEM_FIRMWARE (self), FALSE);
    g_return_val_if_fail (selected != NULL, FALSE);
    g_return_val_if_fail (installed != NULL, FALSE);

    if (!mm_gdbus_modem_firmware_call_list_sync (MM_GDBUS_MODEM_FIRMWARE (self),
                                                 &str_selected,
                                                 &dictionaries_installed,
                                                 cancellable,
                                                 error))
        return FALSE;

    parsed = build_results (str_selected,
                            dictionaries_installed,
                            selected,
                            installed,
                            error);

    if (dictionaries_installed)
        g_variant_unref (dictionaries_installed);
    g_free (str_selected);

    return parsed;
}

/*****************************************************************************/

static void
mm_modem_firmware_init (MMModemFirmware *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MODEM_FIRMWARE, MMModemFirmwarePrivate);
    g_mutex_init (&self->priv->mutex);

    PROPERTY_INITIALIZE (update_settings, "update-settings")
}

static void
finalize (GObject *object)
{
    MMModemFirmware *self = MM_MODEM_FIRMWARE (object);

    g_mutex_clear (&self->priv->mutex);

    PROPERTY_OBJECT_FINALIZE (update_settings)

    G_OBJECT_CLASS (mm_modem_firmware_parent_class)->finalize (object);
}

static void
mm_modem_firmware_class_init (MMModemFirmwareClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemFirmwarePrivate));

    object_class->finalize = finalize;
}
