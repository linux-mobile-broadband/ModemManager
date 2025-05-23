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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2018 Red Hat, Inc.
 * Copyright (C) 2011 - 2018 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2024 JUCR GmbH
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#include <ModemManager-tags.h>

#include <mm-errors-types.h>

#include "mm-port-probe.h"
#include "mm-log-object.h"
#include "mm-port-serial-at.h"
#include "mm-port-serial.h"
#include "mm-serial-parsers.h"
#include "mm-port-probe-at.h"
#include "libqcdm/src/commands.h"
#include "libqcdm/src/utils.h"
#include "libqcdm/src/errors.h"
#include "mm-port-serial-qcdm.h"
#include "mm-daemon-enums-types.h"

#if defined WITH_QMI
#include "mm-port-qmi.h"
#endif

#if defined WITH_QRTR
#include "mm-kernel-device-qrtr.h"
#endif

#if defined WITH_MBIM
#include "mm-port-mbim.h"
#endif

/*
 * Steps and flow of the Probing process:
 * ----> AT Serial Open
 *   |----> Custom Init
 *   |----> AT?
 *      |----> Vendor
 *      |----> Product
 *      |----> Is Icera?
 *      |----> Is Xmm?
 * ----> QCDM Serial Open
 *   |----> QCDM?
 * ----> QMI Device Open
 *   |----> QMI Version Info check
 * ----> MBIM Device Open
 *   |----> MBIM capabilities check
 */

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMPortProbe, mm_port_probe, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_DEVICE,
    PROP_PORT,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMPortProbePrivate {
    /* Properties */
    MMDevice *device;
    MMKernelDevice *port;

    /* From udev tags */
    gboolean is_ignored;
    gboolean is_gps;
    gboolean is_audio;
    gboolean is_xmmrpc;
    gboolean maybe_at;
    gboolean maybe_qcdm;
    gboolean maybe_qmi;
    gboolean maybe_mbim;

    /* Probing results */
    guint32 flags;
    gboolean is_at;
    gboolean is_qcdm;
    gchar *vendor;
    gchar *product;
    gboolean is_icera;
    gboolean is_xmm;
    gboolean is_qmi;
    gboolean is_mbim;

    /* Current probing task. Only one can be available at a time */
    GTask *task;
};

static const MMStringUintMap port_subsys_map[] = {
    { "usbmisc", MM_PORT_SUBSYS_USBMISC },
    { "rpmsg",   MM_PORT_SUBSYS_RPMSG },
    { "wwan",    MM_PORT_SUBSYS_WWAN },
};

/*****************************************************************************/

static const MMPortProbeAtCommand at_probing[] = {
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { NULL }
};

typedef struct {
    MMPortSerialAt             *serial;
    const MMPortProbeAtCommand *at_commands;
    guint                       at_commands_limit;
} EarlyAtProbeContext;

static void
early_at_probe_context_free (EarlyAtProbeContext *ctx)
{
    g_clear_object (&ctx->serial);
    g_slice_free (EarlyAtProbeContext, ctx);
}

gboolean
mm_port_probe_run_early_at_probe_finish (MMPortProbe   *self,
                                         GAsyncResult  *result,
                                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void
early_at_probe_parse_response (MMPortSerialAt *serial,
                               GAsyncResult   *res,
                               GTask          *task)
{
    g_autoptr(GVariant)  result = NULL;
    g_autoptr(GError)    result_error = NULL;
    g_autofree gchar    *response = NULL;
    g_autoptr(GError)    command_error = NULL;
    EarlyAtProbeContext *ctx;
    MMPortProbe         *self;
    gboolean             is_at = FALSE;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    /* If already cancelled, do nothing else */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    response = mm_port_serial_at_command_finish (serial, res, &command_error);
    if (!ctx->at_commands->response_processor (ctx->at_commands->command,
                                               response,
                                               !!ctx->at_commands[1].command,
                                               command_error,
                                               &result,
                                               &result_error)) {
        /* Were we told to abort the whole probing? */
        if (result_error) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "(%s/%s) error while probing AT features: %s",
                                     mm_kernel_device_get_subsystem (self->priv->port),
                                     mm_kernel_device_get_name (self->priv->port),
                                     result_error->message);
            g_object_unref (task);
            return;
        }

        /* Go on to next command */
        ctx->at_commands++;
        ctx->at_commands_limit--;
        if (ctx->at_commands->command && ctx->at_commands_limit > 0) {
            /* More commands in the group? */
            mm_port_serial_at_command (
                ctx->serial,
                ctx->at_commands->command,
                ctx->at_commands->timeout,
                FALSE, /* raw */
                FALSE, /* allow_cached */
                g_task_get_cancellable (task),
                (GAsyncReadyCallback)early_at_probe_parse_response,
                task);
            return;
        }

        /* No more commands in the group; end probing; not AT */
    } else if (result) {
        /* If any result given, it must be a boolean */
        g_assert (g_variant_is_of_type (result, G_VARIANT_TYPE_BOOLEAN));
        is_at = g_variant_get_boolean (result);
    }

    mm_port_probe_set_result_at (self, is_at);
    g_task_return_boolean (task, is_at);
    g_object_unref (task);
}

gboolean
mm_port_probe_run_early_at_probe (MMPortProbe         *self,
                                  MMPortSerialAt      *serial,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    GTask               *task;
    EarlyAtProbeContext *ctx;
    gint                 tries;

    tries = mm_kernel_device_get_global_property_as_int (mm_port_probe_peek_port (self),
                                                         ID_MM_TTY_AT_PROBE_TRIES);
    if (tries == 0) {
        /* Early probing not required */
        return FALSE;
    }

    task = g_task_new (self, cancellable, callback, user_data);

    ctx = g_slice_new0 (EarlyAtProbeContext);
    ctx->serial                = g_object_ref (serial);
    ctx->at_commands           = at_probing;
    ctx->at_commands_limit     = CLAMP (tries, 1, (gint) G_N_ELEMENTS (at_probing));
    g_task_set_task_data (task, ctx, (GDestroyNotify) early_at_probe_context_free);

    mm_port_serial_at_command (
        ctx->serial,
        ctx->at_commands->command,
        ctx->at_commands->timeout,
        FALSE, /* raw */
        FALSE, /* allow_cached */
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)early_at_probe_parse_response,
        task);
    return TRUE;
}

/*****************************************************************************/

static void
mm_port_probe_clear (MMPortProbe *self)
{
    /* Clears existing probe results so probing can restart from the beginning.
     * Should only be used internally as it does not ensure `task` is NULL.
     */
    self->priv->flags = 0;
    self->priv->is_at = FALSE;
    self->priv->is_qcdm = FALSE;
    g_clear_pointer (&self->priv->vendor, g_free);
    g_clear_pointer (&self->priv->product, g_free);
    self->priv->is_icera = FALSE;
    self->priv->is_xmm = FALSE;
    self->priv->is_qmi = FALSE;
    self->priv->is_mbim = FALSE;
}

void
mm_port_probe_reset (MMPortProbe *self)
{
    /* Clears existing probe results after probing is complete */
    g_assert (!self->priv->task);
    mm_port_probe_clear (self);
}

/*****************************************************************************/
/* Probe task completions.
 * Always make sure that the stored task is NULL when the task is completed.
 */

static gboolean
port_probe_task_return_error_if_cancelled (MMPortProbe *self)
{
    GTask *task;

    task = self->priv->task;
    self->priv->task = NULL;

    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return TRUE;
    }

    self->priv->task = task;
    return FALSE;
}

static void
port_probe_task_return_error (MMPortProbe *self,
                              GError      *error)
{
    GTask *task;

    task = self->priv->task;
    self->priv->task = NULL;
    g_task_return_error (task, error);
    g_object_unref (task);
}

static void
port_probe_task_return_boolean (MMPortProbe *self,
                                gboolean     result)
{
    GTask *task;

    task = self->priv->task;
    self->priv->task = NULL;
    g_task_return_boolean (task, result);
    g_object_unref (task);
}

/*****************************************************************************/

void
mm_port_probe_set_result_at (MMPortProbe *self,
                             gboolean at)
{
    self->priv->is_at = at;
    self->priv->flags |= MM_PORT_PROBE_AT;

    if (self->priv->is_at) {
        mm_obj_dbg (self, "port is AT-capable");

        /* Also set as not a QCDM/QMI/MBIM port */
        self->priv->is_qcdm = FALSE;
        self->priv->is_qmi = FALSE;
        self->priv->is_mbim = FALSE;
        self->priv->is_xmmrpc = FALSE;
        self->priv->flags |= (MM_PORT_PROBE_QCDM | MM_PORT_PROBE_QMI | MM_PORT_PROBE_MBIM);
    } else {
        mm_obj_dbg (self, "port is not AT-capable");
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->is_icera = FALSE;
        self->priv->is_xmm = FALSE;
        self->priv->flags |= (MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA |
                              MM_PORT_PROBE_AT_XMM);
    }
}

void
mm_port_probe_set_result_at_vendor (MMPortProbe *self,
                                    const gchar *at_vendor)
{
    if (at_vendor) {
        mm_obj_dbg (self, "vendor probing finished");
        self->priv->vendor = g_utf8_casefold (at_vendor, -1);
        self->priv->flags |= MM_PORT_PROBE_AT_VENDOR;
    } else {
        mm_obj_dbg (self, "couldn't probe for vendor string");
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->flags |= (MM_PORT_PROBE_AT_VENDOR | MM_PORT_PROBE_AT_PRODUCT);
    }
}

