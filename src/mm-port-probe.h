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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_PORT_PROBE_H
#define MM_PORT_PROBE_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#define MM_TYPE_PORT_PROBE            (mm_port_probe_get_type ())
#define MM_PORT_PROBE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_PROBE, MMPortProbe))
#define MM_PORT_PROBE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_PROBE, MMPortProbeClass))
#define MM_IS_PORT_PROBE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_PROBE))
#define MM_IS_PLUBIN_PROBE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_PROBE))
#define MM_PORT_PROBE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_PROBE, MMPortProbeClass))

/* Flags to request port probing */
#define MM_PORT_PROBE_AT              0x0001
#define MM_PORT_PROBE_AT_CAPABILITIES 0x0002
#define MM_PORT_PROBE_AT_VENDOR       0x0004

/* Flags to report probed capabilities */
#define MM_PORT_PROBE_CAPABILITY_GSM     0x0001 /* GSM */
#define MM_PORT_PROBE_CAPABILITY_IS707_A 0x0002 /* CDMA Circuit Switched Data */
#define MM_PORT_PROBE_CAPABILITY_IS707_P 0x0004 /* CDMA Packet Switched Data */
#define MM_PORT_PROBE_CAPABILITY_DS      0x0008 /* Data compression selection (v.42bis) */
#define MM_PORT_PROBE_CAPABILITY_ES      0x0010 /* Error control selection (v.42) */
#define MM_PORT_PROBE_CAPABILITY_FCLASS  0x0020 /* Group III Fax */
#define MM_PORT_PROBE_CAPABILITY_MS      0x0040 /* Modulation selection */
#define MM_PORT_PROBE_CAPABILITY_W       0x0080 /* Wireless commands */
#define MM_PORT_PROBE_CAPABILITY_IS856   0x0100 /* CDMA 3G EVDO rev 0 */
#define MM_PORT_PROBE_CAPABILITY_IS856_A 0x0200 /* CDMA 3G EVDO rev A */

#define MM_PORT_PROBE_CAPABILITY_CDMA        \
    (MM_PORT_PROBE_CAPABILITY_IS707_A |      \
     MM_PORT_PROBE_CAPABILITY_IS707_P |      \
     MM_PORT_PROBE_CAPABILITY_IS856 |        \
     MM_PORT_PROBE_CAPABILITY_IS856_A)

#define MM_PORT_PROBE_CAPABILITY_GSM_OR_CDMA \
    (MM_PORT_PROBE_CAPABILITY_CDMA |         \
     MM_PORT_PROBE_CAPABILITY_GSM)

typedef struct _MMPortProbe MMPortProbe;
typedef struct _MMPortProbeClass MMPortProbeClass;
typedef struct _MMPortProbePrivate MMPortProbePrivate;

struct _MMPortProbe {
    GObject parent;
    MMPortProbePrivate *priv;
};

struct _MMPortProbeClass {
    GObjectClass parent;
};

GType mm_port_probe_get_type (void);

MMPortProbe *mm_port_probe_new (GUdevDevice *port,
                                const gchar *physdev_path,
                                const gchar *driver);

GUdevDevice *mm_port_probe_get_port         (MMPortProbe *self);
const gchar *mm_port_probe_get_port_name    (MMPortProbe *self);
const gchar *mm_port_probe_get_port_subsys  (MMPortProbe *self);
const gchar *mm_port_probe_get_port_physdev (MMPortProbe *self);
const gchar *mm_port_probe_get_port_driver  (MMPortProbe *self);

/* Run probing */
void     mm_port_probe_run        (MMPortProbe *self,
                                   guint32 flags,
                                   guint64 at_send_delay,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_port_probe_run_finish (MMPortProbe *self,
                                   GAsyncResult *result,
                                   GError **error);
gboolean mm_port_probe_run_cancel (MMPortProbe *self);

/* Probing result getters */
gboolean     mm_port_probe_is_at            (MMPortProbe *self);
guint32      mm_port_probe_get_capabilities (MMPortProbe *self);
const gchar *mm_port_probe_get_vendor       (MMPortProbe *self);

#endif /* MM_PORT_PROBE_H */

