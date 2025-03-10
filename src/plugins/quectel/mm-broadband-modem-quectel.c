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
 * Copyright (C) 2018-2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2024 JUCR GmbH
 */

#include <config.h>

#include "mm-broadband-modem-quectel.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-time.h"
#include "mm-log-object.h"
#include "mm-shared-quectel.h"
#include "mm-base-modem-at.h"

static void iface_modem_init          (MMIfaceModemInterface         *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface);
static void iface_modem_location_init (MMIfaceModemLocationInterface *iface);
static void iface_modem_time_init     (MMIfaceModemTimeInterface     *iface);
static void shared_quectel_init       (MMSharedQuectelInterface      *iface);

static MMIfaceModemInterface         *iface_modem_parent;
static MMIfaceModemFirmwareInterface *iface_modem_firmware_parent;
static MMIfaceModemLocationInterface *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQuectel, mm_broadband_modem_quectel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init))

struct _MMBroadbandModemQuectelPrivate {
    GRegex *powered_down_regex;
};

#define MM_BROADBAND_MODEM_QUECTEL_POWERED_DOWN "powered-down"

enum {
    POWERED_DOWN,
    LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

/*****************************************************************************/
/* Power state loading (Modem interface) */

static MMModemPowerState
load_power_state_finish (MMIfaceModem  *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    MMModemPowerState  state = MM_MODEM_POWER_STATE_UNKNOWN;
    guint              cfun_state = 0;
    const gchar       *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return MM_MODEM_POWER_STATE_UNKNOWN;

    if (!mm_3gpp_parse_cfun_query_response (response, &cfun_state, error))
        return MM_MODEM_POWER_STATE_UNKNOWN;

    switch (cfun_state) {
    case 0:
        state = MM_MODEM_POWER_STATE_OFF;
        break;
    case 1:
        state = MM_MODEM_POWER_STATE_ON;
        break;
    case 4:
        state = MM_MODEM_POWER_STATE_LOW;
        break;
    /* Some modems support the following (ASR chipsets at least) */
    case 3: /* Disable RX */
    case 5: /* Disable (U)SIM */
        /* We'll just call these low-power... */
        state = MM_MODEM_POWER_STATE_LOW;
        break;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown +CFUN pòwer state: '%u'", state);
        return MM_MODEM_POWER_STATE_UNKNOWN;
    }

    return state;
}

static void
load_power_state (MMIfaceModem        *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* POWERED DOWN unsolicited event handler */

static void
powered_down_handler (MMPortSerialAt   *port,
                      GMatchInfo       *match_info,
                      MMBroadbandModem *self)
{
    /* The POWERED DOWN URC indicates the modem is ready to be powered down */
    g_signal_emit (self, signals[POWERED_DOWN], 0);
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

typedef struct {
    MMBroadbandModemQuectel *modem;
    guint                    urc_id;
    guint                    timeout_id;
} PowerDownContext;

static void
power_down_context_clear_timeout (PowerDownContext *ctx)
{
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }
}

static void
power_down_context_disconnect_urc (PowerDownContext *ctx)
{
    if (ctx->urc_id) {
        g_signal_handler_disconnect (ctx->modem, ctx->urc_id);
        ctx->urc_id = 0;
    }
}

static void
power_down_context_free (PowerDownContext *ctx)
{
    g_assert (!ctx->urc_id);
    g_assert (!ctx->timeout_id);
    g_clear_object (&ctx->modem);
    g_slice_free (PowerDownContext, ctx);
}

static gboolean
modem_power_down_finish (MMIfaceModem  *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
power_off_powered_down (MMBroadbandModemQuectel *self,
                        GTask                   *task)
{
    PowerDownContext *ctx;

    ctx = g_task_get_task_data (task);
    mm_obj_dbg (self, "got POWERED DOWN URC; proceeding with power off");
    power_down_context_clear_timeout (ctx);
    power_down_context_disconnect_urc (ctx);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
powered_down_timeout (GTask *task)
{
    PowerDownContext *ctx;

    ctx = g_task_get_task_data (task);
    power_down_context_clear_timeout (ctx);
    power_down_context_disconnect_urc (ctx);

    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_TIMEOUT,
                             "timed out waiting for POWERED DOWN URC");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
power_off_ready (MMBroadbandModemQuectel *self,
                 GAsyncResult            *res,
                 GTask                   *task)
{
    g_autoptr(GError) error = NULL;

    /* Docs for many devices state that +QPOWD's OK response comes very
     * quickly but caller must wait for a "POWERED DOWN" URC before allowing
     * modem power to be cut.
     */

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Wait for the POWERED DOWN URC */
    mm_obj_dbg (self, "waiting for POWERED DOWN URC...");
}

static void
modem_power_off (MMIfaceModem        *_self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    MMBroadbandModemQuectel *self = MM_BROADBAND_MODEM_QUECTEL (_self);
    GTask                   *task;
    PowerDownContext        *ctx;

    task = g_task_new (self,
                       mm_base_modem_peek_cancellable (MM_BASE_MODEM (self)),
                       callback,
                       user_data);

    ctx = g_slice_new0 (PowerDownContext);
    ctx->modem = g_object_ref (self);
    ctx->urc_id = g_signal_connect (self,
                                    MM_BROADBAND_MODEM_QUECTEL_POWERED_DOWN,
                                    (GCallback)power_off_powered_down,
                                    task);
    /* Docs state caller must wait up to 60 seconds for POWERED DOWN URC */
    ctx->timeout_id = g_timeout_add_seconds (62,
                                             (GSourceFunc)powered_down_timeout,
                                             task);
    g_task_set_task_data (task, ctx, (GDestroyNotify)power_down_context_free);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+QPOWD=1",
                              5,
                              FALSE,
                              (GAsyncReadyCallback)power_off_ready,
                              task);
}

/*****************************************************************************/
/* Modem power up/off (Modem interface) */

static gboolean
common_modem_power_operation_finish (MMIfaceModem  *self,
                                     GAsyncResult  *res,
                                     GError       **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
common_modem_power_operation (MMBroadbandModemQuectel  *self,
                              const gchar              *command,
                              GAsyncReadyCallback       callback,
                              gpointer                  user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              15,
                              FALSE,
                              callback,
                              user_data);
}

static void
modem_reset (MMIfaceModem        *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_QUECTEL (self), "+CFUN=1,1", callback, user_data);
}

static void
modem_power_down (MMIfaceModem        *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_QUECTEL (self), "+CFUN=4", callback, user_data);
}

static void
modem_power_up (MMIfaceModem        *self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_QUECTEL (self), "+CFUN=1", callback, user_data);
}

