/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_GSM_NETWORK_H
#define MM_MODEM_GSM_NETWORK_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_GSM_NETWORK      (mm_modem_gsm_network_get_type ())
#define MM_MODEM_GSM_NETWORK(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_GSM_NETWORK, MMModemGsmNetwork))
#define MM_IS_MODEM_GSM_NETWORK(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_GSM_NETWORK))
#define MM_MODEM_GSM_NETWORK_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_GSM_NETWORK, MMModemGsmNetwork))

typedef enum {
    MM_MODEM_GSM_NETWORK_MODE_ANY          = 0,
    MM_MODEM_GSM_NETWORK_MODE_GPRS         = 1,
    MM_MODEM_GSM_NETWORK_MODE_EDGE         = 2,
    MM_MODEM_GSM_NETWORK_MODE_UMTS         = 3,
    MM_MODEM_GSM_NETWORK_MODE_HSDPA        = 4,
    MM_MODEM_GSM_NETWORK_MODE_2G_PREFERRED = 5,
    MM_MODEM_GSM_NETWORK_MODE_3G_PREFERRED = 6,
    MM_MODEM_GSM_NETWORK_MODE_2G_ONLY      = 7,
    MM_MODEM_GSM_NETWORK_MODE_3G_ONLY      = 8,
    MM_MODEM_GSM_NETWORK_MODE_HSUPA        = 9,
    MM_MODEM_GSM_NETWORK_MODE_HSPA         = 10,

    MM_MODEM_GSM_NETWORK_MODE_LAST = MM_MODEM_GSM_NETWORK_MODE_HSPA
} MMModemGsmNetworkMode;

typedef enum {
    MM_MODEM_GSM_NETWORK_BAND_ANY   = 0,
    MM_MODEM_GSM_NETWORK_BAND_EGSM  = 1,  /*  900 MHz */
    MM_MODEM_GSM_NETWORK_BAND_DCS   = 2,  /* 1800 MHz */
    MM_MODEM_GSM_NETWORK_BAND_PCS   = 3,  /* 1900 MHz */
    MM_MODEM_GSM_NETWORK_BAND_G850  = 4,  /*  850 MHz */
    MM_MODEM_GSM_NETWORK_BAND_U2100 = 5,  /* WCDMA 2100 MHz               (Class I) */
    MM_MODEM_GSM_NETWORK_BAND_U1700 = 6,  /* WCDMA 3GPP UMTS1800 MHz      (Class III) */
    MM_MODEM_GSM_NETWORK_BAND_17IV  = 7,  /* WCDMA 3GPP AWS 1700/2100 MHz (Class IV) */
    MM_MODEM_GSM_NETWORK_BAND_U800  = 8,  /* WCDMA 3GPP UMTS800 MHz       (Class VI) */
    MM_MODEM_GSM_NETWORK_BAND_U850  = 9,  /* WCDMA 3GPP UMTS850 MHz       (Class V) */
    MM_MODEM_GSM_NETWORK_BAND_U900  = 10, /* WCDMA 3GPP UMTS900 MHz       (Class VIII) */
    MM_MODEM_GSM_NETWORK_BAND_U17IX = 11, /* WCDMA 3GPP UMTS MHz          (Class IX) */

    MM_MODEM_GSM_NETWORK_BAND_LAST = MM_MODEM_GSM_NETWORK_BAND_U17IX
} MMModemGsmNetworkBand;

typedef enum {
    MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE = 0,
    MM_MODEM_GSM_NETWORK_REG_STATUS_HOME = 1,
    MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING = 2,
    MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED = 3,
    MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN = 4,
    MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING = 5
} MMModemGsmNetworkRegStatus;

typedef struct _MMModemGsmNetwork MMModemGsmNetwork;

typedef void (*MMModemGsmNetworkScanFn) (MMModemGsmNetwork *self,
                                         GPtrArray *results,
                                         GError *error,
                                         gpointer user_data);

