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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-probe.h"
#include "mm-log.h"
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
 * ----> QCDM Serial Open
 *   |----> QCDM?
 * ----> QMI Device Open
 *   |----> QMI Version Info check
 * ----> MBIM Device Open
 *   |----> MBIM capabilities check
 */

G_DEFINE_TYPE (MMPortProbe, mm_port_probe, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_DEVICE,
    PROP_PORT,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

typedef struct {
    /* ---- Generic task context ---- */

    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    guint32 flags;
    guint source_id;

    /* ---- Serial probing specific context ---- */

    guint buffer_full_id;
    MMPortSerial *serial;

    /* ---- AT probing specific context ---- */

    GCancellable *at_probing_cancellable;
    /* Send delay for AT commands */
    guint64 at_send_delay;
    /* Flag to leave/remove echo in AT responses */
    gboolean at_remove_echo;
    /* Flag to send line-feed at the end of AT commands */
    gboolean at_send_lf;
    /* Number of times we tried to open the AT port */
    guint at_open_tries;
    /* Custom initialization setup */
    gboolean at_custom_init_run;
    MMPortProbeAtCustomInit at_custom_init;
    MMPortProbeAtCustomInitFinish at_custom_init_finish;
    /* Custom commands to look for AT support */
    const MMPortProbeAtCommand *at_custom_probe;
    /* Current group of AT commands to be sent */
    const MMPortProbeAtCommand *at_commands;
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

} PortProbeRunTask;

struct _MMPortProbePrivate {
    /* Properties */
    MMDevice *device;
    GUdevDevice *port;
    GUdevDevice *parent;

    /* Probing results */
    guint32 flags;
    gboolean is_at;
    gboolean is_qcdm;
    gchar *vendor;
    gchar *product;
    gboolean is_icera;
    gboolean is_qmi;
    gboolean is_mbim;

    /* From udev tags */
    gboolean is_ignored;

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
        mm_dbg ("(%s/%s) port is AT-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));

        /* Also set as not a QCDM/QMI/MBIM port */
        self->priv->is_qcdm = FALSE;
        self->priv->is_qmi = FALSE;
        self->priv->is_mbim = FALSE;
        self->priv->flags |= (MM_PORT_PROBE_QCDM | MM_PORT_PROBE_QMI | MM_PORT_PROBE_MBIM);
    } else {
        mm_dbg ("(%s/%s) port is not AT-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->is_icera = FALSE;
        self->priv->flags |= (MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA);
    }
}

void
mm_port_probe_set_result_at_vendor (MMPortProbe *self,
                                    const gchar *at_vendor)
{
    if (at_vendor) {
        mm_dbg ("(%s/%s) vendor probing finished",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        self->priv->vendor = g_utf8_casefold (at_vendor, -1);
        self->priv->flags |= MM_PORT_PROBE_AT_VENDOR;
    } else {
        mm_dbg ("(%s/%s) couldn't probe for vendor string",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
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
        mm_dbg ("(%s/%s) product probing finished",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        self->priv->product = g_utf8_casefold (at_product, -1);
        self->priv->flags |= MM_PORT_PROBE_AT_PRODUCT;
    } else {
        mm_dbg ("(%s/%s) couldn't probe for product string",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        self->priv->product = NULL;
        self->priv->flags |= MM_PORT_PROBE_AT_PRODUCT;
    }
}

void
mm_port_probe_set_result_at_icera (MMPortProbe *self,
                                   gboolean is_icera)
{
    if (is_icera) {
        mm_dbg ("(%s/%s) Modem is Icera-based",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        self->priv->is_icera = TRUE;
        self->priv->flags |= MM_PORT_PROBE_AT_ICERA;
    } else {
        mm_dbg ("(%s/%s) Modem is probably not Icera-based",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        self->priv->is_icera = FALSE;
        self->priv->flags |= MM_PORT_PROBE_AT_ICERA;
    }
}

void
mm_port_probe_set_result_qcdm (MMPortProbe *self,
                               gboolean qcdm)
{
    self->priv->is_qcdm = qcdm;
    self->priv->flags |= MM_PORT_PROBE_QCDM;

    if (self->priv->is_qcdm) {
        mm_dbg ("(%s/%s) port is QCDM-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));

        /* Also set as not an AT/QMI/MBIM port */
        self->priv->is_at = FALSE;
        self->priv->is_qmi = FALSE;
        self->priv->is_mbim = FALSE;
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->is_icera = FALSE;
        self->priv->flags |= (MM_PORT_PROBE_AT |
                              MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA |
                              MM_PORT_PROBE_QMI |
                              MM_PORT_PROBE_MBIM);
    } else
        mm_dbg ("(%s/%s) port is not QCDM-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
}

void
mm_port_probe_set_result_qmi (MMPortProbe *self,
                              gboolean qmi)
{
    self->priv->is_qmi = qmi;
    self->priv->flags |= MM_PORT_PROBE_QMI;

    if (self->priv->is_qmi) {
        mm_dbg ("(%s/%s) port is QMI-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));

        /* Also set as not an AT/QCDM/MBIM port */
        self->priv->is_at = FALSE;
        self->priv->is_qcdm = FALSE;
        self->priv->is_mbim = FALSE;
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->flags |= (MM_PORT_PROBE_AT |
                              MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA |
                              MM_PORT_PROBE_QCDM |
                              MM_PORT_PROBE_MBIM);
    } else
        mm_dbg ("(%s/%s) port is not QMI-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
}

void
mm_port_probe_set_result_mbim (MMPortProbe *self,
                               gboolean mbim)
{
    self->priv->is_mbim = mbim;
    self->priv->flags |= MM_PORT_PROBE_MBIM;

    if (self->priv->is_mbim) {
        mm_dbg ("(%s/%s) port is MBIM-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));

        /* Also set as not an AT/QCDM/QMI port */
        self->priv->is_at = FALSE;
        self->priv->is_qcdm = FALSE;
        self->priv->is_qmi = FALSE;
        self->priv->vendor = NULL;
        self->priv->product = NULL;
        self->priv->flags |= (MM_PORT_PROBE_AT |
                              MM_PORT_PROBE_AT_VENDOR |
                              MM_PORT_PROBE_AT_PRODUCT |
                              MM_PORT_PROBE_AT_ICERA |
                              MM_PORT_PROBE_QCDM |
                              MM_PORT_PROBE_QMI);
    } else
        mm_dbg ("(%s/%s) port is not MBIM-capable",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
}

static gboolean serial_probe_at (MMPortProbe *self);
static gboolean serial_probe_qcdm (MMPortProbe *self);
static void serial_probe_schedule (MMPortProbe *self);

static void
port_probe_run_task_free (PortProbeRunTask *task)
{
    if (task->source_id)
        g_source_remove (task->source_id);

    if (task->serial) {
        if (task->buffer_full_id) {
            g_warn_if_fail (MM_IS_PORT_SERIAL_AT (task->serial));
            g_signal_handler_disconnect (task->serial, task->buffer_full_id);
        }
        if (mm_port_serial_is_open (task->serial))
            mm_port_serial_close (task->serial);
        g_object_unref (task->serial);
    }

#if defined WITH_QMI
    if (task->port_qmi) {
        if (mm_port_qmi_is_open (task->port_qmi))
            mm_port_qmi_close (task->port_qmi);
        g_object_unref (task->port_qmi);
    }
#endif

#if defined WITH_MBIM
    if (task->mbim_port) {
        /* We should have closed it cleanly before */
        g_assert (!mm_port_mbim_is_open (task->mbim_port));
        g_object_unref (task->mbim_port);
    }
#endif

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
    if (task->serial && task->buffer_full_id) {
        g_signal_handler_disconnect (task->serial, task->buffer_full_id);
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
                         "(%s/%s) port probing cancelled",
                         g_udev_device_get_subsystem (self->priv->port),
                         g_udev_device_get_name (self->priv->port)));
        return TRUE;
    }

    return FALSE;
}

/***************************************************************/
/* QMI & MBIM */

static gboolean wdm_probe (MMPortProbe *self);

#if defined WITH_QMI

static void
port_qmi_open_ready (MMPortQmi *port_qmi,
                     GAsyncResult *res,
                     MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;
    gboolean is_qmi;

    is_qmi = mm_port_qmi_open_finish (port_qmi, res, &error);
    if (!is_qmi) {
        mm_dbg ("(%s/%s) error checking QMI support: '%s'",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port),
                error ? error->message : "unknown error");
        g_clear_error (&error);
    }

    /* Set probing result */
    mm_port_probe_set_result_qmi (self, is_qmi);

    mm_port_qmi_close (port_qmi);

    /* Keep on */
    task->source_id = g_idle_add ((GSourceFunc)wdm_probe, self);
}

#endif /* WITH_QMI */

static void
wdm_probe_qmi (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

#if defined WITH_QMI
    mm_dbg ("(%s/%s) probing QMI...",
            g_udev_device_get_subsystem (self->priv->port),
            g_udev_device_get_name (self->priv->port));

    /* Create a port and try to open it */
    task->port_qmi = mm_port_qmi_new (g_udev_device_get_name (self->priv->port));
    mm_port_qmi_open (task->port_qmi,
                      FALSE,
                      NULL,
                      (GAsyncReadyCallback)port_qmi_open_ready,
                      self);
#else
    /* If not compiled with QMI support, just assume we won't have any QMI port */
    mm_port_probe_set_result_qmi (self, FALSE);
    task->source_id = g_idle_add ((GSourceFunc)wdm_probe, self);
#endif /* WITH_QMI */
}

#if defined WITH_MBIM

static void
mbim_port_close_ready (MMPortMbim *mbim_port,
                       GAsyncResult *res,
                       MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

    mm_port_mbim_close_finish (mbim_port, res, NULL);

    /* Keep on */
    task->source_id = g_idle_add ((GSourceFunc)wdm_probe, self);
}

static void
mbim_port_open_ready (MMPortMbim *mbim_port,
                      GAsyncResult *res,
                      MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;
    gboolean is_mbim;

    is_mbim = mm_port_mbim_open_finish (mbim_port, res, &error);
    if (!is_mbim) {
        mm_dbg ("(%s/%s) error checking MBIM support: '%s'",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port),
                error ? error->message : "unknown error");
        g_clear_error (&error);
    }

    /* Set probing result */
    mm_port_probe_set_result_mbim (self, is_mbim);

    mm_port_mbim_close (task->mbim_port,
                        (GAsyncReadyCallback)mbim_port_close_ready,
                        self);
}

#endif /* WITH_MBIM */

static void
wdm_probe_mbim (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

#if defined WITH_MBIM
    mm_dbg ("(%s/%s) probing MBIM...",
            g_udev_device_get_subsystem (self->priv->port),
            g_udev_device_get_name (self->priv->port));

    /* Create a port and try to open it */
    task->mbim_port = mm_port_mbim_new (g_udev_device_get_name (self->priv->port));
    mm_port_mbim_open (task->mbim_port,
                       NULL,
                       (GAsyncReadyCallback)mbim_port_open_ready,
                       self);
#else
    /* If not compiled with MBIM support, just assume we won't have any MBIM port */
    mm_port_probe_set_result_mbim (self, FALSE);
    task->source_id = g_idle_add ((GSourceFunc)wdm_probe, self);
#endif /* WITH_MBIM */
}

static gboolean
wdm_probe (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

    task->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return FALSE;

    if ((task->flags & MM_PORT_PROBE_QMI) &&
        !(self->priv->flags & MM_PORT_PROBE_QMI))
        /* QMI probing needed */
        wdm_probe_qmi (self);
    else if ((task->flags & MM_PORT_PROBE_MBIM) &&
             !(self->priv->flags & MM_PORT_PROBE_MBIM))
        /* MBIM probing needed */
        wdm_probe_mbim (self);
    else
        /* All done now */
        port_probe_run_task_complete (task, TRUE, NULL);

    return FALSE;
}

/***************************************************************/
/* QCDM */

static void
serial_probe_qcdm_parse_response (MMPortSerialQcdm *port,
                                  GAsyncResult *res,
                                  MMPortProbe *self)
{
    QcdmResult *result;
    gint err = QCDM_SUCCESS;
    gboolean is_qcdm = FALSE;
    gboolean retry = FALSE;
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;
    GByteArray *response;

    response = mm_port_serial_qcdm_command_finish (port, res, &error);

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        goto out;

    if (!error) {
        /* Parse the response */
        result = qcdm_cmd_version_info_result ((const gchar *) response->data,
                                               response->len,
                                               &err);
        if (!result) {
            mm_warn ("(%s/%s) failed to parse QCDM version info command result: %d",
                     g_udev_device_get_subsystem (self->priv->port),
                     g_udev_device_get_name (self->priv->port),
                     err);
            retry = TRUE;
        } else {
            /* yay, probably a QCDM port */
            is_qcdm = TRUE;
            qcdm_result_unref (result);
        }
    } else if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_PARSE_FAILED)) {
        /* Failed to unescape QCDM packet: don't retry */
        mm_dbg ("QCDM parsing error: %s", error->message);
    } else {
        if (!g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            mm_dbg ("QCDM probe error: (%d) %s", error->code, error->message);
        retry = TRUE;
    }

    if (retry) {
        GByteArray *cmd2;

        cmd2 = g_object_steal_data (G_OBJECT (self), "cmd2");
        if (cmd2) {
            /* second try */
            mm_port_serial_qcdm_command (MM_PORT_SERIAL_QCDM (task->serial),
                                         cmd2,
                                         3,
                                         NULL,
                                         (GAsyncReadyCallback)serial_probe_qcdm_parse_response,
                                         self);
            g_byte_array_unref (cmd2);
            goto out;
        }

        /* no more retries left */
    }

    /* Set probing result */
    mm_port_probe_set_result_qcdm (self, is_qcdm);

    /* Reschedule probing */
    serial_probe_schedule (self);

out:
    if (response)
        g_byte_array_unref (response);
    if (error)
        g_error_free (error);
}

static gboolean
serial_probe_qcdm (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;
    GByteArray *verinfo = NULL;
    GByteArray *verinfo2;
    gint len;
    guint8 marker = 0x7E;

    task->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return FALSE;

    mm_dbg ("(%s/%s) probing QCDM...",
            g_udev_device_get_subsystem (self->priv->port),
            g_udev_device_get_name (self->priv->port));

    /* If open, close the AT port */
    if (task->serial) {
        /* Explicitly clear the buffer full signal handler */
        if (task->buffer_full_id) {
            g_signal_handler_disconnect (task->serial, task->buffer_full_id);
            task->buffer_full_id = 0;
        }
        mm_port_serial_close (task->serial);
        g_object_unref (task->serial);
    }

    /* Open the QCDM port */
    task->serial = MM_PORT_SERIAL (mm_port_serial_qcdm_new (g_udev_device_get_name (self->priv->port)));
    if (!task->serial) {
        port_probe_run_task_complete (
            task,
            FALSE,
            g_error_new (MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "(%s/%s) Couldn't create QCDM port",
                         g_udev_device_get_subsystem (self->priv->port),
                         g_udev_device_get_name (self->priv->port)));
        return FALSE;
    }

    /* Try to open the port */
    if (!mm_port_serial_open (task->serial, &error)) {
        port_probe_run_task_complete (
            task,
            FALSE,
            g_error_new (MM_SERIAL_ERROR,
                         MM_SERIAL_ERROR_OPEN_FAILED,
                         "(%s/%s) Failed to open QCDM port: %s",
                         g_udev_device_get_subsystem (self->priv->port),
                         g_udev_device_get_name (self->priv->port),
                         (error ? error->message : "unknown error")));
        g_clear_error (&error);
        return FALSE;
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
        port_probe_run_task_complete (
            task,
            FALSE,
            g_error_new (MM_SERIAL_ERROR,
                         MM_SERIAL_ERROR_OPEN_FAILED,
                         "(%s/%s) Failed to create QCDM versin info command",
                         g_udev_device_get_subsystem (self->priv->port),
                         g_udev_device_get_name (self->priv->port)));
        return FALSE;
    }
    verinfo->len = len + 1;

    /* Queuing the command takes ownership over it; save it for the second try */
    verinfo2 = g_byte_array_sized_new (verinfo->len);
    g_byte_array_append (verinfo2, verinfo->data, verinfo->len);
    g_object_set_data_full (G_OBJECT (self), "cmd2", verinfo2, (GDestroyNotify) g_byte_array_unref);

    mm_port_serial_qcdm_command (MM_PORT_SERIAL_QCDM (task->serial),
                                 verinfo,
                                 3,
                                 NULL,
                                 (GAsyncReadyCallback)serial_probe_qcdm_parse_response,
                                 self);
    g_byte_array_unref (verinfo);

    return FALSE;
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
serial_probe_at_icera_result_processor (MMPortProbe *self,
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
serial_probe_at_parse_response (MMPortSerialAt *port,
                                GAsyncResult *res,
                                MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;
    GVariant *result = NULL;
    GError *result_error = NULL;
    const gchar *response;
    GError *error = NULL;

    response = mm_port_serial_at_command_finish (port, res, &error);

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        goto out;

    /* If AT probing cancelled, end this partial probing */
    if (g_cancellable_is_cancelled (task->at_probing_cancellable)) {
        mm_dbg ("(%s/%s) no need to keep on probing the port for AT support",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        task->at_result_processor (self, NULL);
        serial_probe_schedule (self);
        goto out;
    }

    if (!task->at_commands->response_processor (task->at_commands->command,
                                                response,
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
                             "(%s/%s) error while probing AT features: %s",
                             g_udev_device_get_subsystem (self->priv->port),
                             g_udev_device_get_name (self->priv->port),
                             result_error->message));
            goto out;
        }

        /* Go on to next command */
        task->at_commands++;
        if (!task->at_commands->command) {
            /* Was it the last command in the group? If so,
             * end this partial probing */
            task->at_result_processor (self, NULL);
            /* Reschedule */
            serial_probe_schedule (self);
            goto out;
        }

        /* Schedule the next command in the probing group */
        if (task->at_commands_wait_secs == 0)
            task->source_id = g_idle_add ((GSourceFunc)serial_probe_at, self);
        else {
            mm_dbg ("(%s/%s) re-scheduling next command in probing group in %u seconds...",
                    g_udev_device_get_subsystem (self->priv->port),
                    g_udev_device_get_name (self->priv->port),
                    task->at_commands_wait_secs);
            task->source_id = g_timeout_add_seconds (task->at_commands_wait_secs, (GSourceFunc)serial_probe_at, self);
        }
        goto out;
    }

    /* Run result processor.
     * Note that custom init commands are allowed to not return anything */
    task->at_result_processor (self, result);

    /* Reschedule probing */
    serial_probe_schedule (self);

out:
    if (result)
        g_variant_unref (result);
    if (error)
        g_error_free (error);
    if (result_error)
        g_error_free (result_error);
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
        mm_dbg ("(%s/%s) no need to launch probing for AT support",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        task->at_result_processor (self, NULL);
        serial_probe_schedule (self);
        return FALSE;
    }

    mm_port_serial_at_command (
        MM_PORT_SERIAL_AT (task->serial),
        task->at_commands->command,
        task->at_commands->timeout,
        FALSE,
        FALSE,
        task->at_probing_cancellable,
        (GAsyncReadyCallback)serial_probe_at_parse_response,
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

static const MMPortProbeAtCommand icera_probing[] = {
    { "%IPSYS?", 3, mm_port_probe_response_processor_string },
    { "%IPSYS?", 3, mm_port_probe_response_processor_string },
    { "%IPSYS?", 3, mm_port_probe_response_processor_string },
    { NULL }
};

static void
at_custom_init_ready (MMPortProbe *self,
                      GAsyncResult *res)
{
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;

    if (!task->at_custom_init_finish (self, res, &error)) {
        /* All errors propagated up end up forcing an UNSUPPORTED result */
        port_probe_run_task_complete (task, FALSE, error);
        return;
    }

    /* Keep on with remaining probings */
    task->at_custom_init_run = TRUE;
    serial_probe_schedule (self);
}

/***************************************************************/

static void
serial_probe_schedule (MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return;

    /* If we got some custom initialization setup requested, go on with it
     * first. */
    if (!task->at_custom_init_run &&
        task->at_custom_init &&
        task->at_custom_init_finish) {
        task->at_custom_init (self,
                              MM_PORT_SERIAL_AT (task->serial),
                              task->at_probing_cancellable,
                              (GAsyncReadyCallback)at_custom_init_ready,
                              NULL);
        return;
    }

    /* Cleanup */
    task->at_result_processor = NULL;
    task->at_commands = NULL;
    task->at_commands_wait_secs = 0;

    /* AT check requested and not already probed? */
    if ((task->flags & MM_PORT_PROBE_AT) &&
        !(self->priv->flags & MM_PORT_PROBE_AT)) {
        /* Prepare AT probing */
        if (task->at_custom_probe)
            task->at_commands = task->at_custom_probe;
        else
            task->at_commands = at_probing;
        task->at_result_processor = serial_probe_at_result_processor;
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
    /* Icera support check requested and not already done? */
    else if ((task->flags & MM_PORT_PROBE_AT_ICERA) &&
             !(self->priv->flags & MM_PORT_PROBE_AT_ICERA)) {
        /* Prepare AT product probing */
        task->at_result_processor = serial_probe_at_icera_result_processor;
        task->at_commands = icera_probing;
        /* By default, wait 2 seconds between ICERA probing retries */
        task->at_commands_wait_secs = 2;
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
serial_flash_ready (MMPortSerial *port,
                    GAsyncResult *res,
                    MMPortProbe *self)
{
    mm_port_serial_flash_finish (port, res, NULL);

    /* Schedule probing */
    serial_probe_schedule (self);
}

static void
serial_buffer_full (MMPortSerial *serial,
                    GByteArray *buffer,
                    MMPortProbe *self)
{
    PortProbeRunTask *task = self->priv->task;

    if (is_non_at_response (buffer->data, buffer->len)) {
        mm_dbg ("(%s/%s) serial buffer full",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
        /* Don't explicitly close the AT port, just end the AT probing
         * (or custom init probing) */
        mm_port_probe_set_result_at (self, FALSE);
        g_cancellable_cancel (task->at_probing_cancellable);
    }
}

static gboolean
serial_parser_filter_cb (gpointer filter,
                         gpointer user_data,
                         GString *response,
                         GError **error)
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
    PortProbeRunTask *task = self->priv->task;
    GError *error = NULL;

    task->source_id = 0;

    /* If already cancelled, do nothing else */
    if (port_probe_run_is_cancelled (self))
        return FALSE;

    /* Create AT serial port if not done before */
    if (!task->serial) {
        gpointer parser;
        MMPortSubsys subsys = MM_PORT_SUBSYS_TTY;

        if (g_str_has_prefix (g_udev_device_get_subsystem (self->priv->port), "usb"))
            subsys = MM_PORT_SUBSYS_USB;

        task->serial = MM_PORT_SERIAL (mm_port_serial_at_new (g_udev_device_get_name (self->priv->port),
                                                              subsys));
        if (!task->serial) {
            port_probe_run_task_complete (
                task,
                FALSE,
                g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "(%s/%s) couldn't create AT port",
                             g_udev_device_get_subsystem (self->priv->port),
                             g_udev_device_get_name (self->priv->port)));
            return FALSE;
        }

        g_object_set (task->serial,
                      MM_PORT_SERIAL_SPEW_CONTROL,   TRUE,
                      MM_PORT_SERIAL_SEND_DELAY,     (subsys == MM_PORT_SUBSYS_TTY ? task->at_send_delay : 0),
                      MM_PORT_SERIAL_AT_REMOVE_ECHO, task->at_remove_echo,
                      MM_PORT_SERIAL_AT_SEND_LF,     task->at_send_lf,
                      NULL);

        parser = mm_serial_parser_v1_new ();
        mm_serial_parser_v1_add_filter (parser,
                                        serial_parser_filter_cb,
                                        NULL);
        mm_port_serial_at_set_response_parser (MM_PORT_SERIAL_AT (task->serial),
                                               mm_serial_parser_v1_parse,
                                               parser,
                                               mm_serial_parser_v1_destroy);
    }

    /* Try to open the port */
    if (!mm_port_serial_open (task->serial, &error)) {
        /* Abort if maximum number of open tries reached */
        if (++task->at_open_tries > 4) {
            /* took too long to open the port; give up */
            port_probe_run_task_complete (
                task,
                FALSE,
                g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "(%s/%s) failed to open port after 4 tries",
                             g_udev_device_get_subsystem (self->priv->port),
                             g_udev_device_get_name (self->priv->port)));
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
                             "(%s/%s) failed to open port: %s",
                             g_udev_device_get_subsystem (self->priv->port),
                             g_udev_device_get_name (self->priv->port),
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

    mm_port_serial_flash (MM_PORT_SERIAL (task->serial),
                          100,
                          TRUE,
                          (GAsyncReadyCallback)serial_flash_ready,
                          self);
    return FALSE;
}

gboolean
mm_port_probe_run_cancel_at_probing (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (self->priv->task) {
        mm_dbg ("(%s/%s) requested to cancel all AT probing",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
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
        mm_dbg ("(%s/%s) requested to cancel the probing",
                g_udev_device_get_subsystem (self->priv->port),
                g_udev_device_get_name (self->priv->port));
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
                   gboolean at_remove_echo,
                   gboolean at_send_lf,
                   const MMPortProbeAtCommand *at_custom_probe,
                   const MMAsyncMethod *at_custom_init,
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
    task->at_remove_echo = at_remove_echo;
    task->at_send_lf = at_send_lf;
    task->flags = MM_PORT_PROBE_NONE;
    task->at_custom_probe = at_custom_probe;
    task->at_custom_init = at_custom_init ? (MMPortProbeAtCustomInit)at_custom_init->async : NULL;
    task->at_custom_init_finish = at_custom_init ? (MMPortProbeAtCustomInitFinish)at_custom_init->finish : NULL;

    task->result = g_simple_async_result_new (G_OBJECT (self),
                                              callback,
                                              user_data,
                                              mm_port_probe_run);

    /* Check if we already have the requested probing results.
     * We will fix here the 'task->flags' so that we only request probing
     * for the missing things. */
    for (i = MM_PORT_PROBE_AT; i <= MM_PORT_PROBE_MBIM; i = (i << 1)) {
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
    mm_dbg ("(%s/%s) launching port probing: '%s'",
            g_udev_device_get_subsystem (self->priv->port),
            g_udev_device_get_name (self->priv->port),
            probe_list_str);
    g_free (probe_list_str);

    /* If any AT probing is needed, start by opening as AT port */
    if (task->flags & MM_PORT_PROBE_AT ||
        task->flags & MM_PORT_PROBE_AT_VENDOR ||
        task->flags & MM_PORT_PROBE_AT_PRODUCT ||
        task->flags & MM_PORT_PROBE_AT_ICERA) {
        task->at_probing_cancellable = g_cancellable_new ();
        task->source_id = g_idle_add ((GSourceFunc)serial_open_at, self);
        return;
    }

    /* If QCDM probing needed, start by opening as QCDM port */
    if (task->flags & MM_PORT_PROBE_QCDM) {
        task->source_id = g_idle_add ((GSourceFunc)serial_probe_qcdm, self);
        return;
    }

    /* If QMI/MBIM probing needed, go on */
    if (task->flags & MM_PORT_PROBE_QMI || task->flags & MM_PORT_PROBE_MBIM) {
        task->source_id = g_idle_add ((GSourceFunc)wdm_probe, self);
        return;
    }

    /* Shouldn't happen */
    g_assert_not_reached ();
}

gboolean
mm_port_probe_is_at (MMPortProbe *self)
{
    const gchar *subsys;
    const gchar *name;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = g_udev_device_get_subsystem (self->priv->port);
    name = g_udev_device_get_name (self->priv->port);
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
    const gchar *subsys;
    const gchar *name;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = g_udev_device_get_subsystem (self->priv->port);
    name = g_udev_device_get_name (self->priv->port);
    if (g_str_equal (subsys, "net") ||
        (g_str_has_prefix (subsys, "usb") &&
         g_str_has_prefix (name, "cdc-wdm")))
        return FALSE;

    return (self->priv->flags & MM_PORT_PROBE_QCDM ?
            self->priv->is_qcdm :
            FALSE);
}

gboolean
mm_port_probe_is_qmi (MMPortProbe *self)
{
    const gchar *subsys;
    const gchar *name;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = g_udev_device_get_subsystem (self->priv->port);
    name = g_udev_device_get_name (self->priv->port);
    if (!g_str_has_prefix (subsys, "usb") ||
        !name ||
        !g_str_has_prefix (name, "cdc-wdm"))
        return FALSE;

    return self->priv->is_qmi;
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
    const gchar *subsys;
    const gchar *name;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = g_udev_device_get_subsystem (self->priv->port);
    name = g_udev_device_get_name (self->priv->port);
    if (!g_str_has_prefix (subsys, "usb") ||
        !name ||
        !g_str_has_prefix (name, "cdc-wdm"))
        return FALSE;

    return self->priv->is_mbim;
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

MMPortType
mm_port_probe_get_port_type (MMPortProbe *self)
{
    const gchar *subsys;
    const gchar *name;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = g_udev_device_get_subsystem (self->priv->port);
    name = g_udev_device_get_name (self->priv->port);

    if (g_str_equal (subsys, "net"))
        return MM_PORT_TYPE_NET;

#if defined WITH_QMI
    if (g_str_has_prefix (subsys, "usb") &&
        g_str_has_prefix (name, "cdc-wdm") &&
        self->priv->is_qmi)
        return MM_PORT_TYPE_QMI;
#endif

#if defined WITH_MBIM
    if (g_str_has_prefix (subsys, "usb") &&
        g_str_has_prefix (name, "cdc-wdm") &&
        self->priv->is_mbim)
        return MM_PORT_TYPE_MBIM;
#endif

    if (self->priv->flags & MM_PORT_PROBE_QCDM &&
        self->priv->is_qcdm)
        return MM_PORT_TYPE_QCDM;

    if (self->priv->flags & MM_PORT_PROBE_AT &&
        self->priv->is_at)
        return MM_PORT_TYPE_AT;

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

GUdevDevice *
mm_port_probe_peek_port (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->port;
};

GUdevDevice *
mm_port_probe_get_port (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return G_UDEV_DEVICE (g_object_ref (self->priv->port));
};

const gchar *
mm_port_probe_get_vendor (MMPortProbe *self)
{
    const gchar *subsys;
    const gchar *name;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = g_udev_device_get_subsystem (self->priv->port);
    name = g_udev_device_get_name (self->priv->port);
    if (g_str_equal (subsys, "net") ||
        (g_str_has_prefix (subsys, "usb") &&
         g_str_has_prefix (name, "cdc-wdm")))
        return NULL;

    return (self->priv->flags & MM_PORT_PROBE_AT_VENDOR ?
            self->priv->vendor :
            NULL);
}

const gchar *
mm_port_probe_get_product (MMPortProbe *self)
{
    const gchar *subsys;
    const gchar *name;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    subsys = g_udev_device_get_subsystem (self->priv->port);
    name = g_udev_device_get_name (self->priv->port);
    if (g_str_equal (subsys, "net") ||
        (g_str_has_prefix (subsys, "usb") &&
         g_str_has_prefix (name, "cdc-wdm")))
        return NULL;

    return (self->priv->flags & MM_PORT_PROBE_AT_PRODUCT ?
            self->priv->product :
            NULL);
}

gboolean
mm_port_probe_is_icera (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    if (g_str_equal (g_udev_device_get_subsystem (self->priv->port), "net"))
        return FALSE;

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
mm_port_probe_is_ignored (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);

    return self->priv->is_ignored;
}

const gchar *
mm_port_probe_get_port_name (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return g_udev_device_get_name (self->priv->port);
}

const gchar *
mm_port_probe_get_port_subsys (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return g_udev_device_get_subsystem (self->priv->port);
}

const gchar *
mm_port_probe_get_parent_path (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return (self->priv->parent ? g_udev_device_get_sysfs_path (self->priv->parent) : NULL);
}

/*****************************************************************************/

MMPortProbe *
mm_port_probe_new (MMDevice *device,
                   GUdevDevice *port)
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
        self->priv->parent = g_udev_device_get_parent (self->priv->port);
        self->priv->is_ignored = g_udev_device_get_property_as_boolean (self->priv->port, "ID_MM_PORT_IGNORE");
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

    g_clear_object (&self->priv->parent);
    g_clear_object (&self->priv->port);

    G_OBJECT_CLASS (mm_port_probe_parent_class)->dispose (object);
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
                             "UDev device object of the port",
                             G_UDEV_TYPE_DEVICE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PORT, properties[PROP_PORT]);
}