void
mm_port_probe_set_result_at_product (MMPortProbe *self,
                                     const gchar *at_product)
{
    if (at_product) {
        mm_obj_dbg (self, "product probing finished");
        self->priv->product = g_utf8_casefold (at_product, -1);
        self->priv->flags |= MM_PORT_PROBE_AT_PRODUCT;
    } else {
        mm_obj_dbg (self, "couldn't probe for product string");
        self->priv->product = NULL;
        self->priv->flags |= MM_PORT_PROBE_AT_PRODUCT;
    }
}

void
mm_port_probe_set_result_at_icera (MMPortProbe *self,
                                   gboolean is_icera)
{
    if (is_icera) {
        mm_obj_dbg (self, "modem is Icera-based");
        self->priv->is_icera = TRUE;
        self->priv->flags |= MM_PORT_PROBE_AT_ICERA;
    } else {
        mm_obj_dbg (self, "modem is probably not Icera-based");
        self->priv->is_icera = FALSE;
        self->priv->flags |= MM_PORT_PROBE_AT_ICERA;
    }
}

void
mm_port_probe_set_result_at_xmm (MMPortProbe *self,
                                 gboolean is_xmm)
{
    if (is_xmm) {
        mm_obj_dbg (self, "modem is XMM-based");
        self->priv->is_xmm = TRUE;
        self->priv->flags |= MM_PORT_PROBE_AT_XMM;
    } else {
        mm_obj_dbg (self, "modem is probably not XMM-based");
        self->priv->is_xmm = FALSE;
        self->priv->flags |= MM_PORT_PROBE_AT_XMM;
    }
}

void
mm_port_probe_set_result_qcdm (MMPortProbe *self,
                               gboolean qcdm)
{
    self->priv->is_qcdm = qcdm;
    self->priv->flags |= MM_PORT_PROBE_QCDM;

    if (self->priv->is_qcdm) {
        mm_obj_dbg (self, "port is QCDM-capable");

        /* Also set as not an AT/QMI/MBIM port */
        self->priv->is_at = FALSE;
        self->priv->is_qmi = FALSE;
        self->priv->is_mbim = FALSE;
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->is_icera = FALSE;
        self->priv->is_xmm = FALSE;
        self->priv->is_xmmrpc = FALSE;
        self->priv->flags |= (MM_PORT_PROBE_AT |
                              MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA |
                              MM_PORT_PROBE_AT_XMM |
                              MM_PORT_PROBE_QMI |
                              MM_PORT_PROBE_MBIM);
    } else
        mm_obj_dbg (self, "port is not QCDM-capable");
}

void
mm_port_probe_set_result_qmi (MMPortProbe *self,
                              gboolean qmi)
{
    self->priv->is_qmi = qmi;
    self->priv->flags |= MM_PORT_PROBE_QMI;

    if (self->priv->is_qmi) {
        mm_obj_dbg (self, "port is QMI-capable");

        /* Also set as not an AT/QCDM/MBIM port */
        self->priv->is_at = FALSE;
        self->priv->is_qcdm = FALSE;
        self->priv->is_mbim = FALSE;
        self->priv->is_xmmrpc = FALSE;
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->flags |= (MM_PORT_PROBE_AT |
                              MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA |
                              MM_PORT_PROBE_AT_XMM |
                              MM_PORT_PROBE_QCDM |
                              MM_PORT_PROBE_MBIM);
    } else
        mm_obj_dbg (self, "port is not QMI-capable");
}

void
mm_port_probe_set_result_mbim (MMPortProbe *self,
                               gboolean mbim)
{
    self->priv->is_mbim = mbim;
    self->priv->flags |= MM_PORT_PROBE_MBIM;

    if (self->priv->is_mbim) {
        mm_obj_dbg (self, "port is MBIM-capable");

        /* Also set as not an AT/QCDM/QMI port */
        self->priv->is_at = FALSE;
        self->priv->is_qcdm = FALSE;
        self->priv->is_qmi = FALSE;
        self->priv->is_xmmrpc = FALSE;
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->flags |= (MM_PORT_PROBE_AT |
                              MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA |
                              MM_PORT_PROBE_AT_XMM |
                              MM_PORT_PROBE_QCDM |
                              MM_PORT_PROBE_QMI);
    } else
        mm_obj_dbg (self, "port is not MBIM-capable");
}

/*****************************************************************************/

typedef enum {
    PROBE_STEP_FIRST,
    PROBE_STEP_AT_CUSTOM_INIT_OPEN_PORT,
    PROBE_STEP_AT_CUSTOM_INIT,
    PROBE_STEP_AT_OPEN_PORT,
    PROBE_STEP_AT,
    PROBE_STEP_AT_VENDOR,
    PROBE_STEP_AT_PRODUCT,
    PROBE_STEP_AT_ICERA,
    PROBE_STEP_AT_XMM,
    PROBE_STEP_AT_CLOSE_PORT,
    PROBE_STEP_QCDM,
    PROBE_STEP_QCDM_CLOSE_PORT,
    PROBE_STEP_QMI,
    PROBE_STEP_MBIM,
    PROBE_STEP_LAST
} ProbeStep;

typedef struct {
    /* ---- Generic task context ---- */
    guint32       flags;
    guint         source_id;
    GCancellable *cancellable;
    ProbeStep     step;

    /* ---- Serial probing specific context ---- */

    guint buffer_full_id;
    MMPortSerial *serial;

    /* ---- AT probing specific context ---- */

    GCancellable *at_probing_cancellable;
    gulong at_probing_cancellable_linked;
    /* Send delay for AT commands */
    guint64 at_send_delay;
    /* Flag to leave/remove echo in AT responses */
    gboolean at_remove_echo;
    /* Flag to send line-feed at the end of AT commands */
    gboolean at_send_lf;
    /* Number of times we tried to open the AT port */
    guint at_open_tries;
    /* Custom initialization setup */
    MMPortProbeAtCustomInit at_custom_init;
    MMPortProbeAtCustomInitFinish at_custom_init_finish;
    /* Custom commands to look for AT support */
    const MMPortProbeAtCommand *at_custom_probe;
    /* Current group of AT commands to be sent */
    const MMPortProbeAtCommand *at_commands;
    /* Maximum number of at_commands to be sent */
    guint at_commands_limit;
    /* Seconds between each AT command sent in the group */
    guint at_commands_wait_secs;
    /* Current AT Result processor */
    void (* at_result_processor) (MMPortProbe *self,
                                  GVariant *result);

#if defined WITH_QMI
    /* ---- QMI probing specific context ---- */
    MMPortQmi *port_qmi;
#endif

#if defined WITH_MBIM
    /* ---- MBIM probing specific context ---- */
    MMPortMbim *mbim_port;
#endif

    /* ---- QCDM probing specific context ---- */
    gboolean qcdm_required;
} PortProbeRunContext;

static gboolean probe_at        (MMPortProbe *self);
static void     probe_step_next (MMPortProbe *self);

static void
clear_probe_serial_port (PortProbeRunContext *ctx)
{
    if (ctx->serial) {
        if (ctx->buffer_full_id) {
            g_signal_handler_disconnect (ctx->serial, ctx->buffer_full_id);
            ctx->buffer_full_id = 0;
        }

        if (mm_port_serial_is_open (ctx->serial))
            mm_port_serial_close (ctx->serial);
        g_clear_object (&ctx->serial);
    }
}

static void
port_probe_run_context_free (PortProbeRunContext *ctx)
{
    if (ctx->cancellable && ctx->at_probing_cancellable_linked) {
        g_cancellable_disconnect (ctx->cancellable, ctx->at_probing_cancellable_linked);
        ctx->at_probing_cancellable_linked = 0;
    }

    if (ctx->source_id) {
        g_source_remove (ctx->source_id);
        ctx->source_id = 0;
    }

    clear_probe_serial_port (ctx);

#if defined WITH_QMI
    if (ctx->port_qmi) {
        /* We should have closed it cleanly before */
        g_assert (!mm_port_qmi_is_open (ctx->port_qmi));
        g_object_unref (ctx->port_qmi);
    }
#endif

#if defined WITH_MBIM
    if (ctx->mbim_port) {
        /* We should have closed it cleanly before */
        g_assert (!mm_port_mbim_is_open (ctx->mbim_port));
        g_object_unref (ctx->mbim_port);
    }
#endif

    g_clear_object (&ctx->at_probing_cancellable);
    g_clear_object (&ctx->cancellable);

    g_slice_free (PortProbeRunContext, ctx);
}

/***************************************************************/
/* QMI & MBIM */

#if defined WITH_QMI

static void
qmi_port_close_ready (MMPortQmi    *qmi_port,
                      GAsyncResult *res,
                      MMPortProbe  *self)
{
    g_assert (self->priv->task);

    mm_port_qmi_close_finish (qmi_port, res, NULL);

    /* Continue with remaining probings */
    probe_step_next (self);
}

static void
port_qmi_open_ready (MMPortQmi    *port_qmi,
                     GAsyncResult *res,
                     MMPortProbe  *self)
{
    GError              *error = NULL;
    PortProbeRunContext *ctx;
    gboolean             is_qmi;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);

    is_qmi = mm_port_qmi_open_finish (port_qmi, res, &error);
    if (!is_qmi) {
        mm_obj_dbg (self, "error checking QMI support: %s",
                    error ? error->message : "unknown error");
        g_clear_error (&error);
    }

    /* Set probing result */
    mm_port_probe_set_result_qmi (self, is_qmi);

    mm_port_qmi_close (ctx->port_qmi,
                       (GAsyncReadyCallback) qmi_port_close_ready,
                       self);
}

