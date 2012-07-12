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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-probe.h"
#include "mm-log.h"
#include "mm-at-serial-port.h"
#include "mm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-port-probe-at.h"
#include "libqcdm/src/commands.h"
#include "libqcdm/src/utils.h"
#include "libqcdm/src/errors.h"
#include "mm-qcdm-serial-port.h"
#include "mm-daemon-enums-types.h"

/*
 * Steps and flow of the Probing process:
 * ----> AT Serial Open
 *   |----> Custom Init
 *   |----> AT?
 *      |----> Vendor
 *      |----> Product
 * ----> QCDM Serial Open
 *   |----> QCDM?
 */

G_DEFINE_TYPE (MMPortProbe, mm_port_probe, G_TYPE_OBJECT)

typedef struct {
    /* ---- Generic task context ---- */
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    GCancellable *at_probing_cancellable;
    guint32 flags;
    guint source_id;
    guint buffer_full_id;
    MMSerialPort *serial;

    /* ---- AT probing specific context ---- */
    guint64 at_send_delay;
    /* Number of times we tried to open the AT port */
    guint at_open_tries;
    /* Custom initialization commands for the AT port */
    const MMPortProbeAtCommand *at_custom_init;
    /* Current group of AT commands to be sent */
    const MMPortProbeAtCommand *at_commands;
    /* Current AT Result processor */
    void (* at_result_processor) (MMPortProbe *self,
                                  GVariant *result);
} PortProbeRunTask;

struct _MMPortProbePrivate {
    /* Port and properties */
    GUdevDevice *port;
    gchar *subsys;
    gchar *name;
    gchar *physdev_path;
    gchar *driver;

    /* Probing results */
    guint32 flags;
    gboolean is_at;
    gboolean is_qcdm;
    gchar *vendor;
    gchar *product;

    /* Current probing task. Only one can be available at a time */
    PortProbeRunTask *task;
};

void
mm_port_probe_set_result_at (MMPortProbe *self,
                             gboolean at)
{
    self->priv->is_at = at;
    self->priv->flags |= MM_PORT_PROBE_AT;

    if (self->priv->is_at) {
        mm_dbg ("(%s) port is AT-capable", self->priv->name);

        /* Also set as not a QCDM port */
        self->priv->is_qcdm = FALSE;
        self->priv->flags |= MM_PORT_PROBE_QCDM;
    } else {
        mm_dbg ("(%s) port is not AT-capable", self->priv->name);
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->flags |= (MM_PORT_PROBE_AT_VENDOR | MM_PORT_PROBE_AT_PRODUCT);
    }
}

void
mm_port_probe_set_result_at_vendor (MMPortProbe *self,
                                    const gchar *at_vendor)
{
    if (at_vendor) {
        mm_dbg ("(%s) vendor probing finished", self->priv->name);
        self->priv->vendor = g_utf8_casefold (at_vendor, -1);
        self->priv->flags |= MM_PORT_PROBE_AT_VENDOR;
    } else {
        mm_dbg ("(%s) couldn't probe for vendor string", self->priv->name);
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
        mm_dbg ("(%s) product probing finished", self->priv->name);
        self->priv->product = g_utf8_casefold (at_product, -1);
        self->priv->flags |= MM_PORT_PROBE_AT_PRODUCT;
    } else {
        mm_dbg ("(%s) couldn't probe for product string", self->priv->name);
        self->priv->product = NULL;
        self->priv->flags |= MM_PORT_PROBE_AT_PRODUCT;
    }
}

void
mm_port_probe_set_result_qcdm (MMPortProbe *self,
                               gboolean qcdm)
{
    self->priv->is_qcdm = qcdm;
    self->priv->flags |= MM_PORT_PROBE_QCDM;

    if (self->priv->is_qcdm) {
        mm_dbg ("(%s) port is QCDM-capable", self->priv->name);

        /* Also set as not an AT port */
        self->priv->is_at = FALSE;
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->flags |= (MM_PORT_PROBE_AT |
                              MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT);
    } else
        mm_dbg ("(%s) port is not QCDM-capable", self->priv->name);
}

static gboolean serial_probe_at (MMPortProbe *self);
static gboolean serial_probe_qcdm (MMPortProbe *self);
static void serial_probe_schedule (MMPortProbe *self);

