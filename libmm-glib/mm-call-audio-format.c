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
 * Copyright (C) 2017 Red Hat, Inc.
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-call-audio-format.h"

/**
 * SECTION: mm-call-audio-format
 * @title: MMCallAudioFormat
 * @short_description: Helper object to handle voice call audio formats.
 *
 * The #MMCallAudioFormat is an object handling the voice call audio format
 * which describes how to send/receive voice call audio from the host.
 *
 * This object is retrieved with either mm_call_get_audio_format() or
 * mm_call_peek_audio_format().
 */

G_DEFINE_TYPE (MMCallAudioFormat, mm_call_audio_format, G_TYPE_OBJECT)

#define PROPERTY_ENCODING   "encoding"
#define PROPERTY_RESOLUTION "resolution"
#define PROPERTY_RATE       "rate"

struct _MMCallAudioFormatPrivate {
    gchar *encoding;
    gchar *resolution;
    guint rate;
};

/*****************************************************************************/

/**
 * mm_call_audio_format_get_encoding:
 * @self: a #MMCallAudioFormat.
 *
 * Gets the encoding of the audio format.  For example, "pcm" for PCM-encoded
 * audio.
 *
 * Returns: a string with the encoding, or #NULL if unknown. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.10
 */
const gchar *
mm_call_audio_format_get_encoding (MMCallAudioFormat *self)
{
    g_return_val_if_fail (MM_IS_CALL_AUDIO_FORMAT (self), NULL);

    return self->priv->encoding;
}

/**
 * mm_call_audio_format_set_encoding: (skip)
 */
void
mm_call_audio_format_set_encoding (MMCallAudioFormat *self,
                                   const gchar *encoding)
{
    g_return_if_fail (MM_IS_CALL_AUDIO_FORMAT (self));

    g_free (self->priv->encoding);
    self->priv->encoding = g_strdup (encoding);
}

/*****************************************************************************/

/**
 * mm_call_audio_format_get_resolution:
 * @self: a #MMCallAudioFormat.
 *
 * Gets the resolution of the audio format.  For example, "s16le" for signed
 * 16-bit little-endian audio sampling resolution.
 *
 * Returns: a string with the resolution, or #NULL if unknown. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.10
 */
const gchar *
mm_call_audio_format_get_resolution (MMCallAudioFormat *self)
{
    g_return_val_if_fail (MM_IS_CALL_AUDIO_FORMAT (self), NULL);

    return self->priv->resolution;
}

/**
 * mm_call_audio_format_set_resolution: (skip)
 */
void
mm_call_audio_format_set_resolution (MMCallAudioFormat *self,
                                     const gchar *resolution)
{
    g_return_if_fail (MM_IS_CALL_AUDIO_FORMAT (self));

    g_free (self->priv->resolution);
    self->priv->resolution = g_strdup (resolution);
}

/*****************************************************************************/

/**
 * mm_call_audio_format_get_rate:
 * @self: a #MMCallAudioFormat.
 *
 * Gets the sampling rate of the audio format.  For example, 8000 for an 8000hz
 * sampling rate.
 *
 * Returns: the sampling rate, or 0 if unknown.
 *
 * Since: 1.10
 */
guint
mm_call_audio_format_get_rate (MMCallAudioFormat *self)
{
    g_return_val_if_fail (MM_IS_CALL_AUDIO_FORMAT (self), 0);

    return self->priv->rate;
}

/**
 * mm_call_audio_format_set_rate: (skip)
 */
void
mm_call_audio_format_set_rate (MMCallAudioFormat *self,
                               guint rate)
{
    g_return_if_fail (MM_IS_CALL_AUDIO_FORMAT (self));

    self->priv->rate = rate;
}

/*****************************************************************************/

/**
 * mm_call_audio_format_get_dictionary: (skip)
 */
GVariant *
mm_call_audio_format_get_dictionary (MMCallAudioFormat *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->encoding)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ENCODING,
                               g_variant_new_string (self->priv->encoding));

    if (self->priv->resolution)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_RESOLUTION,
                               g_variant_new_string (self->priv->resolution));

    if (self->priv->rate)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_RATE,
                               g_variant_new_uint32 (self->priv->rate));

    return g_variant_builder_end (&builder);
}

/*****************************************************************************/

/**
 * mm_call_audio_format_new_from_dictionary: (skip)
 */
MMCallAudioFormat *
mm_call_audio_format_new_from_dictionary (GVariant *dictionary,
                                          GError **error)
{
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMCallAudioFormat *self;

    self = mm_call_audio_format_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create call audio format from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_ENCODING))
            mm_call_audio_format_set_encoding (
                self,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_RESOLUTION))
            mm_call_audio_format_set_resolution (
                self,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_RATE))
            mm_call_audio_format_set_rate (
                self,
                g_variant_get_uint32 (value));

        g_free (key);
        g_variant_unref (value);
    }

    return self;
}

/*****************************************************************************/

/**
 * mm_call_audio_format_new: (skip)
 */
MMCallAudioFormat *
mm_call_audio_format_new (void)
{
    return (MM_CALL_AUDIO_FORMAT (
                g_object_new (MM_TYPE_CALL_AUDIO_FORMAT, NULL)));
}

static void
mm_call_audio_format_init (MMCallAudioFormat *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_CALL_AUDIO_FORMAT,
                                              MMCallAudioFormatPrivate);
}

static void
finalize (GObject *object)
{
    MMCallAudioFormat *self = MM_CALL_AUDIO_FORMAT (object);

    g_free (self->priv->encoding);
    g_free (self->priv->resolution);

    G_OBJECT_CLASS (mm_call_audio_format_parent_class)->finalize (object);
}

static void
mm_call_audio_format_class_init (MMCallAudioFormatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallAudioFormatPrivate));

    object_class->finalize = finalize;
}