/*****************************************************************************/

static void
setup_ports (MMBroadbandModem *_self)
{
    MMBroadbandModemQuectel *self = MM_BROADBAND_MODEM_QUECTEL (_self);
    MMPortSerialAt          *ports[2];
    guint                    i;

    mm_shared_quectel_setup_ports (_self);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (ports[i]) {
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                self->priv->powered_down_regex,
                (MMPortSerialAtUnsolicitedMsgFn)powered_down_handler,
                self,
                NULL);
        }
    }
}

/*****************************************************************************/

MMBroadbandModemQuectel *
mm_broadband_modem_quectel_new (const gchar  *device,
                                const gchar  *physdev,
                                const gchar **drivers,
                                const gchar  *plugin,
                                guint16       vendor_id,
                                guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QUECTEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Generic bearer supports TTY only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, FALSE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_quectel_init (MMBroadbandModemQuectel *self)
{
    /* Initialize opaque pointer to private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_QUECTEL,
                                              MMBroadbandModemQuectelPrivate);

    self->priv->powered_down_regex = g_regex_new ("\\r\\nPOWERED DOWN", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (self->priv->powered_down_regex);
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->setup_sim_hot_swap = mm_shared_quectel_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = mm_shared_quectel_setup_sim_hot_swap_finish;
    iface->cleanup_sim_hot_swap = mm_shared_quectel_cleanup_sim_hot_swap;
    iface->load_power_state        = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;
    iface->modem_power_up        = modem_power_up;
    iface->modem_power_up_finish = common_modem_power_operation_finish;
    iface->modem_power_down        = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->modem_power_off        = modem_power_off;
    iface->modem_power_off_finish = common_modem_power_operation_finish;
    iface->reset        = modem_reset;
    iface->reset_finish = common_modem_power_operation_finish;
}

static void
iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface)
{
    iface_modem_firmware_parent = g_type_interface_peek_parent (iface);

    iface->load_update_settings = mm_shared_quectel_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_quectel_firmware_load_update_settings_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocationInterface *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_quectel_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_quectel_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_quectel_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_quectel_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_quectel_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_quectel_disable_location_gathering_finish;
}

static void
iface_modem_time_init (MMIfaceModemTimeInterface *iface)
{
    iface->check_support        = mm_shared_quectel_time_check_support;
    iface->check_support_finish = mm_shared_quectel_time_check_support_finish;
}

static MMBaseModemClass *
peek_parent_class (MMSharedQuectel *self)
{
    return MM_BASE_MODEM_CLASS (mm_broadband_modem_quectel_parent_class);
}

static MMIfaceModemInterface *
peek_parent_modem_interface (MMSharedQuectel *self)
{
    return iface_modem_parent;
}

static MMIfaceModemFirmwareInterface *
peek_parent_modem_firmware_interface (MMSharedQuectel *self)
{
    return iface_modem_firmware_parent;
}

static MMIfaceModemLocationInterface *
peek_parent_modem_location_interface (MMSharedQuectel *self)
{
    return iface_modem_location_parent;
}

static void
shared_quectel_init (MMSharedQuectelInterface *iface)
{
    iface->peek_parent_modem_interface          = peek_parent_modem_interface;
    iface->peek_parent_modem_firmware_interface = peek_parent_modem_firmware_interface;
    iface->peek_parent_modem_location_interface = peek_parent_modem_location_interface;
    iface->peek_parent_class                    = peek_parent_class;
}

static void
finalize (GObject *object)
{
    MMBroadbandModemQuectel *self = MM_BROADBAND_MODEM_QUECTEL (object);

    g_regex_unref (self->priv->powered_down_regex);

    G_OBJECT_CLASS (mm_broadband_modem_quectel_parent_class)->finalize (object);
}

static void
mm_broadband_modem_quectel_class_init (MMBroadbandModemQuectelClass *klass)
{
    GObjectClass          *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemQuectelPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;

    broadband_modem_class->setup_ports = setup_ports;

    signals[POWERED_DOWN] = g_signal_new (MM_BROADBAND_MODEM_QUECTEL_POWERED_DOWN,
                                          G_OBJECT_CLASS_TYPE (object_class),
                                          G_SIGNAL_RUN_FIRST,
                                          0,
                                          NULL, /* accumulator      */
                                          NULL, /* accumulator data */
                                          g_cclosure_marshal_generic,
                                          G_TYPE_NONE,
                                          0,
                                          NULL);
}