static void
port_probe_run_task_free (PortProbeRunTask *task)
{
    if (task->source_id)
        g_source_remove (task->source_id);

    if (task->buffer_full_id)
        g_source_remove (task->buffer_full_id);

    if (task->serial) {
        if (mm_serial_port_is_open (task->serial))
            mm_serial_port_close (task->serial);
        g_object_unref (task->serial);
    }

    if (task->cancellable)
        g_object_unref (task->cancellable);
    if (task->at_probing_cancellable)
        g_object_unref (task->at_probing_cancellable);

    g_object_unref (task->result);
    g_free (task);
}

static void
port_probe_run_task_complete (PortProbeRunTask *task,
                              gboolean result,
                              GError *error)
{
    /* As soon as we have the task completed, disable the buffer-full signal
     * handling, so that we do not get unwanted errors reported */
    if (task->buffer_full_id) {
        g_source_remove (task->buffer_full_id);
        task->buffer_full_id = 0;
    }

    if (error)
        g_simple_async_result_take_error (task->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (task->result, result);

    /* Always complete in idle */
    g_simple_async_result_complete_in_idle (task->result);
}

static gboolean
port_probe_run_is_cancelled (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

    /* Manually check if cancelled.
     * TODO: Make the serial port response wait cancellable,
     * so that we can connect a callback to the cancellable and forget about
     * manually checking it.
     */
    if (g_cancellable_is_cancelled (task->cancellable)) {
        port_probe_run_task_complete (
            task,
            FALSE,
            g_error_new (MM_CORE_ERROR,
                         MM_CORE_ERROR_CANCELLED,
                         "(%s) port probing cancelled",
                         self->priv->name));
        return TRUE;
    }

    return FALSE;
}

static void
serial_probe_qcdm_parse_response (MMQcdmSerialPort *port,
                                  GByteArray *response,
                                  GError *error,
                                  MMPortProbe *self)
{
    QcdmResult *result;
    gint err = QCDM_SUCCESS;
    gboolean is_qcdm = FALSE;

    /* Just the initial poke; ignore it */
    if (!self)
        return;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return;

    if (!error) {
        /* Parse the response */
        result = qcdm_cmd_version_info_result ((const gchar *) response->data,
                                               response->len,
                                               &err);
        if (!result) {
            mm_warn ("(%s) failed to parse QCDM version info command result: %d",
                     self->priv->name,
                     err);
        } else {
            /* yay, probably a QCDM port */
            is_qcdm = TRUE;

            qcdm_result_unref (result);
        }
    }

    /* Set probing result */
    mm_port_probe_set_result_qcdm (self, is_qcdm);

    /* Reschedule probing */
    serial_probe_schedule (self);
}

static gboolean
serial_probe_qcdm (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;
    GByteArray *verinfo = NULL;
    GByteArray *verinfo2;
    gint len;

    task->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return FALSE;

    mm_dbg ("(%s) probing QCDM...", self->priv->name);

    /* If open, close the AT port */
    if (task->serial) {
        mm_serial_port_close (task->serial);
        g_object_unref (task->serial);
    }

    /* Open the QCDM port */
    task->serial = MM_SERIAL_PORT (mm_qcdm_serial_port_new (self->priv->name));
    if (!task->serial) {
        port_probe_run_task_complete (
            task,
            FALSE,
            g_error_new (MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "(%s) Couldn't create QCDM port",
                         self->priv->name));
        return FALSE;
    }

    /* Try to open the port */
    if (!mm_serial_port_open (task->serial, &error)) {
        port_probe_run_task_complete (
            task,
            FALSE,
            g_error_new (MM_SERIAL_ERROR,
                         MM_SERIAL_ERROR_OPEN_FAILED,
                         "(%s) Failed to open QCDM port: %s",
                         self->priv->name,
                         (error ? error->message : "unknown error")));
        g_clear_error (&error);
        return FALSE;
    }

    /* Build up the probe command */
    verinfo = g_byte_array_sized_new (50);
    len = qcdm_cmd_version_info_new ((gchar *) verinfo->data, 50);
    if (len <= 0) {
        g_byte_array_free (verinfo, TRUE);
        port_probe_run_task_complete (
            task,
            FALSE,
            g_error_new (MM_SERIAL_ERROR,
                         MM_SERIAL_ERROR_OPEN_FAILED,
                         "(%s) Failed to create QCDM versin info command",
                         self->priv->name));
        return FALSE;
    }
    verinfo->len = len;

    /* Queuing the command takes ownership over it; dup it for the second try */
    verinfo2 = g_byte_array_sized_new (verinfo->len);
    g_byte_array_append (verinfo2, verinfo->data, verinfo->len);

    /* Send the command twice; the ports often need to be woken up */
    mm_qcdm_serial_port_queue_command (MM_QCDM_SERIAL_PORT (task->serial),
                                       verinfo,
                                       3,
                                       NULL,
                                       (MMQcdmSerialResponseFn)serial_probe_qcdm_parse_response,
                                       NULL);
    mm_qcdm_serial_port_queue_command (MM_QCDM_SERIAL_PORT (task->serial),
                                       verinfo2,
                                       3,
                                       NULL,
                                       (MMQcdmSerialResponseFn)serial_probe_qcdm_parse_response,
                                       self);

    return FALSE;
}

static void
serial_probe_at_product_result_processor (MMPortProbe *self,
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
serial_probe_at_vendor_result_processor (MMPortProbe *self,
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
serial_probe_at_result_processor (MMPortProbe *self,
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
serial_probe_at_custom_init_result_processor (MMPortProbe *self,
                                              GVariant *result)
{
    PortProbeRunTask *task = self->priv->task;

    /* No result is really expected here, but we could get a boolean to indicate
     * AT support */
    if (result)
        serial_probe_at_result_processor (self, result);

    /* Reset so that it doesn't get scheduled again */
    task->at_custom_init = NULL;
}

static void
serial_probe_at_parse_response (MMAtSerialPort *port,
                                GString *response,
                                GError *error,
                                MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;
    GVariant *result = NULL;
    GError *result_error = NULL;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return;

    /* If AT probing cancelled, end this partial probing */
    if (g_cancellable_is_cancelled (task->at_probing_cancellable)) {
        mm_dbg ("(%s) no need to keep on probing the port for AT support",
                self->priv->name);
        task->at_result_processor (self, NULL);
        serial_probe_schedule (self);
        return;
    }

    if (!task->at_commands->response_processor (task->at_commands->command,
                                                response ? response->str : NULL,
                                                !!task->at_commands[1].command,
                                                error,
                                                &result,
                                                &result_error)) {
        /* Were we told to abort the whole probing? */
        if (result_error) {
            port_probe_run_task_complete (
                task,
                FALSE,
                g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "(%s) error while probing AT features: %s",
                             self->priv->name,
                             result_error->message));
            g_error_free (result_error);
            return;
        }

        /* Go on to next command */
        task->at_commands++;
        if (!task->at_commands->command) {
            /* Was it the last command in the group? If so,
             * end this partial probing */
            task->at_result_processor (self, NULL);
            /* Reschedule */
            serial_probe_schedule (self);
            return;
        }

        /* Schedule the next command in the probing group */
        task->source_id = g_idle_add ((GSourceFunc)serial_probe_at, self);
        return;
    }

    /* Run result processor.
     * Note that custom init commands are allowed to not return anything */
    task->at_result_processor (self, result);
    if (result)
        g_variant_unref (result);

    /* Reschedule probing */
    serial_probe_schedule (self);
}

static gboolean
serial_probe_at (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

    task->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return FALSE;

    /* If AT probing cancelled, end this partial probing */
    if (g_cancellable_is_cancelled (task->at_probing_cancellable)) {
        mm_dbg ("(%s) no need to launch probing for AT support",
                self->priv->name);
        task->at_result_processor (self, NULL);
        serial_probe_schedule (self);
        return FALSE;
    }

    mm_at_serial_port_queue_command (
        MM_AT_SERIAL_PORT (task->serial),
        task->at_commands->command,
        task->at_commands->timeout,
        task->at_probing_cancellable,
        (MMAtSerialResponseFn)serial_probe_at_parse_response,
        self);
    return FALSE;
}

static const MMPortProbeAtCommand at_probing[] = {
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { "AT",  3, mm_port_probe_response_processor_is_at },
    { NULL }
};

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

static void
serial_probe_schedule (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return;

    /* Cleanup */
    task->at_result_processor = NULL;
    task->at_commands = NULL;

    /* If we got some custom initialization commands requested, go on with them
     * first. */
    if (task->at_custom_init) {
        task->at_result_processor = serial_probe_at_custom_init_result_processor;
        task->at_commands = task->at_custom_init;
    }
    /* AT check requested and not already probed? */
    else if ((task->flags & MM_PORT_PROBE_AT) &&
             !(self->priv->flags & MM_PORT_PROBE_AT)) {
        /* Prepare AT probing */
        task->at_result_processor = serial_probe_at_result_processor;
        task->at_commands = at_probing;
    }
    /* Vendor requested and not already probed? */
    else if ((task->flags & MM_PORT_PROBE_AT_VENDOR) &&
        !(self->priv->flags & MM_PORT_PROBE_AT_VENDOR)) {
        /* Prepare AT vendor probing */
        task->at_result_processor = serial_probe_at_vendor_result_processor;
        task->at_commands = vendor_probing;
    }
    /* Product requested and not already probed? */
    else if ((task->flags & MM_PORT_PROBE_AT_PRODUCT) &&
             !(self->priv->flags & MM_PORT_PROBE_AT_PRODUCT)) {
        /* Prepare AT product probing */
        task->at_result_processor = serial_probe_at_product_result_processor;
        task->at_commands = product_probing;
    }

    /* If a next AT group detected, go for it */
    if (task->at_result_processor &&
        task->at_commands) {
        task->source_id = g_idle_add ((GSourceFunc)serial_probe_at, self);
        return;
    }

    /* QCDM requested and not already probed? */
    if ((task->flags & MM_PORT_PROBE_QCDM) &&
        !(self->priv->flags & MM_PORT_PROBE_QCDM)) {
        task->source_id = g_idle_add ((GSourceFunc)serial_probe_qcdm, self);
        return;
    }

    /* All done! Finish asynchronously */
    port_probe_run_task_complete (task, TRUE, NULL);
}

static void
serial_flash_done (MMSerialPort *port,
                   GError *error,
                   MMPortProbe *self)
{
    /* Schedule probing */
    serial_probe_schedule (self);
}

static const gchar *dq_strings[] = {
    /* Option Icera-based devices */
    "option/faema_",
    "os_logids.h",
    /* Sierra CnS port */
    "NETWORK SERVICE CHANGE",
    "/SRC/AMSS",
    NULL
};

static const guint8 zerobuf[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void
serial_buffer_full (MMSerialPort *serial,
                    GByteArray *buffer,
                    PortProbeRunTask *task)
{
    const gchar **iter;
    size_t iter_len;
    int i;

    /* Some devices (observed on a ZTE branded "QUALCOMM INCORPORATED" model
     * "154") spew NULLs from some ports.
     */
    if (   (buffer->len >= sizeof (zerobuf))
        && (memcmp (buffer->data, zerobuf, sizeof (zerobuf)) == 0)) {
        mm_serial_port_close (serial);
        port_probe_run_task_complete (task, FALSE, NULL);
        return;
    }

    /* Check for an immediate disqualification response.  There are some
     * ports (Option Icera-based chipsets have them, as do Qualcomm Gobi
     * devices before their firmware is loaded) that just shouldn't be
     * probed if we get a certain response because we know they can't be
     * used.  Kernel bugs (at least with 2.6.31 and 2.6.32) also trigger port
     * flow control kernel oopses if we read too much data for these ports.
     */

    for (iter = &dq_strings[0]; iter && *iter; iter++) {
        /* Search in the response for the item; the response could have embedded
         * nulls so we can't use memcmp() or strstr() on the whole response.
         */
        iter_len = strlen (*iter);
        for (i = 0; i < buffer->len - iter_len; i++) {
            if (!memcmp (&buffer->data[i], *iter, iter_len)) {
                /* Immediately close the port and complete probing */
                mm_serial_port_close (serial);
                port_probe_run_task_complete (task, FALSE, NULL);
                return;
            }
        }
    }
}

static gboolean
serial_open_at (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;

    task->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return FALSE;

    /* Create AT serial port if not done before */
    if (!task->serial) {
        task->serial = MM_SERIAL_PORT (mm_at_serial_port_new (self->priv->name));
        if (!task->serial) {
            port_probe_run_task_complete (
                task,
                FALSE,
                g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "(%s) couldn't create AT port",
                             self->priv->name));
            return FALSE;
        }

        g_object_set (task->serial,
                      MM_SERIAL_PORT_SEND_DELAY, task->at_send_delay,
                      MM_PORT_CARRIER_DETECT, FALSE,
                      MM_SERIAL_PORT_SPEW_CONTROL, TRUE,
                      NULL);

        mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (task->serial),
                                               mm_serial_parser_v1_parse,
                                               mm_serial_parser_v1_new (),
                                               mm_serial_parser_v1_destroy);
    }

    /* Try to open the port */
    if (!mm_serial_port_open (task->serial, &error)) {
        /* Abort if maximum number of open tries reached */
        if (++task->at_open_tries > 4) {
            /* took too long to open the port; give up */
            port_probe_run_task_complete (
                task,
                FALSE,
                g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "(%s) failed to open port after 4 tries",
                             self->priv->name));
        } else if (g_error_matches (error,
                                    MM_SERIAL_ERROR,
                                    MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE)) {
            /* this is nozomi being dumb; try again */
            task->source_id = g_timeout_add_seconds (1,
                                                     (GSourceFunc)serial_open_at,
                                                     self);
        } else {
            port_probe_run_task_complete (
                task,
                FALSE,
                g_error_new (MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_OPEN_FAILED,
                             "(%s) failed to open port: %s",
                             self->priv->name,
                             (error ? error->message : "unknown error")));
        }

        g_clear_error (&error);
        return FALSE;
    }

    /* success, start probing */
    task->buffer_full_id = g_signal_connect (task->serial,
                                             "buffer-full",
                                             G_CALLBACK (serial_buffer_full),
                                             self);

    mm_serial_port_flash (MM_SERIAL_PORT (task->serial),
                          100,
                          TRUE,
                          (MMSerialFlashFn)serial_flash_done,
                          self);
    return FALSE;
}

gboolean
mm_port_probe_run_cancel_at_probing (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (self->priv->task) {
        mm_dbg ("(%s) requested to cancel all AT probing", self->priv->name);
        g_cancellable_cancel (self->priv->task->at_probing_cancellable);
        return TRUE;
    }

    return FALSE;
}

gboolean
mm_port_probe_run_cancel (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (self->priv->task) {
        mm_dbg ("(%s) requested to cancel the probing", self->priv->name);
        g_cancellable_cancel (self->priv->task->cancellable);
        return TRUE;
    }

    return FALSE;
}

gboolean
mm_port_probe_run_finish (MMPortProbe *self,
                          GAsyncResult *result,
                          GError **error)
{
    gboolean res;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

    /* Propagate error, if any */
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
        res = FALSE;
    else
        res = g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (result));

    /* Cleanup probing task */
    if (self->priv->task) {
        port_probe_run_task_free (self->priv->task);
        self->priv->task = NULL;
    }
    return res;
}

