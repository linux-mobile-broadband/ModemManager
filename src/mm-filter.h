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
 * Copyright (C) 2017 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_FILTER_H
#define MM_FILTER_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mm-device.h"
#include "mm-kernel-device.h"

#define MM_TYPE_FILTER            (mm_filter_get_type ())
#define MM_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_FILTER, MMFilter))
#define MM_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_FILTER, MMFilterClass))
#define MM_IS_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_FILTER))
#define MM_IS_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_FILTER))
#define MM_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_FILTER, MMFilterClass))

#define MM_FILTER_ENABLED_RULES "enabled-rules" /* construct-only */

typedef struct _MMFilterPrivate MMFilterPrivate;

typedef struct {
    GObject          parent;
    MMFilterPrivate *priv;
} MMFilter;

typedef struct {
    GObjectClass parent;
} MMFilterClass;

GType mm_filter_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMFilter, g_object_unref)

typedef enum { /*< underscore_name=mm_filter_rule >*/
    MM_FILTER_RULE_NONE                  = 0,
    MM_FILTER_RULE_EXPLICIT_ALLOWLIST    = 1 << 0,
    MM_FILTER_RULE_EXPLICIT_BLOCKLIST    = 1 << 1,
    MM_FILTER_RULE_PLUGIN_ALLOWLIST      = 1 << 2,
    MM_FILTER_RULE_QRTR                  = 1 << 3,
    MM_FILTER_RULE_VIRTUAL               = 1 << 4,
    MM_FILTER_RULE_NET                   = 1 << 5,
    MM_FILTER_RULE_USBMISC               = 1 << 6,
    MM_FILTER_RULE_RPMSG                 = 1 << 7,
    MM_FILTER_RULE_WWAN                  = 1 << 8,
    MM_FILTER_RULE_TTY                   = 1 << 9,
    MM_FILTER_RULE_TTY_PLATFORM_DRIVER   = 1 << 10,
    MM_FILTER_RULE_TTY_DRIVER            = 1 << 11,
    MM_FILTER_RULE_TTY_ACM_INTERFACE     = 1 << 12,
    MM_FILTER_RULE_TTY_WITH_NET          = 1 << 13,
    MM_FILTER_RULE_TTY_DEFAULT_FORBIDDEN = 1 << 14,
} MMFilterRule;

/* This is a stricter policy which will only automatically probe device ports
 * if they are allowed by any of the automatic allowlist rules. */
#define MM_FILTER_POLICY_STRICT             \
    (MM_FILTER_RULE_EXPLICIT_ALLOWLIST    | \
     MM_FILTER_RULE_EXPLICIT_BLOCKLIST    | \
     MM_FILTER_RULE_PLUGIN_ALLOWLIST      | \
     MM_FILTER_RULE_QRTR                  | \
     MM_FILTER_RULE_VIRTUAL               | \
     MM_FILTER_RULE_NET                   | \
     MM_FILTER_RULE_USBMISC               | \
     MM_FILTER_RULE_RPMSG                 | \
     MM_FILTER_RULE_WWAN                  | \
     MM_FILTER_RULE_TTY                   | \
     MM_FILTER_RULE_TTY_PLATFORM_DRIVER   | \
     MM_FILTER_RULE_TTY_DRIVER            | \
     MM_FILTER_RULE_TTY_ACM_INTERFACE     | \
     MM_FILTER_RULE_TTY_WITH_NET          | \
     MM_FILTER_RULE_TTY_DEFAULT_FORBIDDEN)

/* This policy only allows using device ports explicitly allowlisted via
 * udev rules. i.e. ModemManager won't do any kind of automatic probing. */
#define MM_FILTER_POLICY_ALLOWLIST_ONLY MM_FILTER_RULE_EXPLICIT_ALLOWLIST

/* The strict policy has all supported rules */
#define MM_FILTER_RULE_ALL MM_FILTER_POLICY_STRICT

MMFilter *mm_filter_new (MMFilterRule   enabled_rules,
                         GError       **error);

gboolean mm_filter_port (MMFilter        *self,
                         MMKernelDevice  *port,
                         gboolean         manual_scan);

gboolean mm_filter_device_and_port (MMFilter       *self,
                                    MMDevice       *device,
                                    MMKernelDevice *port);

void     mm_filter_register_plugin_allowlist_tag                 (MMFilter    *self,
                                                                  const gchar *tag);
void     mm_filter_register_plugin_allowlist_vendor_id           (MMFilter    *self,
                                                                  guint16      vid);
void     mm_filter_register_plugin_allowlist_product_id          (MMFilter    *self,
                                                                  guint16      vid,
                                                                  guint16      pid);
void     mm_filter_register_plugin_allowlist_subsystem_vendor_id (MMFilter    *self,
                                                                  guint16      vid,
                                                                  guint16      subsystem_vid);

gboolean mm_filter_check_rule_enabled (MMFilter     *self,
                                       MMFilterRule  rule);

#endif /* MM_FILTER_H */
