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
 */

#ifndef MM_CALLBACK_INFO_H
#define MM_CALLBACK_INFO_H

#include "mm-modem.h"

typedef struct _MMCallbackInfo MMCallbackInfo;

typedef void (*MMCallbackInfoInvokeFn) (MMCallbackInfo *info);

struct _MMCallbackInfo {
    guint32 refcount;

    /* # of ops left in this callback chain */
    guint32 chain_left;

    GData *qdata;
    MMModem *modem;

    MMCallbackInfoInvokeFn invoke_fn;
    GCallback callback;
    gboolean called;

    gpointer user_data;
    GError *error;
    guint pending_id;
};

MMCallbackInfo *mm_callback_info_new_full (MMModem *modem,
                                           MMCallbackInfoInvokeFn invoke_fn,
                                           GCallback callback,
                                           gpointer user_data);

MMCallbackInfo *mm_callback_info_new      (MMModem *modem,
                                           MMModemFn callback,
                                           gpointer user_data);

MMCallbackInfo *mm_callback_info_uint_new (MMModem *modem,
                                           MMModemUIntFn callback,
                                           gpointer user_data);

MMCallbackInfo *mm_callback_info_string_new (MMModem *modem,
                                             MMModemStringFn callback,
                                             gpointer user_data);

void            mm_callback_info_schedule (MMCallbackInfo *info);
void            mm_callback_info_set_result (MMCallbackInfo *info,
                                             gpointer data,
                                             GDestroyNotify destroy);

void            mm_callback_info_set_data (MMCallbackInfo *info,
                                           const char *key,
                                           gpointer data,
                                           GDestroyNotify destroy);

gpointer        mm_callback_info_get_data (MMCallbackInfo *info,
                                           const char *key);

MMCallbackInfo *mm_callback_info_ref (MMCallbackInfo *info);
void            mm_callback_info_unref (MMCallbackInfo *info);

void            mm_callback_info_chain_start (MMCallbackInfo *info, guint num);
void            mm_callback_info_chain_complete_one (MMCallbackInfo *info);

#endif /* MM_CALLBACK_INFO_H */

