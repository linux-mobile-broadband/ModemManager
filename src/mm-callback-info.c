/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "mm-callback-info.h"
#include "mm-modem-error.h"

static void
callback_info_done (gpointer user_data)
{
	MMCallbackInfo *info = (MMCallbackInfo *) user_data;

	info->pending_id = 0;

    if (info->callback)
        info->callback (info->modem, info->error, info->user_data);
    else if (info->uint_callback)
        info->uint_callback (info->modem, info->uint_result, info->error, info->user_data);

	if (info->error)
		g_error_free (info->error);

	g_object_unref (info->modem);
	g_datalist_clear (&info->qdata);

	g_slice_free (MMCallbackInfo, info);
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
	info->pending_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, callback_info_do, info, callback_info_done);
}

void
mm_callback_info_cancel (MMCallbackInfo *info)
{
    if (info->pending_id) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "%s", "Call cancelled");
        mm_callback_info_schedule (info);
    }
}

MMCallbackInfo *
mm_callback_info_new (MMModem *modem, MMModemFn callback, gpointer user_data)
{
	MMCallbackInfo *info;

	info = g_slice_new0 (MMCallbackInfo);
	g_datalist_init (&info->qdata);
	info->modem = g_object_ref (modem);
	info->callback = callback;
	info->user_data = user_data;

	return info;
}

MMCallbackInfo *
mm_callback_info_uint_new (MMModem *modem,
                           MMModemUIntFn callback,
                           gpointer user_data)
{
    MMCallbackInfo *info;

	info = g_slice_new0 (MMCallbackInfo);
	g_datalist_init (&info->qdata);
	info->modem = g_object_ref (modem);
	info->uint_callback = callback;
	info->user_data = user_data;

	return info;
}

void
mm_callback_info_set_data (MMCallbackInfo *info,
                           const char *key,
                           gpointer data,
                           GDestroyNotify destroy)
{
	g_datalist_id_set_data_full (&info->qdata, g_quark_from_string (key), data,
                                 data ? destroy : (GDestroyNotify) NULL);
}

gpointer
mm_callback_info_get_data (MMCallbackInfo *info, const char *key)
{
	GQuark quark;

	quark = g_quark_try_string (key);

	return quark ? g_datalist_id_get_data (&info->qdata, quark) : NULL;
}