void
mm_port_probe_run (MMPortProbe *self,
                   MMPortProbeFlag flags,
                   guint64 at_send_delay,
                   const MMPortProbeAtCommand *at_custom_init,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    PortProbeRunTask *task;
    guint32 i;
    gchar *probe_list_str;

    g_return_if_fail (MM_IS_PORT_PROBE (self));
    g_return_if_fail (flags != MM_PORT_PROBE_NONE);
    g_return_if_fail (callback != NULL);

    /* Shouldn't schedule more than one probing at a time */
    g_assert (self->priv->task == NULL);

    task = g_new0 (PortProbeRunTask, 1);
    task->at_send_delay = at_send_delay;
    task->flags = MM_PORT_PROBE_NONE;
    task->at_custom_init = at_custom_init;
    task->result = g_simple_async_result_new (G_OBJECT (self),
                                              callback,
                                              user_data,
                                              mm_port_probe_run);

    /* Check if we already have the requested probing results.
     * We will fix here the 'task->flags' so that we only request probing
     * for the missing things. */
    for (i = MM_PORT_PROBE_AT; i <= MM_PORT_PROBE_QCDM; i = (i << 1)) {
        if ((flags & i) && !(self->priv->flags & i)) {
            task->flags += i;
        }
    }

    /* Store as current task. We need to keep it internally, as it will be
     * freed during _finish() when the operation is completed. */
    self->priv->task = task;

    /* All requested probings already available? If so, we're done */
    if (!task->flags) {
        port_probe_run_task_complete (task, TRUE, NULL);
        return;
    }

    /* Setup internal cancellable */
    task->cancellable = g_cancellable_new ();

    probe_list_str = mm_port_probe_flag_build_string_from_mask (task->flags);
    mm_info ("(%s) launching port probing: '%s'",
             self->priv->name,
             probe_list_str);
    g_free (probe_list_str);

    /* If any AT probing is needed, start by opening as AT port */
    if (task->flags & MM_PORT_PROBE_AT ||
        task->flags & MM_PORT_PROBE_AT_VENDOR ||
        task->flags & MM_PORT_PROBE_AT_PRODUCT) {
        task->at_probing_cancellable = g_cancellable_new ();
        task->source_id = g_idle_add ((GSourceFunc)serial_open_at, self);
        return;
    }

    /* Otherwise, start by opening as QCDM port */
    if (task->flags & MM_PORT_PROBE_QCDM) {
        task->source_id = g_idle_add ((GSourceFunc)serial_probe_qcdm, self);
        return;
    }

    /* Shouldn't happen */
    g_assert_not_reached ();
}

