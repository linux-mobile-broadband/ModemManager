/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_GSM_MODEM_H
#define MM_GSM_MODEM_H

#include <mm-modem.h>

#define MM_TYPE_GSM_MODEM      (mm_gsm_modem_get_type ())
#define MM_GSM_MODEM(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GSM_MODEM, MMGsmModem))
#define MM_IS_GSM_MODEM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GSM_MODEM))
#define MM_GSM_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_GSM_MODEM, MMGsmModem))

typedef enum {
    MM_GSM_MODEM_NETWORK_MODE_ANY       = 0,
    MM_GSM_MODEM_NETWORK_MODE_GPRS      = 1,
    MM_GSM_MODEM_NETWORK_MODE_EDGE      = 2,
    MM_GSM_MODEM_NETWORK_MODE_3G        = 3,
    MM_GSM_MODEM_NETWORK_MODE_HSDPA     = 4,
    MM_GSM_MODEM_NETWORK_MODE_PREFER_2G = 5,
    MM_GSM_MODEM_NETWORK_MODE_PREFER_3G = 6,

    MM_GSM_MODEM_NETWORK_MODE_LAST = MM_GSM_MODEM_NETWORK_MODE_PREFER_3G
} MMGsmModemNetworkMode;

typedef enum {
    MM_GSM_MODEM_BAND_ANY   = 0,
    MM_GSM_MODEM_BAND_EGSM  = 1,  /*  900 MHz */
    MM_GSM_MODEM_BAND_DCS   = 2,  /* 1800 MHz */
    MM_GSM_MODEM_BAND_PCS   = 3,  /* 1900 MHz */
    MM_GSM_MODEM_BAND_G850  = 4,  /*  850 MHz */
    MM_GSM_MODEM_BAND_U2100 = 5,  /* WCDMA 2100 MHz               (Class I) */
    MM_GSM_MODEM_BAND_U1700 = 6,  /* WCDMA 3GPP UMTS1800 MHz      (Class III) */
    MM_GSM_MODEM_BAND_17IV  = 7,  /* WCDMA 3GPP AWS 1700/2100 MHz (Class IV) */
    MM_GSM_MODEM_BAND_U800  = 8,  /* WCDMA 3GPP UMTS800 MHz       (Class VI) */
    MM_GSM_MODEM_BAND_U850  = 9,  /* WCDMA 3GPP UMTS850 MHz       (Class V) */
    MM_GSM_MODEM_BAND_U900  = 10, /* WCDMA 3GPP UMTS900 MHz       (Class VIII) */
    MM_GSM_MODEM_BAND_U17IX = 11, /* WCDMA 3GPP UMTS MHz          (Class IX) */

    MM_GSM_MODEM_BAND_LAST = MM_GSM_MODEM_BAND_U17IX
} MMGsmModemBand;

typedef enum {
    MM_GSM_MODEM_REG_STATUS_IDLE = 0,
    MM_GSM_MODEM_REG_STATUS_HOME = 1,
    MM_GSM_MODEM_REG_STATUS_SEARCHING = 2,
    MM_GSM_MODEM_REG_STATUS_DENIED = 3,
    MM_GSM_MODEM_REG_STATUS_UNKNOWN = 4,
    MM_GSM_MODEM_REG_STATUS_ROAMING = 5
} NMGsmModemRegStatus;

typedef struct _MMGsmModem MMGsmModem;

typedef void (*MMGsmModemScanFn) (MMGsmModem *modem,
                                  GPtrArray *results,
                                  GError *error,
                                  gpointer user_data);

typedef void (*MMGsmModemRegInfoFn) (MMGsmModem *modem,
                                     NMGsmModemRegStatus status,
                                     const char *oper_code,
                                     const char *oper_name,
                                     GError *error,
                                     gpointer user_data);

struct _MMGsmModem {
    GTypeInterface g_iface;

    /* Methods */
    void (*set_pin) (MMGsmModem *self,
                     const char *pin,
                     MMModemFn callback,
                     gpointer user_data);

    /* 'register' is a reserved word */
    void (*do_register) (MMGsmModem *self,
                         const char *network_id,
                         MMModemFn callback,
                         gpointer user_data);

    void (*get_registration_info) (MMGsmModem *self,
                                   MMGsmModemRegInfoFn callback,
                                   gpointer user_data);

    void (*scan) (MMGsmModem *self,
                  MMGsmModemScanFn callback,
                  gpointer user_data);

    void (*get_signal_quality) (MMGsmModem *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

    void (*set_apn) (MMGsmModem *self,
                     const char *apn,
                     MMModemFn callback,
                     gpointer user_data);

    void (*set_band) (MMGsmModem *self,
                      MMGsmModemBand band,
                      MMModemFn callback,
                      gpointer user_data);

    void (*get_band) (MMGsmModem *self,
                      MMModemUIntFn callback,
                      gpointer user_data);

    void (*set_network_mode) (MMGsmModem *self,
                              MMGsmModemNetworkMode mode,
                              MMModemFn callback,
                              gpointer user_data);

    void (*get_network_mode) (MMGsmModem *self,
                              MMModemUIntFn callback,
                              gpointer user_data);

    /* Signals */
    void (*signal_quality) (MMGsmModem *self,
                            guint32 quality);

    void (*network_mode) (MMGsmModem *self,
                          MMGsmModemNetworkMode mode);
};

GType mm_gsm_modem_get_type (void);

void mm_gsm_modem_set_pin (MMGsmModem *self,
                           const char *pin,
                           MMModemFn callback,
                           gpointer user_data);

void mm_gsm_modem_register (MMGsmModem *self,
                            const char *network_id,
                            MMModemFn callback,
                            gpointer user_data);

void mm_gsm_modem_get_reg_info (MMGsmModem *self,
                                MMGsmModemRegInfoFn callback,
                                gpointer user_data);

void mm_gsm_modem_scan (MMGsmModem *self,
                        MMGsmModemScanFn callback,
                        gpointer user_data);

void mm_gsm_modem_set_apn (MMGsmModem *self,
                           const char *apn,
                           MMModemFn callback,
                           gpointer user_data);

void mm_gsm_modem_get_signal_quality (MMGsmModem *self,
                                      MMModemUIntFn callback,
                                      gpointer user_data);

void mm_gsm_modem_set_band (MMGsmModem *self,
                            MMGsmModemBand band,
                            MMModemFn callback,
                            gpointer user_data);

void mm_gsm_modem_get_band (MMGsmModem *self,
                            MMModemUIntFn callback,
                            gpointer user_data);

void mm_gsm_modem_set_network_mode (MMGsmModem *self,
                                    MMGsmModemNetworkMode mode,
                                    MMModemFn callback,
                                    gpointer user_data);

void mm_gsm_modem_get_network_mode (MMGsmModem *self,
                                    MMModemUIntFn callback,
                                    gpointer user_data);

/* Protected */

void mm_gsm_modem_signal_quality (MMGsmModem *self,
                                  guint32 quality);

void mm_gsm_modem_network_mode (MMGsmModem *self,
                                MMGsmModemNetworkMode mode);

#endif  /* MM_GSM_MODEM_H */
