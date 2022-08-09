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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Google, Inc.
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-3gpp-profile-manager.h"

/**
 * SECTION: mm-modem-3gpp-profile-manager
 * @title: MMModem3gppProfileManager
 * @short_description: The 3GPP profile manager interface
 *
 * The #MMModem3gppProfileManager is an object providing access to the methods
 * and signals of the 3GPP Profile Manager interface.
 *
 * This interface is only exposed when the 3GPP modem is known to handle profile
 * management operations.
 */

G_DEFINE_TYPE (MMModem3gppProfileManager, mm_modem_3gpp_profile_manager, MM_GDBUS_TYPE_MODEM3GPP_PROFILE_MANAGER_PROXY)

/*****************************************************************************/

/**
 * mm_modem_3gpp_profile_manager_get_path:
 * @self: A #MMModem3gppProfileManager.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.18
 */
const gchar *
mm_modem_3gpp_profile_manager_get_path (MMModem3gppProfileManager *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_3gpp_profile_manager_dup_path:
 * @self: A #MMModem3gppProfileManager.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.18
 */
gchar *
mm_modem_3gpp_profile_manager_dup_path (MMModem3gppProfileManager *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_profile_manager_get_index_field:
 * @self: A #MMModem3gppProfileManager.
 *
 * Gets the name of the field used as index in profile management
 * operations.
 *
 * Returns: (transfer none): The index field, or %NULL if none available.
 * Do not free the returned value, it belongs to @self.
 *
 * Since: 1.20
 */
const gchar *
mm_modem_3gpp_profile_manager_get_index_field (MMModem3gppProfileManager *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_profile_manager_get_index_field (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self)));
}

/**
 * mm_modem_3gpp_profile_manager_dup_index_field:
 * @self: A #MMModem3gppProfileManager.
 *
 * Gets a copy of the name of the field used as index in profile management
 * operations.
 *
 * Returns: (transfer full): The index field, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.20
 */
gchar *
mm_modem_3gpp_profile_manager_dup_index_field (MMModem3gppProfileManager *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_profile_manager_dup_index_field (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self)));
}

/*****************************************************************************/

static gboolean
build_list_results (GVariant  *dictionaries,
                    GList    **out_profiles,
                    GError   **error)
{
    g_autoptr(GError)  saved_error = NULL;
    GVariantIter       iter;
    guint              n;
    GList             *profiles = NULL;

    if (out_profiles)
        *out_profiles = NULL;

    if (!dictionaries)
        return TRUE;

    /* Parse array of dictionaries */
    g_variant_iter_init (&iter, dictionaries);
    n = g_variant_iter_n_children (&iter);

    if (n > 0) {
        GVariant* dictionary = NULL;

        while ((dictionary = g_variant_iter_next_value (&iter))) {
            MM3gppProfile     *profile = NULL;
            g_autoptr(GError)  inner_error = NULL;

            profile = mm_3gpp_profile_new_from_dictionary (dictionary, &inner_error);
            if (!profile) {
                g_warning ("Couldn't create 3GPP profile: %s", inner_error->message);
                if (!saved_error)
                    saved_error = g_steal_pointer (&inner_error);
            } else
                profiles = g_list_append (profiles, profile);
            g_variant_unref (dictionary);
        }
    }

    if (saved_error && !profiles) {
        g_propagate_error (error, g_steal_pointer (&saved_error));
        return FALSE;
    }

    if (out_profiles)
        *out_profiles = profiles;
    else
        g_list_free_full (profiles, g_object_unref);
    return TRUE;
}

/**
 * mm_modem_3gpp_profile_manager_list_finish:
 * @self: A #MMModem3gppProfileManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_profile_manager_list().
 * @profiles: (out) (allow-none) (transfer full) (element-type ModemManager.3gppProfile):
 *  A list of #MM3gppProfile objects available in the device. The returned value
 *  should be freed with g_list_free_full() using g_object_unref() as
 *  #GDestroyNotify.
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_profile_manager_list().
 *
 * Returns: %TRUE if the list was correctly retrieved, %FALSE if @error is set.
 *
 * Since: 1.18
 */
