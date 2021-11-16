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

#ifndef MM_CALL_AUDIO_FORMAT_H
#define MM_CALL_AUDIO_FORMAT_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_CALL_AUDIO_FORMAT            (mm_call_audio_format_get_type ())
#define MM_CALL_AUDIO_FORMAT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL_AUDIO_FORMAT, MMCallAudioFormat))
#define MM_CALL_AUDIO_FORMAT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CALL_AUDIO_FORMAT, MMCallAudioFormatClass))
#define MM_IS_CALL_AUDIO_FORMAT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL_AUDIO_FORMAT))
#define MM_IS_CALL_AUDIO_FORMAT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CALL_AUDIO_FORMAT))
#define MM_CALL_AUDIO_FORMAT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CALL_AUDIO_FORMAT, MMCallAudioFormatClass))

typedef struct _MMCallAudioFormat MMCallAudioFormat;
typedef struct _MMCallAudioFormatClass MMCallAudioFormatClass;
typedef struct _MMCallAudioFormatPrivate MMCallAudioFormatPrivate;

/**
 * MMCallAudioFormat:
 *
 * The #MMCallAudioFormat structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMCallAudioFormat {
    /*< private >*/
    GObject parent;
    MMCallAudioFormatPrivate *priv;
};

struct _MMCallAudioFormatClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_call_audio_format_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCallAudioFormat, g_object_unref)

const gchar *mm_call_audio_format_get_encoding   (MMCallAudioFormat *self);
const gchar *mm_call_audio_format_get_resolution (MMCallAudioFormat *self);
guint        mm_call_audio_format_get_rate       (MMCallAudioFormat *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMCallAudioFormat *mm_call_audio_format_new (void);
MMCallAudioFormat *mm_call_audio_format_new_from_dictionary (GVariant *dictionary,
                                                             GError **error);

void mm_call_audio_format_set_encoding   (MMCallAudioFormat *self,
                                          const gchar *encoding);
void mm_call_audio_format_set_resolution (MMCallAudioFormat *self,
                                          const gchar *resolution);
void mm_call_audio_format_set_rate       (MMCallAudioFormat *self,
                                          guint rate);

GVariant *mm_call_audio_format_get_dictionary (MMCallAudioFormat *self);

#endif

G_END_DECLS

#endif /* MM_CALL_AUDIO_FORMAT_H */
