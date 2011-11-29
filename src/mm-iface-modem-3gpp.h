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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_3GPP_H
#define MM_IFACE_MODEM_3GPP_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mm-at-serial-port.h"

#define MM_TYPE_IFACE_MODEM_3GPP               (mm_iface_modem_3gpp_get_type ())
#define MM_IFACE_MODEM_3GPP(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_3GPP, MMIfaceModem3gpp))
#define MM_IS_IFACE_MODEM_3GPP(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_3GPP))
#define MM_IFACE_MODEM_3GPP_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_3GPP, MMIfaceModem3gpp))

#define MM_IFACE_MODEM_3GPP_DBUS_SKELETON        "iface-modem-3gpp-dbus-skeleton"
#define MM_IFACE_MODEM_3GPP_REGISTRATION_STATE   "iface-modem-3gpp-registration-state"

typedef struct _MMIfaceModem3gpp MMIfaceModem3gpp;

struct _MMIfaceModem3gpp {
    GTypeInterface g_iface;

    /* Loading of the IMEI property */
    void (*load_imei) (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gchar * (*load_imei_finish) (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 GError **error);

    /* Setup unsolicited registration messages */
    void (* setup_unsolicited_registration) (MMIfaceModem3gpp *self,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
    gboolean (*setup_unsolicited_registration_finish) (MMIfaceModem3gpp *self,
                                                       GAsyncResult *res,
                                                       GError **error);

    /* Setup CS Registration */
    void (* setup_cs_registration) (MMIfaceModem3gpp *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
    gboolean (*setup_cs_registration_finish) (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error);

    /* Setup PS Registration */
    void (* setup_ps_registration) (MMIfaceModem3gpp *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
    gboolean (*setup_ps_registration_finish) (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error);

    /* Run CS registration state check.
     * Note that no registration state is returned, implementations should call
     * mm_iface_modem_3gpp_update_registration_state(). */
    void (* run_cs_registration_check) (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (*run_cs_registration_check_finish) (MMIfaceModem3gpp *self,
                                                  GAsyncResult *res,
                                                  GError **error);

    /* Run PS registration state check.
     * Note that no registration state is returned, implementations should call
     * mm_iface_modem_3gpp_update_registration_state(). */
    void (* run_ps_registration_check) (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (*run_ps_registration_check_finish) (MMIfaceModem3gpp *self,
                                                  GAsyncResult *res,
                                                  GError **error);
};

GType mm_iface_modem_3gpp_get_type (void);

/* Initialize Modem 3GPP interface (async) */
void     mm_iface_modem_3gpp_initialize        (MMIfaceModem3gpp *self,
                                                MMAtSerialPort *port,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
gboolean mm_iface_modem_3gpp_initialize_finish (MMIfaceModem3gpp *self,
                                                GAsyncResult *res,
                                                GError **error);

/* Enable Modem interface (async) */
void     mm_iface_modem_3gpp_enable        (MMIfaceModem3gpp *self,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_iface_modem_3gpp_enable_finish (MMIfaceModem3gpp *self,
                                            GAsyncResult *res,
                                            GError **error);

/* Shutdown Modem 3GPP interface */
void mm_iface_modem_3gpp_shutdown (MMIfaceModem3gpp *self);

/* Objects implementing this interface can report new registration states.
 * This may happen when handling unsolicited registration messages, or when
 * the interface asks to run registration state checks.
 * Returns FALSE if registration process is still ongoing. */
void mm_iface_modem_3gpp_update_registration_state (MMIfaceModem3gpp *self,
                                                    MMModem3gppRegistrationState new_state);

#endif /* MM_IFACE_MODEM_3GPP_H */