gboolean
mm_modem_3gpp_profile_manager_list_finish (MMModem3gppProfileManager  *self,
                                           GAsyncResult               *res,
                                           GList                     **profiles,
                                           GError                    **error)
{
    g_autoptr(GVariant) dictionaries = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), FALSE);

    if (!mm_gdbus_modem3gpp_profile_manager_call_list_finish (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                              &dictionaries,
                                                              res,
                                                              error))
        return FALSE;

    return build_list_results (dictionaries, profiles, error);
}

/**
 * mm_modem_3gpp_profile_manager_list:
 * @self: A #MMModem3gppProfileManager.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the list of available connection profiles.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_profile_manager_list_finish() to get the result of the
 * operation.
 *
 * See mm_modem_3gpp_profile_manager_list_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.18
 */
void
mm_modem_3gpp_profile_manager_list (MMModem3gppProfileManager *self,
                                    GCancellable              *cancellable,
                                    GAsyncReadyCallback        callback,
                                    gpointer                   user_data)
{
    g_return_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self));

    mm_gdbus_modem3gpp_profile_manager_call_list (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                  cancellable,
                                                  callback,
                                                  user_data);
}

/**
 * mm_modem_3gpp_profile_manager_list_sync:
 * @self: A #MMModem3gppProfileManager.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @profiles: (out) (allow-none) (transfer full) (element-type ModemManager.3gppProfile):
 *  A list of #MM3gppProfile objects available in the device. The returned value
 *  should be freed with g_list_free_full() using g_object_unref() as
 *  #GDestroyNotify.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the list of available connection profiles.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_profile_manager_list() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if the list was correctly retrieved, %FALSE if @error is set.
 *
 * Since: 1.18
 */
gboolean
mm_modem_3gpp_profile_manager_list_sync (MMModem3gppProfileManager  *self,
                                         GCancellable               *cancellable,
                                         GList                     **profiles,
                                         GError                    **error)
{
    g_autoptr(GVariant) dictionaries = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), FALSE);

    if (!mm_gdbus_modem3gpp_profile_manager_call_list_sync (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                            &dictionaries,
                                                            cancellable,
                                                            error))
        return FALSE;

    return build_list_results (dictionaries, profiles, error);
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_profile_manager_set_finish:
 * @self: A #MMModem3gppProfileManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_profile_manager_set().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_profile_manager_set().
 *
 * Returns: (transfer full): A #MM3gppProfile with the stored settings, or %NULL if @error is set.
 *
 * Since: 1.18
 */
MM3gppProfile *
mm_modem_3gpp_profile_manager_set_finish (MMModem3gppProfileManager  *self,
                                          GAsyncResult               *res,
                                          GError                    **error)
{
    g_autoptr(GVariant) stored_dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), NULL);

    if (!mm_gdbus_modem3gpp_profile_manager_call_set_finish (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                             &stored_dictionary,
                                                             res,
                                                             error))
        return NULL;

    return mm_3gpp_profile_new_from_dictionary (stored_dictionary, error);
}

/**
 * mm_modem_3gpp_profile_manager_set:
 * @self: A #MMModem3gppProfileManager.
 * @requested: A #MM3gppProfile with the requested settings.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously updates a connection profile with the settings
 * given in @profile.
 *
 * If @profile does not have an explicit profile ID set, a new profile will
 * be created.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_profile_manager_set_finish() to get the result of the
 * operation.
 *
 * See mm_modem_3gpp_profile_manager_set_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.18
 */
void
mm_modem_3gpp_profile_manager_set (MMModem3gppProfileManager *self,
                                   MM3gppProfile             *requested,
                                   GCancellable              *cancellable,
                                   GAsyncReadyCallback        callback,
                                   gpointer                   user_data)
{
    g_autoptr(GVariant) requested_dictionary = NULL;

    g_return_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self));

    requested_dictionary = mm_3gpp_profile_get_dictionary (requested);
    mm_gdbus_modem3gpp_profile_manager_call_set (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                 requested_dictionary,
                                                 cancellable,
                                                 callback,
                                                 user_data);
}

