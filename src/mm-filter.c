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

#include <config.h>
#include <string.h>

#include <ModemManager.h>
#include <ModemManager-tags.h>

#include "mm-daemon-enums-types.h"
#include "mm-filter.h"
#include "mm-log-object.h"

#define FILTER_PORT_MAYBE_FORBIDDEN "maybe-forbidden"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMFilter, mm_filter, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_ENABLED_RULES,
    LAST_PROP
};

struct _MMFilterPrivate {
    MMFilterRule  enabled_rules;
    GList        *plugin_allowlist_tags;
    GArray       *plugin_allowlist_vendor_ids;
    GArray       *plugin_allowlist_product_ids;
    GArray       *plugin_allowlist_subsystem_vendor_ids;
};

/*****************************************************************************/

void
mm_filter_register_plugin_allowlist_tag (MMFilter    *self,
                                         const gchar *tag)
{
    if (!g_list_find_custom (self->priv->plugin_allowlist_tags, tag, (GCompareFunc) g_strcmp0)) {
        mm_obj_dbg (self, "registered plugin allowlist tag: %s", tag);
        self->priv->plugin_allowlist_tags = g_list_prepend (self->priv->plugin_allowlist_tags, g_strdup (tag));
    }
}

void
mm_filter_register_plugin_allowlist_vendor_id (MMFilter *self,
                                               guint16   vid)
{
    guint i;

    if (!self->priv->plugin_allowlist_vendor_ids)
        self->priv->plugin_allowlist_vendor_ids = g_array_sized_new (FALSE, FALSE, sizeof (guint16), 64);

    for (i = 0; i < self->priv->plugin_allowlist_vendor_ids->len; i++) {
        guint16 item;

        item = g_array_index (self->priv->plugin_allowlist_vendor_ids, guint16, i);
        if (item == vid)
            return;
    }

    g_array_append_val (self->priv->plugin_allowlist_vendor_ids, vid);
    mm_obj_dbg (self, "registered plugin allowlist vendor id: %04x", vid);
}

void
mm_filter_register_plugin_allowlist_product_id (MMFilter *self,
                                                guint16   vid,
                                                guint16   pid)
{
    mm_uint16_pair new_item;
    guint          i;

    if (!self->priv->plugin_allowlist_product_ids)
        self->priv->plugin_allowlist_product_ids = g_array_sized_new (FALSE, FALSE, sizeof (mm_uint16_pair), 10);

    for (i = 0; i < self->priv->plugin_allowlist_product_ids->len; i++) {
        mm_uint16_pair *item;

        item = &g_array_index (self->priv->plugin_allowlist_product_ids, mm_uint16_pair, i);
        if (item->l == vid && item->r == pid)
            return;
    }

    new_item.l = vid;
    new_item.r = pid;
    g_array_append_val (self->priv->plugin_allowlist_product_ids, new_item);
    mm_obj_dbg (self, "registered plugin allowlist product id: %04x:%04x", vid, pid);
}

void
mm_filter_register_plugin_allowlist_subsystem_vendor_id (MMFilter *self,
                                                         guint16   vid,
                                                         guint16   subsystem_vid)
{
    mm_uint16_pair new_item;
    guint          i;

    if (!self->priv->plugin_allowlist_subsystem_vendor_ids)
        self->priv->plugin_allowlist_subsystem_vendor_ids = g_array_sized_new (FALSE, FALSE, sizeof (mm_uint16_pair), 10);

    for (i = 0; i < self->priv->plugin_allowlist_subsystem_vendor_ids->len; i++) {
        mm_uint16_pair *item;

        item = &g_array_index (self->priv->plugin_allowlist_subsystem_vendor_ids, mm_uint16_pair, i);
        if (item->l == vid && item->r == subsystem_vid)
            return;
    }

    new_item.l = vid;
    new_item.r = subsystem_vid;
    g_array_append_val (self->priv->plugin_allowlist_subsystem_vendor_ids, new_item);
    mm_obj_dbg (self, "registered plugin allowlist subsystem vendor id: %04x:%04x", vid, subsystem_vid);
}

/*****************************************************************************/