typedef void (*MMModemGsmNetworkRegInfoFn) (MMModemGsmNetwork *self,
                                            MMModemGsmNetworkRegStatus status,
                                            const char *oper_code,
                                            const char *oper_name,
                                            GError *error,
                                            gpointer user_data);

struct _MMModemGsmNetwork {
    GTypeInterface g_iface;

    /* Methods */
    /* 'register' is a reserved word */
    void (*do_register) (MMModemGsmNetwork *self,
                         const char *network_id,
                         MMModemFn callback,
                         gpointer user_data);

    void (*scan) (MMModemGsmNetwork *self,
                  MMModemGsmNetworkScanFn callback,
                  gpointer user_data);

    void (*set_apn) (MMModemGsmNetwork *self,
                     const char *apn,
                     MMModemFn callback,
                     gpointer user_data);

    void (*get_signal_quality) (MMModemGsmNetwork *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

    void (*set_band) (MMModemGsmNetwork *self,
                      MMModemGsmNetworkBand band,
                      MMModemFn callback,
                      gpointer user_data);

    void (*get_band) (MMModemGsmNetwork *self,
                      MMModemUIntFn callback,
                      gpointer user_data);

    void (*set_network_mode) (MMModemGsmNetwork *self,
                              MMModemGsmNetworkMode mode,
                              MMModemFn callback,
                              gpointer user_data);

    void (*get_network_mode) (MMModemGsmNetwork *self,
                              MMModemUIntFn callback,
                              gpointer user_data);

    void (*get_registration_info) (MMModemGsmNetwork *self,
                                   MMModemGsmNetworkRegInfoFn callback,
                                   gpointer user_data);

    /* Signals */
    void (*signal_quality) (MMModemGsmNetwork *self,
                            guint32 quality);

    void (*registration_info) (MMModemGsmNetwork *self,
                               MMModemGsmNetworkRegStatus status,
                               const char *open_code,
                               const char *oper_name);

    void (*network_mode) (MMModemGsmNetwork *self,
                          MMModemGsmNetworkMode mode);
};

GType mm_modem_gsm_network_get_type (void);

void mm_modem_gsm_network_register (MMModemGsmNetwork *self,
                                    const char *network_id,
                                    MMModemFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_scan (MMModemGsmNetwork *self,
                                MMModemGsmNetworkScanFn callback,
                                gpointer user_data);

void mm_modem_gsm_network_set_apn (MMModemGsmNetwork *self,
                                   const char *apn,
                                   MMModemFn callback,
                                   gpointer user_data);

void mm_modem_gsm_network_get_signal_quality (MMModemGsmNetwork *self,
                                              MMModemUIntFn callback,
                                              gpointer user_data);

void mm_modem_gsm_network_set_band (MMModemGsmNetwork *self,
                                    MMModemGsmNetworkBand band,
                                    MMModemFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_get_band (MMModemGsmNetwork *self,
                                    MMModemUIntFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_set_mode (MMModemGsmNetwork *self,
                                    MMModemGsmNetworkMode mode,
                                    MMModemFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_get_mode (MMModemGsmNetwork *self,
                                    MMModemUIntFn callback,
                                    gpointer user_data);

void mm_modem_gsm_network_get_registration_info (MMModemGsmNetwork *self,
                                                 MMModemGsmNetworkRegInfoFn callback,
                                                 gpointer user_data);

/* Protected */

void mm_modem_gsm_network_signal_quality (MMModemGsmNetwork *self,
                                          guint32 quality);

void mm_modem_gsm_network_registration_info (MMModemGsmNetwork *self,
                                             MMModemGsmNetworkRegStatus status,
                                             const char *oper_code,
                                             const char *oper_name);

void mm_modem_gsm_network_mode (MMModemGsmNetwork *self,
                                MMModemGsmNetworkMode mode);

#endif /* MM_MODEM_GSM_NETWORK_H */