/**
 * mm_modem_3gpp_profile_manager_set_sync:
 * @self: A #MMModem3gppProfileManager.
 * @requested: A #MM3gppProfile with the requested settings.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously updates a connection profile with the settings
 * given in @profile.
 *
 * If @profile does not have an explicit profile ID set, a new profile will
 * be created.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_profile_manager_set() for the asynchronous version of this
 * method.
 *
 * Returns: (transfer full): A #MM3gppProfile with the stored settings, or %NULL if @error is set.
 *
 * Since: 1.18
 */
MM3gppProfile *
mm_modem_3gpp_profile_manager_set_sync (MMModem3gppProfileManager  *self,
                                        MM3gppProfile              *requested,
                                        GCancellable               *cancellable,
                                        GError                    **error)
{
    g_autoptr(GVariant) requested_dictionary = NULL;
    g_autoptr(GVariant) stored_dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), NULL);

    requested_dictionary = mm_3gpp_profile_get_dictionary (requested);

    if (!mm_gdbus_modem3gpp_profile_manager_call_set_sync (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                           requested_dictionary,
                                                           &stored_dictionary,
                                                           cancellable,
                                                           error))
        return NULL;

    return mm_3gpp_profile_new_from_dictionary (stored_dictionary, error);
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_profile_manager_delete_finish:
 * @self: A #MMModem3gppProfileManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_profile_manager_delete().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_profile_manager_delete().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.18
 */
gboolean
mm_modem_3gpp_profile_manager_delete_finish (MMModem3gppProfileManager  *self,
                                             GAsyncResult               *res,
                                             GError                    **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), FALSE);

    return mm_gdbus_modem3gpp_profile_manager_call_delete_finish (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self), res, error);
}

/**
 * mm_modem_3gpp_profile_manager_delete:
 * @self: A #MMModem3gppProfileManager.
 * @profile: A #MM3gppProfile.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously deletes the connection profile.
 *
 * The @profile should have at least the profile ID set for the delete operation
 * to succeed.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_profile_manager_delete_finish() to get the result of the
 * operation.
 *
 * See mm_modem_3gpp_profile_manager_delete_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.18
 */
void
mm_modem_3gpp_profile_manager_delete (MMModem3gppProfileManager *self,
                                      MM3gppProfile             *profile,
                                      GCancellable              *cancellable,
                                      GAsyncReadyCallback        callback,
                                      gpointer                   user_data)
{
    g_autoptr(GVariant) profile_dictionary = NULL;

    g_return_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self));

    profile_dictionary = mm_3gpp_profile_get_dictionary (profile);
    mm_gdbus_modem3gpp_profile_manager_call_delete (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                    profile_dictionary,
                                                    cancellable,
                                                    callback,
                                                    user_data);
}

/**
 * mm_modem_3gpp_profile_manager_delete_sync:
 * @self: A #MMModem3gppProfileManager.
 * @profile: A #MM3gppProfile.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously deletes the connection profile.
 *
 * The @profile should have at least the profile ID set for the delete operation
 * to succeed.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_profile_manager_delete() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.18
 */
gboolean
mm_modem_3gpp_profile_manager_delete_sync (MMModem3gppProfileManager  *self,
                                           MM3gppProfile              *profile,
                                           GCancellable               *cancellable,
                                           GError                    **error)
{
    g_autoptr(GVariant) profile_dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_PROFILE_MANAGER (self), FALSE);

    profile_dictionary = mm_3gpp_profile_get_dictionary (profile);
    return mm_gdbus_modem3gpp_profile_manager_call_delete_sync (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (self),
                                                                profile_dictionary,
                                                                cancellable,
                                                                error);
}

/*****************************************************************************/

static void
mm_modem_3gpp_profile_manager_init (MMModem3gppProfileManager *self)
{
}

static void
mm_modem_3gpp_profile_manager_class_init (MMModem3gppProfileManagerClass *modem_class)
{
}