gboolean
mm_filter_port (MMFilter        *self,
                MMKernelDevice  *port,
                gboolean         manual_scan)
{
    const gchar *subsystem;
    const gchar *name;

    subsystem = mm_kernel_device_get_subsystem (port);
    name      = mm_kernel_device_get_name      (port);

    /* If the device is explicitly allowlisted, we process every port. Also
     * allow specifying this flag per-port instead of for the full device, e.g.
     * for platform tty ports where there's only one port anyway. */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_EXPLICIT_ALLOWLIST) &&
        (mm_kernel_device_get_global_property_as_boolean (port, ID_MM_DEVICE_PROCESS) ||
         mm_kernel_device_get_property_as_boolean (port, ID_MM_DEVICE_PROCESS))) {
        mm_obj_dbg (self, "(%s/%s) port allowed: device is allowlisted", subsystem, name);
        return TRUE;
    }

    /* If the device is explicitly ignored, we ignore every port. */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_EXPLICIT_BLOCKLIST) &&
        (mm_kernel_device_get_global_property_as_boolean (port, ID_MM_DEVICE_IGNORE))) {
        mm_obj_dbg (self, "(%s/%s): port filtered: device is blocklisted", subsystem, name);
        return FALSE;
    }

    /* If the device is allowlisted by a plugin, we allow it. */
    if (self->priv->enabled_rules & MM_FILTER_RULE_PLUGIN_ALLOWLIST) {
        GList   *l;
        guint16  vid = 0;
        guint16  pid = 0;
        guint16  subsystem_vid = 0;

        for (l = self->priv->plugin_allowlist_tags; l; l = g_list_next (l)) {
            if (mm_kernel_device_get_global_property_as_boolean (port, (const gchar *)(l->data)) ||
                mm_kernel_device_get_property_as_boolean (port, (const gchar *)(l->data))) {
                mm_obj_dbg (self, "(%s/%s) port allowed: device is allowlisted by plugin (tag)", subsystem, name);
                return TRUE;
            }
        }

        vid = mm_kernel_device_get_physdev_vid (port);
        if (vid) {
            pid = mm_kernel_device_get_physdev_pid (port);
            subsystem_vid = mm_kernel_device_get_physdev_subsystem_vid (port);
        }

        if (vid && pid && self->priv->plugin_allowlist_product_ids) {
            guint i;

            for (i = 0; i < self->priv->plugin_allowlist_product_ids->len; i++) {
                mm_uint16_pair *item;

                item = &g_array_index (self->priv->plugin_allowlist_product_ids, mm_uint16_pair, i);
                if (item->l == vid && item->r == pid) {
                    mm_obj_dbg (self, "(%s/%s) port allowed: device is allowlisted by plugin (vid/pid)", subsystem, name);
                    return TRUE;
                }
            }
        }

        if (vid && self->priv->plugin_allowlist_vendor_ids) {
            guint i;

            for (i = 0; i < self->priv->plugin_allowlist_vendor_ids->len; i++) {
                guint16 item;

                item = g_array_index (self->priv->plugin_allowlist_vendor_ids, guint16, i);
                if (item == vid) {
                    mm_obj_dbg (self, "(%s/%s) port allowed: device is allowlisted by plugin (vid)", subsystem, name);
                    return TRUE;
                }
            }
        }

        if (vid && subsystem_vid && self->priv->plugin_allowlist_subsystem_vendor_ids) {
            guint i;

            for (i = 0; i < self->priv->plugin_allowlist_subsystem_vendor_ids->len; i++) {
                mm_uint16_pair *item;

                item = &g_array_index (self->priv->plugin_allowlist_subsystem_vendor_ids, mm_uint16_pair, i);
                if (item->l == vid && item->r == subsystem_vid) {
                    mm_obj_dbg (self, "(%s/%s) port allowed: device is allowlisted by plugin (vid/subsystem vid)", subsystem, name);
                    return TRUE;
                }
            }
        }
    }

    /* If this is a QRTR device, we always allow it. This check comes before
     * checking for VIRTUAL since qrtr devices don't have a sysfs path, and the
     * check for VIRTUAL will return FALSE. */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_QRTR) &&
        g_str_equal (subsystem, "qrtr")) {
        mm_obj_dbg (self, "(%s/%s) port allowed: qrtr device", subsystem, name);
        return TRUE;
    }

    /* If this is a virtual device, don't allow it */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_VIRTUAL) &&
        (!mm_kernel_device_get_physdev_sysfs_path (port))) {
        mm_obj_dbg (self, "(%s/%s) port filtered: virtual device", subsystem, name);
        return FALSE;
    }

    /* If this is a net device, we always allow it */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_NET) &&
        (g_strcmp0 (subsystem, "net") == 0)) {
        mm_obj_dbg (self, "(%s/%s) port allowed: net device", subsystem, name);
        return TRUE;
    }

    /* If this is a cdc-wdm device, we always allow it */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_USBMISC) &&
        (g_strcmp0 (subsystem, "usbmisc") == 0)) {
        mm_obj_dbg (self, "(%s/%s) port allowed: usbmisc device", subsystem, name);
        return TRUE;
    }

    /* If this is a rpmsg channel device, we always allow it */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_RPMSG) &&
        (g_strcmp0 (subsystem, "rpmsg") == 0)) {
        mm_obj_dbg (self, "(%s/%s) port allowed: rpmsg device", subsystem, name);
        return TRUE;
    }

    /* If this is a wwan port/device, we always allow it */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_WWAN) &&
        (g_strcmp0 (subsystem, "wwan") == 0)) {
        mm_obj_dbg (self, "(%s/%s) port allowed: wwan device", subsystem, name);
        return TRUE;
    }

    /* If this is a tty device, we may allow it */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_TTY) &&
        (g_strcmp0 (subsystem, "tty") == 0)) {
        const gchar *physdev_subsystem;
        const gchar *driver;

        /* Mixed blocklist/allowlist rules */

        /* If the physdev is a 'platform' or 'pnp' device that's not allowlisted, ignore it */
        physdev_subsystem = mm_kernel_device_get_physdev_subsystem (port);
        if ((self->priv->enabled_rules & MM_FILTER_RULE_TTY_PLATFORM_DRIVER) &&
            (!g_strcmp0 (physdev_subsystem, "platform") ||
             !g_strcmp0 (physdev_subsystem, "pci") ||
             !g_strcmp0 (physdev_subsystem, "pnp") ||
             !g_strcmp0 (physdev_subsystem, "sdio"))) {
            mm_obj_dbg (self, "(%s/%s): port filtered: tty platform driver", subsystem, name);
            return FALSE;
        }

        /* Allowlist rules last */

        /* If the TTY kernel driver is one expected modem kernel driver, allow it */
        driver = mm_kernel_device_get_driver (port);
        if ((self->priv->enabled_rules & MM_FILTER_RULE_TTY_DRIVER) &&
            (!g_strcmp0 (driver, "option") ||
             !g_strcmp0 (driver, "option1") ||
             !g_strcmp0 (driver, "qcserial") ||
             !g_strcmp0 (driver, "qcaux") ||
             !g_strcmp0 (driver, "nozomi") ||
             !g_strcmp0 (driver, "sierra"))) {
            mm_obj_dbg (self, "(%s/%s): port allowed: modem-specific kernel driver detected", subsystem, name);
            return TRUE;
        }

        /*
         * If the TTY kernel driver is cdc-acm and the interface is not
         * class=2/subclass=2/protocol=[1-6], forbidden.
         *
         * Otherwise, we'll require the modem to have more ports other
         * than the ttyACM one (see mm_filter_device_and_port()), because
         * there are lots of Arduino devices out there exposing a single
         * ttyACM port and wrongly claiming AT protocol support...
         *
         * Class definitions for Communication Devices 1.2
         * Communications Interface Class Control Protocol Codes:
         *     00h     | USB specification | No class specific protocol required
         *     01h     | ITU-T V.250       | AT Commands: V.250 etc
         *     02h     | PCCA-101          | AT Commands defined by PCCA-101
         *     03h     | PCCA-101          | AT Commands defined by PCCA-101 & Annex O
         *     04h     | GSM 7.07          | AT Commands defined by GSM 07.07
         *     05h     | 3GPP 27.07        | AT Commands defined by 3GPP 27.007
         *     06h     | C-S0017-0         | AT Commands defined by TIA for CDMA
         *     07h     | USB EEM           | Ethernet Emulation Model
         *     08h-FDh |                   | RESERVED (future use)
         *     FEh     |                   | External Protocol: Commands defined by Command Set Functional Descriptor
         *     FFh     | USB Specification | Vendor-specific
         */
        if ((self->priv->enabled_rules & MM_FILTER_RULE_TTY_ACM_INTERFACE) &&
            (!g_strcmp0 (driver, "cdc_acm")) &&
            ((mm_kernel_device_get_interface_class (port) != 2)    ||
             (mm_kernel_device_get_interface_subclass (port) != 2) ||
             (mm_kernel_device_get_interface_protocol (port) < 1)  ||
             (mm_kernel_device_get_interface_protocol (port) > 6))) {
            mm_obj_dbg (self, "(%s/%s): port filtered: cdc-acm interface is not AT-capable", subsystem, name);
            return FALSE;
        }

        /* Default forbidden? flag the port as maybe-forbidden, and go on */
        if (self->priv->enabled_rules & MM_FILTER_RULE_TTY_DEFAULT_FORBIDDEN) {
            g_object_set_data (G_OBJECT (port), FILTER_PORT_MAYBE_FORBIDDEN, GUINT_TO_POINTER (TRUE));
            return TRUE;
        }

        g_assert_not_reached ();
    }

    /* Otherwise forbidden */
    mm_obj_dbg (self, "(%s/%s) port filtered: forbidden port type", subsystem, name);
    return FALSE;
}

