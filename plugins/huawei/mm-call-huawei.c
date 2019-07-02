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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-huawei.h"
#include "mm-call-huawei.h"

G_DEFINE_TYPE (MMCallHuawei, mm_call_huawei, MM_TYPE_BASE_CALL)

enum {
    PROP_0,
    PROP_AUDIO_HZ,
    PROP_AUDIO_BITS,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMCallHuaweiPrivate {
    guint audio_hz;
    guint audio_bits;
};

/*****************************************************************************/
/* Audio channel setup */

typedef struct {
    MMBaseModem       *modem;
    MMPort            *audio_port;
    MMCallAudioFormat *audio_format;
} SetupAudioChannelContext;

static void
setup_audio_channel_context_free (SetupAudioChannelContext *ctx)
{
    g_clear_object (&ctx->audio_port);
    g_clear_object (&ctx->audio_format);
    g_clear_object (&ctx->modem);
    g_slice_free (SetupAudioChannelContext, ctx);
}

static gboolean
setup_audio_channel_finish (MMBaseCall         *self,
                            GAsyncResult       *res,
                            MMPort            **audio_port,
                            MMCallAudioFormat **audio_format,
                            GError            **error)
{
    SetupAudioChannelContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));

    if (audio_port && ctx->audio_port)
        *audio_port = g_object_ref (ctx->audio_port);
    if (audio_format && ctx->audio_format)
        *audio_format = g_object_ref (ctx->audio_format);

    return TRUE;
}

static void
ddsetex_ready (MMBaseModem  *modem,
               GAsyncResult *res,
               GTask        *task)
{
    MMCallHuawei             *self;
    SetupAudioChannelContext *ctx;
    GError                   *error = NULL;
    const gchar              *response = NULL;
    gchar                    *resolution_str;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        mm_dbg ("Error enabling audio streaming: '%s'", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* Setup audio format */
    g_assert (self->priv->audio_hz && self->priv->audio_bits);
    resolution_str = g_strdup_printf ("s%ule", self->priv->audio_bits);
    ctx->audio_format = mm_call_audio_format_new ();
    mm_call_audio_format_set_encoding   (ctx->audio_format, "pcm");
    mm_call_audio_format_set_resolution (ctx->audio_format, resolution_str);
    mm_call_audio_format_set_rate       (ctx->audio_format, self->priv->audio_hz);

    /* The QCDM port, if present, switches from QCDM to voice while
     * a voice call is active. */
    ctx->audio_port = MM_PORT (mm_base_modem_get_port_qcdm (modem));

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
setup_audio_channel (MMBaseCall           *_self,
                     GAsyncReadyCallback   callback,
                     gpointer              user_data)
{
    SetupAudioChannelContext *ctx;
    MMCallHuawei             *self;
    GTask                    *task;
    MMBaseModem              *modem = NULL;

    self = MM_CALL_HUAWEI (_self);

    task = g_task_new (self, NULL, callback, user_data);

    /* If there is no CVOICE support, no custom audio setup required
     * (i.e. audio path is externally managed) */
    if (!self->priv->audio_hz && !self->priv->audio_bits) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetupAudioChannelContext);
    g_object_get (self,
                  MM_BASE_CALL_MODEM, &ctx->modem,
                  NULL);
    g_task_set_task_data (task, ctx, (GDestroyNotify) setup_audio_channel_context_free);

    /* Enable audio streaming on the audio port */
    mm_base_modem_at_command (modem,
                              "AT^DDSETEX=2",
                              5,
                              FALSE,
                              (GAsyncReadyCallback)ddsetex_ready,
                              task);
}

/*****************************************************************************/

MMBaseCall *
mm_call_huawei_new (MMBaseModem     *modem,
                    MMCallDirection  direction,
                    const gchar     *number,
                    guint            audio_hz,
                    guint            audio_bits)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_CALL_HUAWEI,
                                       MM_BASE_CALL_MODEM,        modem,
                                       "direction",               direction,
                                       "number",                  number,
                                       MM_CALL_HUAWEI_AUDIO_HZ,   audio_hz,
                                       MM_CALL_HUAWEI_AUDIO_BITS, audio_bits,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
}

static void
mm_call_huawei_init (MMCallHuawei *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CALL_HUAWEI, MMCallHuaweiPrivate);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMCallHuawei *self = MM_CALL_HUAWEI (object);

    switch (prop_id) {
    case PROP_AUDIO_HZ:
        self->priv->audio_hz = g_value_get_uint (value);
        break;
    case PROP_AUDIO_BITS:
        self->priv->audio_bits = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMCallHuawei *self = MM_CALL_HUAWEI (object);

    switch (prop_id) {
    case PROP_AUDIO_HZ:
        g_value_set_uint (value, self->priv->audio_hz);
        break;
    case PROP_AUDIO_BITS:
        g_value_set_uint (value, self->priv->audio_bits);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_call_huawei_class_init (MMCallHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallHuaweiPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    base_call_class->setup_audio_channel          = setup_audio_channel;
    base_call_class->setup_audio_channel_finish   = setup_audio_channel_finish;

    properties[PROP_AUDIO_HZ] =
        g_param_spec_uint (MM_CALL_HUAWEI_AUDIO_HZ,
                           "Audio Hz",
                           "Voice call audio hz if call audio is routed via the host",
                           0, 24000, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_AUDIO_HZ, properties[PROP_AUDIO_HZ]);

    properties[PROP_AUDIO_BITS] =
        g_param_spec_uint (MM_CALL_HUAWEI_AUDIO_BITS,
                           "Audio Bits",
                           "Voice call audio bits if call audio is routed via the host",
                           0, 24, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_AUDIO_BITS, properties[PROP_AUDIO_BITS]);
}