#endif /* WITH_QMI */

static gboolean
wdm_probe_qmi (MMPortProbe *self)
{
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);
    ctx->source_id = 0;

#if defined WITH_QMI
    /* Create a port and try to open it */
    mm_obj_dbg (self, "probing QMI...");

#if defined WITH_QRTR
    if (MM_IS_KERNEL_DEVICE_QRTR (self->priv->port)) {
        g_autoptr(QrtrNode) node = NULL;

        node = mm_kernel_device_qrtr_get_node (MM_KERNEL_DEVICE_QRTR (self->priv->port));

        /* Will set MM_PORT_SUBSYS_QRTR when creating the mm-port */
        ctx->port_qmi = mm_port_qmi_new_from_node (mm_kernel_device_get_name (self->priv->port), node);
    } else
#endif /* WITH_QRTR */
    {
        MMPortSubsys subsys;

        subsys = mm_string_uint_map_lookup (port_subsys_map,
                                            G_N_ELEMENTS (port_subsys_map),
                                            mm_kernel_device_get_subsystem (self->priv->port),
                                            MM_PORT_SUBSYS_USBMISC);
        ctx->port_qmi = mm_port_qmi_new (mm_kernel_device_get_name (self->priv->port), subsys);
    }

    mm_port_qmi_open (ctx->port_qmi,
                      FALSE,
                      g_task_get_cancellable (self->priv->task),
                      (GAsyncReadyCallback) port_qmi_open_ready,
                      self);
#else
    /* If not compiled with QMI support, just assume we won't have any QMI port */
    mm_port_probe_set_result_qmi (self, FALSE);
    probe_step_next (self);
#endif /* WITH_QMI */

    return G_SOURCE_REMOVE;
}

#if defined WITH_MBIM

static void
mbim_port_close_ready (MMPortMbim   *mbim_port,
                       GAsyncResult *res,
                       MMPortProbe  *self)
{
    mm_port_mbim_close_finish (mbim_port, res, NULL);

    /* Continue with remaining probings */
    probe_step_next (self);
}

static void
mbim_port_open_ready (MMPortMbim   *mbim_port,
                      GAsyncResult *res,
                      MMPortProbe  *self)
{
    GError              *error = NULL;
    PortProbeRunContext *ctx;
    gboolean             is_mbim;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);

    is_mbim = mm_port_mbim_open_finish (mbim_port, res, &error);
    if (!is_mbim) {
        mm_obj_dbg (self, "error checking MBIM support: %s",
                    error ? error->message : "unknown error");
        g_clear_error (&error);
    }

    /* Set probing result */
    mm_port_probe_set_result_mbim (self, is_mbim);

    mm_port_mbim_close (ctx->mbim_port,
                        (GAsyncReadyCallback) mbim_port_close_ready,
                        self);
}

#endif /* WITH_MBIM */

static gboolean
wdm_probe_mbim (MMPortProbe *self)
{
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);
    ctx->source_id = 0;

#if defined WITH_MBIM
    mm_obj_dbg (self, "probing MBIM...");

    /* Create a port and try to open it */
    ctx->mbim_port = mm_port_mbim_new (mm_kernel_device_get_name (self->priv->port),
                                       MM_PORT_SUBSYS_USBMISC);
    mm_port_mbim_open (ctx->mbim_port,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                       FALSE, /* Don't check QMI over MBIM support at this stage */
#endif
                       g_task_get_cancellable (self->priv->task),
                       (GAsyncReadyCallback) mbim_port_open_ready,
                       self);
#else
    /* If not compiled with MBIM support, just assume we won't have any MBIM port */
    mm_port_probe_set_result_mbim (self, FALSE);
    probe_step_next (self);
#endif /* WITH_MBIM */

    return G_SOURCE_REMOVE;
}

/***************************************************************/

static void
common_serial_port_setup (MMPortProbe  *self,
                          MMPortSerial *serial)
{
    const gchar *flow_control_tag;

    if (mm_kernel_device_has_property (self->priv->port, ID_MM_TTY_BAUDRATE))
        g_object_set (serial,
                      MM_PORT_SERIAL_BAUD, mm_kernel_device_get_property_as_int (self->priv->port, ID_MM_TTY_BAUDRATE),
                      NULL);

    flow_control_tag = mm_kernel_device_get_property (self->priv->port, ID_MM_TTY_FLOW_CONTROL);
    if (flow_control_tag) {
        MMFlowControl flow_control;
        GError *error = NULL;

        flow_control = mm_flow_control_from_string (flow_control_tag, &error);
        if (flow_control == MM_FLOW_CONTROL_UNKNOWN) {
            mm_obj_warn (self, "unsupported flow control settings in port: %s", error->message);
            g_error_free (error);
        } else {
            g_object_set (serial,
                          MM_PORT_SERIAL_FLOW_CONTROL, flow_control,
                          NULL);
        }
    }
}

/***************************************************************/
/* QCDM */

static void
probe_qcdm_parse_response (MMPortSerialQcdm *port,
                           GAsyncResult     *res,
                           MMPortProbe      *self)
{
    QcdmResult          *result;
    gint                 err = QCDM_SUCCESS;
    gboolean             is_qcdm = FALSE;
    gboolean             retry = FALSE;
    g_autoptr(GError)    error = NULL;
    GByteArray          *response;
    PortProbeRunContext *ctx;

    ctx = g_task_get_task_data (self->priv->task);

    /* If already cancelled, do nothing else */
    if (port_probe_task_return_error_if_cancelled (self))
        return;

    response = mm_port_serial_qcdm_command_finish (port, res, &error);
    if (!error) {
        /* Parse the response */
        result = qcdm_cmd_version_info_result ((const gchar *) response->data, response->len, &err);
        if (!result) {
            mm_obj_warn (self, "failed to parse QCDM version info command result: %d", err);
            retry = TRUE;
        } else {
            /* yay, probably a QCDM port */
            is_qcdm = TRUE;
            qcdm_result_unref (result);
        }
        g_byte_array_unref (response);
    } else if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_PARSE_FAILED)) {
        /* Failed to unescape QCDM packet: don't retry */
        mm_obj_dbg (self, "QCDM parsing error: %s", error->message);
    } else if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_CARRIER)) {
        /* Special-case: the port may have been in PPP mode (if system is restarted
         * but the modem still had power) and failed AT probing. QCDM probing
         * sends empty HDLC frames that PPP parses and then terminates the
         * connection with "NO CARRIER". Match this and go back to AT probing.
         */
        mm_obj_dbg (self, "QCDM parsing got NO CARRIER; retrying AT probing");
        mm_port_probe_clear (self);
    } else {
        if (!g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            mm_obj_dbg (self, "QCDM probe error: (%d) %s", error->code, error->message);
        retry = TRUE;
    }

    if (retry) {
        GByteArray *cmd2;

        cmd2 = g_object_steal_data (G_OBJECT (self), "cmd2");
        if (cmd2) {
            /* second try */
            mm_port_serial_qcdm_command (MM_PORT_SERIAL_QCDM (ctx->serial),
                                         cmd2,
                                         3,
                                         NULL,
                                         (GAsyncReadyCallback) probe_qcdm_parse_response,
                                         self);
            g_byte_array_unref (cmd2);
            return;
        }
        /* no more retries left */
    }

    /* Set probing result */
    mm_port_probe_set_result_qcdm (self, is_qcdm);

    /* Continue with remaining probings */
    probe_step_next (self);
}