/*****************************************************************************/

static gboolean
device_has_net_port (MMDevice *device)
{
    GList *l;

    for (l = mm_device_peek_port_probe_list (device); l; l = g_list_next (l)) {
        if (!g_strcmp0 (mm_port_probe_get_port_subsys (MM_PORT_PROBE (l->data)), "net"))
            return TRUE;
    }
    return FALSE;
}

static gboolean
device_has_multiple_ports (MMDevice *device)
{
    return (g_list_length (mm_device_peek_port_probe_list (device)) > 1);
}

gboolean
mm_filter_device_and_port (MMFilter       *self,
                           MMDevice       *device,
                           MMKernelDevice *port)
{
    const gchar *subsystem;
    const gchar *name;
    const gchar *driver;

    /* If it wasn't flagged as maybe forbidden, there's nothing to do */
    if (!GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (port), FILTER_PORT_MAYBE_FORBIDDEN)))
        return TRUE;

    subsystem = mm_kernel_device_get_subsystem (port);
    name      = mm_kernel_device_get_name      (port);

    /* Check whether this device holds a NET port in addition to this TTY */
    if ((self->priv->enabled_rules & MM_FILTER_RULE_TTY_WITH_NET) &&
        device_has_net_port (device)) {
        mm_obj_dbg (self, "(%s/%s): port allowed: device also exports a net interface", subsystem, name);
        return TRUE;
    }

    /* Check whether this device holds any other port in addition to the ttyACM port */
    driver = mm_kernel_device_get_driver (port);
    if ((self->priv->enabled_rules & MM_FILTER_RULE_TTY_ACM_INTERFACE) &&
        (!g_strcmp0 (driver, "cdc_acm")) &&
        device_has_multiple_ports (device)) {
        mm_obj_dbg (self, "(%s/%s): port allowed: device exports multiple interfaces", subsystem, name);
        return TRUE;
    }

    mm_obj_dbg (self, "(%s/%s) port filtered: forbidden", subsystem, name);
    return FALSE;
}

