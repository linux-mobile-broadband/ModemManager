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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-broadband-modem-qmi-qcom-soc.h"

G_DEFINE_TYPE (MMBroadbandModemQmiQcomSoc, mm_broadband_modem_qmi_qcom_soc, MM_TYPE_BROADBAND_MODEM_QMI)

/*****************************************************************************/

static const QmiSioPort sio_port_per_port_number[] = {
    QMI_SIO_PORT_A2_MUX_RMNET0,
    QMI_SIO_PORT_A2_MUX_RMNET1,
    QMI_SIO_PORT_A2_MUX_RMNET2,
    QMI_SIO_PORT_A2_MUX_RMNET3,
    QMI_SIO_PORT_A2_MUX_RMNET4,
    QMI_SIO_PORT_A2_MUX_RMNET5,
    QMI_SIO_PORT_A2_MUX_RMNET6,
    QMI_SIO_PORT_A2_MUX_RMNET7
};

static MMPortQmi *
peek_port_qmi_for_data_bam_dmux (MMBroadbandModemQmi  *self,
                                 MMPort               *data,
                                 MMQmiDataEndpoint    *out_endpoint,
                                 GError              **error)
{
    MMPortQmi      *found = NULL;
    MMKernelDevice *net_port;
    gint            net_port_number;

    net_port = mm_port_peek_kernel_device (data);

    /* The dev_port notified by the bam-dmux driver indicates which SIO port we should be using */
    net_port_number = mm_kernel_device_get_attribute_as_int (net_port, "dev_port");
    if (net_port_number < 0 || net_port_number >= (gint) G_N_ELEMENTS (sio_port_per_port_number)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Couldn't find SIO port number for 'net/%s'",
                     mm_port_get_device (data));
        return NULL;
    }

    /* Find one QMI port, we don't care which one */
    found = mm_broadband_modem_qmi_peek_port_qmi (self);

    if (!found)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Couldn't find any QMI port for 'net/%s'",
                     mm_port_get_device (data));
    else if (out_endpoint) {
        /* WDS Bind (Mux) Data Port must be called with the correct endpoint
         * interface number/SIO port to make multiplexing work with BAM-DMUX */
        out_endpoint->type = QMI_DATA_ENDPOINT_TYPE_BAM_DMUX;
        out_endpoint->interface_number = net_port_number;
        out_endpoint->sio_port = sio_port_per_port_number[net_port_number];
    }

    return found;
}

static MMPortQmi *
peek_port_qmi_for_data_ipa (MMBroadbandModemQmi  *self,
                            MMPort               *data,
                            MMQmiDataEndpoint    *out_endpoint,
                            GError              **error)
{
    MMPortQmi *found = NULL;

    /* when using IPA, we have a main network interface that will be multiplexed
     * to create link interfaces. We can assume any of the available QMI ports is
     * able to manage that. */

    found = mm_broadband_modem_qmi_peek_port_qmi (self);

    if (!found)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Couldn't find any QMI port for 'net/%s'",
                     mm_port_get_device (data));
    else if (out_endpoint)
        mm_port_qmi_get_endpoint_info (found, out_endpoint);

    return found;
}

static MMPortQmi *
peek_port_qmi_for_data (MMBroadbandModemQmi  *self,
                        MMPort               *data,
                        MMQmiDataEndpoint    *out_endpoint,
                        GError              **error)
{
    MMKernelDevice *net_port;
    const gchar    *net_port_driver;

    g_assert (MM_IS_BROADBAND_MODEM_QMI (self));
    g_assert (mm_port_get_subsys (data) == MM_PORT_SUBSYS_NET);

    net_port = mm_port_peek_kernel_device (data);
    net_port_driver = mm_kernel_device_get_driver (net_port);

    if (g_strcmp0 (net_port_driver, "ipa") == 0)
        return peek_port_qmi_for_data_ipa (self, data, out_endpoint, error);

    if (g_strcmp0 (net_port_driver, "bam-dmux") == 0)
        return peek_port_qmi_for_data_bam_dmux (self, data, out_endpoint, error);

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Unsupported QMI kernel driver for 'net/%s': %s",
                 mm_port_get_device (data),
                 net_port_driver);
    return NULL;
}

/*****************************************************************************/

MMBroadbandModemQmiQcomSoc *
mm_broadband_modem_qmi_qcom_soc_new (const gchar  *device,
                                     const gchar  *physdev,
                                     const gchar **drivers,
                                     const gchar  *plugin,
                                     guint16       vendor_id,
                                     guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI_QCOM_SOC,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_PHYSDEV,    physdev,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* QMI bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_qmi_qcom_soc_init (MMBroadbandModemQmiQcomSoc *self)
{
}

static void
mm_broadband_modem_qmi_qcom_soc_class_init (MMBroadbandModemQmiQcomSocClass *klass)
{
    MMBroadbandModemQmiClass *broadband_modem_qmi_class = MM_BROADBAND_MODEM_QMI_CLASS (klass);

    broadband_modem_qmi_class->peek_port_qmi_for_data = peek_port_qmi_for_data;
}