static gboolean
probe_qcdm (MMPortProbe *self)
{
    GError              *error = NULL;
    GByteArray          *verinfo = NULL;
    GByteArray          *verinfo2;
    gint                 len;
    guint8               marker = 0x7E;
    MMPortSubsys         subsys = MM_PORT_SUBSYS_TTY;
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);
    ctx->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_task_return_error_if_cancelled (self))
        return G_SOURCE_REMOVE;

    /* If the plugin specifies QCDM is not required, we can right away complete the QCDM
     * probing task. */
    if (!ctx->qcdm_required) {
        mm_obj_dbg (self, "Maybe a QCDM port, but plugin does not require probing and grabbing...");
        /* If we had a port type hint, flag the port as QCDM capable but ignored. Otherwise,
         * no QCDM capable and not ignored. The outcome is really the same, i.e. the port is not
         * used any more, but the way it's reported in DBus will be different (i.e. "ignored" vs
         "unknown" */
        if (self->priv->maybe_qcdm) {
            mm_port_probe_set_result_qcdm (self, TRUE);
            self->priv->is_ignored = TRUE;
        } else
            mm_port_probe_set_result_qcdm (self, FALSE);

        /* Continue with remaining probings */
        probe_step_next (self);
        return G_SOURCE_REMOVE;
    }

    mm_obj_dbg (self, "probing QCDM...");

    /* If open, close the AT port */
    clear_probe_serial_port (ctx);

    if (g_str_equal (mm_kernel_device_get_subsystem (self->priv->port), "wwan"))
        subsys = MM_PORT_SUBSYS_WWAN;

    /* Open the QCDM port */
    ctx->serial = MM_PORT_SERIAL (mm_port_serial_qcdm_new (mm_kernel_device_get_name (self->priv->port), subsys));
    if (!ctx->serial) {
        port_probe_task_return_error (self,
                                      g_error_new (MM_CORE_ERROR,
                                                   MM_CORE_ERROR_FAILED,
                                                   "(%s/%s) Couldn't create QCDM port",
                                                   mm_kernel_device_get_subsystem (self->priv->port),
                                                   mm_kernel_device_get_name (self->priv->port)));
        return G_SOURCE_REMOVE;
    }

    /* Setup port if needed */
    common_serial_port_setup (self, ctx->serial);

    /* Try to open the port */
    if (!mm_port_serial_open (ctx->serial, &error)) {
        port_probe_task_return_error (self,
                                      g_error_new (MM_SERIAL_ERROR,
                                                   MM_SERIAL_ERROR_OPEN_FAILED,
                                                   "(%s/%s) Failed to open QCDM port: %s",
                                                   mm_kernel_device_get_subsystem (self->priv->port),
                                                   mm_kernel_device_get_name (self->priv->port),
                                                   (error ? error->message : "unknown error")));
        g_clear_error (&error);
        return G_SOURCE_REMOVE;
    }

    /* Build up the probe command; 0x7E is the frame marker, so put one at the
     * beginning of the buffer to ensure that the device discards any AT
     * commands that probing might have sent earlier.  Should help devices
     * respond more quickly and speed up QCDM probing.
     */
    verinfo = g_byte_array_sized_new (10);
    g_byte_array_append (verinfo, &marker, 1);
    len = qcdm_cmd_version_info_new ((char *) (verinfo->data + 1), 9);
    if (len <= 0) {
        g_byte_array_unref (verinfo);
        port_probe_task_return_error (self,
                                      g_error_new (MM_SERIAL_ERROR,
                                                   MM_SERIAL_ERROR_OPEN_FAILED,
                                                   "(%s/%s) Failed to create QCDM version info command",
                                                   mm_kernel_device_get_subsystem (self->priv->port),
                                                   mm_kernel_device_get_name (self->priv->port)));
        return G_SOURCE_REMOVE;
    }
    verinfo->len = len + 1;

    /* Queuing the command takes ownership over it; save it for the second try */
    verinfo2 = g_byte_array_sized_new (verinfo->len);
    g_byte_array_append (verinfo2, verinfo->data, verinfo->len);
    g_object_set_data_full (G_OBJECT (self), "cmd2", verinfo2, (GDestroyNotify) g_byte_array_unref);

    mm_port_serial_qcdm_command (MM_PORT_SERIAL_QCDM (ctx->serial),
                                 verinfo,
                                 3,
                                 NULL,
                                 (GAsyncReadyCallback) probe_qcdm_parse_response,
                                 self);
    g_byte_array_unref (verinfo);
    return G_SOURCE_REMOVE;
}

/***************************************************************/
/* AT */

static const gchar *non_at_strings[] = {
    /* Option Icera-based devices */
    "option/faema_",
    "os_logids.h",
    /* Sierra CnS port */
    "NETWORK SERVICE CHANGE",
    NULL
};

static const guint8 zerobuf[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const guint8 quectel_qcdm[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
};

static gboolean
is_non_at_response (const guint8 *data, gsize len)
{
    const gchar **iter;
    gsize iter_len;
    gsize i;

    /* Some devices (observed on a ZTE branded "QUALCOMM INCORPORATED" model
     * "154") spew NULLs from some ports.
     */
    for (i = 0; (len >= sizeof (zerobuf)) && (i < len - sizeof (zerobuf)); i++) {
        if (!memcmp (&data[i], zerobuf, sizeof (zerobuf)))
            return TRUE;
    }

    /* Observed on a Quectel EG915Q Qualcomm-based device's DIAG port */
    for (i = 0; (len >= sizeof (quectel_qcdm)) && (i < len - sizeof (quectel_qcdm)); i++) {
        if (!memcmp (&data[i], quectel_qcdm, sizeof (quectel_qcdm)))
            return TRUE;
    }

    /* Check for a well-known non-AT response.  There are some ports (eg many
     * Icera-based chipsets, Qualcomm Gobi devices before their firmware is
     * loaded, Sierra CnS ports) that just shouldn't be probed for AT capability
     * if we get a certain response since that response means they aren't AT
     * ports.  Also, kernel bugs (at least with 2.6.31 and 2.6.32) trigger port
     * flow control kernel oopses if we read too much data for these ports.
     */
    for (iter = &non_at_strings[0]; iter && *iter; iter++) {
        /* Search in the response for the item; the response could have embedded
         * nulls so we can't use memcmp() or strstr() on the whole response.
         */
        iter_len = strlen (*iter);
        for (i = 0; (len >= iter_len) && (i < len - iter_len); i++) {
            if (!memcmp (&data[i], *iter, iter_len))
                return TRUE;
        }
    }

    return FALSE;
}

static void
probe_at_xmm_result_processor (MMPortProbe *self,
                               GVariant *result)
{
    if (result) {
        /* If any result given, it must be a string */
        g_assert (g_variant_is_of_type (result, G_VARIANT_TYPE_STRING));
        if (strstr (g_variant_get_string (result, NULL), "XACT:")) {
            mm_port_probe_set_result_at_xmm (self, TRUE);
            return;
        }
    }

    mm_port_probe_set_result_at_xmm (self, FALSE);
}

static void
probe_at_icera_result_processor (MMPortProbe *self,
                                 GVariant *result)
{
    if (result) {
        /* If any result given, it must be a string */
        g_assert (g_variant_is_of_type (result, G_VARIANT_TYPE_STRING));
        if (strstr (g_variant_get_string (result, NULL), "%IPSYS:")) {
            mm_port_probe_set_result_at_icera (self, TRUE);
            return;
        }
    }

    mm_port_probe_set_result_at_icera (self, FALSE);
}

static void
probe_at_product_result_processor (MMPortProbe *self,
                                   GVariant *result)
{
    if (result) {
        /* If any result given, it must be a string */
        g_assert (g_variant_is_of_type (result, G_VARIANT_TYPE_STRING));
        mm_port_probe_set_result_at_product (self,
                                             g_variant_get_string (result, NULL));
        return;
    }

    mm_port_probe_set_result_at_product (self, NULL);
}

static void
probe_at_vendor_result_processor (MMPortProbe *self,
                                  GVariant *result)
{
    if (result) {
        /* If any result given, it must be a string */
        g_assert (g_variant_is_of_type (result, G_VARIANT_TYPE_STRING));
        mm_port_probe_set_result_at_vendor (self,
                                            g_variant_get_string (result, NULL));
        return;
    }

    mm_port_probe_set_result_at_vendor (self, NULL);
}

static void
probe_at_result_processor (MMPortProbe *self,
                           GVariant *result)
{
    if (result) {
        /* If any result given, it must be a boolean */
        g_assert (g_variant_is_of_type (result, G_VARIANT_TYPE_BOOLEAN));

        if (g_variant_get_boolean (result)) {
            mm_port_probe_set_result_at (self, TRUE);
            return;
        }
    }

    mm_port_probe_set_result_at (self, FALSE);
}

static void
probe_at_parse_response (MMPortSerialAt *port,
                         GAsyncResult   *res,
                         MMPortProbe    *self)
{
    g_autoptr(GVariant)  result = NULL;
    g_autoptr(GError)    result_error = NULL;
    g_autofree gchar    *response = NULL;
    g_autoptr(GError)    command_error = NULL;
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);

    /* If already cancelled, do nothing else */
    if (port_probe_task_return_error_if_cancelled (self))
        return;

    /* If AT probing cancelled, end this partial probing */
    if (g_cancellable_is_cancelled (ctx->at_probing_cancellable)) {
        mm_obj_dbg (self, "no need to keep on probing the port for AT support");
        ctx->at_result_processor (self, NULL);
        probe_step_next (self);
        return;
    }

    response = mm_port_serial_at_command_finish (port, res, &command_error);

    if (!ctx->at_commands->response_processor (ctx->at_commands->command,
                                               response,
                                               !!ctx->at_commands[1].command,
                                               command_error,
                                               &result,
                                               &result_error)) {
        /* Were we told to abort the whole probing? */
        if (result_error) {
            port_probe_task_return_error (self,
                                          g_error_new (MM_CORE_ERROR,
                                                       MM_CORE_ERROR_UNSUPPORTED,
                                                       "(%s/%s) error while probing AT features: %s",
                                                       mm_kernel_device_get_subsystem (self->priv->port),
                                                       mm_kernel_device_get_name (self->priv->port),
                                                       result_error->message));
            return;
        }

        /* Go on to next command */
        ctx->at_commands++;
        ctx->at_commands_limit--;
        if (!ctx->at_commands->command || ctx->at_commands_limit == 0) {
            /* Was it the last command in the group? If so,
             * end this partial probing */
            ctx->at_result_processor (self, NULL);
            probe_step_next (self);
            return;
        }

        /* Schedule the next command in the probing group */
        if (ctx->at_commands_wait_secs == 0)
            ctx->source_id = g_idle_add ((GSourceFunc) probe_at, self);
        else {
            mm_obj_dbg (self, "re-scheduling next command in probing group in %u seconds...",
                        ctx->at_commands_wait_secs);
            ctx->source_id = g_timeout_add_seconds (ctx->at_commands_wait_secs, (GSourceFunc) probe_at, self);
        }
        return;
    }

    /* Run result processor.
     * Note that custom init commands are allowed to not return anything */
    ctx->at_result_processor (self, result);
    probe_step_next (self);
}