/*****************************************************************************/
/* Use filter rule names as environment variables to control them on startup:
 *  - MM_FILTER_RULE_XXX=1 to explicitly enable the rule.
 *  - MM_FILTER_RULE_XXX=0 to explicitly disable the rule.
 */

static MMFilterRule
filter_rule_env_process (MMFilterRule enabled_rules)
{
    MMFilterRule  updated_rules = enabled_rules;
    GFlagsClass  *flags_class;
    guint         i;

    flags_class = g_type_class_ref (MM_TYPE_FILTER_RULE);

    for (i = 0; (1 << i) & MM_FILTER_RULE_ALL; i++) {
        GFlagsValue *flags_value;
        const gchar *env_value;

        flags_value = g_flags_get_first_value (flags_class, (1 << i));
        g_assert (flags_value);

        env_value = g_getenv (flags_value->value_name);
        if (!env_value)
            continue;

        if (g_str_equal (env_value, "0"))
            updated_rules &= ~(1 << i);
        else if (g_str_equal (env_value, "1"))
            updated_rules |= (1 << i);
    }

    g_type_class_unref (flags_class);

    return updated_rules;
}

/*****************************************************************************/

gboolean
mm_filter_check_rule_enabled (MMFilter     *self,
                              MMFilterRule  rule)
{
    return !!(self->priv->enabled_rules & rule);
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("filter");
}

/*****************************************************************************/

/* If TTY rule enabled, DEFAULT_FORBIDDEN must be set. */
#define VALIDATE_RULE_TTY(rules) (!(rules & MM_FILTER_RULE_TTY) || (rules & (MM_FILTER_RULE_TTY_DEFAULT_FORBIDDEN)))

