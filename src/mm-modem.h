/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_H
#define MM_MODEM_H

#include <glib-object.h>

#define MM_TYPE_MODEM      (mm_modem_get_type ())
#define MM_MODEM(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM, MMModem))
#define MM_IS_MODEM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM))
#define MM_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM, MMModem))

#define MM_MODEM_DATA_DEVICE "data-device"
#define MM_MODEM_DRIVER      "driver"
#define MM_MODEM_TYPE        "type"

#define MM_MODEM_TYPE_GSM  1
#define MM_MODEM_TYPE_CDMA 2

typedef enum {
    MM_MODEM_PROP_FIRST = 0x1000,

    MM_MODEM_PROP_DATA_DEVICE = MM_MODEM_PROP_FIRST,
    MM_MODEM_PROP_DRIVER,
    MM_MODEM_PROP_TYPE
} MMModemProp;

typedef enum {
    MM_MODEM_NETWORK_MODE_ANY       = 0,
    MM_MODEM_NETWORK_MODE_GPRS      = 1,
    MM_MODEM_NETWORK_MODE_EDGE      = 2,
    MM_MODEM_NETWORK_MODE_3G        = 3,
    MM_MODEM_NETWORK_MODE_HSDPA     = 4,
    MM_MODEM_NETWORK_MODE_PREFER_2G = 5,
    MM_MODEM_NETWORK_MODE_PREFER_3G = 6
} MMModemNetworkMode;

typedef enum {
    MM_MODEM_BAND_ANY   = 0,
    MM_MODEM_BAND_EGSM  = 1,  /*  900 MHz */
    MM_MODEM_BAND_DCS   = 2,  /* 1800 MHz */
    MM_MODEM_BAND_PCS   = 3,  /* 1900 MHz */
    MM_MODEM_BAND_G850  = 4,  /*  850 MHz */
    MM_MODEM_BAND_U2100 = 5,  /* WCDMA 2100 MHz               (Class I) */
    MM_MODEM_BAND_U1700 = 6,  /* WCDMA 3GPP UMTS1800 MHz      (Class III) */
    MM_MODEM_BAND_17IV  = 7,  /* WCDMA 3GPP AWS 1700/2100 MHz (Class IV) */
    MM_MODEM_BAND_U800  = 8,  /* WCDMA 3GPP UMTS800 MHz       (Class VI) */
    MM_MODEM_BAND_U850  = 9,  /* WCDMA 3GPP UMTS850 MHz       (Class V) */
    MM_MODEM_BAND_U900  = 10, /* WCDMA 3GPP UMTS900 MHz       (Class VIII) */
    MM_MODEM_BAND_U17IX = 11  /* WCDMA 3GPP UMTS MHz          (Class IX) */
} MMModemBand;

typedef struct _MMModem MMModem;

typedef void (*MMModemFn) (MMModem *modem,
                           GError *error,
                           gpointer user_data);

typedef void (*MMModemUIntFn) (MMModem *modem,
                               guint32 result,
                               GError *error,
                               gpointer user_data);

typedef void (*MMModemScanFn) (MMModem *modem,
                               GPtrArray *results,
                               GError *error,
                               gpointer user_data);

struct _MMModem {
    GTypeInterface g_iface;

    /* Methods */
    void (*enable) (MMModem *self,
                    gboolean enable,
                    MMModemFn callback,
                    gpointer user_data);

    void (*set_pin) (MMModem *self,
                     const char *pin,
                     MMModemFn callback,
                     gpointer user_data);

    /* 'register' is a reserved word */
    void (*do_register) (MMModem *self,
                         const char *network_id,
                         MMModemFn callback,
                         gpointer user_data);

    void (*connect) (MMModem *self,
                     const char *number,
                     const char *apn,
                     MMModemFn callback,
                     gpointer user_data);

    void (*disconnect) (MMModem *self,
                        MMModemFn callback,
                        gpointer user_data);

    void (*scan) (MMModem *self,
                  MMModemScanFn callback,
                  gpointer user_data);

    void (*get_signal_quality) (MMModem *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

    void (*set_band) (MMModem *self,
                      MMModemBand band,
                      MMModemFn callback,
                      gpointer user_data);

    void (*get_band) (MMModem *self,
                      MMModemUIntFn callback,
                      gpointer user_data);

    void (*set_network_mode) (MMModem *self,
                              MMModemNetworkMode mode,
                              MMModemFn callback,
                              gpointer user_data);

    void (*get_network_mode) (MMModem *self,
                              MMModemUIntFn callback,
                              gpointer user_data);

    /* Signals */
    void (*signal_quality) (MMModem *self,
                            guint32 quality);

    void (*network_mode) (MMModem *self,
                          MMModemNetworkMode mode);
};

GType mm_modem_get_type (void);

void mm_modem_enable (MMModem *self,
                      gboolean enable,
                      MMModemFn callback,
                      gpointer user_data);

void mm_modem_set_pin (MMModem *self,
                       const char *pin,
                       MMModemFn callback,
                       gpointer user_data);

void mm_modem_register (MMModem *self,
                        const char *network_id,
                        MMModemFn callback,
                        gpointer user_data);

void mm_modem_connect (MMModem *self,
                       const char *number,
                       const char *apn,
                       MMModemFn callback,
                       gpointer user_data);

void mm_modem_disconnect (MMModem *self,
                          MMModemFn callback,
                          gpointer user_data);

void mm_modem_scan (MMModem *self,
                    MMModemScanFn callback,
                    gpointer user_data);

void mm_modem_get_signal_quality (MMModem *self,
                                  MMModemUIntFn callback,
                                  gpointer user_data);

void mm_modem_set_band (MMModem *self,
                        MMModemBand band,
                        MMModemFn callback,
                        gpointer user_data);

void mm_modem_get_band (MMModem *self,
                        MMModemUIntFn callback,
                        gpointer user_data);

void mm_modem_set_network_mode (MMModem *self,
                                MMModemNetworkMode mode,
                                MMModemFn callback,
                                gpointer user_data);

void mm_modem_get_network_mode (MMModem *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

/* Protected */

void mm_modem_install_dbus_info (GType type);

void mm_modem_signal_quality (MMModem *self,
                              guint32 quality);

void mm_modem_network_mode (MMModem *self,
                            MMModemNetworkMode mode);

#endif  /* MM_MODEM_H */