static gboolean
probe_at (MMPortProbe *self)
{
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);
    ctx->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_task_return_error_if_cancelled (self))
        return G_SOURCE_REMOVE;

    /* If AT probing cancelled, end this partial probing */
    if (g_cancellable_is_cancelled (ctx->at_probing_cancellable)) {
        mm_obj_dbg (self, "no need to launch probing for AT support");
        ctx->at_result_processor (self, NULL);
        probe_step_next (self);
        return G_SOURCE_REMOVE;
    }

    mm_port_serial_at_command (
        MM_PORT_SERIAL_AT (ctx->serial),
        ctx->at_commands->command,
        ctx->at_commands->timeout,
        FALSE,
        FALSE,
        ctx->at_probing_cancellable,
        (GAsyncReadyCallback)probe_at_parse_response,
        self);
    return G_SOURCE_REMOVE;
}

static const MMPortProbeAtCommand vendor_probing[] = {
    { "+CGMI", 3, mm_port_probe_response_processor_string },
    { "+GMI",  3, mm_port_probe_response_processor_string },
    { "I",     3, mm_port_probe_response_processor_string },
    { NULL }
};

static const MMPortProbeAtCommand product_probing[] = {
    { "+CGMM", 3, mm_port_probe_response_processor_string },
    { "+GMM",  3, mm_port_probe_response_processor_string },
    { "I",     3, mm_port_probe_response_processor_string },
    { NULL }
};

static const MMPortProbeAtCommand icera_probing[] = {
    { "%IPSYS?", 3, mm_port_probe_response_processor_string },
    { "%IPSYS?", 3, mm_port_probe_response_processor_string },
    { "%IPSYS?", 3, mm_port_probe_response_processor_string },
    { NULL }
};

static const MMPortProbeAtCommand xmm_probing[] = {
    { "+XACT=?", 3, mm_port_probe_response_processor_string },
    { NULL }
};

static void
at_custom_init_ready (MMPortProbe *self,
                      GAsyncResult *res)
{
    GError              *error = NULL;
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);

    if (!ctx->at_custom_init_finish (self, res, &error)) {
        /* All errors propagated up end up forcing an UNSUPPORTED result */
        port_probe_task_return_error (self, error);
        return;
    }

    /* Continue with remaining probings */
    probe_step_next (self);
}

/***************************************************************/

static void
serial_flash_ready (MMPortSerial *port,
                    GAsyncResult *res,
                    MMPortProbe *self)
{
    mm_port_serial_flash_finish (port, res, NULL);

    /* Continue with remaining probings */
    probe_step_next (self);
}

static void
serial_buffer_full (MMPortSerial *serial,
                    GByteArray   *buffer,
                    MMPortProbe  *self)
{
    PortProbeRunContext *ctx;

    if (!is_non_at_response (buffer->data, buffer->len))
        return;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);

    mm_obj_dbg (self, "serial buffer full");
    /* Don't explicitly close the AT port, just end the AT probing
     * (or custom init probing) */
    mm_port_probe_set_result_at (self, FALSE);
    g_cancellable_cancel (ctx->at_probing_cancellable);
}

static gboolean
serial_parser_filter_cb (gpointer   filter,
                         gpointer   user_data,
                         GString   *response,
                         GError   **error)
{
    if (is_non_at_response ((const guint8 *) response->str, response->len)) {
        g_set_error (error,
                     MM_SERIAL_ERROR,
                     MM_SERIAL_ERROR_PARSE_FAILED,
                     "Not an AT response");
        return FALSE;
    }

    return TRUE;
}

static gboolean
serial_open_at (MMPortProbe *self)
{
    GError              *error = NULL;
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);
    ctx->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_task_return_error_if_cancelled (self))
        return G_SOURCE_REMOVE;

    /* Create AT serial port if not done before */
    if (!ctx->serial) {
        gpointer     parser;
        MMPortSubsys subsys;

        subsys = mm_string_uint_map_lookup (port_subsys_map,
                                            G_N_ELEMENTS (port_subsys_map),
                                            mm_kernel_device_get_subsystem (self->priv->port),
                                            MM_PORT_SUBSYS_TTY);

        ctx->serial = MM_PORT_SERIAL (mm_port_serial_at_new (mm_kernel_device_get_name (self->priv->port), subsys));
        if (!ctx->serial) {
            port_probe_task_return_error (self,
                                          g_error_new (MM_CORE_ERROR,
                                                       MM_CORE_ERROR_FAILED,
                                                       "(%s/%s) couldn't create AT port",
                                                       mm_kernel_device_get_subsystem (self->priv->port),
                                                       mm_kernel_device_get_name (self->priv->port)));
            return G_SOURCE_REMOVE;
        }

        g_object_set (ctx->serial,
                      MM_PORT_SERIAL_SPEW_CONTROL,   TRUE,
                      MM_PORT_SERIAL_SEND_DELAY,     (guint64)(subsys == MM_PORT_SUBSYS_TTY ? ctx->at_send_delay : 0),
                      MM_PORT_SERIAL_AT_REMOVE_ECHO, ctx->at_remove_echo,
                      MM_PORT_SERIAL_AT_SEND_LF,     ctx->at_send_lf,
                      NULL);

        common_serial_port_setup (self, ctx->serial);

        parser = mm_serial_parser_v1_new ();
        mm_serial_parser_v1_add_filter (parser,
                                        serial_parser_filter_cb,
                                        NULL);
        mm_port_serial_at_set_response_parser (MM_PORT_SERIAL_AT (ctx->serial),
                                               mm_serial_parser_v1_parse,
                                               mm_serial_parser_v1_remove_echo,
                                               parser,
                                               mm_serial_parser_v1_destroy);
    }

    /* Try to open the port */
    if (!mm_port_serial_open (ctx->serial, &error)) {
        /* Abort if maximum number of open tries reached */
        if (++ctx->at_open_tries > 4) {
            /* took too long to open the port; give up */
            port_probe_task_return_error (self,
                                          g_error_new (MM_CORE_ERROR,
                                                       MM_CORE_ERROR_FAILED,
                                                       "(%s/%s) failed to open port after 4 tries",
                                                       mm_kernel_device_get_subsystem (self->priv->port),
                                                       mm_kernel_device_get_name (self->priv->port)));
            g_clear_error (&error);
            return G_SOURCE_REMOVE;
        }

        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE)) {
            /* this is nozomi being dumb; try again */
            ctx->source_id = g_timeout_add_seconds (1, (GSourceFunc) serial_open_at, self);
            g_clear_error (&error);
            return G_SOURCE_REMOVE;
        }

        port_probe_task_return_error (self,
                                      g_error_new (MM_SERIAL_ERROR,
                                                   MM_SERIAL_ERROR_OPEN_FAILED,
                                                   "(%s/%s) failed to open port: %s",
                                                   mm_kernel_device_get_subsystem (self->priv->port),
                                                   mm_kernel_device_get_name (self->priv->port),
                                                   (error ? error->message : "unknown error")));
        g_clear_error (&error);
        return G_SOURCE_REMOVE;
    }

    /* success, start probing */
    ctx->buffer_full_id = g_signal_connect (ctx->serial, "buffer-full",
                                            G_CALLBACK (serial_buffer_full), self);
    mm_port_serial_flash (MM_PORT_SERIAL (ctx->serial),
                          100,
                          TRUE,
                          (GAsyncReadyCallback) serial_flash_ready,
                          self);
    return G_SOURCE_REMOVE;
}

#define PROBE_FLAGS_AT_MASK (MM_PORT_PROBE_AT | \
                             MM_PORT_PROBE_AT_VENDOR | \
                             MM_PORT_PROBE_AT_PRODUCT | \
                             MM_PORT_PROBE_AT_ICERA | \
                             MM_PORT_PROBE_AT_XMM)

#define AT_PROBING_DEFAULT_TRIES 6

