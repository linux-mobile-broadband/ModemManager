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
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include "mm-callback-info.h"
#include "mm-errors.h"

#define CALLBACK_INFO_RESULT "callback-info-result"

static void
invoke_mm_modem_fn (MMCallbackInfo *info)
{
    MMModemFn callback = (MMModemFn) info->callback;

    callback (info->modem, info->error, info->user_data);
}

static void
invoke_mm_modem_uint_fn (MMCallbackInfo *info)
{
    MMModemUIntFn callback = (MMModemUIntFn) info->callback;

    callback (info->modem,
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, CALLBACK_INFO_RESULT)),
              info->error, info->user_data);
}

static void
invoke_mm_modem_string_fn (MMCallbackInfo *info)
{
    MMModemStringFn callback = (MMModemStringFn) info->callback;

    callback (info->modem,
              (const char *) mm_callback_info_get_data (info, CALLBACK_INFO_RESULT),
              info->error, info->user_data);
}

static void
modem_destroyed_cb (gpointer data, GObject *destroyed)
{
    MMCallbackInfo *info = data;

    /* Reset modem pointer, so that callback know that they shouldn't do
     * anything else */
    info->modem = NULL;

    /* Overwrite any possible previous error set */
    g_clear_error (&(info->error));
    info->error = g_error_new_literal (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_REMOVED,
                                       "The modem was removed.");

    /* Only schedule the info if not already done before */
    if (!info->pending_id)
        mm_callback_info_schedule (info);
}

static void
callback_info_done (gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    info->pending_id = 0;
    info->called = TRUE;

    if (info->invoke_fn && info->callback)
        info->invoke_fn (info);

    mm_callback_info_unref (info);
}

static gboolean
callback_info_do (gpointer user_data)
{
    /* Nothing here, everything is done in callback_info_done to make sure the info->callback
       always gets called, even if the pending call gets cancelled. */
    return FALSE;
}

void
mm_callback_info_schedule (MMCallbackInfo *info)
{
    g_return_if_fail (info != NULL);
    g_return_if_fail (info->pending_id == 0);
    g_return_if_fail (info->called == FALSE);

    g_warn_if_fail (info->chain_left == 0);

    info->pending_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, callback_info_do, info, callback_info_done);
}

MMCallbackInfo *
mm_callback_info_new_full (MMModem *modem,
                           MMCallbackInfoInvokeFn invoke_fn,
                           GCallback callback,
                           gpointer user_data)
{
    MMCallbackInfo *info;

    g_return_val_if_fail (modem != NULL, NULL);

    info = g_slice_new0 (MMCallbackInfo);
    g_datalist_init (&info->qdata);
    info->modem = modem;
    g_object_weak_ref (G_OBJECT (modem), modem_destroyed_cb, info);
    info->invoke_fn = invoke_fn;
    info->callback = callback;
    info->user_data = user_data;
    info->refcount = 1;

    return info;
}

MMCallbackInfo *
mm_callback_info_new (MMModem *modem, MMModemFn callback, gpointer user_data)
{
    g_return_val_if_fail (modem != NULL, NULL);

    return mm_callback_info_new_full (modem, invoke_mm_modem_fn, (GCallback) callback, user_data);
}

MMCallbackInfo *
mm_callback_info_uint_new (MMModem *modem,
                           MMModemUIntFn callback,
                           gpointer user_data)
{
    g_return_val_if_fail (modem != NULL, NULL);

    return mm_callback_info_new_full (modem, invoke_mm_modem_uint_fn, (GCallback) callback, user_data);
}

MMCallbackInfo *
mm_callback_info_string_new (MMModem *modem,
                             MMModemStringFn callback,
                             gpointer user_data)
{
    g_return_val_if_fail (modem != NULL, NULL);

    return mm_callback_info_new_full (modem, invoke_mm_modem_string_fn, (GCallback) callback, user_data);
}

gpointer
mm_callback_info_get_result (MMCallbackInfo *info)
{
    g_return_val_if_fail (info != NULL, NULL);

    return mm_callback_info_get_data (info, CALLBACK_INFO_RESULT);
}

void
mm_callback_info_set_result (MMCallbackInfo *info,
                             gpointer data,
                             GDestroyNotify destroy)
{
    g_return_if_fail (info != NULL);

    mm_callback_info_set_data (info, CALLBACK_INFO_RESULT, data, destroy);
}

void
mm_callback_info_set_data (MMCallbackInfo *info,
                           const char *key,
                           gpointer data,
                           GDestroyNotify destroy)
{
    g_return_if_fail (info != NULL);
    g_return_if_fail (key != NULL);

    g_datalist_id_set_data_full (&info->qdata, g_quark_from_string (key), data,
                                 data ? destroy : (GDestroyNotify) NULL);
}

gpointer
mm_callback_info_get_data (MMCallbackInfo *info, const char *key)
{
    GQuark quark;

    g_return_val_if_fail (info != NULL, NULL);
    g_return_val_if_fail (key != NULL, NULL);

    quark = g_quark_try_string (key);

    return quark ? g_datalist_id_get_data (&info->qdata, quark) : NULL;
}

gboolean
mm_callback_info_check_modem_removed (MMCallbackInfo *info)
{
    g_return_val_if_fail (info != NULL, TRUE);

    return (info->modem ? FALSE : TRUE);
}

MMCallbackInfo *
mm_callback_info_ref (MMCallbackInfo *info)
{
    g_return_val_if_fail (info != NULL, NULL);
    g_return_val_if_fail (info->refcount > 0, NULL);

    info->refcount++;
    return info;
}

void
mm_callback_info_unref (MMCallbackInfo *info)
{
    g_return_if_fail (info != NULL);

    info->refcount--;
    if (info->refcount == 0) {
        if (info->error)
            g_error_free (info->error);

        if (info->modem)
            g_object_weak_unref (G_OBJECT (info->modem), modem_destroyed_cb, info);

        g_datalist_clear (&info->qdata);
        g_slice_free (MMCallbackInfo, info);
    }
}

void
mm_callback_info_chain_start (MMCallbackInfo *info, guint num)
{
    g_return_if_fail (info != NULL);
    g_return_if_fail (num > 0);
    g_return_if_fail (info->chain_left == 0);

    info->chain_left = num;
}

void
mm_callback_info_chain_complete_one (MMCallbackInfo *info)
{
    g_return_if_fail (info != NULL);
    g_return_if_fail (info->chain_left > 0);

    info->chain_left--;
    if (info->chain_left == 0)
        mm_callback_info_schedule (info);
}

