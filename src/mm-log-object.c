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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "mm-log-object.h"

G_DEFINE_INTERFACE (MMLogObject, mm_log_object, G_TYPE_OBJECT)

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "log-object"
static GQuark private_quark;

typedef struct {
  gchar *owner_id;
  gchar *id;
} Private;

static void
private_free (Private *priv)
{
    g_free (priv->owner_id);
    g_free (priv->id);
    g_slice_free (Private, priv);
}

static Private *
get_private (MMLogObject *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);
        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

const gchar *
mm_log_object_get_id (MMLogObject *self)
{
    Private *priv;

    priv = get_private (self);
    if (!priv->id) {
        gchar *self_id;

        self_id = MM_LOG_OBJECT_GET_IFACE (self)->build_id (self);
        if (priv->owner_id) {
            priv->id = g_strdup_printf ("%s/%s", priv->owner_id, self_id);
            g_free (self_id);
        } else
            priv->id = self_id;
    }
    return priv->id;
}

void
mm_log_object_set_owner_id (MMLogObject *self,
                            const gchar *owner_id)
{
    Private *priv;

    priv = get_private (self);
    g_free (priv->owner_id);
    priv->owner_id = g_strdup (owner_id);
}

static void
mm_log_object_default_init (MMLogObjectInterface *iface)
{
}