static void
probe_step (MMPortProbe *self)
{
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);

    /* If already cancelled, do nothing else */
    if (port_probe_task_return_error_if_cancelled (self))
        return;

    /* Cleanup from previous iterations */
    ctx->at_result_processor   = NULL;
    ctx->at_commands           = NULL;
    ctx->at_commands_wait_secs = 0;
    ctx->at_commands_limit     = G_MAXUINT; /* run all given AT probes */

    switch (ctx->step) {
    case PROBE_STEP_FIRST:
        mm_obj_msg (self, "probe step: start");
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_CUSTOM_INIT_OPEN_PORT:
        if ((ctx->flags & MM_PORT_PROBE_AT) && (ctx->at_custom_init && ctx->at_custom_init_finish)) {
            mm_obj_msg (self, "probe step: AT custom init open port");
            ctx->source_id = g_idle_add ((GSourceFunc) serial_open_at, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_CUSTOM_INIT:
        /* If we got some custom initialization setup requested, go on with it
         * first. We completely ignore the custom initialization if the serial port
         * that we receive in the context isn't an AT port (e.g. if it was flagged
         * as not being an AT port early) */
        if ((ctx->flags & MM_PORT_PROBE_AT) && (ctx->at_custom_init && ctx->at_custom_init_finish)) {
            mm_obj_msg (self, "probe step: AT custom init run");
            g_assert (MM_IS_PORT_SERIAL_AT (ctx->serial));
            ctx->at_custom_init (self,
                                 MM_PORT_SERIAL_AT (ctx->serial),
                                 ctx->at_probing_cancellable,
                                 (GAsyncReadyCallback) at_custom_init_ready,
                                 NULL);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_OPEN_PORT:
        /* If the port has AT probes, but at least one of the AT probes hasn't
         * completed yet, open the serial port.
         */
        if ((ctx->flags & PROBE_FLAGS_AT_MASK) &&
            ((ctx->flags & PROBE_FLAGS_AT_MASK) != (self->priv->flags & PROBE_FLAGS_AT_MASK))) {
            mm_obj_msg (self, "probe step: AT open port");
            /* We might end up back here after later probe types fail, so make
             * sure we have a usable AT port.
             */
            if (ctx->serial && !MM_IS_PORT_SERIAL_AT (ctx->serial))
                clear_probe_serial_port (ctx);
            ctx->source_id = g_idle_add ((GSourceFunc) serial_open_at, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT:
        if ((ctx->flags & MM_PORT_PROBE_AT) && !(self->priv->flags & MM_PORT_PROBE_AT)) {
            mm_obj_msg (self, "probe step: AT");
            /* Prepare AT probing */
            if (ctx->at_custom_probe)
                ctx->at_commands = ctx->at_custom_probe;
            else {
                gint at_probe_tries;

                /* NOTE: update ID_MM_TTY_AT_PROBE_TRIES documentation when changing min/max/default */
                at_probe_tries = mm_kernel_device_get_property_as_int (mm_port_probe_peek_port (self),
                                                                       ID_MM_TTY_AT_PROBE_TRIES);
                /* If no tag, use default number of tries */
                if (at_probe_tries <= 0)
                    at_probe_tries = AT_PROBING_DEFAULT_TRIES;
                ctx->at_commands_limit = MIN (at_probe_tries, (gint) G_N_ELEMENTS (at_probing));
                ctx->at_commands = at_probing;
            }
            ctx->at_result_processor = probe_at_result_processor;
            ctx->source_id = g_idle_add ((GSourceFunc) probe_at, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_VENDOR:
        /* Vendor requested and not already probed? */
        if ((ctx->flags & MM_PORT_PROBE_AT_VENDOR) && !(self->priv->flags & MM_PORT_PROBE_AT_VENDOR)) {
            mm_obj_msg (self, "probe step: AT vendor");
            ctx->at_result_processor = probe_at_vendor_result_processor;
            ctx->at_commands = vendor_probing;
            ctx->source_id = g_idle_add ((GSourceFunc) probe_at, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_PRODUCT:
        /* Product requested and not already probed? */
        if ((ctx->flags & MM_PORT_PROBE_AT_PRODUCT) && !(self->priv->flags & MM_PORT_PROBE_AT_PRODUCT)) {
            mm_obj_msg (self, "probe step: AT product");
            ctx->at_result_processor = probe_at_product_result_processor;
            ctx->at_commands = product_probing;
            ctx->source_id = g_idle_add ((GSourceFunc) probe_at, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_ICERA:
        /* Icera support check requested and not already done? */
        if ((ctx->flags & MM_PORT_PROBE_AT_ICERA) && !(self->priv->flags & MM_PORT_PROBE_AT_ICERA)) {
            mm_obj_msg (self, "probe step: Icera");
            ctx->at_result_processor = probe_at_icera_result_processor;
            ctx->at_commands = icera_probing;
            /* By default, wait 2 seconds between ICERA probing retries */
            ctx->at_commands_wait_secs = 2;
            ctx->source_id = g_idle_add ((GSourceFunc) probe_at, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_XMM:
        /* XMM support check requested and not already done? */
        if ((ctx->flags & MM_PORT_PROBE_AT_XMM) && !(self->priv->flags & MM_PORT_PROBE_AT_XMM)) {
            mm_obj_msg (self, "probe step: XMM");
            /* Prepare AT product probing */
            ctx->at_result_processor = probe_at_xmm_result_processor;
            ctx->at_commands = xmm_probing;
            ctx->source_id = g_idle_add ((GSourceFunc) probe_at, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_AT_CLOSE_PORT:
        if (ctx->serial) {
            mm_obj_msg (self, "probe step: AT close port");
            clear_probe_serial_port (ctx);
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_QCDM:
        /* QCDM requested and not already probed? */
        if ((ctx->flags & MM_PORT_PROBE_QCDM) && !(self->priv->flags & MM_PORT_PROBE_QCDM)) {
            mm_obj_msg (self, "probe step: QCDM");
            ctx->source_id = g_idle_add ((GSourceFunc) probe_qcdm, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_QCDM_CLOSE_PORT:
        if (ctx->serial) {
             mm_obj_msg (self, "probe step: QCDM close port");
            clear_probe_serial_port (ctx);
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_QMI:
        /* QMI probing needed? */
        if ((ctx->flags & MM_PORT_PROBE_QMI) && !(self->priv->flags & MM_PORT_PROBE_QMI)) {
            mm_obj_msg (self, "probe step: QMI");
            ctx->source_id = g_idle_add ((GSourceFunc) wdm_probe_qmi, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_MBIM:
        /* MBIM probing needed */
        if ((ctx->flags & MM_PORT_PROBE_MBIM) && !(self->priv->flags & MM_PORT_PROBE_MBIM)) {
            mm_obj_msg (self, "probe step: MBIM");
            ctx->source_id = g_idle_add ((GSourceFunc) wdm_probe_mbim, self);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PROBE_STEP_LAST:
        /* All done! */
        mm_obj_msg (self, "probe step: done");
        port_probe_task_return_boolean (self, TRUE);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
probe_step_next (MMPortProbe *self)
{
    PortProbeRunContext *ctx;

    g_assert (self->priv->task);
    ctx = g_task_get_task_data (self->priv->task);

    ctx->step++;
    probe_step (self);
}

static void
at_cancellable_cancel (GCancellable        *cancellable,
                       PortProbeRunContext *ctx)
{
    /* Avoid trying to disconnect cancellable on the handler, or we'll deadlock */
    ctx->at_probing_cancellable_linked = 0;
    g_cancellable_cancel (ctx->at_probing_cancellable);
}

gboolean
mm_port_probe_run_cancel_at_probing (MMPortProbe *self)
{
    PortProbeRunContext *ctx;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (!self->priv->task)
        return FALSE;

    ctx = g_task_get_task_data (self->priv->task);
    if (g_cancellable_is_cancelled (ctx->at_probing_cancellable))
        return FALSE;

    mm_obj_dbg (self, "requested to cancel all AT probing");
    g_cancellable_cancel (ctx->at_probing_cancellable);
    return TRUE;
}

gboolean
mm_port_probe_run_finish (MMPortProbe   *self,
                          GAsyncResult  *result,
                          GError       **error)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);
    g_return_val_if_fail (G_IS_TASK (result), FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}

void
mm_port_probe_run (MMPortProbe                *self,
                   MMPortProbeFlag             flags,
                   guint64                     at_send_delay,
                   gboolean                    at_remove_echo,
                   gboolean                    at_send_lf,
                   const MMPortProbeAtCommand *at_custom_probe,
                   const MMAsyncMethod        *at_custom_init,
                   gboolean                    qcdm_required,
                   GCancellable               *cancellable,
                   GAsyncReadyCallback         callback,
                   gpointer                    user_data)
{
    PortProbeRunContext *ctx;
    gchar               *probe_list_str;
    guint32              i;

    g_return_if_fail (MM_IS_PORT_PROBE (self));
    g_return_if_fail (flags != MM_PORT_PROBE_NONE);
    g_return_if_fail (callback != NULL);

    /* Shouldn't schedule more than one probing at a time */
    g_assert (self->priv->task == NULL);
    self->priv->task = g_task_new (self, cancellable, callback, user_data);

    /* Task context */
    ctx = g_slice_new0 (PortProbeRunContext);
    ctx->step = PROBE_STEP_FIRST;
    ctx->at_send_delay = at_send_delay;
    ctx->at_remove_echo = at_remove_echo;
    ctx->at_send_lf = at_send_lf;
    ctx->flags = MM_PORT_PROBE_NONE;
    ctx->at_custom_probe = at_custom_probe;
    ctx->at_custom_init = at_custom_init ? (MMPortProbeAtCustomInit)at_custom_init->async : NULL;
    ctx->at_custom_init_finish = at_custom_init ? (MMPortProbeAtCustomInitFinish)at_custom_init->finish : NULL;
    ctx->qcdm_required = qcdm_required;
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

    /* The context will be owned by the task */
    g_task_set_task_data (self->priv->task, ctx, (GDestroyNotify) port_probe_run_context_free);

    /* If we're told to completely ignore the port, don't do any probing */
    if (self->priv->is_ignored) {
        mm_obj_dbg (self, "port probing finished: skipping for ignored port");
        port_probe_task_return_boolean (self, TRUE);
        return;
    }

    /* If this is a port flagged as a GPS port, don't do any other probing */
    if (self->priv->is_gps) {
        mm_obj_dbg (self, "GPS port detected");
        mm_port_probe_set_result_at   (self, FALSE);
        mm_port_probe_set_result_qcdm (self, FALSE);
        mm_port_probe_set_result_qmi  (self, FALSE);
        mm_port_probe_set_result_mbim (self, FALSE);
    }

    /* If this is a port flagged as an audio port, don't do any other probing */
    if (self->priv->is_audio) {
        mm_obj_dbg (self, "audio port detected");
        mm_port_probe_set_result_at   (self, FALSE);
        mm_port_probe_set_result_qcdm (self, FALSE);
        mm_port_probe_set_result_qmi  (self, FALSE);
        mm_port_probe_set_result_mbim (self, FALSE);
    }

    /* If this is a port flagged as an XMMRPC port, don't do any other probing */
    if (self->priv->is_xmmrpc) {
        mm_obj_dbg (self, "XMMRPC port detected");
        mm_port_probe_set_result_at   (self, FALSE);
        mm_port_probe_set_result_qcdm (self, FALSE);
        mm_port_probe_set_result_qmi  (self, FALSE);
        mm_port_probe_set_result_mbim (self, FALSE);
    }

    /* If this is a port flagged as being an AT port, don't do any other probing */
    if (self->priv->maybe_at) {
        mm_obj_dbg (self, "no QCDM/QMI/MBIM probing in possible AT port");
        mm_port_probe_set_result_qcdm (self, FALSE);
        mm_port_probe_set_result_qmi  (self, FALSE);
        mm_port_probe_set_result_mbim (self, FALSE);
    }

    /* If this is a port flagged as being a QCDM port, don't do any other probing */
    if (self->priv->maybe_qcdm) {
        mm_obj_dbg (self, "no AT/QMI/MBIM probing in possible QCDM port");
        mm_port_probe_set_result_at   (self, FALSE);
        mm_port_probe_set_result_qmi  (self, FALSE);
        mm_port_probe_set_result_mbim (self, FALSE);
    }

    /* If this is a port flagged as being a QMI port, don't do any other probing */
    if (self->priv->maybe_qmi) {
        mm_obj_dbg (self, "no AT/QCDM/MBIM probing in possible QMI port");
        mm_port_probe_set_result_at   (self, FALSE);
        mm_port_probe_set_result_qcdm (self, FALSE);
        mm_port_probe_set_result_mbim (self, FALSE);
    }

    /* If this is a port flagged as being a MBIM port, don't do any other probing */
    if (self->priv->maybe_mbim) {
        mm_obj_dbg (self, "no AT/QCDM/QMI probing in possible MBIM port");
        mm_port_probe_set_result_at   (self, FALSE);
        mm_port_probe_set_result_qcdm (self, FALSE);
        mm_port_probe_set_result_qmi  (self, FALSE);
    }

    /* Check if we already have the requested probing results.
     * We will fix here the 'ctx->flags' so that we only request probing
     * for the missing things. */
    for (i = MM_PORT_PROBE_AT; i <= MM_PORT_PROBE_MBIM; i = (i << 1)) {
        if ((flags & i) && !(self->priv->flags & i))
            ctx->flags += i;
    }

    /* All requested probings already available? If so, we're done */
    if (!ctx->flags) {
        mm_obj_dbg (self, "port probing finished: no more probings needed");
        port_probe_task_return_boolean (self, TRUE);
        return;
    }

    /* Log the probes scheduled to be run */
    probe_list_str = mm_port_probe_flag_build_string_from_mask (ctx->flags);
    mm_obj_dbg (self, "launching port probing: '%s'", probe_list_str);
    g_free (probe_list_str);

    if (ctx->flags & PROBE_FLAGS_AT_MASK) {
        ctx->at_probing_cancellable = g_cancellable_new ();
        /* If the main cancellable is cancelled, so will be the at-probing one */
        if (cancellable)
            ctx->at_probing_cancellable_linked = g_cancellable_connect (cancellable,
                                                                        (GCallback) at_cancellable_cancel,
                                                                        ctx,
                                                                        NULL);
    }

    probe_step (self);
}

gboolean
mm_port_probe_is_at (MMPortProbe *self)
{
    const gchar *subsys;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = mm_kernel_device_get_subsystem (self->priv->port);
    if (g_str_equal (subsys, "net"))
        return FALSE;

    return (self->priv->flags & MM_PORT_PROBE_AT ?
            self->priv->is_at :
            FALSE);
}

gboolean
mm_port_probe_list_has_at_port (GList *list)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)){
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (!probe->priv->is_ignored &&
            probe->priv->flags & MM_PORT_PROBE_AT &&
            probe->priv->is_at)
            return TRUE;
    }

    return FALSE;
}

gboolean
mm_port_probe_is_qcdm (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return (self->priv->flags & MM_PORT_PROBE_QCDM ?
            self->priv->is_qcdm :
            FALSE);
}

gboolean
mm_port_probe_is_qmi (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return (self->priv->flags & MM_PORT_PROBE_QMI ?
            self->priv->is_qmi :
            FALSE);
}

gboolean
mm_port_probe_list_has_qmi_port (GList *list)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (!probe->priv->is_ignored &&
            mm_port_probe_is_qmi (probe))
            return TRUE;
    }

    return FALSE;
}

gboolean
mm_port_probe_is_mbim (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return (self->priv->flags & MM_PORT_PROBE_MBIM ?
            self->priv->is_mbim :
            FALSE);
}

gboolean
mm_port_probe_list_has_mbim_port (GList *list)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (!probe->priv->is_ignored &&
            mm_port_probe_is_mbim (probe))
            return TRUE;
    }

    return FALSE;
}

gboolean
mm_port_probe_is_xmmrpc (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return self->priv->is_xmmrpc;
}

gboolean
mm_port_probe_list_has_xmmrpc_port (GList *list)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (!probe->priv->is_ignored &&
            mm_port_probe_is_xmmrpc (probe))
            return TRUE;
    }

    return FALSE;
}

MMPortGroup
mm_port_probe_get_port_group (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), MM_PORT_GROUP_UNKNOWN);

    if (self->priv->is_ignored)
        return MM_PORT_GROUP_IGNORED;

    return MM_PORT_GROUP_USED;
}

MMPortType
mm_port_probe_get_port_type (MMPortProbe *self)
{
    const gchar *subsys;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = mm_kernel_device_get_subsystem (self->priv->port);

    if (g_str_equal (subsys, "net"))
        return MM_PORT_TYPE_NET;

#if defined WITH_QMI
    if (self->priv->flags & MM_PORT_PROBE_QMI &&
        self->priv->is_qmi)
        return MM_PORT_TYPE_QMI;
#endif

#if defined WITH_MBIM
    if (self->priv->flags & MM_PORT_PROBE_MBIM &&
        self->priv->is_mbim)
        return MM_PORT_TYPE_MBIM;
#endif

    if (self->priv->flags & MM_PORT_PROBE_QCDM &&
        self->priv->is_qcdm)
        return MM_PORT_TYPE_QCDM;

    if (self->priv->flags & MM_PORT_PROBE_AT &&
        self->priv->is_at)
        return MM_PORT_TYPE_AT;

    if (self->priv->is_gps)
        return MM_PORT_TYPE_GPS;

    if (self->priv->is_audio)
        return MM_PORT_TYPE_AUDIO;

    if (self->priv->is_xmmrpc)
        return MM_PORT_TYPE_XMMRPC;

    return MM_PORT_TYPE_UNKNOWN;
}

MMDevice *
mm_port_probe_peek_device (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->device;
}

MMDevice *
mm_port_probe_get_device (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return MM_DEVICE (g_object_ref (self->priv->device));
}

MMKernelDevice *
mm_port_probe_peek_port (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->port;
};

MMKernelDevice *
mm_port_probe_get_port (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return MM_KERNEL_DEVICE (g_object_ref (self->priv->port));
};

const gchar *
mm_port_probe_get_vendor (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return (self->priv->flags & MM_PORT_PROBE_AT_VENDOR ?
            self->priv->vendor :
            NULL);
}

const gchar *
mm_port_probe_get_product (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return (self->priv->flags & MM_PORT_PROBE_AT_PRODUCT ?
            self->priv->product :
            NULL);
}

gboolean
mm_port_probe_is_icera (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return (self->priv->flags & MM_PORT_PROBE_AT_ICERA ?
            self->priv->is_icera :
            FALSE);
}

gboolean
mm_port_probe_list_is_icera (GList *probes)
{
    GList *l;

    for (l = probes; l; l = g_list_next (l)) {
        if (mm_port_probe_is_icera (MM_PORT_PROBE (l->data)))
            return TRUE;
    }

    return FALSE;
}

gboolean
mm_port_probe_is_xmm (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return (self->priv->flags & MM_PORT_PROBE_AT_XMM ?
            self->priv->is_xmm :
            FALSE);
}

gboolean
mm_port_probe_list_is_xmm (GList *probes)
{
    GList *l;

    for (l = probes; l; l = g_list_next (l)) {
        if (mm_port_probe_is_xmm (MM_PORT_PROBE (l->data)))
            return TRUE;
    }

    return FALSE;
}

const gchar *
mm_port_probe_get_port_name (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return mm_kernel_device_get_name (self->priv->port);
}

const gchar *
mm_port_probe_get_port_subsys (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return mm_kernel_device_get_subsystem (self->priv->port);
}

static void
initialize_port_type_hints (MMPortProbe *self)
{
    g_autoptr(GString) udev_tags = NULL;
    guint              n_udev_hints = 0;
    gboolean           auto_maybe_qmi = FALSE;
    gboolean           auto_maybe_mbim = FALSE;
    gboolean           auto_is_xmmrpc = FALSE;
    gboolean           auto_maybe_at = FALSE;
    gboolean           auto_maybe_qcdm = FALSE;
    gboolean           auto_ignored = FALSE;

#define ADD_HINT_FROM_UDEV_TAG(TAG, FIELD) do {                                 \
        if (!self->priv->FIELD &&                                               \
            mm_kernel_device_get_property_as_boolean (self->priv->port, TAG)) { \
            mm_obj_dbg (self, "port type hint detected in udev tag: %s", TAG);  \
            self->priv->FIELD = TRUE;                                           \
            n_udev_hints++;                                                     \
            if (!udev_tags)                                                     \
                udev_tags = g_string_new (TAG);                                 \
            else                                                                \
                g_string_append_printf (udev_tags, ", %s", TAG);                \
        }                                                                       \
    } while (0)

    /* Process udev-configured port type hints */
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_GPS,           is_gps);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_AUDIO,         is_audio);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_XMMRPC,        is_xmmrpc);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_AT_PRIMARY,    maybe_at);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_AT_SECONDARY,  maybe_at);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_AT_PPP,        maybe_at);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_QCDM,          maybe_qcdm);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_QMI,           maybe_qmi);
    ADD_HINT_FROM_UDEV_TAG (ID_MM_PORT_TYPE_MBIM,          maybe_mbim);

    /* Warn if more than one given at the same time */
    if (n_udev_hints > 1)
        mm_obj_warn (self, "multiple incompatible port type hints configured via udev: %s", udev_tags->str);

    /* Process automatic port type hints, and warn if the hint doesn't match the
     * one provided via udev. The udev-provided hints are always preferred. */
    if (!g_strcmp0 (mm_kernel_device_get_subsystem (self->priv->port), "usbmisc")) {
        const gchar *driver;

        driver = mm_kernel_device_get_driver (self->priv->port);
        if (!g_strcmp0 (driver, "qmi_wwan")) {
            mm_obj_dbg (self, "port may be QMI based on the driver in use");
            auto_maybe_qmi = TRUE;
        } else if (!g_strcmp0 (driver, "cdc_mbim")) {
            mm_obj_dbg (self, "port may be MBIM based on the driver in use");
            auto_maybe_mbim = TRUE;
        } else {
            mm_obj_dbg (self, "port may be AT based on the driver in use: %s", driver);
            auto_maybe_at = TRUE;
        }
    } else if (!g_strcmp0 (mm_kernel_device_get_subsystem (self->priv->port), "wwan")) {
        /* Linux >= 5.14 has at 'type' attribute specifying the type of port */
        if (mm_kernel_device_has_attribute (self->priv->port, "type")) {
            const gchar *type;

            type = mm_kernel_device_get_attribute (self->priv->port, "type");
            if (!g_strcmp0 (type, "AT")) {
                mm_obj_dbg (self, "port may be AT based on the wwan type attribute");
                auto_maybe_at = TRUE;
            } else if (!g_strcmp0 (type, "MBIM")) {
                mm_obj_dbg (self, "port may be MBIM based on the wwan type attribute");
                auto_maybe_mbim = TRUE;
            } else if (!g_strcmp0 (type, "XMMRPC")) {
                mm_obj_dbg (self, "port is XMMRPC based on the wwan type attribute");
                auto_is_xmmrpc = TRUE;
            } else if (!g_strcmp0 (type, "QMI")) {
                mm_obj_dbg (self, "port may be QMI based on the wwan type attribute");
                auto_maybe_qmi = TRUE;
            } else if (!g_strcmp0 (type, "QCDM")) {
                mm_obj_dbg (self, "port may be QCDM based on the wwan type attribute");
                auto_maybe_qcdm = TRUE;
            } else if (!g_strcmp0 (type, "FIREHOSE")) {
                mm_obj_dbg (self, "port may be FIREHOSE based on the wwan type attribute");
                auto_ignored = TRUE;
            } else if (!g_strcmp0 (type, "FASTBOOT")) {
                mm_obj_dbg (self, "port may be FASTBOOT based on the wwan type attribute");
                auto_ignored = TRUE;
            }
        }
        /* Linux 5.13 does not have 'type' attribute yet, match kernel name instead */
        else {
            const gchar *name;

            name = mm_kernel_device_get_name (self->priv->port);
            if (g_str_has_suffix (name, "AT")) {
                mm_obj_dbg (self, "port may be AT based on the wwan device name");
                auto_maybe_at = TRUE;
            } else if (g_str_has_suffix (name, "MBIM")) {
                mm_obj_dbg (self, "port may be MBIM based on the wwan device name");
                auto_maybe_mbim = TRUE;
            } else if (g_str_has_suffix (name, "QMI")) {
                mm_obj_dbg (self, "port may be QMI based on the wwan device name");
                auto_maybe_qmi = TRUE;
            } else if (g_str_has_suffix (name, "QCDM")) {
                mm_obj_dbg (self, "port may be QCDM based on the wwan device name");
                auto_maybe_qcdm = TRUE;
            } else if (g_str_has_suffix (name, "FIREHOSE")) {
                mm_obj_dbg (self, "port may be FIREHOSE based on the wwan device name");
                auto_ignored = TRUE;
            }
        }
    }

    g_assert ((auto_maybe_qmi +
               auto_maybe_mbim +
               auto_is_xmmrpc +
               auto_maybe_at +
               auto_maybe_qcdm +
               auto_ignored) <= 1);

#define PROCESS_AUTO_HINTS(TYPE, FIELD) do {                            \
        if (auto_##FIELD) {                                             \
            if (n_udev_hints > 0 && !self->priv->FIELD)                 \
                mm_obj_warn (self, "overriding type in possible " TYPE " port with udev tag: %s", udev_tags->str); \
            else                                                        \
                self->priv->FIELD = TRUE;                               \
        }                                                               \
    } while (0)

    PROCESS_AUTO_HINTS ("QMI",    maybe_qmi);
    PROCESS_AUTO_HINTS ("MBIM",   maybe_mbim);
    PROCESS_AUTO_HINTS ("XMMRPC", is_xmmrpc);
    PROCESS_AUTO_HINTS ("AT",     maybe_at);
    PROCESS_AUTO_HINTS ("QCDM",   maybe_qcdm);

#undef PROCESS_AUTO_HINTS

    mm_obj_dbg (self, "port type hints loaded: AT %s, QMI %s, MBIM %s, QCDM %s, XMMRPC %s, AUDIO %s, GPS %s",
                self->priv->maybe_at ? "yes" : "no",
                self->priv->maybe_qmi ? "yes" : "no",
                self->priv->maybe_mbim ? "yes" : "no",
                self->priv->maybe_qcdm ? "yes" : "no",
                self->priv->is_xmmrpc ? "yes" : "no",
                self->priv->is_audio ? "yes" : "no",
                self->priv->is_gps ? "yes" : "no");

    /* Regardless of the type, the port may be ignored */
    if (mm_kernel_device_get_property_as_boolean (self->priv->port, ID_MM_PORT_IGNORE)) {
        mm_obj_dbg (self, "port is ignored via udev tag");
        self->priv->is_ignored = TRUE;
    } else if (auto_ignored) {
        mm_obj_dbg (self, "port is ignored via automatic rules");
        self->priv->is_ignored = TRUE;
    }
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMPortProbe *self;

    self = MM_PORT_PROBE (_self);
    return g_strdup_printf ("%s/probe", mm_kernel_device_get_name (self->priv->port));
}

/*****************************************************************************/

MMPortProbe *
mm_port_probe_new (MMDevice       *device,
                   MMKernelDevice *port)
{
    return MM_PORT_PROBE (g_object_new (MM_TYPE_PORT_PROBE,
                                        MM_PORT_PROBE_DEVICE, device,
                                        MM_PORT_PROBE_PORT,   port,
                                        NULL));
}

static void
mm_port_probe_init (MMPortProbe *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_PORT_PROBE,
                                              MMPortProbePrivate);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMPortProbe *self = MM_PORT_PROBE (object);

    switch (prop_id) {
    case PROP_DEVICE:
        /* construct only, no new reference! */
        self->priv->device = g_value_get_object (value);
        break;
    case PROP_PORT:
        /* construct only */
        self->priv->port = g_value_dup_object (value);
        initialize_port_type_hints (self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMPortProbe *self = MM_PORT_PROBE (object);

    switch (prop_id) {
    case PROP_DEVICE:
        g_value_set_object (value, self->priv->device);
        break;
    case PROP_PORT:
        g_value_set_object (value, self->priv->port);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMPortProbe *self = MM_PORT_PROBE (object);

    /* We should never have a task here */
    g_assert (self->priv->task == NULL);

    g_free (self->priv->vendor);
    g_free (self->priv->product);

    G_OBJECT_CLASS (mm_port_probe_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMPortProbe *self = MM_PORT_PROBE (object);

    /* We didn't get a reference to the device */
    self->priv->device = NULL;

    g_clear_object (&self->priv->port);

    G_OBJECT_CLASS (mm_port_probe_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_port_probe_class_init (MMPortProbeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortProbePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_DEVICE] =
        g_param_spec_object (MM_PORT_PROBE_DEVICE,
                             "Device",
                             "Device owning this probe",
                             MM_TYPE_DEVICE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DEVICE, properties[PROP_DEVICE]);

    properties[PROP_PORT] =
        g_param_spec_object (MM_PORT_PROBE_PORT,
                             "Port",
                             "kernel device object of the port",
                             MM_TYPE_KERNEL_DEVICE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PORT, properties[PROP_PORT]);
}
