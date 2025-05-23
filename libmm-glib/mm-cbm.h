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

#ifndef _MM_CBM_H_
#define _MM_CBM_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-cbm.h"

G_BEGIN_DECLS

#define MM_TYPE_CBM            (mm_cbm_get_type ())
#define MM_CBM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CBM, MMCbm))
#define MM_CBM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_CBM, MMCbmClass))
#define MM_IS_CBM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CBM))
#define MM_IS_CBM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_CBM))
#define MM_CBM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_CBM, MMCbmClass))

typedef struct _MMCbm MMCbm;
typedef struct _MMCbmClass MMCbmClass;

/**
 * MMCbm:
 *
 * The #MMCbm structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMCbm {
    /*< private >*/
    MmGdbusCbmProxy parent;
    gpointer unused;
};

struct _MMCbmClass {
    /*< private >*/
    MmGdbusCbmProxyClass parent;
};

GType mm_cbm_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCbm, g_object_unref)

const gchar  *mm_cbm_get_path                    (MMCbm *self);
gchar        *mm_cbm_dup_path                    (MMCbm *self);

const gchar  *mm_cbm_get_text                    (MMCbm *self);
gchar        *mm_cbm_dup_text                    (MMCbm *self);

MMCbmState    mm_cbm_get_state                   (MMCbm *self);

guint         mm_cbm_get_channel                 (MMCbm *self);
guint         mm_cbm_get_message_code            (MMCbm *self);
guint         mm_cbm_get_update                  (MMCbm *self);

const gchar  *mm_cbm_get_language                (MMCbm *self);
gchar        *mm_cbm_dup_language                (MMCbm *self);

G_END_DECLS

#endif /* _MM_CBM_H_ */