gboolean
mm_port_probe_is_at (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (g_str_equal (self->priv->subsys, "net"))
        return FALSE;

    /* Warn if it wasn't probed */
    g_return_val_if_fail (self->priv->flags & MM_PORT_PROBE_AT, FALSE);

    return self->priv->is_at;
}

gboolean
mm_port_probe_is_qcdm (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (g_str_equal (self->priv->subsys, "net"))
        return FALSE;

    /* Warn if it wasn't probed */
    g_return_val_if_fail (self->priv->flags & MM_PORT_PROBE_QCDM, FALSE);

    return self->priv->is_qcdm;
}

MMPortType
mm_port_probe_get_port_type (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (g_str_equal (self->priv->subsys, "net"))
        return MM_PORT_TYPE_NET;

    if (self->priv->flags & MM_PORT_PROBE_QCDM &&
        self->priv->is_qcdm)
        return MM_PORT_TYPE_QCDM;

    if (self->priv->flags & MM_PORT_PROBE_AT &&
        self->priv->is_at)
        return MM_PORT_TYPE_AT;

    return MM_PORT_TYPE_UNKNOWN;
}

GUdevDevice *
mm_port_probe_get_port (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->port;
};

const gchar *
mm_port_probe_get_vendor (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    if (g_str_equal (self->priv->subsys, "net"))
        return NULL;

    /* Warn if it wasn't probed */
    g_return_val_if_fail (self->priv->flags & MM_PORT_PROBE_AT_VENDOR, NULL);

    return self->priv->vendor;
}