MMFilter *
mm_filter_new (MMFilterRule   enabled_rules,
               GError       **error)
{
    MMFilter     *self;
    MMFilterRule  updated_rules;

    /* The input enabled rules are coming from predefined filter profiles. */
    g_assert (VALIDATE_RULE_TTY (enabled_rules));
    updated_rules = filter_rule_env_process (enabled_rules);
    if (!VALIDATE_RULE_TTY (updated_rules)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid rules after processing envvars");
        return NULL;
    }

    self = g_object_new (MM_TYPE_FILTER,
                         MM_FILTER_ENABLED_RULES, updated_rules,
                         NULL);

#define RULE_ENABLED_STR(flag) ((self->priv->enabled_rules & flag) ? "yes" : "no")

    mm_obj_dbg (self, "created");
    mm_obj_dbg (self, "  explicit allowlist:         %s", RULE_ENABLED_STR (MM_FILTER_RULE_EXPLICIT_ALLOWLIST));
    mm_obj_dbg (self, "  explicit blocklist:         %s", RULE_ENABLED_STR (MM_FILTER_RULE_EXPLICIT_BLOCKLIST));
    mm_obj_dbg (self, "  plugin allowlist:           %s", RULE_ENABLED_STR (MM_FILTER_RULE_PLUGIN_ALLOWLIST));
    mm_obj_dbg (self, "  qrtr devices allowed:       %s", RULE_ENABLED_STR (MM_FILTER_RULE_QRTR));
    mm_obj_dbg (self, "  virtual devices forbidden:  %s", RULE_ENABLED_STR (MM_FILTER_RULE_VIRTUAL));
    mm_obj_dbg (self, "  net devices allowed:        %s", RULE_ENABLED_STR (MM_FILTER_RULE_NET));
    mm_obj_dbg (self, "  usbmisc devices allowed:    %s", RULE_ENABLED_STR (MM_FILTER_RULE_USBMISC));
    mm_obj_dbg (self, "  rpmsg devices allowed:      %s", RULE_ENABLED_STR (MM_FILTER_RULE_RPMSG));
    mm_obj_dbg (self, "  wwan devices allowed:       %s", RULE_ENABLED_STR (MM_FILTER_RULE_WWAN));
    if (self->priv->enabled_rules & MM_FILTER_RULE_TTY) {
        mm_obj_dbg (self, "  tty devices:");
        mm_obj_dbg (self, "      platform driver check:    %s", RULE_ENABLED_STR (MM_FILTER_RULE_TTY_PLATFORM_DRIVER));
        mm_obj_dbg (self, "      driver check:             %s", RULE_ENABLED_STR (MM_FILTER_RULE_TTY_DRIVER));
        mm_obj_dbg (self, "      cdc-acm interface check:  %s", RULE_ENABLED_STR (MM_FILTER_RULE_TTY_ACM_INTERFACE));
        mm_obj_dbg (self, "      with net check:           %s", RULE_ENABLED_STR (MM_FILTER_RULE_TTY_WITH_NET));
        if (self->priv->enabled_rules & MM_FILTER_RULE_TTY_DEFAULT_FORBIDDEN)
            mm_obj_dbg (self, "      default:                  forbidden");
        else
            g_assert_not_reached ();
    } else
        mm_obj_dbg (self, "  tty devices:                no");

#undef RULE_ENABLED_STR

    return self;
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMFilter *self = MM_FILTER (object);

    switch (prop_id) {
    case PROP_ENABLED_RULES:
        self->priv->enabled_rules = g_value_get_flags (value);
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
    MMFilter *self = MM_FILTER (object);

    switch (prop_id) {
    case PROP_ENABLED_RULES:
        g_value_set_flags (value, self->priv->enabled_rules);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_filter_init (MMFilter *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_FILTER, MMFilterPrivate);
}

static void
finalize (GObject *object)
{
    MMFilter *self = MM_FILTER (object);

    g_clear_pointer (&self->priv->plugin_allowlist_vendor_ids, g_array_unref);
    g_clear_pointer (&self->priv->plugin_allowlist_product_ids, g_array_unref);
    g_clear_pointer (&self->priv->plugin_allowlist_subsystem_vendor_ids, g_array_unref);
    g_list_free_full (self->priv->plugin_allowlist_tags, g_free);

    G_OBJECT_CLASS (mm_filter_parent_class)->finalize (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_filter_class_init (MMFilterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMFilterPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize     = finalize;

    g_object_class_install_property (
        object_class, PROP_ENABLED_RULES,
        g_param_spec_flags (MM_FILTER_ENABLED_RULES,
                            "Enabled rules",
                            "Mask of rules enabled in the filter",
                            MM_TYPE_FILTER_RULE,
                            MM_FILTER_RULE_NONE,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
