/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_CALLBACK_INFO_H
#define MM_CALLBACK_INFO_H

#include "mm-modem.h"

typedef struct _MMCallbackInfo MMCallbackInfo;

typedef void (*MMCallbackInfoInvokeFn) (MMCallbackInfo *info);

struct _MMCallbackInfo {
    GData *qdata;
    MMModem *modem;

    MMCallbackInfoInvokeFn invoke_fn;
    GCallback callback;

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

#endif /* MM_CALLBACK_INFO_H */
