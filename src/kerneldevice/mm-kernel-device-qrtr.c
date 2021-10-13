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
 * Copyright 2020 Google LLC
 */

#include <string.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <ModemManager-tags.h>

#include "mm-kernel-device-qrtr.h"

G_DEFINE_TYPE (MMKernelDeviceQrtr, mm_kernel_device_qrtr,  MM_TYPE_KERNEL_DEVICE)

enum {
    PROP_0,
    PROP_QRTR_NODE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMKernelDeviceQrtrPrivate {
    QrtrNode *node;
    gchar    *name;
    gchar    *physdev_uid;
};

/*****************************************************************************/

gchar *
mm_kernel_device_qrtr_helper_build_name (guint32 node_id)
{
    return g_strdup_printf ("qrtr%u", node_id);
}

/*****************************************************************************/

QrtrNode *
mm_kernel_device_qrtr_get_node (MMKernelDeviceQrtr *self)
{
    return g_object_ref (self->priv->node);
}

/*****************************************************************************/

static gboolean
kernel_device_cmp (MMKernelDevice *_a,
                   MMKernelDevice *_b)
{
    MMKernelDeviceQrtr *a;
    MMKernelDeviceQrtr *b;

    a = MM_KERNEL_DEVICE_QRTR (_a);
    b = MM_KERNEL_DEVICE_QRTR (_b);

    return qrtr_node_get_id (a->priv->node) == qrtr_node_get_id (b->priv->node);
}

static gboolean
kernel_device_has_property (MMKernelDevice *_self,
                            const gchar    *property)
{
    MMKernelDeviceQrtr *self;

    self = MM_KERNEL_DEVICE_QRTR (_self);

    return !!g_object_get_data (G_OBJECT (self), property);
}

static const gchar *
kernel_device_get_property (MMKernelDevice *_self,
                            const gchar    *property)
{
    MMKernelDeviceQrtr *self;

    self = MM_KERNEL_DEVICE_QRTR (_self);

    return g_object_get_data (G_OBJECT (self), property);
}

static const gchar *
kernel_device_get_driver (MMKernelDevice *_self)
{
    return MM_KERNEL_DEVICE_QRTR_DRIVER;
}

static const gchar *
kernel_device_get_name (MMKernelDevice *_self)
{
    MMKernelDeviceQrtr *self;

    self = MM_KERNEL_DEVICE_QRTR (_self);
    if (!self->priv->name)
        self->priv->name = mm_kernel_device_qrtr_helper_build_name (qrtr_node_get_id (self->priv->node));

    return self->priv->name;
}

static const gchar *
kernel_device_get_physdev_uid (MMKernelDevice *_self)
{
    return MM_KERNEL_DEVICE_QRTR_PHYSDEV_UID;
}

static const gchar *
kernel_device_get_subsystem (MMKernelDevice *_self)
{
    return MM_KERNEL_DEVICE_QRTR_SUBSYSTEM;
}
/*****************************************************************************/

MMKernelDevice *
mm_kernel_device_qrtr_new (QrtrNode *qrtr_node)
{
    MMKernelDevice *self;

    self = MM_KERNEL_DEVICE (g_object_new (MM_TYPE_KERNEL_DEVICE_QRTR,
                                           "qrtr-node", qrtr_node,
                                           NULL));
    return self;
}

/*****************************************************************************/

static void
mm_kernel_device_qrtr_init (MMKernelDeviceQrtr *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_KERNEL_DEVICE_QRTR, MMKernelDeviceQrtrPrivate);

    /* Set properties*/
    g_object_set_data_full (G_OBJECT (self), ID_MM_PORT_TYPE_QMI, g_strdup ("true"), g_free);
    g_object_set_data_full (G_OBJECT (self), ID_MM_CANDIDATE, g_strdup ("1"), g_free);
    /* For now we're assuming that QRTR ports are available exclusively on Qualcomm SoCs */
    g_object_set_data_full (G_OBJECT (self), "ID_MM_QCOM_SOC", g_strdup ("1"), g_free);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMKernelDeviceQrtr *self = MM_KERNEL_DEVICE_QRTR (object);

    switch (prop_id) {
    case PROP_QRTR_NODE:
        g_assert (!self->priv->node);
        self->priv->node = g_value_dup_object (value);
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
    MMKernelDeviceQrtr *self = MM_KERNEL_DEVICE_QRTR (object);

    switch (prop_id) {
    case PROP_QRTR_NODE:
        g_value_set_object (value, self->priv->node);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
    MMKernelDeviceQrtr *self = MM_KERNEL_DEVICE_QRTR (object);

    g_clear_pointer (&self->priv->name, g_free);
    g_clear_pointer (&self->priv->physdev_uid, g_free);
    g_object_unref (self->priv->node);

    G_OBJECT_CLASS (mm_kernel_device_qrtr_parent_class)->dispose (object);
}

static void
mm_kernel_device_qrtr_class_init (MMKernelDeviceQrtrClass *klass)
{
    GObjectClass        *object_class        = G_OBJECT_CLASS (klass);
    MMKernelDeviceClass *kernel_device_class = MM_KERNEL_DEVICE_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMKernelDeviceQrtrPrivate));

    object_class->dispose      = dispose;
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    kernel_device_class->get_driver                     = kernel_device_get_driver;
    kernel_device_class->get_name                       = kernel_device_get_name;
    kernel_device_class->get_physdev_uid                = kernel_device_get_physdev_uid;
    kernel_device_class->get_subsystem                  = kernel_device_get_subsystem;
    kernel_device_class->cmp                            = kernel_device_cmp;
    kernel_device_class->has_property                   = kernel_device_has_property;
    kernel_device_class->get_property                   = kernel_device_get_property;

    /* Device-wide properties are stored per-port in the qrtr backend */
    kernel_device_class->has_global_property            = kernel_device_has_property;
    kernel_device_class->get_global_property            = kernel_device_get_property;

    properties[PROP_QRTR_NODE] =
        g_param_spec_object ("qrtr-node",
                             "qrtr node",
                             "Node object as reported by QrtrNode",
                             QRTR_TYPE_NODE,
                             G_PARAM_READWRITE);

    g_object_class_install_properties (object_class, PROP_LAST, properties);
}