const gchar *
mm_port_probe_get_product (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    if (g_str_equal (self->priv->subsys, "net"))
        return NULL;

    /* Warn if it wasn't probed */
    g_return_val_if_fail (self->priv->flags & MM_PORT_PROBE_AT_PRODUCT, NULL);

    return self->priv->product;
}

const gchar *
mm_port_probe_get_port_name (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->name;
}

const gchar *
mm_port_probe_get_port_subsys (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->subsys;
}

const gchar *
mm_port_probe_get_port_physdev (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->physdev_path;
}

const gchar *
mm_port_probe_get_port_driver (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->driver;
}

MMPortProbe *
mm_port_probe_new (GUdevDevice *port,
                   const gchar *physdev_path,
                   const gchar *driver)
{
    MMPortProbe *self;

    self = MM_PORT_PROBE (g_object_new (MM_TYPE_PORT_PROBE, NULL));
    self->priv->port = g_object_ref (port);
    self->priv->subsys = g_strdup (g_udev_device_get_subsystem (port));
    self->priv->name = g_strdup (g_udev_device_get_name (port));
    self->priv->physdev_path = g_strdup (physdev_path);
    self->priv->driver = g_strdup (driver);

    return self;
}

static void
mm_port_probe_init (MMPortProbe *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),                  \
                                              MM_TYPE_PORT_PROBE,      \
                                              MMPortProbePrivate);
}

static void
finalize (GObject *object)
{
    MMPortProbe *self = MM_PORT_PROBE (object);

    /* We should never have a task here */
    g_assert (self->priv->task == NULL);

    g_free (self->priv->subsys);
    g_free (self->priv->name);
    g_free (self->priv->physdev_path);
    g_free (self->priv->driver);
    g_object_unref (self->priv->port);

    g_free (self->priv->vendor);
    g_free (self->priv->product);

    G_OBJECT_CLASS (mm_port_probe_parent_class)->finalize (object);
}

static void
mm_port_probe_class_init (MMPortProbeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortProbePrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
