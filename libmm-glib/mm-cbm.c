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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#include "string.h"

#include "mm-helpers.h"
#include "mm-cbm.h"
#include "mm-modem.h"

/**
 * SECTION: mm-cbm
 * @title: MMCbm
 * @short_description: The CBM interface
 *
 * The #MMCbm is an object providing access to the methods, signals and
 * properties of the CBM interface.
 *
 * When the CBM is exposed and available in the bus, it is ensured that at
 * least this interface is also available.
 */

G_DEFINE_TYPE (MMCbm, mm_cbm, MM_GDBUS_TYPE_CBM_PROXY)

/*****************************************************************************/

/**
 * mm_cbm_get_path:
 * @self: A #MMCbm.
 *
 * Gets the DBus path of the #MMCbm object.
 *
 * Returns: (transfer none): The DBus path of the #MMCbm object.
 *
 * Since: 1.24
 */
const gchar *
mm_cbm_get_path (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_cbm_dup_path:
 * @self: A #MMCbm.
 *
 * Gets a copy of the DBus path of the #MMCbm object.
 *
 * Returns: (transfer full): The DBus path of the #MMCbm object. The returned
 * value should be freed with g_free().
 *
 * Since: 1.24
 */
gchar *
mm_cbm_dup_path (MMCbm *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_CBM (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_cbm_get_text:
 * @self: A #MMCbm.
 *
 * Gets the message text, in UTF-8.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_cbm_dup_text() if on another thread.</warning>
 *
 * Returns: (transfer none): The message text, or %NULL if it doesn't contain
 * any (e.g. contains data instead).
 *
 * Since: 1.24
 */
const gchar *
mm_cbm_get_text (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_cbm_get_text (MM_GDBUS_CBM (self)));
}

/**
 * mm_cbm_dup_text:
 * @self: A #MMCbm.
 *
 * Gets the message text, in UTF-8.
 *
 * Returns: (transfer full): The message text, or %NULL if it doesn't contain
 * any (e.g. contains data instead). The returned value should be freed with
 * g_free().
 *
 * Since: 1.24
 */
gchar *
mm_cbm_dup_text (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_cbm_dup_text (MM_GDBUS_CBM (self)));
}

/*****************************************************************************/

/**
 * mm_cbm_get_state:
 * @self: A #MMCbm.
 *
 * Gets the state of this CBM.
 *
 * Returns: A #MMCbmState specifying the state.
 *
 * Since: 1.24
 */
MMCbmState
mm_cbm_get_state (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), MM_CBM_STATE_UNKNOWN);

    return (MMCbmState)mm_gdbus_cbm_get_state (MM_GDBUS_CBM (self));
}

/*****************************************************************************/

/**
 * mm_cbm_get_channel:
 * @self: A #MMCbm.
 *
 * Gets the channel of this CBM.
 *
 * Returns: The channel
 *
 * Since: 1.24
 */
guint
mm_cbm_get_channel (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), 0);

    return mm_gdbus_cbm_get_channel (MM_GDBUS_CBM (self));
}

/*****************************************************************************/

/**
 * mm_cbm_get_message_code:
 * @self: A #MMCbm.
 *
 * Gets the message code of this CBM.
 *
 * Returns: The message code
 *
 * Since: 1.24
 */
guint
mm_cbm_get_message_code (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), 0);

    return mm_gdbus_cbm_get_message_code (MM_GDBUS_CBM (self));
}

/*****************************************************************************/

/**
 * mm_cbm_get_update:
 * @self: A #MMCbm.
 *
 * Gets the update number of this CBM.
 *
 * Returns: The update number
 *
 * Since: 1.24
 */
guint
mm_cbm_get_update (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), 0);

    return mm_gdbus_cbm_get_update (MM_GDBUS_CBM (self));
}

/*****************************************************************************/

/**
 * mm_cbm_get_language:
 * @self: A #MMCbm.
 *
 * Gets the language the message is in as ISO639 two letter code
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_cbm_dup_language() if on another thread.</warning>
 *
 * Returns: (transfer none): The message's language, or %NULL if unknown
 *
 * Since: 1.26
 */
const gchar *
mm_cbm_get_language (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_cbm_get_language (MM_GDBUS_CBM (self)));
}

/**
 * mm_cbm_dup_language:
 * @self: A #MMCbm.
 *
 * Gets the language the message is in as ISO639 two letter code
 *
 * Returns: (transfer full): The message lang, or %NULL if it doesn't contain
 * any (e.g. contains data instead). The returned value should be freed with
 * g_free().
 *
 * Since: 1.26
 */
gchar *
mm_cbm_dup_language (MMCbm *self)
{
    g_return_val_if_fail (MM_IS_CBM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_cbm_dup_language (MM_GDBUS_CBM (self)));
}

/*****************************************************************************/

static void
mm_cbm_init (MMCbm *self)
{
}

static void
mm_cbm_class_init (MMCbmClass *cbm_class)
{
}
