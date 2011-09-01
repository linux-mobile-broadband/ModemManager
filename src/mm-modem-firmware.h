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
 * Copyright (C) 2011 The Chromium OS Authors
 */

#ifndef MM_MODEM_FIRMWARE_H
#define MM_MODEM_FIRMWARE_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_FIRMWARE      (mm_modem_firmware_get_type ())
#define MM_MODEM_FIRMWARE(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_FIRMWARE, MMModemFirmware))
#define MM_IS_MODEM_FIRMWARE(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_FIRMWARE))
#define MM_MODEM_FIRMWARE_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_FIRMWARE, MMModemFirmware))

typedef struct _MMModemFirmware MMModemFirmware;

typedef void (*MMModemFirmwareListFn) (MMModemFirmware *modem,
                                       const char *selected,
                                       GHashTable *installed,
                                       GHashTable *available,
                                       GError *error,
                                       gpointer user_data);

struct _MMModemFirmware {
    GTypeInterface g_iface;

    /* Methods */
    void (*list) (MMModemFirmware *modem,
                  MMModemFirmwareListFn callback,
                  gpointer user_data);

    void (*select) (MMModemFirmware *modem,
                    const char *slot,
                    MMModemFn callback,
                    gpointer user_data);

    void (*install) (MMModemFirmware *modem,
                     const char *image,
                     const char *slot,
                     MMModemFn callback,
                     gpointer user_data);
};

GType mm_modem_firmware_get_type (void);

void mm_modem_firmware_list (MMModemFirmware *self,
                             MMModemFirmwareListFn callback,
                             gpointer user_data);

void mm_modem_firmware_select (MMModemFirmware *self,
                               const char *slot,
                               MMModemFn callback,
                               gpointer user_data);

void mm_modem_firmware_install (MMModemFirmware *self,
                                const char *image,
                                const char *slot,
                                MMModemFn callback,
                                gpointer user_data);

#endif /* MM_MODEM_FIRMWARE_H */
